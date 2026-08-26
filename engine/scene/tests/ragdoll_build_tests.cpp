// Building a ragdoll out of a rig, aimed at the arithmetic.
//
// **What can be wrong here is geometric.** A capsule whose length came out of
// the wrong pair of joints, a frame whose Y does not run down the bone, a `Bone`
// left at the part's centre instead of at its joint -- every one of those looks
// plausible in the Explorer and produces a character that folds itself in half
// the moment it goes down. So the cases below measure: where a limb is, how long
// it is, which way it points, and what the bone on it says.
//
// The rig is invented, because `SkeletonHost` is a seam and the point of a seam
// is that the thing above it needs no renderer, no mesh file and no GPU.
#include "luaug/core/math.h"
#include "luaug/scene/components.h"
#include "luaug/scene/ragdoll_build.h"
#include "luaug/scene/skeleton_host.h"
#include "luaug/scene/world.h"

#include <doctest/doctest.h>
#include <string>
#include <string_view>

#include "scene_fixture.h"

using namespace luaug;
using namespace luaug::scene;

namespace {

// A three-joint arm standing along Y: `Hips` at the origin, `Chest` a metre up,
// `Head` half a metre above that. Three is the smallest rig with a bone that has
// a bone above it, which is what makes it the right size for a test about
// lengths derived from the joint after.
class ArmRig final : public SkeletonHost
{
public:
    [[nodiscard]] u32 jointCount(core::InstanceId part) const override { return part == rig ? 3u : 0u; }

    [[nodiscard]] i32 findJoint(core::InstanceId, std::string_view name) const override
    {
        if (name == "Hips")
            return 0;
        if (name == "Chest")
            return 1;
        return name == "Head" ? 2 : -1;
    }

    [[nodiscard]] i32 jointParent(core::InstanceId, u32 joint) const override
    {
        return joint == 0 ? -1 : static_cast<i32>(joint) - 1;
    }

    [[nodiscard]] std::string_view jointName(core::InstanceId, u32 joint) const override
    {
        if (joint == 0)
            return "Hips";
        return joint == 1 ? "Chest" : "Head";
    }

    [[nodiscard]] bool jointModel(core::InstanceId, u32 joint, core::CFrameD& out) const override
    {
        if (joint >= 3)
            return false;
        out = core::CFrameD{};
        out.position = core::DVec3{0.0, joint == 0 ? 0.0 : (joint == 1 ? 1.0 : 1.5), 0.0};
        return true;
    }

    void setJointOverride(core::InstanceId, u32, const core::CFrameD&) override {}
    void clearJointOverrides(core::InstanceId) override {}
    void commitOverrides() override {}

    core::InstanceId rig;
};

// The fixture registers one class for `Attachment` and `Bone` and one for the
// three constraints, deliberately -- they differ only in what their own hook
// stamps. That is exactly the shape `RagdollClasses` takes, so the test says so
// rather than working around it.
[[nodiscard]] RagdollClasses fixtureClasses(const testing::Hierarchy& schema)
{
    RagdollClasses classes;
    classes.part = schema.partClass;
    classes.bone = schema.attachmentClass;
    classes.attachment = schema.attachmentClass;
    classes.ballSocket = schema.constraintClass;
    classes.hinge = schema.constraintClass;
    classes.fixed = schema.constraintClass;
    return classes;
}

// A rigged character, its ragdoll, and the rig that answers about it.
struct Character
{
    testing::Fixture fixture;
    ArmRig rig;
    core::InstanceId meshPart;
    core::InstanceId ragdoll;

