#include "luaug/audio/audio.h"

#include "luaug/core/i18n.h"
#include "luaug/core/log.h"
#include "luaug/core/text_key.h"
#include "luaug/jobs/jobs.h"
#include "luaug/platform/async_io.h"
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
#include <string_view>
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

// How long the PLACEHOLDER TONE lasts, and nothing else. See the header: a
// sound whose content names nothing still plays, as a tone that is audibly a
// placeholder rather than as silence, and this is that tone's length.
//
// It used to be every sound's length, which is D092: `tick` measured
// `TimePosition` against this constant whatever the file was, so a two-minute
// track fired `Ended` and stopped itself after one second. A decoded sound is
// measured against ITS OWN length now -- `clipDuration` -- and this is what is
// left when there is no clip to ask.
constexpr f64 kPlaceholderDuration = 1.0;

// How far the mixer's cursor and the simulation's timeline may drift apart
// before the cursor is taken from the timeline. See `detail::shouldTakeTimeline`
// in the header for what the two clocks are and why they differ at all.
//
// A quarter second is chosen from both sides. It is far larger than the gap the
// two quantisations can open -- one fixed step plus one device buffer, which is
// under thirty milliseconds at any rate this engine runs -- and far smaller than
// any seek a person or a script performs on purpose. A seek SMALLER than this is
// not resynced, and that is the honest cost: the sound keeps playing from within
// a quarter second of where it was asked to go.
constexpr f64 kResyncTolerance = 0.25;

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

// What a prefetch job reads and writes, in ONE heap allocation.
//
// Not fields on the pending entry, and that is the rule `MeshLoader` and
// `UiText` each learned the hard way: the pending list grows whenever a new
// sound names a URN nobody has named before, and a job holding the address of
// anything inside an element is writing into freed memory after the
// reallocation.
struct ClipWork
{
    std::vector<std::byte> bytes;
    std::shared_ptr<Clip> clip;
};

struct Voice
{
    // **Which sound this is.** Voices used to be matched between frames by
    // INDEX, which is stable only while the sound set is: one sound stopping
    // shifted every voice after it onto a different clip's cursor. The id costs
    // a linear scan over at most sixty-four entries once a frame and makes
    // "the same sound" mean the same sound.
    core::InstanceId id;

    f32 amplitude = 0.0f;
    // **What the LISTENER's frame does to this voice**, on top of `amplitude`.
    //
    // Kept apart from the gain rather than folded into it, because the two
    // answer different questions and one of them is asked by something else:
    // `amplitude` is how loud this sound is, and it is what the voice cap sorts
    // on -- a sound panned hard left is not a quieter sound and must not be the
    // first one dropped.
    f32 panLeft = 1.0f;
    f32 panRight = 1.0f;
    // Whether the world claims to know where this sound is, which is exactly
    // whether it is parented to a `BasePart`. It decides the fold above.
    bool positional = false;

    f32 frequency = 440.0f;
    f64 phase = 0.0;
    f64 phaseStep = 0.0;

