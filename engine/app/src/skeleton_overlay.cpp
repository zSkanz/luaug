#include "luaug/app/skeleton_overlay.h"

#include "luaug/render/debug_draw.h"
#include "luaug/scene/skeleton_host.h"
#include "luaug/scene/world.h"

namespace luaug::app {

using core::f32;
using core::i32;
using core::u32;

namespace {

constexpr render::DebugColor kBoneColor = render::DebugColor::fromLinear(0.95f, 0.72f, 0.20f);
constexpr render::DebugColor kJointColor = render::DebugColor::fromLinear(0.35f, 0.85f, 1.0f);
constexpr f32 kJointCrossMetres = 0.02f;

} // namespace

void drawSkeletons(const scene::World& world, const scene::SkeletonHost& skeleton, render::DebugDraw& draw)
{
    // Every skinned mesh in the world, in pool order. Pool order rather than
    // tree order because nothing here depends on the order -- lines are
    // commutative -- and a pool walk skips the parts with no rig for free.
    world.meshParts().forEach([&](core::InstanceId id, const scene::MeshPartComponent&) {
        const u32 joints = skeleton.jointCount(id);
        if (joints == 0)
            return;

        const scene::PartComponent* part = world.parts().find(id);
        if (part == nullptr)
            return;

        // Model space, composed with the part's own world `CFrame` -- the same
        // composition `PhysicsSync::resolveAttachment` does, so a bone drawn
        // here is where a socket welded to it will be.
        const auto worldOf = [&](u32 joint, core::DVec3& out) {
            core::CFrameD model;
            if (!skeleton.jointModel(id, joint, model))
                return false;
            out = (part->cframe * model).position;
            return true;
        };

        for (u32 joint = 0; joint < joints; ++joint) {
            core::DVec3 here{};
            if (!worldOf(joint, here))
                continue;

            // A cross at the joint, so a root and a leaf are both visible: a
            // bone is drawn as the line to its PARENT, which means the last
            // joint of every chain would otherwise have nothing at all.
            const core::Vec3 at = core::toVec3(here);
            draw.line(at - core::Vec3{kJointCrossMetres, 0.0f, 0.0f}, at + core::Vec3{kJointCrossMetres, 0.0f, 0.0f},
                      kJointColor);
            draw.line(at - core::Vec3{0.0f, kJointCrossMetres, 0.0f}, at + core::Vec3{0.0f, kJointCrossMetres, 0.0f},
                      kJointColor);
            draw.line(at - core::Vec3{0.0f, 0.0f, kJointCrossMetres}, at + core::Vec3{0.0f, 0.0f, kJointCrossMetres},
                      kJointColor);

            const i32 parent = skeleton.jointParent(id, joint);
            if (parent < 0)
                continue;
            core::DVec3 up{};
            if (worldOf(static_cast<u32>(parent), up))
                draw.line(core::toVec3(up), at, kBoneColor);
        }
    });
}

} // namespace luaug::app
