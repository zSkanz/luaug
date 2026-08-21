#include "luaug/audio/audio.h"

#include "luaug/core/i18n.h"
#include "luaug/core/log.h"
#include "luaug/core/text_key.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>

// The one translation unit that defines miniaudio. Nothing else in the engine
// includes it, which is what makes ADR 0009's "the public API never leaks
// miniaudio concepts" structural rather than a promise.
//
// The feature switches are not tidiness. Decoding, the node graph and the
// high-level engine are all things v1 does not use -- `Sound.Content` is
// `Inert` until M7 -- and every one of them is compiled code, a thread, or an
// allocation the soak would have to account for.
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

namespace luaug::audio {
namespace {

constexpr u32 kSampleRate = 48000;
constexpr u32 kChannels = 2;

// How many voices the mixer will sum. Past this, the quietest are dropped
// rather than the newest -- a footstep lost under an explosion is the right
// thing to lose, and an unbounded mix is an unbounded callback.
constexpr core::usize kMaxVoices = 64;

// What one voice sounds like until M7 can decode a file. See the header: a
// placeholder that is audibly a placeholder, and not silence.
constexpr f64 kPlaceholderDuration = 1.0;

// The mixer's view of one sound. Written by the main thread under the lock and
// read by the audio callback -- a lock rather than a lock-free ring because the
// critical section is a memcpy of a few hundred bytes at 60 Hz against a
// callback that runs at about 100 Hz, and a mutex held that briefly is simpler
// than a ring nobody can prove correct.
struct Voice
{
    f32 amplitude = 0.0f;
    f32 frequency = 440.0f;
    f64 phase = 0.0;
    f64 phaseStep = 0.0;
};

// A stable pitch per content id, so two different sounds are audibly different
// and the same sound is the same every run. FNV-1a over the bytes, mapped into
// two octaves from A3 -- a range that is unmistakably a placeholder and does not
// hurt to listen to for sixty seconds.
[[nodiscard]] f32 placeholderPitch(std::string_view content) noexcept
{
    core::u64 hash = 1469598103934665603ull;
    for (const char byte : content) {
        hash ^= static_cast<core::u64>(static_cast<unsigned char>(byte));
        hash *= 1099511628211ull;
    }
    const f64 semitone = static_cast<f64>(hash % 25u);
    return static_cast<f32>(220.0 * std::pow(2.0, semitone / 12.0));
}

} // namespace

struct AudioSystem::Impl
{
    ma_device device{};
    bool deviceStarted = false;

    std::mutex mutex;
    std::vector<Voice> voices;

    std::atomic<u64> underruns{0};
    std::atomic<u64> dropped{0};
    std::atomic<u32> activeVoices{0};

    // Set while `update` is swapping the voice list, so a callback that arrives
    // mid-swap knows it is looking at a stale frame rather than a torn one.
    std::atomic<bool> voicesFresh{false};