    explicit Character(core::DVec3 at = {})
    {
        meshPart = fixture.world.create(fixture.schema.meshPartClass);
        fixture.world.setName(meshPart, fixture.atom("Character"));
        PartComponent body;
        body.cframe.position = at;
        fixture.world.parts().add(meshPart, body);
        fixture.world.meshParts().add(meshPart, MeshPartComponent{});

        ragdoll = fixture.world.create(fixture.schema.ragdollClass);
        REQUIRE(fixture.world.setParent(ragdoll, meshPart) == std::nullopt);
        rig.rig = meshPart;
    }
};

// Hips -> Chest -> Head, all ball sockets. The names are the rig's own, with one
// alias in front of each so the alias path is exercised by every case rather
// than by one.
[[nodiscard]] RagdollProfile humanoid()
{
    RagdollProfile profile;
    profile.limbs.push_back(RagdollLimb{{"mixamorig:Hips", "Hips"}, -1, 0.14f, 0.2f, 1, 0.7f, 0.4f, 1.0f, -1.0f});
    profile.limbs.push_back(RagdollLimb{{"mixamorig:Spine", "Chest"}, 0, 0.12f, 0.2f, 1, 0.5f, 0.3f, 1.0f, -1.0f});
    profile.limbs.push_back(RagdollLimb{{"mixamorig:Head", "Head"}, 1, 0.10f, 0.2f, 1, 0.6f, 0.3f, 1.0f, -1.0f});
    return profile;
}

[[nodiscard]] core::InstanceId childNamed(const World& world, core::InstanceId parent, std::string_view name)
{
    for (core::InstanceId child = world.firstChild(parent); child.valid(); child = world.nextSibling(child)) {
        if (world.atoms().text(world.name(child)) == name)
            return child;
    }
    return {};
}

} // namespace

TEST_CASE("a limb is as long as the bone it stands for, and points down it")
{
    Character character;
    const RagdollBuildResult result = buildRagdoll(character.fixture.world, character.rig, character.ragdoll,
                                                   humanoid(), fixtureClasses(character.fixture.schema));

    REQUIRE_FALSE(result.error.has_value());
    CHECK(result.limbs == 3);
    CHECK(result.skipped == 0);

    // `Hips` runs to `Chest`, which is one metre. The capsule's full height is
    // that, its diameter is twice the profile's radius, and its centre is the
    // midpoint -- half a metre up, not at the joint.
    const core::InstanceId hips = childNamed(character.fixture.world, character.ragdoll, "Hips");
    REQUIRE(hips.valid());
    const PartComponent* part = character.fixture.world.parts().find(hips);
    REQUIRE(part != nullptr);
    CHECK(static_cast<core::f64>(part->size.y) == doctest::Approx(1.0));
    CHECK(static_cast<core::f64>(part->size.x) == doctest::Approx(0.28));
    CHECK(part->cframe.position.y == doctest::Approx(0.5));

    // Its own +Y runs from the hips towards the chest, which is world +Y here.
    const core::Vec3 along = part->cframe.rotation * core::Vec3{0.0f, 1.0f, 0.0f};
    CHECK(static_cast<core::f64>(along.y) == doctest::Approx(1.0).epsilon(0.001));
}

TEST_CASE("the bone sits at the limb's joint, not at its centre")
{
    // The part is a capsule centred halfway down the bone; the joint is at its
    // top. A bone left at the part's origin would write the pose half a
    // limb-length out -- a character whose every joint has slid down its own
    // bone, which reads as a puppet coming apart.
    Character character;
    REQUIRE_FALSE(buildRagdoll(character.fixture.world, character.rig, character.ragdoll, humanoid(),
                               fixtureClasses(character.fixture.schema))
                      .error.has_value());

    const core::InstanceId hips = childNamed(character.fixture.world, character.ragdoll, "Hips");
    const core::InstanceId bone = childNamed(character.fixture.world, hips, "Bone");
    REQUIRE(bone.valid());
    const PartComponent* part = character.fixture.world.parts().find(hips);
    const AttachmentComponent* attachment = character.fixture.world.attachments().find(bone);
    REQUIRE(part != nullptr);
    REQUIRE(attachment != nullptr);

    // Composed back to world, the bone is at the hips joint: the origin.
    const core::CFrameD world0 = part->cframe * attachment->cframe;
    CHECK(world0.position.y == doctest::Approx(0.0).epsilon(0.001));
    CHECK(std::string(character.fixture.world.atoms().text(attachment->jointName)) == "Hips");
}

TEST_CASE("a rig ten metres out puts its ragdoll ten metres out")
{
    // The joints are in the MESH's own space and the parts are in the world's.
    // A build that forgot the composition would put every character's ragdoll at
    // the origin, which is the one error that looks fine in a test at the origin.
    Character character({10.0, 0.0, -4.0});
    REQUIRE_FALSE(buildRagdoll(character.fixture.world, character.rig, character.ragdoll, humanoid(),
                               fixtureClasses(character.fixture.schema))
                      .error.has_value());

    const core::InstanceId hips = childNamed(character.fixture.world, character.ragdoll, "Hips");
    const PartComponent* part = character.fixture.world.parts().find(hips);
    REQUIRE(part != nullptr);
    CHECK(part->cframe.position.x == doctest::Approx(10.0));
    CHECK(part->cframe.position.z == doctest::Approx(-4.0));
}

