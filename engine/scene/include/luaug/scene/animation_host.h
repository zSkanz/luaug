// The seam between the Instance tree and skeletal animation.
//
// Exactly the shape `PhysicsSync` uses for `IPhysics3D*`, and for the same
// reason: the thing that can answer these questions is `render` (it owns the
// loaded clips and the skeleton), the thing that asks them is `script` (it owns
// the `AnimationTrack` userdata and its signal), and neither may include the
// other -- `script` is L5 and `render` is L4, but architecture.md §2 does not
// give `script` a `render` dependency and this is not the milestone to add one.
//
// So the interface lives here at L3, `render` implements it, `app` injects it,
// and `script` holds a pointer. Nothing in this header names a clip, a joint or
// a matrix: what crosses is a track id and six numbers.
//
// **Null is a real state and not an error.** A build with no render module has
// no animation host, and every call site checks -- exactly as it does for the
// physics mirror, which answers `nil` to a raycast in a world with no physics.
#pragma once

#include "luaug/core/id.h"
#include "luaug/scene/types.h"

#include <span>
#include <string_view>

namespace luaug::scene {

// A handle into the host's own table. 0 is "no track", which is what
// `LoadAnimation` returns for a player whose mesh has no such clip -- and the
// reason it returns a track at all rather than nil is that a mesh which has not
// finished loading would otherwise make a perfectly ordinary frame a crash.
using TrackId = u32;

// Everything a script can read off a track, in one read.
//
// One struct rather than six virtual getters: a property read crosses this seam
// and a virtual call per property would be six for what a HUD does every frame.
struct TrackState
{
    f64 timePosition = 0.0;
    f32 length = 0.0f;
    f32 speed = 1.0f;
    f32 weight = 1.0f;
    bool looped = false;
    bool playing = false;
};

class AnimationHost
{
public:
    virtual ~AnimationHost() = default;

    // A track for one clip of `player`'s parented mesh, by name. An empty name
    // means the file's first clip. Returns 0 when the player has no skeleton or
    // the clip is not there; the track still exists and still answers reads,
    // with a length of zero.
    [[nodiscard]] virtual TrackId createTrack(core::InstanceId player, std::string_view clip) = 0;

    virtual void play(TrackId track, f32 fadeTime, f32 weight, f32 speed) = 0;
    virtual void stop(TrackId track, f32 fadeTime) = 0;
    virtual void adjustWeight(TrackId track, f32 weight, f32 fadeTime) = 0;
    virtual void adjustSpeed(TrackId track, f32 speed) = 0;
    virtual void setLooped(TrackId track, bool looped) = 0;

    [[nodiscard]] virtual TrackState state(TrackId track) const = 0;

    // Advances every playing track by one tick and rebuilds each player's pose.
    // Called at `PreAnimation` (architecture.md §3, step 5b).
    virtual void sample(f64 fixedDt) = 0;

    // The tracks that reached the end of a non-looping clip since the last call.
    // Drained rather than pushed as a `Change`, because a `scene::Change` names
    // an Instance and a track is not one -- and inventing a change kind that
    // carried a track id would put animation's vocabulary in scene's queue.
    [[nodiscard]] virtual std::span<const TrackId> drainEnded() = 0;

    // Forgets every track belonging to instances that no longer exist. Called
    // once per frame: a track is a reference to a player and never a reason to
    // keep one alive.
    virtual void retire(const class World& world) = 0;
};

} // namespace luaug::scene