    // The decoded audio, or null for a sound whose content could not be
    // resolved -- which still plays the placeholder tone, because a sound that
    // went silent because a file was missing is a bug report about the sound.
    const Clip* clip = nullptr;
    // Where in the clip this voice is, in FRAMES. **Advanced by the callback and
    // SEEDED from the sound's `TimePosition`**, rather than taken from it afresh
    // every frame.
    //
    // Taken afresh is what M6 shipped and it is D093. The reasoning was sound --
    // the timeline is the sim clock's (M6 brief, Decision 9) and the mixer must
    // not become a second clock the simulation can read -- but the two clocks
    // are quantised differently and only one of them is smooth. `TimePosition`
    // moves in fixed steps at the tick rate; this cursor moves at the device's.
    // A frame in which no tick ran therefore dragged the cursor BACKWARDS by a
    // frame's worth of samples and the mixer replayed audio it had just played,
    // as often as sixty times a second. That is what it sounded like.
    //
    // The invariant that mattered is untouched, because it was never about this
    // number: nothing here is ever read BY the simulation. `TimePosition` still
    // advances by `FixedTimestep * PlaybackSpeed` per tick, `Ended` still fires
    // from it, and a replay still reproduces both exactly. What changed is only
    // what the speakers do between two ticks, which the API's own words already
    // called downstream of the simulation and never an input to it.
    //
    // The timeline is still taken whenever the two have genuinely parted
    // company -- a seek, a rewind, a sound stopped and started -- which is
    // `detail::shouldTakeTimeline`.
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

// **One clip, mixed into the buffer, cursor advanced.** Written once because
// there are two callers now -- the world's voices and the audition -- and a
// second copy of a resampling loop is a second place for them to disagree about
// what the end of a clip means.
//
// Returns false when a non-looped voice ran off the end, which is what retires
// an audition.
[[nodiscard]] bool mixClip(float* samples, ma_uint32 frameCount, const Clip& clip, f64& cursor, f64 step, f32 leftGain,
                           f32 rightGain, bool downmix, bool looped) noexcept
{
    for (ma_uint32 frame = 0; frame < frameCount; ++frame) {
        // Nearest sample rather than interpolated. At playback speed one --
        // which is every sound in every game most of the time -- the cursor
        // lands exactly on a frame and this is a copy; at other speeds it is a
        // repitch whose artefacts are below what a game mix reveals.
        // Interpolation is a quality decision and there is no quality dial to
        // hang it on.
        const auto index = static_cast<core::usize>(cursor);
        if (index >= clip.frames) {
            if (!looped) {
                return false;
            }
            // Wrapped by the CLIP's length rather than reset to zero: a loop
            // that restarted at the buffer boundary would click once per buffer
            // instead of once per loop.
            cursor = std::fmod(cursor, static_cast<f64>(clip.frames));
            continue;
        }
        const core::usize at = index * kChannels;
        // **A POSITIONAL sound is folded to mono before it is panned**, and a 2D
        // one keeps its own stereo image untouched.
        //
        // The two cannot both be true of one voice. A file whose content sits in
        // its right channel, placed to the listener's left, is in two places at
        // once -- and the answer a person expects is the one the WORLD gives,
        // because they put it there. A sound that is not parented to a part was
        // never claimed to be anywhere, so its image is all it has and nothing
        // here may take it.
        if (downmix) {
            const float mono = (clip.samples[at] + clip.samples[at + 1]) * 0.5f;
            samples[frame * kChannels] += mono * leftGain;
            samples[frame * kChannels + 1] += mono * rightGain;
        }
        else {
            samples[frame * kChannels] += clip.samples[at] * leftGain;
            samples[frame * kChannels + 1] += clip.samples[at + 1] * rightGain;
        }
        cursor += step;
    }
    return true;
}

// What the editor is auditioning, if anything. See `AudioSystem::audition`: it
// is deliberately not a `Voice` and deliberately not a `Sound`, because the one
// thing it has that neither of those may have is a cursor the wall clock drives.
struct Audition
{
    const Clip* clip = nullptr;
    std::string content;
    f64 cursor = 0.0;
    f64 cursorStep = 1.0;
    f32 amplitude = 0.0f;
    bool active = false;
};

} // namespace

struct AudioSystem::Impl
{
    ma_device device{};
    bool deviceStarted = false;

    std::mutex mutex;
    std::vector<Voice> voices;
    Audition audition;

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
    //
    // **This still decodes on the calling thread when it has to** (D129), and
    // that is deliberate rather than unfinished. The tick reads it to know how
    // long a sound is, and the answer decides when `Ended` fires -- which
    // `replay.cpp` hashes. An answer that arrived a few ticks later on a slow
    // disk would make the world hash depend on the disk, which R10 forbids
    // outright. So the synchronous path stays as the floor, and `beginPrefetch`
    // below is what stops it being reached.
    [[nodiscard]] const Clip* clipFor(std::string_view content);

    // --- Prefetch (D129) ----------------------------------------------------
    //
    // **The hitch this removes.** A sound's first play used to decode the whole
    // file wherever it was first asked for -- tens of milliseconds for a short
    // effect, and a three-minute track is about seventy megabytes of f32 that
    // has to be produced before the frame can end.
    //
    // The shape is a prefetch with a synchronous floor, and the floor is what
    // makes it legal: the read is asynchronous and the decode is a job, both
    // started from the FRAME and never from the tick, and if the tick asks for a
    // clip that has not landed it decodes it itself exactly as it always did. So
    // the result is identical whether the prefetch won the race or not, and the
    // world hash cannot learn anything about the disk.
    //
    // What it buys: a sound authored in a scene is prefetched from the first
    // frame the scene is alive, so by the time anything plays it the clip is
    // resident and neither path decodes at all. What it does NOT buy: a sound
    // created and played in the same tick still pays, which is what it paid
    // before.
    struct Prefetch
    {
        std::string urn;
        platform::IoRequest read;
        jobs::JobHandle decode;
        std::unique_ptr<ClipWork> work;
    };