    static void dataCallback(ma_device* device, void* output, const void* input, ma_uint32 frameCount)
    {
        (void)input;
        auto* self = static_cast<Impl*>(device->pUserData);
        auto* samples = static_cast<float*>(output);
        std::memset(samples, 0, static_cast<core::usize>(frameCount) * kChannels * sizeof(float));
        if (self == nullptr)
            return;

        // An underrun in this design's terms: the callback ran and the main
        // thread had not published a frame since the last one. It means the
        // simulation is not keeping up with the mixer, which is exactly the
        // thing the soak is watching for.
        if (!self->voicesFresh.exchange(false))
            self->underruns.fetch_add(1, std::memory_order_relaxed);

        std::lock_guard<std::mutex> lock(self->mutex);
        self->activeVoices.store(static_cast<u32>(self->voices.size()), std::memory_order_relaxed);

        for (Voice& voice : self->voices) {
            if (voice.amplitude <= 0.0f)
                continue;
            for (ma_uint32 frame = 0; frame < frameCount; ++frame) {
                // The widening is written out: `voice.phase` is f64 and the
                // amplitude f32, and `-Wdouble-promotion` is an error on
                // `engine/` so that a narrow value entering a wide computation
                // is a decision rather than an accident.
                const auto value = static_cast<float>(std::sin(voice.phase) * static_cast<double>(voice.amplitude));
                samples[frame * kChannels] += value;
                samples[frame * kChannels + 1] += value;
                voice.phase += voice.phaseStep;
                if (voice.phase > 6.283185307179586)
                    voice.phase -= 6.283185307179586;
            }
        }

        // Soft-clipped rather than left to wrap: a mix past full scale is a game
        // balance problem, and clipping it is what a mixer does. Wrapping is
        // what a bug does.
        const core::usize total = static_cast<core::usize>(frameCount) * kChannels;
        for (core::usize index = 0; index < total; ++index)
            samples[index] = std::clamp(samples[index], -1.0f, 1.0f);
    }
};

AudioSystem::~AudioSystem()
{
    stop();
}

std::optional<core::EngineError> AudioSystem::start(bool headless)
{
    if (m_impl != nullptr)
        return std::nullopt;

    m_impl = new Impl();

    if (headless) {
        // No device at all. A headless run has no reason to hold one open, and
        // on a CI runner the attempt costs a second and a log line nobody reads.
        // The timeline still runs, so `Ended` still fires on the same tick.
        return std::nullopt;
    }

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = kChannels;
    config.sampleRate = kSampleRate;
    config.dataCallback = &Impl::dataCallback;
    config.pUserData = m_impl;

    if (ma_device_init(nullptr, &config, &m_impl->device) != MA_SUCCESS) {
        // Not an error. A machine with no sound card still has to run the game,
        // and every CI runner is such a machine -- so this is said once, at Info,
        // and everything above it carries on unchanged.
        core::logText(core::LogLevel::Info, core::engineCatalog().format(LUAUG_TR("audio.info.no_device"), {}));
        return std::nullopt;
    }

    if (ma_device_start(&m_impl->device) != MA_SUCCESS) {
        ma_device_uninit(&m_impl->device);
        core::logText(core::LogLevel::Info, core::engineCatalog().format(LUAUG_TR("audio.info.no_device"), {}));
        return std::nullopt;
    }

    m_impl->deviceStarted = true;
    return std::nullopt;
}

void AudioSystem::stop()
{
    if (m_impl == nullptr)
        return;
    if (m_impl->deviceStarted) {
        ma_device_uninit(&m_impl->device);
        m_impl->deviceStarted = false;
    }
    delete m_impl;
    m_impl = nullptr;
}

void AudioSystem::tick(scene::World& world, f64 fixedDt)
{
    const core::NameAtom ended = world.atoms().intern("Ended");
    const core::NameAtom loaded = world.atoms().intern("Loaded");

    // Slot order, which is a pure function of the operation sequence, so two
    // sounds ending on one tick raise their events in the same order on every
    // run (R10).
    world.sounds().forEach([&](core::InstanceId id, scene::SoundComponent& sound) {
        if (!sound.loadedFired) {
            // Immediately, because there is nothing to load until M7. Declared
            // now so that code written today does not change when there is.
            sound.loadedFired = true;
            world.changes().push(scene::Change{scene::ChangeKind::InstanceEventNoArgs, id, {}, loaded});
        }

        if (!sound.playing)
            return;

        sound.timePosition += fixedDt * static_cast<f64>(sound.playbackSpeed);
        if (sound.timePosition < kPlaceholderDuration)
            return;

        if (sound.looped) {
            // Wrapped rather than reset, so a loop does not lose the fraction of
            // a tick it overshot by -- which over a minute is a loop that drifts
            // against everything else in the scene.
            sound.timePosition = std::fmod(sound.timePosition, kPlaceholderDuration);
            return;
        }

        sound.timePosition = kPlaceholderDuration;
        sound.playing = false;
        world.changes().push(scene::Change{scene::ChangeKind::InstanceEventNoArgs, id, {}, ended});
    });
}

void AudioSystem::update(scene::World& world, core::InstanceId listener)
{
    if (m_impl == nullptr)
        return;

    core::DVec3 ear;
    if (const scene::CameraComponent* camera = world.cameras().find(listener); camera != nullptr)
        ear = camera->cframe.position;

    std::vector<Voice> next;
    next.reserve(kMaxVoices);

    world.sounds().forEach([&](core::InstanceId id, const scene::SoundComponent& sound) {
        if (!sound.playing)
            return;

        f32 gain = sound.volume * world.engineState().masterVolume;
        if (const scene::AudioGroupComponent* group = world.audioGroups().find(sound.group); group != nullptr)
            gain *= group->volume;

        // Positional iff the sound is parented to a `BasePart`. The whole of
        // the 3D switch, and it is a property of where the instance sits rather
        // than a flag that could disagree with it.
        if (const scene::PartComponent* part = world.parts().find(world.parentOf(id)); part != nullptr) {
            const core::DVec3 delta = part->cframe.position - ear;
            const auto distance =
                static_cast<f32>(std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z));
            // Not `near` and `far`: both are macros in the Windows headers that
            // miniaudio drags in, and the error they produce is a syntax error
            // forty lines long that names neither of them.
            const f32 quiet = sound.rollOffMinDistance;
            const f32 silent = std::fmax(sound.rollOffMaxDistance, quiet + 0.001f);
            // Linear between the two distances, because a game's audible range
            // is a design decision rather than a physical one -- an inverse
            // square makes the far half of it inaudible.
            const f32 falloff =
                distance <= quiet ? 1.0f : (distance >= silent ? 0.0f : (silent - distance) / (silent - quiet));
            gain *= falloff;
        }

        if (gain <= 0.0001f)
            return;

        Voice voice;
        voice.amplitude = std::fmin(gain, 4.0f);
        voice.frequency = placeholderPitch(sound.content) * sound.playbackSpeed;
        voice.phaseStep = 6.283185307179586 * static_cast<f64>(voice.frequency) / static_cast<f64>(kSampleRate);
        next.push_back(voice);
    });

    if (next.size() > kMaxVoices) {
        // The quietest go, not the newest: a footstep lost under an explosion is
        // the right thing to lose.
        std::partial_sort(next.begin(), next.begin() + static_cast<std::ptrdiff_t>(kMaxVoices), next.end(),
                          [](const Voice& a, const Voice& b) { return a.amplitude > b.amplitude; });
        m_impl->dropped.fetch_add(next.size() - kMaxVoices, std::memory_order_relaxed);
        next.resize(kMaxVoices);
    }

    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        // Phases are carried across so a continuing voice does not click every
        // frame. Matched by index, which is stable while the sound set is: a
        // sound stopping shifts the rest, and one click on the frame a sound
        // stops is not worth an id map on the audio thread.
        for (core::usize index = 0; index < next.size() && index < m_impl->voices.size(); ++index)
            next[index].phase = m_impl->voices[index].phase;
        m_impl->voices.swap(next);
    }
    m_impl->voicesFresh.store(true);
}

AudioStats AudioSystem::stats() const noexcept
{
    AudioStats out;
    if (m_impl == nullptr)
        return out;
    out.underruns = m_impl->underruns.load(std::memory_order_relaxed);
    out.droppedCommands = m_impl->dropped.load(std::memory_order_relaxed);
    out.activeVoices = m_impl->activeVoices.load(std::memory_order_relaxed);
    out.deviceOpen = m_impl->deviceStarted;
    return out;
}

} // namespace luaug::audio
