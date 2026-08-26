// The rig drawn as lines, aimed at the one thing in it that can be wrong.
//
// **A joint is in the MESH's space and a picture is in the world's**, and the
// composition between them is what got a horse drawn on its back once already
// (E9's opening defect, a skinned node's transform applied twice). So what these
// cases assert is not that lines appeared -- it is WHERE they appeared, for a
// part that is not at the origin and not facing down -Z.
//
// The rig is invented rather than loaded: `SkeletonHost` is a seam, and the
// whole point of a seam is that the thing above it can be tested without a
// renderer, a mesh file or a GPU.
#include "luaug/app/skeleton_overlay.h"
#include "luaug/core/math.h"
#include "luaug/render/debug_draw.h"
#include "luaug/scene/components.h"
#include "luaug/scene/skeleton_host.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <doctest/doctest.h>

#include "inspector_fixture.h"

using namespace luaug;

namespace {

// A two-joint arm: a root at the mesh's origin and a hand two metres up its own
// Y. Two is the smallest rig with a bone in it, which is what makes it the right
// size for a test about where a bone is drawn.
class ArmRig final : public scene::SkeletonHost
{
public:
    [[nodiscard]] core::u32 jointCount(core::InstanceId part) const override { return part == rig ? 2u : 0u; }

    [[nodiscard]] core::i32 findJoint(core::InstanceId, std::string_view name) const override
    {
        if (name == "Root")
            return 0;
        return name == "Hand" ? 1 : -1;
    }

    [[nodiscard]] core::i32 jointParent(core::InstanceId, core::u32 joint) const override
    {
        return joint == 0 ? -1 : 0;
    }

    [[nodiscard]] std::string_view jointName(core::InstanceId, core::u32 joint) const override
    {
        return joint == 0 ? "Root" : "Hand";
    }

    [[nodiscard]] bool jointModel(core::InstanceId, core::u32 joint, core::CFrameD& out) const override
    {
        if (joint >= 2)
            return false;
        out = core::CFrameD{};
        if (joint == 1)
            out.position = core::DVec3{0.0, 2.0, 0.0};
        return true;
    }

    void setJointOverride(core::InstanceId, core::u32, const core::CFrameD&) override {}
    void clearJointOverrides(core::InstanceId) override {}
    void commitOverrides() override {}

    core::InstanceId rig;
};

// Whether the buffer holds a segment with these two ends, in either direction --
// a line has no direction and asserting one would be asserting an implementation
// detail of the loop.
[[nodiscard]] bool hasSegment(const render::DebugDraw& draw, core::Vec3 a, core::Vec3 b)
{
    const std::span<const render::DebugVertex> vertices = draw.vertices();
    const auto near = [](core::Vec3 lhs, core::Vec3 rhs) { return core::length(lhs - rhs) < 1.0e-3f; };
    for (std::size_t index = 0; index + 1 < vertices.size(); index += 2) {
        const core::Vec3 from = vertices[index].position;
        const core::Vec3 to = vertices[index + 1].position;
        if ((near(from, a) && near(to, b)) || (near(from, b) && near(to, a)))
            return true;
    }
    return false;
}

} // namespace

TEST_CASE("a bone is drawn between the joints it connects, in the part's own frame")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId root = fixture.widget(world, "Root");
    const core::InstanceId mesh = fixture.widget(world, "Character");
    REQUIRE(world.setParent(mesh, root) == std::nullopt);

    // **Ten metres out and turned a quarter turn about Y**, which is the case a
    // composition error survives: at the origin facing forward, a joint drawn in
    // mesh space and a joint drawn in world space are the same point.
    scene::PartComponent part;
    part.cframe = core::CFrameD{core::DVec3{10.0, 0.0, -4.0}, core::fromAxisAngle({0.0f, 1.0f, 0.0f}, 1.5707963f)};
    world.parts().add(mesh, part);
    world.meshParts().add(mesh, scene::MeshPartComponent{});

    ArmRig rig;
    rig.rig = mesh;
    render::DebugDraw draw;
    app::drawSkeletons(world, rig, draw);

    // The rotation is about Y, so the hand's local +Y is still world +Y: the
    // bone runs straight up from the part's own position.
    const core::Vec3 at{10.0f, 0.0f, -4.0f};
    const core::Vec3 hand{10.0f, 2.0f, -4.0f};
    CHECK(hasSegment(draw, at, hand));
}

TEST_CASE("every joint gets a cross, so the end of a chain is visible too")
{
    // A bone is the line to its PARENT, which means the last joint of every
    // chain has no line of its own. Without the crosses a one-bone rig would
    // draw one segment and a hand would be an invisible endpoint.
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId mesh = fixture.widget(world, "Character");
    world.parts().add(mesh, scene::PartComponent{});
    world.meshParts().add(mesh, scene::MeshPartComponent{});

    ArmRig rig;
    rig.rig = mesh;
    render::DebugDraw draw;
    app::drawSkeletons(world, rig, draw);

    // Three crosses per joint plus one bone: two joints is seven segments.
    CHECK(draw.vertices().size() == 7u * 2u);
    CHECK(hasSegment(draw, {-0.02f, 0.0f, 0.0f}, {0.02f, 0.0f, 0.0f}));
    CHECK(hasSegment(draw, {0.0f, 2.0f - 0.02f, 0.0f}, {0.0f, 2.0f + 0.02f, 0.0f}));
}

TEST_CASE("a world with no rig in it draws nothing")
{
    // The cost of the switch being off is what this asserts. A part with no
    // skeleton is most parts, and a pool walk that drew a cross for each of them
    // would put the whole world in lines.
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId plain = fixture.widget(world, "Crate");
    world.parts().add(plain, scene::PartComponent{});
    world.meshParts().add(plain, scene::MeshPartComponent{});

    ArmRig rig;
    // Deliberately not `plain`: the host answers zero for it, which is what a
    // mesh with no skin does.
    render::DebugDraw draw;
    app::drawSkeletons(world, rig, draw);
    CHECK(draw.vertices().empty());
}

TEST_CASE("a mesh part with a rig and no transform is skipped rather than drawn at the origin")
{
    // `MeshPartComponent` and `PartComponent` are separate pools, and a world
    // can hold one without the other for a frame -- during a load, or between a
    // create and its first property write. Drawing that rig at the world origin
    // would be a skeleton somewhere nobody put one.
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId mesh = fixture.widget(world, "Character");
    world.meshParts().add(mesh, scene::MeshPartComponent{});

    ArmRig rig;
    rig.rig = mesh;
    render::DebugDraw draw;
    app::drawSkeletons(world, rig, draw);
    CHECK(draw.vertices().empty());
}
