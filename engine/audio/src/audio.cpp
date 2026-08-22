#include "luaug/audio/audio.h"

#include "luaug/core/i18n.h"
#include "luaug/core/log.h"
#include "luaug/core/text_key.h"
#include "luaug/platform/file.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <utility>
#include <vector>

// The one translation unit that defines miniaudio. Nothing else in the engine
// includes it, which is what makes ADR 0009's "the public API never leaks
// miniaudio concepts" structural rather than a promise.
//
// The feature switches are not tidiness. Every one of them is compiled code, a
// thread, or an allocation the soak would have to account for.
//
// **DECODING is on from M7**, because `Sound.Content` reads a file now. The node
// graph, the resource manager and the high-level engine stay off: this mixer
// owns its own voices and its own lock (see `dataCallback`), and miniaudio's
// resource manager would bring a second thread and a second cache to do a job
// that is already done.
//
// ENCODING stays off for the reason `assetc` links the basis ENCODER and the
// engine only the transcoder: the runtime must not be able to write audio.
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
// One decoded sound, in the mixer's own format: interleaved f32 at `kSampleRate`
// and `kChannels`. Converted at DECODE time rather than in the callback, so the
// audio thread does a copy and a multiply and never a resample.
//
// Held by `shared_ptr` so a voice can keep one alive across a frame in which the
// cache was touched. The audio thread reads through a raw pointer; what makes
// that safe is that a clip is never mutated after it is decoded and never
// evicted while the system lives.
struct Clip
{
    std::vector<f32> samples;
    u32 frames = 0;
};

struct Voice
{
    f32 amplitude = 0.0f;
    f32 frequency = 440.0f;
    f64 phase = 0.0;
    f64 phaseStep = 0.0;

    // The decoded audio, or null for a sound whose content could not be
    // resolved -- which still plays the placeholder tone, because a sound that
    // went silent because a file was missing is a bug report about the sound.
    const Clip* clip = nullptr;
    // Where in the clip this voice is, in FRAMES, taken from the sound's own
    // `TimePosition` every frame.
    //
    // Taken rather than accumulated, and that is the whole reason the audio here
    // can be reasoned about: the timeline is the SIM clock's (M6 brief, Decision
    // 9), so the mixer is a function of simulation state rather than a second
    // clock that drifts against it. A cursor the callback advanced on its own
    // would make "where is this sound" have two answers.
    f64 cursor = 0.0;
    f64 cursorStep = 1.0;
    bool looped = false;
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
    std::atomic<u32> clipsLoaded{0};
    std::atomic<u32> clipsMissing{0};

    // Where `Sound.Content` is resolved from, and what has been decoded.
    //
    // Sorted by URN and never evicted while the system lives. Never evicted is
    // the load-bearing half: the audio thread holds a raw pointer into a clip
    // for the length of a callback, and a cache that could drop one underneath
    // it would be a crash at the worst possible moment. A sound bank is
    // megabytes, not gigabytes, and a game that outgrows this needs streaming
    // audio rather than a smarter cache.
    const asset::ContentMounts* mounts = nullptr;
    std::vector<std::pair<std::string, std::shared_ptr<Clip>>> clips;

    // Decodes on the first ask and answers from the cache after. Null for a URN
    // that names nothing, which plays the placeholder tone.
    [[nodiscard]] const Clip* clipFor(std::string_view content);

