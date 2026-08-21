// Audio (ADR 0009, api-design.md §2.1 and §2.2).
//
// **The simulation owns the timeline and the device is downstream.** That is
// the whole design and it is the M6 brief's Decision 9: `Sound.TimePosition`
// advances by `FixedTimestep * PlaybackSpeed` per tick, `Ended` is raised from
// that timeline at a drain, and `AudioSystem::update` pushes the resulting voice
// state to the mixer once per frame, after the tick. If the mixer's position
// were the source of truth, a script reading `TimePosition` would be reading the
// wall clock through a side door (R10) and the same replay would diverge between
// two machines with different buffer sizes.
//
// **What v1 actually makes a sound with.** `Sound.Content` is `Inert` until M7's
// asset pipeline can decode a file, so a voice synthesizes a short enveloped
// tone whose pitch comes from a hash of the content id. It is obviously a
// placeholder and it is deliberately not silence: a silent audio system is one
// nobody can tell is broken, and the mixer, the buses, the distance attenuation
// and the underrun counter are all real and all exercised by it.
//
// **The underrun counter is ours.** The roadmap's gate says "buffer underrun
// counter zero in a 60 s soak", and the string `underrun` appears in miniaudio
// only in comments and ALSA log messages -- there is no counter to read. So this
// module defines one, and defines what it counts: a data callback that found the
// command ring empty of an update it was expecting, plus every command dropped
// because that ring was full. Both are real failure modes of the design above,
// and both are zero in a healthy run.
#pragma once

#include "luaug/core/error.h"
#include "luaug/core/id.h"
#include "luaug/core/math.h"
#include "luaug/core/types.h"

#include <optional>

namespace luaug::scene {
class World;
}

namespace luaug::audio {

using core::f32;
using core::f64;
using core::u32;
using core::u64;

struct AudioStats
{
    // See the header comment. Zero in a healthy run, and the gate says so.
    u64 underruns = 0;
    u64 droppedCommands = 0;
    // How many voices the mixer summed on its last callback.
    u32 activeVoices = 0;
    // False when no device could be opened and the null backend is in use --
    // every CI runner, and any machine with no sound card. Not an error: a game
    // that cannot open a device still has to run.
    bool deviceOpen = false;
};

// The mixer and the timeline. Held by `app` beside the physics mirror and the
// input system, and for the same reasons: `scene` cannot own it without L3
// depending on a device, and a process-global would make two worlds share one.
class AudioSystem
{
public:
    AudioSystem() = default;
    ~AudioSystem();

    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    // Opens a device, or falls back to the null backend and says so once at
    // Info. `headless` skips the attempt entirely: a headless run has no reason
    // to hold an audio device open, and on a CI runner the attempt is a wasted
    // second and a log line nobody reads.
    [[nodiscard]] std::optional<core::EngineError> start(bool headless);
    void stop();

    // Advances every playing sound by one tick and raises `Ended` and `Loaded`
    // on the world's change queue. Called from the sim tick.
    void tick(scene::World& world, f64 fixedDt);

    // Pushes this frame's voice state to the mixer. Called once per frame, after
    // the ticks -- what the speakers do is a consequence of the simulation.
    void update(scene::World& world, core::InstanceId listener);

    [[nodiscard]] AudioStats stats() const noexcept;

private:
    struct Impl;
    // A pointer rather than a member so that `miniaudio.h` -- 95,000 lines of
    // it -- stays out of every translation unit that includes this header. R17
    // is about the Luau API; the same instinct applies one layer down.
    Impl* m_impl = nullptr;
};

} // namespace luaug::audio
