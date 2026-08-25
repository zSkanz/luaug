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

#include "luaug/asset/content.h"
#include "luaug/core/error.h"
#include "luaug/core/id.h"
#include "luaug/core/math.h"
#include "luaug/core/types.h"

#include <optional>
#include <string_view>

namespace luaug::scene {
class World;
}

namespace luaug::audio {

using core::f32;
using core::f64;
using core::u32;
using core::u64;

namespace detail {

// **When the mixer's cursor is taken from the simulation instead of kept.**
//
// The two clocks are the same clock in a healthy run -- the simulation advances
// `TimePosition` by a fixed step and the device advances the cursor by a buffer
// -- but they are quantised differently, so on any given frame one of them is
// ahead. Re-seeding the cursor from the timeline every frame is what D093 was:
// a frame in which no tick ran dragged the cursor BACK by a frame's worth of
// samples and the mixer replayed sixteen milliseconds it had already played,
// sixty times a second.
//
// So the cursor is kept, and the timeline is taken only when the two have
// genuinely parted company -- which is a seek, a rewind, or a sound that was
// stopped and started again. `tolerance` is what "genuinely" means: a drift
// smaller than it is the two clocks being quantised differently, and a drift
// larger than it is somebody having moved one of them.
//
// A looped sound is compared the short way round the loop, so the frame in
// which the mixer has wrapped and the timeline has not is not mistaken for a
// seek to the beginning of the file.
[[nodiscard]] bool shouldTakeTimeline(f64 mixerSeconds, f64 timelineSeconds, f64 duration, bool looped,
                                      f64 tolerance) noexcept;

// **Where a source sits across the listener**, as the sine of its azimuth: -1
// hard left, +1 hard right, 0 straight ahead.
//
// **Horizontal only, and that is a decision rather than a simplification.** Two
// speakers cannot express elevation: a source directly overhead has no left and
// no right, and panning it by whatever sliver of horizontal offset it happens to
// have would swing it across the field as somebody looked up.
//
// **Straight behind is also 0**, and that is honest rather than wrong. Front and
// back are indistinguishable on two channels without a head model, and pretending
// otherwise means choosing a side for a sound that is on neither.
[[nodiscard]] f32 panOf(const core::CFrameD& ear, const core::DVec3& source) noexcept;

// The two channel gains for a pan, constant POWER rather than constant
// amplitude.
//
// Two channels at half amplitude are quieter than one channel at full, so a
// linear pan dips in the middle -- a source crossing in front of the listener
// audibly ducks as it passes. Both gains are 0.707 at the centre, which keeps
// the sum of squares at one wherever it is.
void panGains(f32 pan, f32& left, f32& right) noexcept;

} // namespace detail

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
    // How many distinct `Sound.Content` files have been decoded, and how many
    // were asked for and could not be.
    //
    // Here because "is this the real sound or the placeholder tone" is a
    // question a person listening cannot always answer -- a short file and a
    // short tone are hard to tell apart on laptop speakers -- and a number is.
    u32 clipsLoaded = 0;
    u32 clipsMissing = 0;
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
    // Where `Sound.Content` is resolved from. Null -- the state of a test and
    // of a world that has gone away -- makes every sound the placeholder tone,
    // which is what this class did for all of M6.
    void setContentMounts(const asset::ContentMounts* mounts) noexcept;

    [[nodiscard]] std::optional<core::EngineError> start(bool headless);
    void stop();

    // Advances every playing sound by one tick and raises `Ended` and `Loaded`
    // on the world's change queue. Called from the sim tick.
    void tick(scene::World& world, f64 fixedDt);

    // How long the sound this content names is, in seconds -- decoding it on the
    // first ask, like `update` does, and answering from the same cache after.
    //
    // **This is what the timeline is measured against**, which is why it is here
    // rather than inside the mixer: `Ended` fires when `TimePosition` reaches a
    // sound's LENGTH, and until D092 that length was a one-second constant for
    // every sound in the world -- so a two-minute track stopped after a second.
    // A content that names nothing, or that cannot be decoded, answers with the
    // placeholder tone's length, because the tone is what such a sound plays.
    [[nodiscard]] f64 clipDuration(std::string_view content);

    // **Auditioning a file is not the game playing a sound**, and this is the
    // whole difference between the two.
    //
    // Somebody clicking a speaker in the properties grid wants to hear what a
    // `Content` names. Every way of doing that THROUGH the `Sound` is wrong in
    // the same way: `Playing` is the game's state, `Ended` is a past-tense fact
    // about the simulation's timeline, and a world that is not ticking cannot
    // advance either. So an audition is its own voice with its own cursor,
    // advanced by the DEVICE -- which is exactly why it may not be a `Sound`. A
    // timeline the wall clock drives is the one thing the rest of this class
    // exists to keep out of the world (R10).
    //
    // It is heard while `setSuspended` is on, which is the state of an editor
    // that is not playing, and it stops on its own at the end of the clip.
    // Auditioning while one is already playing replaces it: the button is a
    // preview and two previews at once is not a thing anybody asked for.
    void audition(std::string_view content, f32 volume, f32 speed);
    void stopAudition() noexcept;

    // Whether an audition is running -- of this content, or of anything when
    // `content` is empty. False the moment it reaches the end, which is what
    // turns a pause button back into a play button.
    [[nodiscard]] bool auditioning(std::string_view content = {}) const;

    // Pushes this frame's voice state to the mixer. Called once per frame, after
    // the ticks -- what the speakers do is a consequence of the simulation.
    //
    // While suspended it pushes silence and reads the world anyway, which is the
    // difference between pausing and stopping: a `Sound`'s `TimePosition` and
    // `Playing` are untouched, so resuming continues rather than restarts.
    //
    // `ear` overrides where the listener STANDS AND WHICH WAY IT FACES, for the
    // one caller that has a camera the world does not contain: an editor
    // rendering through its own view because the game made none. Null -- every
    // other caller -- puts the ear on `listener`, and a `listener` naming no
    // camera leaves it at the origin facing -Z, which is what a world with no
    // camera has always sounded like.
    //
    // A frame rather than an instance, because an override camera is not in the
    // world and has no id to name. It is the same argument `ViewOverride` makes
    // to the renderer, and it has to be the same DECISION as well or the picture
    // and the sound disagree about where you are standing -- and now about which
    // way you are looking, because the rotation is what decides left from right.
    void update(scene::World& world, core::InstanceId listener, const core::CFrameD* ear = nullptr);

    // Silences the mixer without changing a single `Sound` (D060).
    //
    // The editor is what needs this: a world that is not ticking should not be
    // audible, and the alternative -- pausing a game and still hearing its
    // ambience -- is the same wrong-owner mistake as an editor whose cursor
    // belongs to the game. It is a HOST decision, and no script can reach it:
    // `SoundService.Volume` is the game's control and this is the tool's.
    void setSuspended(bool suspended) noexcept { m_suspended = suspended; }
    [[nodiscard]] bool suspended() const noexcept { return m_suspended; }

    [[nodiscard]] AudioStats stats() const noexcept;

private:
    bool m_suspended = false;

    struct Impl;
    // A pointer rather than a member so that `miniaudio.h` -- 95,000 lines of
    // it -- stays out of every translation unit that includes this header. R17
    // is about the Luau API; the same instinct applies one layer down.
    Impl* m_impl = nullptr;
};

} // namespace luaug::audio