    // Two at a time. A bank of forty sounds must not open forty files and hold
    // forty decoded clips at once, and this is a prefetch: being slow is exactly
    // what it is allowed to be.
    static constexpr core::usize MaxClipsInFlight = 2;

    std::vector<Prefetch> prefetching;

    [[nodiscard]] bool resident(std::string_view content) const noexcept;
    [[nodiscard]] bool prefetchInFlight(std::string_view content) const noexcept;
    void beginPrefetch(std::string_view content);
    void pumpPrefetch();
    void releasePrefetch() noexcept;
    const Clip* install(std::string_view content, std::shared_ptr<Clip> clip);

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
                (void)mixClip(samples, frameCount, *voice.clip, voice.cursor, voice.cursorStep,
                              voice.amplitude * voice.panLeft, voice.amplitude * voice.panRight, voice.positional,
                              voice.looped);
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

        // **The audition is mixed whatever the world is doing**, including while
        // the mixer is suspended -- suspension empties `voices` and the audition
        // was never in them. That is the point of it: the editor suspends audio
        // precisely because the world is not ticking, and a preview is what
        // somebody asks for in exactly that state.
        //
        // It is also the one cursor in this file the device advances on its own
        // authority, which it may do because nothing in the simulation can see
        // it.
        if (self->audition.active && self->audition.clip != nullptr) {
            // Centred and never folded: an audition is the file, and the file
            // is not anywhere.
            self->audition.active =
                mixClip(samples, frameCount, *self->audition.clip, self->audition.cursor, self->audition.cursorStep,
                        self->audition.amplitude, self->audition.amplitude, false, false);
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
    // Before anything else: a decode job writes into memory this object owns,
    // and tearing down while one runs is a use-after-free that reproduces on a
    // fast machine and never on a slow one.
    m_impl->releasePrefetch();
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
            // **Immediately, and that is now a decision rather than a
            // placeholder.** The comment here used to say "there is nothing to
            // load until M7", which stopped being true when M7 made `Content`
            // real -- so this fired before any decode had been attempted, eleven
            // lines above a `clipDuration` that performs one.
            //
            // Firing it when the clip is actually resident was considered and
            // rejected, because there is no way to do it that is both honest and
            // legal. Waiting for the prefetch would make the event's TICK depend
            // on disk speed, and `replay.cpp` hashes the events a tick raises
            // (R10). Forcing residency here instead would make every sound in a
            // scene decode on its first tick -- forty files at load, which is a
            // far worse version of the very stall D129 is about.
            //
            // So it stays immediate and it is documented as meaning "this sound
            // exists and its content is named", not "the bytes are here".
            sound.loadedFired = true;
            world.changes().push(scene::Change{scene::ChangeKind::InstanceEventNoArgs, id, {}, loaded});
        }

        if (!sound.playing)
            return;

        // **How long this sound is, rather than how long a placeholder tone is.**
        //
        // D092: this used to be `kPlaceholderDuration` for every sound in the
        // world, which was right for all of M6 -- nothing decoded a file, so
        // every sound WAS a one-second tone -- and was never revisited when M7
        // made `Content` real. A two-minute track played for one second and then
        // stopped itself, and the `Ended` it fired while doing so was a lie
        // about the file.
        //
        // Decoded on the first ask and answered from the cache after, so this is
        // a lookup per playing sound per tick and not a decode.
        const f64 duration = clipDuration(sound.content);

        sound.timePosition += fixedDt * static_cast<f64>(sound.playbackSpeed);
        if (sound.timePosition < duration)
            return;

        if (sound.looped) {
            // Wrapped rather than reset, so a loop does not lose the fraction of
            // a tick it overshot by -- which over a minute is a loop that drifts
            // against everything else in the scene.
            sound.timePosition = std::fmod(sound.timePosition, duration);
            return;
        }

        sound.timePosition = duration;
        sound.playing = false;
        world.changes().push(scene::Change{scene::ChangeKind::InstanceEventNoArgs, id, {}, ended});
    });
}

