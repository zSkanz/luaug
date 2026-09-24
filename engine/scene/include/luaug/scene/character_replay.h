// Stepping one character again, for a replica correcting its prediction
// (ADR 0076, as amended by the owner's mandate of 2026-09-23).
//
// **The narrow interface replication needs from the physics mirror**, in the
// shape `AnimationHost` and `SkeletonHost` already set: `scene` declares it,
// `PhysicsSync` implements it, and `app` hands one to the replication module.
// Replication never learns there is a physics backend.
#pragma once

#include "luaug/core/id.h"
#include "luaug/core/math.h"
#include "luaug/core/types.h"

#include <optional>
#include <span>
#include <vector>

namespace luaug::scene {

// **What one tick told a character controller to do**, as `PhysicsSync`
// consumed it: the direction and speed it walked at, whether it jumped and how
// hard, and the tick's length. Everything a step of the movement model reads
// that is not the world around it.
struct CharacterCommand
{
    core::Vec3 moveDirection{0.0f, 0.0f, 0.0f};
    bool jump = false;
    core::f32 walkSpeed = 0.0f;
    core::f32 jumpSpeed = 0.0f;
    core::f32 dt = 0.0f;
};

// Where a replay starts: the authority's word on the character at the tick it
// last answered, including the two things a transform does not say.
struct CharacterReplayStart
{
    core::CFrameD transform;
    core::f32 verticalVelocity = 0.0f;
    bool grounded = false;
};

class ICharacterReplay
{
public:
    virtual ~ICharacterReplay() = default;

    // The command the most recent step applied to `character`, or nothing when
    // it was not stepped here -- no controller yet, or a character this
    // machine only follows.
    [[nodiscard]] virtual std::optional<CharacterCommand> lastCommand(core::InstanceId character) const = 0;

    // **Puts `character` at `start` and steps it through `commands`**, one
    // step each, with the same movement model the simulation steps it with, in
    // the world as it is now. Answers the transform after each step and leaves
    // the character -- part, controller and vertical velocity -- where the last
    // one did. Empty when the character has no controller to step.
    [[nodiscard]] virtual std::vector<core::CFrameD> replay(core::InstanceId character,
                                                            const CharacterReplayStart& start,
                                                            std::span<const CharacterCommand> commands) = 0;
};

} // namespace luaug::scene