    static void dataCallback(ma_device* device, void* output, const void* input, ma_uint32 frameCount)
    {
        (void)input;
        auto* self = static_cast<Impl*>(device->pUserData);
        auto* samples = static_cast<float*>(output);
        std::memset(samples, 0, static_cast<core::usize>(frameCount) * kChannels * sizeof(float));
        if (self == nullptr)
            return;

        // **An underrun is a callback that could not get the voices**, and the
        // lock is never waited on (D032).
        //
        // The first definition counted a callback that arrived with no NEW frame
        // published since the last one, and that is the ordinary case rather
        // than a fault: the device asks for buffers faster than the simulation
        // produces ticks, so most callbacks legitimately mix the same voices
        // again. A sixty-second soak on a real device counted 1,348 of them and
        // every one was the mixer working correctly -- a gate reporting a
        // catastrophe for the normal case, which is worse than no gate.
        //
        // What starvation actually is here: the simulation holding the voice
        // list when the device needs it. `try_lock` says so exactly, and a
        // callback that loses the race outputs the silence the buffer was
        // already cleared to -- which is what an underrun SOUNDS like, and is
        // the honest thing to do rather than block. **An audio callback must
        // never wait on a game thread**; the old `lock_guard` did, and a stall
        // in the simulation would have stalled the device with it.
        std::unique_lock<std::mutex> lock(self->mutex, std::try_to_lock);
        if (!lock.owns_lock()) {
            self->underruns.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        self->activeVoices.store(static_cast<u32>(self->voices.size()), std::memory_order_relaxed);

        for (Voice& voice : self->voices) {
            if (voice.amplitude <= 0.0f)
                continue;

            if (voice.clip != nullptr) {
                for (ma_uint32 frame = 0; frame < frameCount; ++frame) {
                    // Nearest sample rather than interpolated. At playback speed
                    // one -- which is every sound in every game most of the time
                    // -- the cursor lands exactly on a frame and this is a copy;
                    // at other speeds it is a repitch whose artefacts are below
                    // what a game mix reveals. Interpolation is a quality
                    // decision and M7 has no quality dial to hang it on.
                    const auto index = static_cast<core::usize>(voice.cursor);
                    if (index >= voice.clip->frames) {
                        if (!voice.looped) {
                            break;
                        }
                        // Wrapped by the CLIP's length rather than reset to
                        // zero: a loop that restarted at the buffer boundary
                        // would click once per buffer instead of once per loop.
                        voice.cursor = std::fmod(voice.cursor, static_cast<f64>(voice.clip->frames));
                        continue;
                    }
                    const core::usize at = index * kChannels;
                    samples[frame * kChannels] += voice.clip->samples[at] * voice.amplitude;
                    samples[frame * kChannels + 1] += voice.clip->samples[at + 1] * voice.amplitude;
                    voice.cursor += voice.cursorStep;
                }
                continue;
            }

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
        // Suspended: the world is read and nothing is heard. Returning before
        // the walk instead would be the same silence, and this way the walk's
        // side effects -- none today, and that is not a promise the future owes
        // -- cannot start depending on being skipped.
        if (m_suspended)
            return;
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
        voice.clip = m_impl->clipFor(sound.content);
        if (voice.clip != nullptr) {
            // The cursor comes from the SOUND, every frame. See `Voice::cursor`.
            voice.cursor = sound.timePosition * static_cast<f64>(kSampleRate);
            voice.cursorStep = static_cast<f64>(sound.playbackSpeed);
            voice.looped = sound.looped;
        }
        else {
            // The placeholder tone, unchanged. A sound whose file is missing is
            // audibly a placeholder rather than silent, which is the same
            // reasoning M6 shipped and the reason it is still here.
            voice.frequency = placeholderPitch(sound.content) * sound.playbackSpeed;
            voice.phaseStep = 6.283185307179586 * static_cast<f64>(voice.frequency) / static_cast<f64>(kSampleRate);
        }
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
}

const Clip* AudioSystem::Impl::clipFor(std::string_view content)
{
    const auto at = std::lower_bound(clips.begin(), clips.end(), content,
                                     [](const auto& entry, std::string_view key) { return entry.first < key; });
    if (at != clips.end() && at->first == content) {
        return at->second.get();
    }
    if (mounts == nullptr) {
        return nullptr;
    }

    // A failed decode is CACHED as a null clip, so a sound naming a missing file
    // costs one lookup a frame rather than one decode attempt a frame.
    std::shared_ptr<Clip> clip;
    const asset::ResolvedContent resolved = mounts->resolve(content);
    std::vector<std::byte> owned;
    std::span<const std::byte> bytes = resolved.bytes;
    if (bytes.empty() && resolved.source == asset::ResolvedContent::Source::Loose) {
        if (platform::readFile(resolved.path, owned)) {
            bytes = owned;
        }
    }

    if (!bytes.empty()) {
        // Decoded straight into the MIXER's format -- f32, stereo, 48 kHz -- so
        // the callback never resamples. miniaudio does the conversion as part of
        // the decode, which is one pass over the data instead of two.
        ma_decoder_config config = ma_decoder_config_init(ma_format_f32, kChannels, kSampleRate);
        ma_decoder decoder{};
        if (ma_decoder_init_memory(bytes.data(), bytes.size(), &config, &decoder) == MA_SUCCESS) {
            ma_uint64 frames = 0;
            if (ma_decoder_get_length_in_pcm_frames(&decoder, &frames) == MA_SUCCESS && frames > 0) {
                clip = std::make_shared<Clip>();
                clip->samples.resize(static_cast<core::usize>(frames) * kChannels);
                ma_uint64 read = 0;
                (void)ma_decoder_read_pcm_frames(&decoder, clip->samples.data(), frames, &read);
                clip->frames = static_cast<u32>(read);
                clip->samples.resize(static_cast<core::usize>(clip->frames) * kChannels);
                if (clip->frames == 0) {
                    clip.reset();
                }
            }
            ma_decoder_uninit(&decoder);
        }
        if (clip == nullptr) {
            const core::I18nArg args[] = {{"content", std::string(content)}};
            core::log(core::LogLevel::Warn, LUAUG_TR("audio.warn.undecodable"), args);
        }
    }
    else {
        const core::I18nArg args[] = {{"content", std::string(content)}};
        core::log(core::LogLevel::Warn, LUAUG_TR("audio.warn.content_missing"), args);
    }

    const bool decoded = clip != nullptr;
    const auto inserted = clips.insert(at, {std::string(content), std::move(clip)});
    if (decoded) {
        clipsLoaded.fetch_add(1, std::memory_order_relaxed);
    }
    else {
        clipsMissing.fetch_add(1, std::memory_order_relaxed);
    }
    return inserted->second.get();
}

void AudioSystem::setContentMounts(const asset::ContentMounts* mounts) noexcept
{
    if (m_impl == nullptr) {
        return;
    }
    // Under the lock: the audio thread reads a clip pointer a voice holds, and
    // swapping the mounts is what invalidates the answers behind those pointers.
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (m_impl->mounts == mounts) {
        return;
    }
    m_impl->mounts = mounts;
    // Every voice is dropped with the cache, because a voice holds a raw pointer
    // into it. One frame of silence when a world is swapped; a dangling read
    // otherwise.
    m_impl->voices.clear();
    m_impl->clips.clear();
    m_impl->clipsLoaded.store(0, std::memory_order_relaxed);
    m_impl->clipsMissing.store(0, std::memory_order_relaxed);
}

AudioStats AudioSystem::stats() const noexcept
{
    AudioStats out;
    if (m_impl == nullptr)
        return out;
    out.underruns = m_impl->underruns.load(std::memory_order_relaxed);
    out.clipsLoaded = m_impl->clipsLoaded.load(std::memory_order_relaxed);
    out.clipsMissing = m_impl->clipsMissing.load(std::memory_order_relaxed);
    out.droppedCommands = m_impl->dropped.load(std::memory_order_relaxed);
    out.activeVoices = m_impl->activeVoices.load(std::memory_order_relaxed);
    out.deviceOpen = m_impl->deviceStarted;
    return out;
}

} // namespace luaug::audio
