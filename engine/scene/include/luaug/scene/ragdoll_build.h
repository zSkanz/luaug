// Turning a rig into a ragdoll: the parts, the bones and the joints that hold
// them (E9 step 13).
//
// **A ragdoll is instances, not a hidden body list.** `Ragdoll` owns nothing --
// the class doc says so and the mirror rule requires it -- so building one means
// creating real `Part`s, real `Bone`s and real constraints under it, every one of
// them a thing you can select, move and delete. What this function does is the
// arithmetic nobody wants to do by hand: where each limb goes, how long it is,
// which way it points, and which joint it stands for.
//
// **Why it is a function in `scene` rather than a method in the binding.** The
// only part of it that can be wrong is geometric -- a capsule whose length came
// out of the wrong pair of joints, a frame whose Y does not run down the bone --
// and that is a thing a test can assert with an invented rig and no VM at all.
// `Ragdoll:Build` is thirty lines on top of this that read a Luau table.
//
// **Determinism (R10).** Limbs are created in the profile's own order, and the
// profile is an ordered list for exactly that reason: a map would leave creation
// order to whatever the table iterated in, and creation order is what instance
// ids are. Every id this produces is therefore a function of the profile and the
// rig, which is what makes a ragdoll replayable.
#pragma once

#include "luaug/core/id.h"
#include "luaug/core/text_key.h"
#include "luaug/core/types.h"
#include "luaug/scene/class_registry.h"

#include <optional>
#include <string>
#include <vector>

namespace luaug::scene {

class World;
class SkeletonHost;

using core::f32;
using core::u32;

// One bone of the ragdoll: a part, the `Bone` that says which joint it is, and
// the joint that holds it to its parent.
struct RagdollLimb
{
    // **What this limb is called in the rig, most-preferred first.** A list
    // rather than a name because the same shoulder is `mixamorig:LeftArm`,
    // `LeftArm`, `upper_arm.L` or `Bip01 L UpperArm` depending on who exported
    // it, and a profile that named one of those would be a profile for one
    // exporter. The first candidate the rig has is the one used.
    std::vector<std::string> joints;

    // The limb this one hangs from, by INDEX into the profile, or -1 for the
    // root. An index rather than a name because a name would have to be resolved
    // against the profile's own aliases -- twice, once per spelling.
    //
    // **Must point BACKWARDS.** A profile is built root-first, so a forward
    // reference is a limb whose parent does not exist yet, and `build` refuses
    // one rather than producing a joint to an invalid id.
    core::i32 parent = -1;

    // The capsule's radius. Its LENGTH is derived: the distance from this limb's
    // joint to the first CHILD limb's joint, which is what a bone's length is.
    // A leaf -- a hand, a foot, a head -- has no child to measure against and
    // uses `leafLength` instead.
    f32 radius = 0.08f;
    f32 leafLength = 0.18f;

    // `physics::ConstraintType`'s value, stored raw for the reason
    // `ConstraintComponent` stores it that way: `scene` names the constraint
    // classes and the backend names the solver, and neither needs the other's
    // enum in its signature.
    core::i32 kind = 0;

    // Swing cone half-angle and twist range for a swing-twist, in RADIANS --
    // unlike the IDL property, which is in degrees because that is what a person
    // types. Ignored by the other kinds.
    f32 swingLimit = 0.7f;
    f32 twistLimit = 0.4f;

    // Hinge range about the joint's own X, in radians. `low > high` is
    // unlimited, matching `ConstraintComponent`.
    f32 limitLow = 1.0f;
    f32 limitHigh = -1.0f;
};

struct RagdollProfile
{
    std::vector<RagdollLimb> limbs;
};

// The classes this creates, resolved by the caller.
//
// **Passed in rather than looked up by name**, and that is not a testing
// convenience. The class NAMES come from the IDL, which is generated -- so a
// function in `scene` that interned `"BallSocketConstraint"` would be `scene`
// depending on a string the generator owns, invisibly, with a silent no-op as
// the failure mode. `resolveRagdollClasses` below does the lookup in one place
// so there is still only one spelling of each.
struct RagdollClasses
{
    ClassId part = InvalidClass;
    ClassId bone = InvalidClass;
    ClassId attachment = InvalidClass;
    ClassId ballSocket = InvalidClass;
    ClassId hinge = InvalidClass;
    ClassId fixed = InvalidClass;

    [[nodiscard]] bool complete() const noexcept
    {
        return part != InvalidClass && bone != InvalidClass && attachment != InvalidClass &&
               ballSocket != InvalidClass && hinge != InvalidClass && fixed != InvalidClass;
    }
};

// The shipped hierarchy's ids, by the names `api/defs/instances.api.luau`
// declares. Any of them may come back `InvalidClass` in a registry that does not
// have them, which `complete()` is for.
[[nodiscard]] RagdollClasses resolveRagdollClasses(World& world);

struct RagdollBuildResult
{
    // Set when nothing was built, and the reason -- a key, never a sentence
    // (R3). An empty optional means it worked.
    std::optional<core::TextKey> error;

    u32 limbs = 0;
    u32 constraints = 0;

    // Limbs the rig had no joint for. **Not an error**, and this is the whole
    // reason a ragdoll can be partial: one humanoid profile has to work on a rig
    // with no toes, and the joints nobody drives keep their place relative to
    // their parent when the forward pass re-runs. Reported so a caller can say
    // so rather than so a caller can fail.
    u32 skipped = 0;
};

// Builds the parts, bones and constraints for `ragdoll` -- which must be
// parented to a `MeshPart` with a skeleton -- from `profile`, placing every limb
// at the rig's CURRENT pose.
//
// Refuses, without creating anything, when: the ragdoll's parent has no rig
// (`scene.err.ragdoll_no_rig`), the ragdoll already has children
// (`scene.err.ragdoll_already_built`), the profile is empty or its first limb is not a
// root (`scene.err.ragdoll_bad_profile`), a limb's parent index points forward or out
// of range (`scene.err.ragdoll_bad_profile`), or the rig has none of the profile's
// joints at all (`scene.err.ragdoll_no_joints_matched`).
//
// **Placed at the current pose, not at the bind pose.** A character that goes
// down mid-stride has to fall from where it was standing, and `jointModel`
// answers for both -- a mesh with no clip playing is in bind pose, which is the
// same call.
[[nodiscard]] RagdollBuildResult buildRagdoll(World& world, const SkeletonHost& skeleton, core::InstanceId ragdoll,
                                              const RagdollProfile& profile, const RagdollClasses& classes);

} // namespace luaug::scene
