// Reading a skeleton, and driving one, from below `render`.
//
// **The pose is computed in the sim tick and stored in the renderer**, and that
// split is not an accident -- `AnimationSystem::sample` runs at `PreAnimation`
// (architecture.md §3, step 5b) while the joint array it fills lives in
// `render::Pose`, because a palette is a thing a vertex shader consumes.
//
// `AnimationHost` deliberately names no joint: it is about tracks, clips and
// weights, and a caller of it never asks where an elbow is. This is the second,
// narrower seam for the callers that do -- a socket welded to a hand, a ragdoll
// writing a simulated limb back into a pose, an inverse-kinematic solver
// bending an arm. It is the third instance of a pattern `IPhysics3D*` and
// `AnimationHost*` already established: an interface in `scene`, an
// implementation in the module that owns the data, and `app` holding the wire.
//
// **A joint is named by its mesh and its index**, never by a pointer into the
// rig. The rig is reloaded when its file changes, and an index survives that
// where a pointer does not.
#pragma once

#include "luaug/core/id.h"
#include "luaug/core/math.h"
#include "luaug/core/types.h"

#include <string_view>

namespace luaug::scene {

using core::i32;
using core::u32;

class SkeletonHost
{
public:
    virtual ~SkeletonHost() = default;

    // --- Reading the rig -----------------------------------------------------

    // Zero for a mesh with no skeleton, which is most of them.
    [[nodiscard]] virtual u32 jointCount(core::InstanceId meshPart) const = 0;

    // The joint's index, or -1. Names come from the file the artist exported,
    // so this is how a `Bone` finds the thing it was authored against.
    [[nodiscard]] virtual i32 findJoint(core::InstanceId meshPart, std::string_view name) const = 0;

    // The parent's index, or -1 for a root. Never a cycle: the loader sorts a
    // skeleton parents-first and refuses one that cannot be.
    [[nodiscard]] virtual i32 jointParent(core::InstanceId meshPart, u32 joint) const = 0;

    [[nodiscard]] virtual std::string_view jointName(core::InstanceId meshPart, u32 joint) const = 0;

    // Where the joint is, in the MESH's own space -- so a caller composes it
    // with the `MeshPart`'s own `CFrame` to get a world transform, exactly as it
    // would for anything else parented to a part.
    //
    // **Answers for a mesh in bind pose too**, and that is the point of it being
    // a call rather than a peek at the palette: a character standing still has
    // no pose at all, and a socket on its hand still has to be somewhere. False
    // only when there is no such joint.
    [[nodiscard]] virtual bool jointModel(core::InstanceId meshPart, u32 joint, core::CFrameD& out) const = 0;

    // --- Driving it ----------------------------------------------------------
    //
    // An override REPLACES a joint's model-space transform for one tick, and
    // `commitOverrides` then re-runs the forward pass so everything below it
    // follows. That last part is why this is not just "write into the palette":
    // a ragdoll simulates a dozen bones and a hand has twenty, and the fingers
    // have to ride along on the wrist rather than stay where the clip left them.
    //
    // Overrides are set every tick by whatever owns them and cleared by
    // `commitOverrides`. Nothing here persists across a tick, because a pose
    // that outlived the thing driving it is a character frozen mid-stride.

    virtual void setJointOverride(core::InstanceId meshPart, u32 joint, const core::CFrameD& model) = 0;

    // Drops this mesh's overrides without committing, for a ragdoll that has
    // just been switched off.
    virtual void clearJointOverrides(core::InstanceId meshPart) = 0;

    // Applies every override set since the last call, rebuilds the palettes that
    // changed, and clears them. Called once per tick, after physics has written
    // back -- `PhysicsSync::step` names the exact place.
    virtual void commitOverrides() = 0;
};

} // namespace luaug::scene