TEST_CASE("a joint is created per limb below the root, and it never lets the pair collide")
{
    // An upper arm and a lower arm overlap at the elbow by construction. Left
    // colliding they shove each other apart on the first step, which is a
    // character that explodes the moment it goes down.
    Character character;
    const RagdollBuildResult result = buildRagdoll(character.fixture.world, character.rig, character.ragdoll,
                                                   humanoid(), fixtureClasses(character.fixture.schema));
    CHECK(result.constraints == 2);

    const core::InstanceId chest = childNamed(character.fixture.world, character.ragdoll, "Chest");
    const core::InstanceId joint = childNamed(character.fixture.world, chest, "Joint");
    REQUIRE(joint.valid());
    const ConstraintComponent* constraint = character.fixture.world.constraints().find(joint);
    REQUIRE(constraint != nullptr);
    CHECK_FALSE(constraint->collideConnected);
    CHECK(constraint->attachment0.valid());
    CHECK(constraint->attachment1.valid());

    // Both ends are the same world point -- the chest's own joint -- expressed
    // in each part's local space. That is what a ball socket holding an arm to a
    // shoulder is, and two ends at different points is a joint that snaps on the
    // first step.
    const AttachmentComponent* end0 = character.fixture.world.attachments().find(constraint->attachment0);
    const AttachmentComponent* end1 = character.fixture.world.attachments().find(constraint->attachment1);
    REQUIRE(end0 != nullptr);
    REQUIRE(end1 != nullptr);
    const PartComponent* anchor =
        character.fixture.world.parts().find(character.fixture.world.parentOf(constraint->attachment0));
    const PartComponent* driven =
        character.fixture.world.parts().find(character.fixture.world.parentOf(constraint->attachment1));
    REQUIRE(anchor != nullptr);
    REQUIRE(driven != nullptr);
    const core::DVec3 at0 = (anchor->cframe * end0->cframe).position;
    const core::DVec3 at1 = (driven->cframe * end1->cframe).position;
    CHECK(at0.y == doctest::Approx(1.0).epsilon(0.001));
    CHECK(at1.y == doctest::Approx(at0.y).epsilon(0.001));
}

TEST_CASE("a limb the rig has no joint for is skipped, and its children hang from what is left")
{
    // The other half of a partial ragdoll. One humanoid profile has to work on a
    // rig with no toes, and a limb whose parent was skipped must not be left
    // floating -- it hangs from the nearest ancestor that WAS built.
    Character character;
    RagdollProfile profile = humanoid();
    profile.limbs[1].joints = {"NoSuchSpine"};

    const RagdollBuildResult result = buildRagdoll(character.fixture.world, character.rig, character.ragdoll, profile,
                                                   fixtureClasses(character.fixture.schema));
    REQUIRE_FALSE(result.error.has_value());
    CHECK(result.limbs == 2);
    CHECK(result.skipped == 1);
    CHECK(result.constraints == 1);

    // The head's joint reaches the HIPS, because the chest between them is gone.
    const core::InstanceId head = childNamed(character.fixture.world, character.ragdoll, "Head");
    const core::InstanceId hips = childNamed(character.fixture.world, character.ragdoll, "Hips");
    const core::InstanceId joint = childNamed(character.fixture.world, head, "Joint");
    REQUIRE(joint.valid());
    const ConstraintComponent* constraint = character.fixture.world.constraints().find(joint);
    REQUIRE(constraint != nullptr);
    CHECK(character.fixture.world.parentOf(constraint->attachment0) == hips);
}

TEST_CASE("the alias list is tried in order, so one profile fits two exporters")
{
    // The same shoulder is `mixamorig:LeftArm`, `LeftArm` or `upper_arm.L`
    // depending on who exported it. A profile that named one of those would be a
    // profile for one exporter, which is a profile nobody can ship.
    Character character;
    RagdollProfile profile;
    profile.limbs.push_back(RagdollLimb{{"Bip01 Pelvis", "mixamorig:Hips", "Hips"}, -1});
    const RagdollBuildResult result = buildRagdoll(character.fixture.world, character.rig, character.ragdoll, profile,
                                                   fixtureClasses(character.fixture.schema));
    REQUIRE_FALSE(result.error.has_value());
    CHECK(result.limbs == 1);
    CHECK(childNamed(character.fixture.world, character.ragdoll, "Hips").valid());
}

