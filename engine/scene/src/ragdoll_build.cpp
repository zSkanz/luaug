#include "luaug/scene/ragdoll_build.h"

#include "luaug/scene/class_registry.h"
#include "luaug/scene/components.h"
#include "luaug/scene/skeleton_host.h"
#include "luaug/scene/world.h"

#include <cmath>

namespace luaug::scene {

namespace {

using core::CFrameD;
using core::DVec3;
using core::i32;
using core::Vec3;

// A quarter turn, and the one number in this file worth naming: it is what
// carries a look-at frame's -Z onto a capsule's +Y.
constexpr f32 kQuarterTurn = 1.5707963267948966f;

// What one limb resolved to, so the second pass can build joints without
// resolving anything a second time.
struct Built
{
    core::InstanceId part;
    // Where the limb's own joint is, in world space -- the point a constraint
    // between this limb and its parent sits on.
    DVec3 origin;
    u32 joint = 0;
    bool made = false;
};

// The rig's index for the first candidate spelling it has, or -1.
[[nodiscard]] i32 resolve(const SkeletonHost& skeleton, core::InstanceId rig, const RagdollLimb& limb)
{
    for (const std::string& candidate : limb.joints) {
        const i32 index = skeleton.findJoint(rig, candidate);
        if (index >= 0)
            return index;
    }
    return -1;
}

// A frame at the midpoint of `from`..`to` whose +Y runs from one to the other,
// which is the axis a `Capsule` stands along.
//
// **Degenerate on purpose when the two coincide.** Two joints in the same place
// happen in real rigs -- a helper bone, a twist bone with no offset -- and the
// answer there is an unrotated frame rather than a NaN one: the capsule ends up
// a sphere at that point, which is the honest picture of a bone with no length.
[[nodiscard]] CFrameD limbFrame(DVec3 from, DVec3 to)
{
    const DVec3 midpoint{(from.x + to.x) * 0.5, (from.y + to.y) * 0.5, (from.z + to.z) * 0.5};
    const Vec3 along{static_cast<f32>(to.x - from.x), static_cast<f32>(to.y - from.y), static_cast<f32>(to.z - from.z)};
    if (core::length(along) < 1.0e-5f)
        return CFrameD{midpoint, core::Mat3{}};

    // `lookAtCFrame` points -Z at the target, and a capsule stands along Y --
    // so the quarter turn about X is what maps one onto the other.
    const CFrameD look = core::lookAtCFrame(midpoint, to, Vec3{0.0f, 1.0f, 0.0f});
    return CFrameD{midpoint, look.rotation * core::rotationX(-kQuarterTurn)};
}

} // namespace

RagdollClasses resolveRagdollClasses(World& world)
{
    RagdollClasses classes;
    classes.part = world.classes().findId(world.atoms().intern("Part"));
    classes.bone = world.classes().findId(world.atoms().intern("Bone"));
    classes.attachment = world.classes().findId(world.atoms().intern("Attachment"));
    classes.ballSocket = world.classes().findId(world.atoms().intern("BallSocketConstraint"));
    classes.hinge = world.classes().findId(world.atoms().intern("HingeConstraint"));
    classes.fixed = world.classes().findId(world.atoms().intern("FixedConstraint"));
    return classes;
}

RagdollBuildResult buildRagdoll(World& world, const SkeletonHost& skeleton, core::InstanceId ragdoll,
                                const RagdollProfile& profile, const RagdollClasses& classes)
{
    RagdollBuildResult result;

    // **The mesh the ragdoll is parented to**, exactly as `driveRagdolls` finds
    // it and as an `AnimationPlayer` finds its own. Asking any other way would
    // be a second answer to "which rig is this".
    const core::InstanceId rig = world.parentOf(ragdoll);
    const PartComponent* mesh = world.parts().find(rig);
    if (mesh == nullptr || skeleton.jointCount(rig) == 0) {
        result.error = LUAUG_TR("scene.err.ragdoll_no_rig");
        return result;
    }

    // **Copied out, because this function creates parts.** A pointer into a
    // `ComponentPool` is valid until the pool grows, and every `Part` below adds
    // to the one this came from -- so holding `mesh` across the creates reads
    // freed memory, and reads it in a way that is right most of the time. That
    // is exactly the shape of D143, and the case that caught it here is the
    // determinism one: two builds in one process agreed until the allocator
    // handed the second a different neighbour (D145).
    const CFrameD rigFrame = mesh->cframe;

    // **Refused rather than added to.** Building twice would double every part
    // and every joint, and the second set would fight the first for the same
    // overrides -- a character that shakes itself apart, from one line run twice.
    if (world.firstChild(ragdoll).valid()) {
        result.error = LUAUG_TR("scene.err.ragdoll_already_built");
        return result;
    }

    if (!classes.complete() || profile.limbs.empty() || profile.limbs.front().parent >= 0) {
        result.error = LUAUG_TR("scene.err.ragdoll_bad_profile");
        return result;
    }
    for (usize index = 0; index < profile.limbs.size(); ++index) {
        // Backwards-only, which is what makes one pass enough and what stops a
        // cycle being expressible at all.
        const i32 parent = profile.limbs[index].parent;
        if (parent >= static_cast<i32>(index)) {
            result.error = LUAUG_TR("scene.err.ragdoll_bad_profile");
            return result;
        }
    }

    // --- Pass one: where every limb's joint is ------------------------------
    //
    // Resolved before anything is created, because the LENGTH of a limb is the
    // distance to its first child's joint -- so a limb cannot be sized until the
    // limbs after it have been located. Two passes rather than a guess.
    std::vector<Built> built(profile.limbs.size());
    u32 matched = 0;
    for (usize index = 0; index < profile.limbs.size(); ++index) {
        const i32 joint = resolve(skeleton, rig, profile.limbs[index]);
        if (joint < 0) {
            result.skipped += 1;
            continue;
        }
        CFrameD model;
        if (!skeleton.jointModel(rig, static_cast<u32>(joint), model)) {
            result.skipped += 1;
            continue;
        }
        built[index].joint = static_cast<u32>(joint);
        built[index].origin = (rigFrame * model).position;
        built[index].made = true;
        matched += 1;
    }

    if (matched == 0) {
        result.error = LUAUG_TR("scene.err.ragdoll_no_joints_matched");
        return result;
    }

    // --- Pass two: the parts and their bones --------------------------------
    //
    // In profile order, which is creation order, which is what instance ids are
    // (R10). A profile is an ordered list for exactly this reason.
    const core::NameAtom sizeName = world.atoms().intern("Size");
    const core::NameAtom cframeName = world.atoms().intern("CFrame");
    const core::NameAtom shapeName = world.atoms().intern("Shape");
    const core::NameAtom jointNameProperty = world.atoms().intern("JointName");
    const EnumId shapeEnum = world.enums().findId(world.atoms().intern("PartShape"));

    for (usize index = 0; index < profile.limbs.size(); ++index) {
        if (!built[index].made)
            continue;
        const RagdollLimb& limb = profile.limbs[index];

        // The first child in the profile that was actually built. A limb whose
        // children were all skipped is a leaf as far as this is concerned, which
        // is right: there is nothing left to measure to.
        DVec3 tip = built[index].origin;
        bool leaf = true;
        for (usize child = index + 1; child < profile.limbs.size(); ++child) {
            if (profile.limbs[child].parent != static_cast<i32>(index) || !built[child].made)
                continue;
            tip = built[child].origin;
            leaf = false;
            break;
        }
        if (leaf) {
            // Down the joint's own -Y, which is where a hand continues past the
            // wrist in every rig that has a convention at all. Guessing a
            // direction beats guessing a length of zero: a zero-length capsule
            // is a sphere at the wrist, and a hand is not one.
            CFrameD model;
            (void)skeleton.jointModel(rig, built[index].joint, model);
            const CFrameD world0 = rigFrame * model;
            const Vec3 down = world0.rotation * Vec3{0.0f, -1.0f, 0.0f};
            tip = DVec3{built[index].origin.x + static_cast<f64>(down.x * limb.leafLength),
                        built[index].origin.y + static_cast<f64>(down.y * limb.leafLength),
                        built[index].origin.z + static_cast<f64>(down.z * limb.leafLength)};
        }

        const core::InstanceId part = world.create(classes.part);
        if (!part.valid())
            continue;
        // Named after the joint it stands for, not after the profile row: what
        // somebody looking at the Explorer wants to read is which bone this is.
        world.setName(part, world.atoms().intern(skeleton.jointName(rig, built[index].joint)));
        (void)world.setParent(part, ragdoll);

        const f32 length = core::length(Vec3{static_cast<f32>(tip.x - built[index].origin.x),
                                             static_cast<f32>(tip.y - built[index].origin.y),
                                             static_cast<f32>(tip.z - built[index].origin.z)});
        // The capsule's caps are hemispheres the collider never stretches, so a
        // limb shorter than its own diameter would be a capsule drawn oval and
        // simulated round. Floored at the diameter, where the two agree exactly.
        const f32 height = length > limb.radius * 2.0f ? length : limb.radius * 2.0f;
        (void)world.setProperty(part, sizeName, Value{Vec3{limb.radius * 2.0f, height, limb.radius * 2.0f}});
        (void)world.setProperty(part, cframeName, Value{limbFrame(built[index].origin, tip)});
        // Looked up by name rather than written as the number it happens to be,
        // so that a reordered enum stops this compiling somewhere else instead
        // of quietly giving every ragdoll cylinders for arms.
        if (const EnumItemDesc* capsule =
                shapeEnum == InvalidEnum ? nullptr : world.enums().findItem(shapeEnum, world.atoms().intern("Capsule"));
            capsule != nullptr) {
            (void)world.setProperty(part, shapeName, Value{EnumValue{shapeEnum, capsule->value}});
        }
        built[index].part = part;
        result.limbs += 1;

        // The `Bone` that says which joint this limb IS. It sits on the part
        // rather than on the mesh, which is what makes it a label the ragdoll
        // reads rather than a socket that follows the animation: `Bone`s under a
        // ragdoll resolve against the rig and are followed only when the rig is
        // their own owner.
        const core::InstanceId bone = world.create(classes.bone);
        if (!bone.valid())
            continue;
        // **Named for what it is, not for the joint it names.** The part already
        // carries the joint's name, and a limb with two children both called
        // `Joint` is an Explorer nobody can read -- which is what the first cut
        // of this produced.
        world.setName(bone, world.atoms().intern("Bone"));
        (void)world.setParent(bone, part);
        (void)world.setProperty(bone, jointNameProperty,
                                Value{std::string(skeleton.jointName(rig, built[index].joint))});

        // **The bone sits at the limb's JOINT, not at its centre.** The part is
        // a capsule centred halfway down the bone; the joint is at its top. A
        // bone left at the part's origin would write the pose half a limb-length
        // out, which is a character whose every joint has slid down its own bone.
        (void)world.setProperty(
            bone, cframeName,
            Value{core::inverse(limbFrame(built[index].origin, tip)) * CFrameD{built[index].origin, core::Mat3{}}});
    }

    // --- Pass three: the joints ---------------------------------------------
    //
    // After every part exists, because a constraint names two of them. Its two
    // ends are `Attachment`s at the SAME world point -- the child limb's own
    // joint -- expressed in each part's local space, which is what a ball socket
    // holding an arm to a shoulder is.
    const core::NameAtom attachment0Name = world.atoms().intern("Attachment0");
    const core::NameAtom attachment1Name = world.atoms().intern("Attachment1");
    const core::NameAtom collideName = world.atoms().intern("CollideConnected");

    for (usize index = 0; index < profile.limbs.size(); ++index) {
        const RagdollLimb& limb = profile.limbs[index];
        if (!built[index].made || !built[index].part.valid() || limb.parent < 0)
            continue;

        // **Reattached to the nearest ancestor that WAS built**, which is the
        // other half of a partial ragdoll: a profile with a spine the rig lacks
        // must not leave the arms floating, so they hang from the hips instead.
        i32 anchor = limb.parent;
        while (anchor >= 0 &&
               !(built[static_cast<usize>(anchor)].made && built[static_cast<usize>(anchor)].part.valid())) {
            anchor = profile.limbs[static_cast<usize>(anchor)].parent;
        }
        if (anchor < 0)
            continue;

        const core::InstanceId parentPart = built[static_cast<usize>(anchor)].part;
        const PartComponent* childPart = world.parts().find(built[index].part);
        const PartComponent* anchorPart = world.parts().find(parentPart);
        if (childPart == nullptr || anchorPart == nullptr)
            continue;
        // By value for the same reason as `rigFrame` above: these are read after
        // three `create` calls, and "nothing in those touches the parts pool" is
        // true today and is not a thing to depend on.
        const CFrameD childFrame = childPart->cframe;
        const CFrameD anchorFrame = anchorPart->cframe;

        // `physics::ConstraintType`: 0 is Fixed, 2 is Hinge, and both Point and
        // SwingTwist are the same class -- a swing-twist IS a ball socket with
        // its limits on, which is what the `LimitsEnabled` setter says.
        const ClassId constraintClass =
            limb.kind == 2 ? classes.hinge : (limb.kind == 0 ? classes.fixed : classes.ballSocket);
        if (constraintClass == InvalidClass)
            continue;

        const CFrameD at{built[index].origin, childFrame.rotation};
        const core::InstanceId end0 = world.create(classes.attachment);
        const core::InstanceId end1 = world.create(classes.attachment);
        if (!end0.valid() || !end1.valid())
            continue;
        world.setName(end0, world.atoms().intern("Joint0"));
        world.setName(end1, world.atoms().intern("Joint1"));
        (void)world.setParent(end0, parentPart);
        (void)world.setParent(end1, built[index].part);
        (void)world.setProperty(end0, cframeName, Value{core::inverse(anchorFrame) * at});
        (void)world.setProperty(end1, cframeName, Value{core::inverse(childFrame) * at});

        const core::InstanceId joint = world.create(constraintClass);
        if (!joint.valid())
            continue;
        world.setName(joint, world.atoms().intern("Joint"));
        (void)world.setParent(joint, built[index].part);
        (void)world.setProperty(joint, attachment0Name, Value{end0});
        (void)world.setProperty(joint, attachment1Name, Value{end1});
        // **False, always.** An upper arm and a lower arm overlap at the elbow by
        // construction, and left colliding they shove each other apart on the
        // first step -- a character that explodes the moment it goes down.
        (void)world.setProperty(joint, collideName, Value{false});

        if (ConstraintComponent* component = world.constraints().find(joint); component != nullptr) {
            component->swingLimit = limb.swingLimit;
            component->twistLimit = limb.twistLimit;
            component->limitsEnabled = limb.kind == 3;
            component->limitLow = limb.limitLow;
            component->limitHigh = limb.limitHigh;
            // A swing-twist IS a ball socket with its limits on, which is what
            // the `LimitsEnabled` setter says -- so the class is the same one and
            // the kind follows the limits rather than the other way round.
            if (limb.kind == 3)
                component->kind = 3;
        }
        result.constraints += 1;
    }

    return result;
}

} // namespace luaug::scene