void AudioSystem::update(scene::World& world, core::InstanceId listener, const core::CFrameD* earOverride)
{
    if (m_impl == nullptr)
        return;

    // The whole frame, not just the point: the rotation is what decides left
    // from right, and reading only the position is what made every positional
    // sound come out of the middle.
    core::CFrameD ear;
    if (earOverride != nullptr) {
        ear = *earOverride;
    }
    else if (const scene::CameraComponent* camera = world.cameras().find(listener); camera != nullptr) {
        ear = camera->cframe;
    }

    // **Here and not in the tick** (D129). The read is asynchronous and its
    // completions land only in `pumpIo`, which is a frame safe point by design --
    // pumping from the tick would put an arbitrary moment between a read and the
    // world, which is what R10 forbids.
    m_impl->pumpPrefetch();

    std::vector<Voice> next;
    next.reserve(kMaxVoices);

    world.sounds().forEach([&](core::InstanceId id, const scene::SoundComponent& sound) {
        // Every sound with content, playing or not, and before any early return
        // below -- which is the whole point. A sound authored in a scene is
        // prefetched from the first frame the scene is alive, so by the time
        // anything plays it the clip is resident and neither path decodes.
        m_impl->beginPrefetch(sound.content);

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

        // Computed here and carried to the voice below, which is built after
        // the gain test: a sound too quiet to hear is a sound with no voice to
        // put a pan on.
        bool positional = false;
        f32 panLeft = 1.0f;
        f32 panRight = 1.0f;

        // Positional iff the sound is parented to a `BasePart`. The whole of
        // the 3D switch, and it is a property of where the instance sits rather
        // than a flag that could disagree with it.
        if (const scene::PartComponent* part = world.parts().find(world.parentOf(id)); part != nullptr) {
            const core::DVec3 delta = part->cframe.position - ear.position;
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

            // **Where it is, and not only how far.** Distance alone is half of
            // positional: it says a sound is near without saying it is on your
            // left, so turning around changed nothing at all.
            positional = true;
            detail::panGains(detail::panOf(ear, part->cframe.position), panLeft, panRight);
        }

        if (gain <= 0.0001f)
            return;

        Voice voice;
        voice.id = id;
        voice.amplitude = std::fmin(gain, 4.0f);
        voice.positional = positional;
        voice.panLeft = panLeft;
        voice.panRight = panRight;
        voice.clip = m_impl->clipFor(sound.content);
        if (voice.clip != nullptr) {
            // The SEED. Whether it is used is decided under the lock below,
            // against the cursor the callback has been advancing -- see
            // `Voice::cursor` and `detail::shouldTakeTimeline`.
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
        // **What a continuing voice keeps from the frame before**, matched by
        // instance id rather than by position in the list. By position is what
        // M6 shipped: it is stable only while the sound set is, so one sound
        // stopping handed every voice after it another sound's phase.
        constexpr f64 rate = static_cast<f64>(kSampleRate);
        for (Voice& fresh : next) {
            const Voice* previous = nullptr;
            for (const Voice& old : m_impl->voices) {
                if (old.id == fresh.id) {
                    previous = &old;
                    break;
                }
            }
            if (previous == nullptr)
                continue;

            // The tone's phase, so a continuing placeholder does not click once
            // a frame.
            fresh.phase = previous->phase;

            // A voice that changed clip -- somebody edited `Content` -- starts
            // where the timeline says and nowhere else. There is no continuity
            // between two different files to preserve.
            if (fresh.clip == nullptr || fresh.clip != previous->clip)
                continue;

            if (!detail::shouldTakeTimeline(previous->cursor / rate, fresh.cursor / rate,
                                            static_cast<f64>(fresh.clip->frames) / rate, fresh.looped,
                                            kResyncTolerance)) {
                fresh.cursor = previous->cursor;
            }
        }
        m_impl->voices.swap(next);
    }
}

namespace detail {

f32 panOf(const core::CFrameD& ear, const core::DVec3& source) noexcept
{
    const core::DVec3 delta{source.x - ear.position.x, source.y - ear.position.y, source.z - ear.position.z};

    // `Mat3` stores columns, and `lookAtCFrame` writes column 0 as RIGHT and
    // column 2 as BACK -- the look axis is -Z. Read straight out rather than
    // through a helper, because `column` is local to `math.cpp`.
    const auto alongRight = static_cast<f64>(ear.rotation.m[0][0]) * delta.x +
                            static_cast<f64>(ear.rotation.m[0][1]) * delta.y +
                            static_cast<f64>(ear.rotation.m[0][2]) * delta.z;
    const auto alongBack = static_cast<f64>(ear.rotation.m[2][0]) * delta.x +
                           static_cast<f64>(ear.rotation.m[2][1]) * delta.y +
                           static_cast<f64>(ear.rotation.m[2][2]) * delta.z;

    // The horizontal magnitude, which is what makes this an AZIMUTH rather than
    // a projection: dividing by the full distance would pull a source overhead
    // towards whichever side it leans, and dividing by nothing at all would let
    // a distant source pan harder than a near one at the same angle.
    const f64 horizontal = std::sqrt(alongRight * alongRight + alongBack * alongBack);
    if (!(horizontal > 1e-6)) {
        // Directly above, directly below, or exactly where the listener stands.
        // None of the three has a side, and a sound standing on the listener is
        // the one case where hard-panning would be most wrong.
        return 0.0f;
    }
    return static_cast<f32>(alongRight / horizontal);
}

void panGains(f32 pan, f32& left, f32& right) noexcept
{
    const f32 clamped = std::clamp(pan, -1.0f, 1.0f);
    // Quarter of a turn across the whole range: -1 lands on cos(0)=1 and
    // sin(0)=0, +1 on the other end, and the centre on 0.707 twice.
    const f64 angle = (static_cast<f64>(clamped) + 1.0) * 0.25 * 3.14159265358979;
    left = static_cast<f32>(std::cos(angle));
    right = static_cast<f32>(std::sin(angle));
}

bool shouldTakeTimeline(f64 mixerSeconds, f64 timelineSeconds, f64 duration, bool looped, f64 tolerance) noexcept
{
    f64 drift = mixerSeconds - timelineSeconds;
    if (looped && duration > 0.0) {
        // The SHORT way round the loop. A mixer that has already wrapped and a
        // timeline that has not are a whole clip apart by subtraction and a few
        // milliseconds apart in fact, and calling that a seek would put a click
        // at the top of every loop.
        drift = std::fmod(drift, duration);
        if (drift > duration * 0.5) {
            drift -= duration;
        }
        else if (drift < -duration * 0.5) {
            drift += duration;
        }
    }
    return std::fabs(drift) > tolerance;
}

} // namespace detail

namespace {

// The decode itself, shared by the synchronous path and the prefetch job so the
// two cannot drift into producing different clips from the same bytes.
//
// Straight into the MIXER's format -- f32, stereo, 48 kHz -- so the callback
// never resamples. miniaudio does the conversion as part of the decode, which is
// one pass over the data instead of two.
//
// Null for bytes that will not decode. **Nothing is logged here**: this runs on
// a job thread as often as not, and the caller knows the URN.
[[nodiscard]] std::shared_ptr<Clip> decodeClip(std::span<const std::byte> bytes)
{
    std::shared_ptr<Clip> clip;
    if (bytes.empty())
        return clip;

    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, kChannels, kSampleRate);
    ma_decoder decoder{};
    if (ma_decoder_init_memory(bytes.data(), bytes.size(), &config, &decoder) == MA_SUCCESS) {
        // **The declared length is a hint, and reading until the decoder
        // stops is the answer.** D094: this used to require a length and
        // drop the clip when it could not get one, and miniaudio's own
        // header says it cannot get one for Ogg Vorbis -- a format this
        // engine's API documents as supported, and which therefore played
        // the placeholder tone instead of the file. The estimate is also a
        // frame or two out either way once a resampler is in the path, which
        // is a clipped tail or a silent one.
        ma_uint64 hint = 0;
        if (ma_decoder_get_length_in_pcm_frames(&decoder, &hint) != MA_SUCCESS) {
            hint = 0;
        }

        // One second at a time when there is no hint: large enough that a
        // short sound is a single read, small enough that the slack on the
        // last chunk is not worth measuring.
        constexpr core::usize kChunkFrames = kSampleRate;

        clip = std::make_shared<Clip>();
        clip->samples.resize((hint > 0 ? static_cast<core::usize>(hint) : kChunkFrames) * kChannels);

        core::usize filled = 0;
        for (;;) {
            core::usize capacity = clip->samples.size() / kChannels;
            if (filled == capacity) {
                capacity += kChunkFrames;
                clip->samples.resize(capacity * kChannels);
            }
            ma_uint64 read = 0;
            const ma_result status = ma_decoder_read_pcm_frames(&decoder, clip->samples.data() + filled * kChannels,
                                                                static_cast<ma_uint64>(capacity - filled), &read);
            filled += static_cast<core::usize>(read);
            // Both halves are an end: `MA_AT_END` with a partial read is the
            // last chunk of a file, and a successful read of nothing is a
            // decoder that has no more to give.
            if (status != MA_SUCCESS || read == 0) {
                break;
            }
        }

        clip->frames = static_cast<u32>(filled);
        clip->samples.resize(filled * kChannels);
        clip->samples.shrink_to_fit();
        if (clip->frames == 0) {
            clip.reset();
        }
        ma_decoder_uninit(&decoder);
    }
    return clip;
}

} // namespace

bool AudioSystem::Impl::resident(std::string_view content) const noexcept
{
    const auto at = std::lower_bound(clips.begin(), clips.end(), content,
                                     [](const auto& entry, std::string_view key) { return entry.first < key; });
    return at != clips.end() && at->first == content;
}

bool AudioSystem::Impl::prefetchInFlight(std::string_view content) const noexcept
{
    return std::any_of(prefetching.begin(), prefetching.end(),
                       [content](const Prefetch& entry) { return entry.urn == content; });
}

const Clip* AudioSystem::Impl::install(std::string_view content, std::shared_ptr<Clip> clip)
{
    const auto at = std::lower_bound(clips.begin(), clips.end(), content,
                                     [](const auto& entry, std::string_view key) { return entry.first < key; });
    // Already there because the synchronous path won the race, which is not an
    // error and is the ordinary outcome for a sound played the tick it was made.
    // The prefetch's answer is dropped rather than replacing a clip the audio
    // thread may be holding a raw pointer into.
    if (at != clips.end() && at->first == content)
        return at->second.get();

    const bool decoded = clip != nullptr;
    const auto inserted = clips.insert(at, {std::string(content), std::move(clip)});
    if (decoded)
        clipsLoaded.fetch_add(1, std::memory_order_relaxed);
    else
        clipsMissing.fetch_add(1, std::memory_order_relaxed);
    return inserted->second.get();
}

void AudioSystem::Impl::beginPrefetch(std::string_view content)
{
    if (content.empty() || mounts == nullptr)
        return;
    if (prefetching.size() >= MaxClipsInFlight)
        return;
    if (resident(content) || prefetchInFlight(content))
        return;

    const asset::ResolvedContent resolved = mounts->resolve(content);

    Prefetch entry;
    entry.urn = std::string(content);
    entry.work = std::make_unique<ClipWork>();

    if (!resolved.bytes.empty()) {
        // Already in memory -- a pack mount, whose bytes are valid for as long
        // as the mount is. Straight to the decode; there is nothing to read.
        ClipWork* work = entry.work.get();
        const std::span<const std::byte> packed = resolved.bytes;
        entry.decode = jobs::schedule("audio.clip.decode", jobs::Domain::AssetIo,
                                      [work, packed]() noexcept { work->clip = decodeClip(packed); });
    }
    else if (resolved.source == asset::ResolvedContent::Source::Loose) {
        // `Low`, because this is speculation by definition: nothing is waiting
        // on it, and anything that IS waiting takes the synchronous floor.
        entry.read = platform::readFileAsync(resolved.path, platform::IoPriority::Low);
        if (!entry.read.valid())
            return;
    }
    else {
        return;
    }

    prefetching.push_back(std::move(entry));
}

void AudioSystem::Impl::pumpPrefetch()
{
    if (prefetching.empty())
        return;

    // **Only when something is in flight.** An unconditional pump would drain
    // completions for other subsystems on frames where they do not currently
    // land, which can move streaming-dependent output.
    platform::pumpIo();

    for (core::usize index = 0; index < prefetching.size();) {
        Prefetch& entry = prefetching[index];
        bool done = false;

        if (entry.read.valid()) {
            const platform::IoStatus status = platform::ioStatus(entry.read);
            if (status == platform::IoStatus::Ready) {
                if (platform::takeIoResult(entry.read, entry.work->bytes)) {
                    entry.read = {};
                    ClipWork* work = entry.work.get();
                    entry.decode = jobs::schedule("audio.clip.decode", jobs::Domain::AssetIo,
                                                  [work]() noexcept { work->clip = decodeClip(work->bytes); });
                }
                else {
                    done = true;
                }
            }
            else if (status != platform::IoStatus::Pending) {
                // Terminal and not consumed, so the slot is released by hand --
                // D131, where every failed read leaked one and all asynchronous
                // IO in the process stopped at five hundred and twelve.
                platform::cancelIo(entry.read);
                done = true;
            }
        }
        else if (entry.decode.valid() && jobs::finished(entry.decode)) {
            (void)install(entry.urn, std::move(entry.work->clip));
            done = true;
        }
        else if (!entry.decode.valid()) {
            done = true;
        }

        if (done) {
            prefetching.erase(prefetching.begin() + static_cast<std::ptrdiff_t>(index));
        }
        else {
            ++index;
        }
    }
}

void AudioSystem::Impl::releasePrefetch() noexcept
{
    for (Prefetch& entry : prefetching) {
        // **Waited for, not abandoned.** The job writes into memory this object
        // owns, and returning while one runs is a use-after-free that reproduces
        // on a fast machine and never on a slow one.
        if (entry.decode.valid())
            jobs::wait(entry.decode);
        if (entry.read.valid())
            platform::cancelIo(entry.read);
    }
    prefetching.clear();
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
    const asset::ResolvedContent resolved = mounts->resolve(content);
    std::vector<std::byte> owned;
    std::span<const std::byte> bytes = resolved.bytes;
    if (bytes.empty() && resolved.source == asset::ResolvedContent::Source::Loose) {
        if (platform::readFile(resolved.path, owned)) {
            bytes = owned;
        }
    }

    std::shared_ptr<Clip> clip;
    if (!bytes.empty()) {
        clip = decodeClip(bytes);
        if (clip == nullptr) {
            const core::I18nArg args[] = {{"content", std::string(content)}};
            core::log(core::LogLevel::Warn, LUAUG_TR("audio.warn.undecodable"), args);
        }
    }
    else {
        const core::I18nArg args[] = {{"content", std::string(content)}};
        core::log(core::LogLevel::Warn, LUAUG_TR("audio.warn.content_missing"), args);
    }

    return install(content, std::move(clip));
}

f64 AudioSystem::clipDuration(std::string_view content)
{
    if (m_impl == nullptr) {
        return kPlaceholderDuration;
    }
    const Clip* clip = m_impl->clipFor(content);
    if (clip == nullptr || clip->frames == 0) {
        // The tone's length, because the tone is what such a sound plays.
        return kPlaceholderDuration;
    }
    return static_cast<f64>(clip->frames) / static_cast<f64>(kSampleRate);
}

void AudioSystem::audition(std::string_view content, f32 volume, f32 speed)
{
    if (m_impl == nullptr) {
        return;
    }
    // Decoded outside the lock, like `update` does and for the same reason: a
    // decode is file I/O and the audio callback must never wait on one.
    const Clip* clip = m_impl->clipFor(content);

    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->audition.clip = clip;
    m_impl->audition.content.assign(content);
    m_impl->audition.cursor = 0.0;
    m_impl->audition.cursorStep = static_cast<f64>(std::fmax(speed, 0.01f));
    m_impl->audition.amplitude = std::clamp(volume, 0.0f, 1.0f);
    // **A file that will not decode is not auditioned.** The warning `clipFor`
    // already logged is the honest answer, and a preview button that latches on
    // silence says the opposite of what happened.
    m_impl->audition.active = clip != nullptr;
}

void AudioSystem::stopAudition() noexcept
{
    if (m_impl == nullptr) {
        return;
    }
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->audition.active = false;
    m_impl->audition.clip = nullptr;
    m_impl->audition.content.clear();
}

bool AudioSystem::auditioning(std::string_view content) const
{
    if (m_impl == nullptr) {
        return false;
    }
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (!m_impl->audition.active) {
        return false;
    }
    return content.empty() || m_impl->audition.content == content;
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
    // The audition holds one of those pointers too.
    m_impl->audition.active = false;
    m_impl->audition.clip = nullptr;
    m_impl->audition.content.clear();
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