TEST_CASE("building a second time is refused rather than doubled")
{
    // Two sets of parts would fight each other for the same overrides -- a
    // character that shakes itself apart, from one line run twice.
    Character character;
    REQUIRE_FALSE(buildRagdoll(character.fixture.world, character.rig, character.ragdoll, humanoid(),
                               fixtureClasses(character.fixture.schema))
                      .error.has_value());
    const RagdollBuildResult again = buildRagdoll(character.fixture.world, character.rig, character.ragdoll, humanoid(),
                                                  fixtureClasses(character.fixture.schema));
    REQUIRE(again.error.has_value());
    CHECK(*again.error == LUAUG_TR("scene.err.ragdoll_already_built"));
    CHECK(again.limbs == 0);
}

TEST_CASE("a ragdoll on something with no rig is refused, and creates nothing")
{
    testing::Fixture fixture;
    const core::InstanceId plain = fixture.part("Crate");
    fixture.world.parts().add(plain, PartComponent{});
    const core::InstanceId ragdoll = fixture.world.create(fixture.schema.ragdollClass);
    REQUIRE(fixture.world.setParent(ragdoll, plain) == std::nullopt);

    ArmRig rig;
    const RagdollBuildResult result =
        buildRagdoll(fixture.world, rig, ragdoll, humanoid(), fixtureClasses(fixture.schema));
    REQUIRE(result.error.has_value());
    CHECK(*result.error == LUAUG_TR("scene.err.ragdoll_no_rig"));
    CHECK_FALSE(fixture.world.firstChild(ragdoll).valid());
}

TEST_CASE("a rig that shares no joint with the profile is refused rather than half built")
{
    // Distinct from a limb being skipped: a profile that matched NOTHING is a
    // profile for a different character, and building zero parts silently would
    // leave somebody wondering why their ragdoll does not fall.
    Character character;
    RagdollProfile profile;
    profile.limbs.push_back(RagdollLimb{{"Tail"}, -1});
    profile.limbs.push_back(RagdollLimb{{"Wing"}, 0});

    const RagdollBuildResult result = buildRagdoll(character.fixture.world, character.rig, character.ragdoll, profile,
                                                   fixtureClasses(character.fixture.schema));
    REQUIRE(result.error.has_value());
    CHECK(*result.error == LUAUG_TR("scene.err.ragdoll_no_joints_matched"));
    CHECK_FALSE(character.fixture.world.firstChild(character.ragdoll).valid());
}

TEST_CASE("a limb that hangs from one declared after it is refused")
{
    // Backwards-only is what makes one pass enough and what stops a cycle being
    // expressible at all. A forward reference would name a limb that does not
    // exist yet, and the joint would reach an invalid id.
    Character character;
    RagdollProfile profile;
    profile.limbs.push_back(RagdollLimb{{"Hips"}, -1});
    profile.limbs.push_back(RagdollLimb{{"Chest"}, 2});
    profile.limbs.push_back(RagdollLimb{{"Head"}, 1});

    const RagdollBuildResult result = buildRagdoll(character.fixture.world, character.rig, character.ragdoll, profile,
                                                   fixtureClasses(character.fixture.schema));
    REQUIRE(result.error.has_value());
    CHECK(*result.error == LUAUG_TR("scene.err.ragdoll_bad_profile"));
}

TEST_CASE("two builds of the same profile over the same rig produce the same instances")
{
    // R10, and the reason a profile is an ordered list rather than a map: what a
    // ragdoll creates is instance ids, creation order is what an id is, and a
    // replay that built its limbs in a different order would diverge from the
    // first tick.
    const auto hashOf = [] {
        Character character;
        REQUIRE_FALSE(buildRagdoll(character.fixture.world, character.rig, character.ragdoll, humanoid(),
                                   fixtureClasses(character.fixture.schema))
                          .error.has_value());
        return character.fixture.world.worldHash();
    };
    // **This case found a dangling pointer** (D145): `buildRagdoll` held a
    // `PartComponent*` across the `create` calls that grow the pool it points
    // into, so the leaf limb's direction was read out of freed memory -- right
    // most of the time, and different in the second build often enough to flip
    // this hash about one run in three.
    CHECK(hashOf() == hashOf());
}
