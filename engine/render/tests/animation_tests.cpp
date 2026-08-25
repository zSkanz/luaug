// Skeletal animation: sampling, blending and the pose walk (M6).
//
// Every case here is a fact about what a clip is supposed to do, written before
// the number was read off a run. The two that matter most are the ones about
// what animation must NOT do: a joint no channel drives keeps its rest pose, and
// two tracks blend in load order rather than in whatever order a container hands
// them over (R10).
#include "luaug/render/animation.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/components.h"
#include "luaug/scene/enum_registry.h"
#include "luaug/scene/world.h"

#include <cmath>
#include <doctest/doctest.h>

using namespace luaug;
using luaug::core::f32;
using luaug::core::Mat4;

namespace {

constexpr f32 kEpsilon = 1e-4f;

bool close(f32 a, f32 b) noexcept
{
    return std::fabs(a - b) <= kEpsilon;
}

// A two-joint skeleton: a root at the origin and a child one unit up. Small
// enough to check every number in the palette by hand, which is the point.
render::SkeletonLibrary::Entry twoJointSkeleton()
{
    render::SkeletonLibrary::Entry entry;

    asset::Joint root;
    root.name = "root";
    root.parent = asset::Joint::NoParent;
    entry.joints.push_back(root);

    asset::Joint child;
    child.name = "child";
    child.parent = 0;
    child.localBind.position = core::DVec3{0.0, 1.0, 0.0};
    // Model space to joint space at bind: the child sits one unit up, so its
    // inverse bind takes a model-space point one unit DOWN.
    child.inverseBind.m[3][1] = -1.0f;
    entry.joints.push_back(child);

    return entry;
}

// One second, one channel: the child slides from y = 1 to y = 3.
asset::AnimationClip slideClip(const char* name)
{
    asset::AnimationChannel channel;
    channel.joint = 1;
    channel.target = asset::AnimationChannel::Target::Translation;
    channel.stride = 3;
    channel.times = {0.0f, 1.0f};
    channel.values = {0.0f, 1.0f, 0.0f, 0.0f, 3.0f, 0.0f};

    asset::AnimationClip clip;
    clip.name = name;
    clip.duration = 1.0f;
    clip.channels.push_back(channel);
    return clip;
}

// A world with a `MeshPart` naming one content URN and an `AnimationPlayer`
// under it. Hand-built for the same reason `render_world_tests.cpp` hand-builds
// its own: an animation test must not fail because an API definition moved.
struct Fixture
{
    core::AtomTable atoms;
    scene::ClassRegistry classes;
    scene::EnumRegistry enums;
    scene::ClassId instanceClass = scene::InvalidClass;
    scene::ClassId meshPartClass = scene::InvalidClass;

    Fixture()
    {
        scene::ClassDescriptor instance;
        instance.name = atoms.intern("Instance");
        instance.defaultName = instance.name;
        instanceClass = classes.registerClass(instance);

        scene::ClassDescriptor meshPart;
        meshPart.name = atoms.intern("MeshPart");
        meshPart.super = instanceClass;
        meshPart.defaultName = meshPart.name;
        meshPart.attachComponents = [](scene::World& w, core::InstanceId id) {
            w.meshParts().add(id, scene::MeshPartComponent{});
        };
        meshPart.detachComponents = [](scene::World& w, core::InstanceId id) { w.meshParts().remove(id); };
        meshPartClass = classes.registerClass(meshPart);
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    scene::World world{classes, enums, atoms, 1u};
    render::SkeletonLibrary skeletons;

    core::NameAtom content = atoms.intern("asset://models/rig.glb");

    // The mesh is the player's parent, which is where `createTrack` looks for
    // the skeleton and where the pose is keyed.
    core::InstanceId mesh;

    [[nodiscard]] core::InstanceId rig(render::SkeletonLibrary::Entry entry)
    {
        skeletons.set(content, std::move(entry));
        mesh = world.create(meshPartClass);
        world.meshParts().find(mesh)->meshContent = content;
        const core::InstanceId player = world.create(instanceClass);
        (void)world.setParent(player, mesh);
        return player;
    }
};

} // namespace

TEST_CASE("a track that names a clip the file does not have still answers reads")
{
    Fixture fixture;
    render::SkeletonLibrary::Entry entry = twoJointSkeleton();
    entry.clips.push_back(slideClip("Bend"));
    const core::InstanceId player = fixture.rig(std::move(entry));

    render::AnimationSystem animation{fixture.world, fixture.skeletons};
    const scene::TrackId missing = animation.createTrack(player, "NoSuchClip");

    // Not zero and not a crash: a mesh that has not finished loading would
    // otherwise make a perfectly ordinary frame a nil index.
    REQUIRE(missing != 0);
    CHECK(close(animation.state(missing).length, 0.0f));
    animation.play(missing, 0.0f, 1.0f, 1.0f);
    animation.sample(1.0 / 60.0);
    // No clip means no pose at all, rather than a bind-pose palette built every
    // tick for a track that drives nothing.
    CHECK(animation.pose(fixture.mesh) == nullptr);
}

TEST_CASE("an empty clip name takes the file's first clip")
{
    Fixture fixture;
    render::SkeletonLibrary::Entry entry = twoJointSkeleton();
    entry.clips.push_back(slideClip("Walk"));
    entry.clips.push_back(slideClip("Run"));
    const core::InstanceId player = fixture.rig(std::move(entry));

    render::AnimationSystem animation{fixture.world, fixture.skeletons};
    CHECK(close(animation.state(animation.createTrack(player, "")).length, 1.0f));
}

TEST_CASE("sampling walks the clip and the palette is joint times inverse bind")
{
    Fixture fixture;
    render::SkeletonLibrary::Entry entry = twoJointSkeleton();
    entry.clips.push_back(slideClip("Slide"));
    const core::InstanceId player = fixture.rig(std::move(entry));

    render::AnimationSystem animation{fixture.world, fixture.skeletons};
    const scene::TrackId track = animation.createTrack(player, "Slide");
    animation.play(track, 0.0f, 1.0f, 1.0f);

    // Half a second in, halfway between the two keys: y = 2.
    for (int tick = 0; tick < 30; ++tick)
        animation.sample(1.0 / 60.0);
    CHECK(close(static_cast<f32>(animation.state(track).timePosition), 0.5f));

    const render::Pose* pose = animation.pose(fixture.mesh);
    REQUIRE(pose != nullptr);
    REQUIRE(pose->palette.size() == 2);
    // The child is at y = 2 and its inverse bind subtracts the 1 it was bound
    // at, so a vertex skinned to it moves up by exactly one unit.
    CHECK(close(pose->palette[1].m[3][1], 1.0f));
    // The root is untouched by the clip, and its palette entry is the identity.
    CHECK(close(pose->palette[0].m[3][1], 0.0f));
    CHECK(close(pose->palette[0].m[0][0], 1.0f));
}

TEST_CASE("a joint no channel drives keeps its rest transform")
{
    // The failure this guards against does not look like a bug in the joint that
    // is animated: it looks like every OTHER joint collapsing to the origin,
    // which is what a single weighted average over all joints would do.
    Fixture fixture;
    render::SkeletonLibrary::Entry entry = twoJointSkeleton();
    entry.joints[0].localBind.position = core::DVec3{5.0, 0.0, 0.0};
    entry.clips.push_back(slideClip("Slide"));
    const core::InstanceId player = fixture.rig(std::move(entry));

    render::AnimationSystem animation{fixture.world, fixture.skeletons};
    animation.play(animation.createTrack(player, "Slide"), 0.0f, 1.0f, 1.0f);
    animation.sample(1.0 / 60.0);

    const render::Pose* pose = animation.pose(fixture.mesh);
    REQUIRE(pose != nullptr);
    CHECK(close(pose->palette[0].m[3][0], 5.0f));
}

TEST_CASE("a non-looping clip stops at its end and reports it once")
{
    Fixture fixture;
    render::SkeletonLibrary::Entry entry = twoJointSkeleton();
    entry.clips.push_back(slideClip("Slide"));
    const core::InstanceId player = fixture.rig(std::move(entry));

    render::AnimationSystem animation{fixture.world, fixture.skeletons};
    const scene::TrackId track = animation.createTrack(player, "Slide");
    animation.play(track, 0.0f, 1.0f, 1.0f);

    for (int tick = 0; tick < 59; ++tick)
        animation.sample(1.0 / 60.0);
    CHECK(animation.drainEnded().empty());

    for (int tick = 0; tick < 5; ++tick)
        animation.sample(1.0 / 60.0);
    const std::span<const scene::TrackId> ended = animation.drainEnded();
    REQUIRE(ended.size() == 1);
    CHECK(ended[0] == track);
    CHECK_FALSE(animation.state(track).playing);
    // Clamped at the end rather than run past it, so a `TimePosition` read after
    // the fact is the clip's length and not whatever the tick overshot to.
    CHECK(close(static_cast<f32>(animation.state(track).timePosition), 1.0f));
    // Drained: the second frame does not fire `Ended` again.
    CHECK(animation.drainEnded().empty());

    // And the pose HOLDS the last frame rather than snapping to bind. `Ended` is
    // a deferred signal, so a handler that starts the next animation runs a tick
    // later -- and a character that returns to its rest pose for that one tick
    // is a visible pop.
    const render::Pose* pose = animation.pose(fixture.mesh);
    REQUIRE(pose != nullptr);
    CHECK(close(pose->palette[1].m[3][1], 2.0f));
    animation.stop(track, 0.0f);
    animation.sample(1.0 / 60.0);
    // Stopped, so the pose goes away rather than freezing the character
    // mid-stride: null means bind pose, which is what an unanimated skinned mesh
    // should look like.
    CHECK(animation.pose(fixture.mesh) == nullptr);
}

TEST_CASE("a looping clip wraps rather than resetting, and never ends")
{
    Fixture fixture;
    render::SkeletonLibrary::Entry entry = twoJointSkeleton();
    entry.clips.push_back(slideClip("Slide"));
    const core::InstanceId player = fixture.rig(std::move(entry));

    render::AnimationSystem animation{fixture.world, fixture.skeletons};
    const scene::TrackId track = animation.createTrack(player, "Slide");
    animation.setLooped(track, true);
    animation.play(track, 0.0f, 1.0f, 1.0f);

    // 1.05 seconds at 1/60 does not land on the loop point, which is the whole
    // point: a reset to zero would lose the 0.05 and the loop would drift
    // against everything else in the scene.
    for (int tick = 0; tick < 63; ++tick)
        animation.sample(1.0 / 60.0);
    CHECK(close(static_cast<f32>(animation.state(track).timePosition), 0.05f));
    CHECK(animation.state(track).playing);
    CHECK(animation.drainEnded().empty());
}

TEST_CASE("Play restarts from the beginning, unlike a tween")
{
    Fixture fixture;
    render::SkeletonLibrary::Entry entry = twoJointSkeleton();
    entry.clips.push_back(slideClip("Slide"));
    const core::InstanceId player = fixture.rig(std::move(entry));

    render::AnimationSystem animation{fixture.world, fixture.skeletons};
    const scene::TrackId track = animation.createTrack(player, "Slide");
    animation.play(track, 0.0f, 1.0f, 1.0f);
    for (int tick = 0; tick < 30; ++tick)
        animation.sample(1.0 / 60.0);

    animation.play(track, 0.0f, 1.0f, 1.0f);
    CHECK(close(static_cast<f32>(animation.state(track).timePosition), 0.0f));
}

TEST_CASE("two tracks at half weight land halfway between their clips")
{
    Fixture fixture;
    render::SkeletonLibrary::Entry entry = twoJointSkeleton();
    entry.clips.push_back(slideClip("Up"));

    // A second clip that drives the same joint the other way.
    asset::AnimationClip down = slideClip("Down");
    down.channels[0].values = {0.0f, 1.0f, 0.0f, 0.0f, -1.0f, 0.0f};
    entry.clips.push_back(down);
    const core::InstanceId player = fixture.rig(std::move(entry));

    render::AnimationSystem animation{fixture.world, fixture.skeletons};
    const scene::TrackId up = animation.createTrack(player, "Up");
    const scene::TrackId downTrack = animation.createTrack(player, "Down");
    // Three to one rather than one to one, so the answer is a number the bind
    // pose could not also produce -- an even blend of +3 and -1 IS the bind
    // pose, and a case that cannot tell blending from doing nothing is not a
    // case.
    animation.play(up, 0.0f, 0.75f, 1.0f);
    animation.play(downTrack, 0.0f, 0.25f, 1.0f);

    for (int tick = 0; tick < 30; ++tick)
        animation.sample(1.0 / 60.0);

    // Halfway: up is at 2, down is at 0, so the joint sits at 1.5 and the
    // inverse bind takes off the 1 it was bound at.
    const render::Pose* pose = animation.pose(fixture.mesh);
    REQUIRE(pose != nullptr);
    CHECK(close(pose->palette[1].m[3][1], 0.5f));
}

TEST_CASE("a weight of zero contributes nothing rather than dragging a joint to the origin")
{
    Fixture fixture;
    render::SkeletonLibrary::Entry entry = twoJointSkeleton();
    entry.clips.push_back(slideClip("Up"));
    entry.clips.push_back(slideClip("Also"));
    const core::InstanceId player = fixture.rig(std::move(entry));

    render::AnimationSystem animation{fixture.world, fixture.skeletons};
    const scene::TrackId loud = animation.createTrack(player, "Up");
    const scene::TrackId silent = animation.createTrack(player, "Also");
    animation.play(loud, 0.0f, 1.0f, 1.0f);
    animation.play(silent, 0.0f, 0.0f, 1.0f);

    for (int tick = 0; tick < 60; ++tick)
        animation.sample(1.0 / 60.0);

    const render::Pose* pose = animation.pose(fixture.mesh);
    REQUIRE(pose != nullptr);
    CHECK(close(pose->palette[1].m[3][1], 2.0f));
}

TEST_CASE("a fade reaches its target and a fade to zero is a stop")
{
    Fixture fixture;
    render::SkeletonLibrary::Entry entry = twoJointSkeleton();
    entry.clips.push_back(slideClip("Slide"));
    const core::InstanceId player = fixture.rig(std::move(entry));

    render::AnimationSystem animation{fixture.world, fixture.skeletons};
    const scene::TrackId track = animation.createTrack(player, "Slide");
    animation.play(track, 0.5f, 1.0f, 1.0f);
    CHECK(close(animation.state(track).weight, 0.0f));

    for (int tick = 0; tick < 15; ++tick)
        animation.sample(1.0 / 60.0);
    CHECK(close(animation.state(track).weight, 0.5f));

    for (int tick = 0; tick < 15; ++tick)
        animation.sample(1.0 / 60.0);
    CHECK(close(animation.state(track).weight, 1.0f));

    animation.stop(track, 0.25f);
    CHECK(animation.state(track).playing);
    for (int tick = 0; tick < 15; ++tick)
        animation.sample(1.0 / 60.0);
    CHECK(close(animation.state(track).weight, 0.0f));
    CHECK_FALSE(animation.state(track).playing);
    // A stop is not an end: `Ended` fires for a clip that finished, and this one
    // was cut short.
    CHECK(animation.drainEnded().empty());
}

TEST_CASE("Speed scales the clock and a speed of zero holds the pose")
{
    Fixture fixture;
    render::SkeletonLibrary::Entry entry = twoJointSkeleton();
    entry.clips.push_back(slideClip("Slide"));
    const core::InstanceId player = fixture.rig(std::move(entry));

    render::AnimationSystem animation{fixture.world, fixture.skeletons};
    const scene::TrackId track = animation.createTrack(player, "Slide");
    animation.play(track, 0.0f, 1.0f, 2.0f);
    for (int tick = 0; tick < 15; ++tick)
        animation.sample(1.0 / 60.0);
    CHECK(close(static_cast<f32>(animation.state(track).timePosition), 0.5f));

    animation.adjustSpeed(track, 0.0f);
    for (int tick = 0; tick < 60; ++tick)
        animation.sample(1.0 / 60.0);
    CHECK(close(static_cast<f32>(animation.state(track).timePosition), 0.5f));
}

TEST_CASE("retire forgets the tracks of an instance that is gone, and keeps answering reads")
{
    Fixture fixture;
    render::SkeletonLibrary::Entry entry = twoJointSkeleton();
    entry.clips.push_back(slideClip("Slide"));
    const core::InstanceId player = fixture.rig(std::move(entry));

    render::AnimationSystem animation{fixture.world, fixture.skeletons};
    const scene::TrackId track = animation.createTrack(player, "Slide");
    animation.play(track, 0.0f, 1.0f, 1.0f);
    animation.sample(1.0 / 60.0);
    REQUIRE(animation.pose(fixture.mesh) != nullptr);

    REQUIRE(fixture.world.destroy(player));
    // `destroy` is synchronous but the generation bump is not: `alive` stays
    // true until the drain retires it, which is what gives a `Destroying`
    // handler a handle to work with.
    fixture.world.retireDestroyed();
    animation.retire(fixture.world);

    CHECK_FALSE(animation.state(track).playing);
    CHECK(animation.pose(fixture.mesh) == nullptr);
    // The handle a script still holds resolves to a stopped track rather than to
    // whatever took the slot.
    CHECK(close(animation.state(track).length, 1.0f));
    animation.sample(1.0 / 60.0);
}

TEST_CASE("rotation is interpolated the short way round")
{
    // The failure is one key long and looks like a joint spinning through 350
    // degrees instead of 10, which is why it is checked rather than eyeballed.
    Fixture fixture;
    render::SkeletonLibrary::Entry entry = twoJointSkeleton();

    asset::AnimationChannel channel;
    channel.joint = 1;
    channel.target = asset::AnimationChannel::Target::Rotation;
    channel.stride = 4;
    channel.times = {0.0f, 1.0f};
    // Identity, then the SAME rotation written with every sign flipped -- which
    // is the same orientation and must therefore not move at all.
    channel.values = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, -1.0f};

    asset::AnimationClip clip;
    clip.name = "Flip";
    clip.duration = 1.0f;
    clip.channels.push_back(channel);
    entry.clips.push_back(clip);
    const core::InstanceId player = fixture.rig(std::move(entry));

    render::AnimationSystem animation{fixture.world, fixture.skeletons};
    animation.play(animation.createTrack(player, "Flip"), 0.0f, 1.0f, 1.0f);
    for (int tick = 0; tick < 30; ++tick)
        animation.sample(1.0 / 60.0);

    const render::Pose* pose = animation.pose(fixture.mesh);
    REQUIRE(pose != nullptr);
    // Halfway between q and -q is the identity if the sign is aligned, and a
    // zero quaternion if it is not.
    CHECK(close(pose->palette[1].m[0][0], 1.0f));
    CHECK(close(pose->palette[1].m[1][1], 1.0f));
    CHECK(close(pose->palette[1].m[2][2], 1.0f));
}

TEST_CASE("the skeleton library answers by content and is sorted, not hashed")
{
    core::AtomTable atoms;
    render::SkeletonLibrary library;

    // Interned out of alphabetical order on purpose: `find` must not depend on
    // the order they arrived in.
    const core::NameAtom second = atoms.intern("asset://models/z.glb");
    const core::NameAtom first = atoms.intern("asset://models/a.glb");

    render::SkeletonLibrary::Entry entry;
    entry.joints.emplace_back();
    library.set(second, entry);
    entry.joints.emplace_back();
    library.set(first, entry);

    REQUIRE(library.find(second) != nullptr);
    REQUIRE(library.find(first) != nullptr);
    CHECK(library.find(second)->joints.size() == 1);
    CHECK(library.find(first)->joints.size() == 2);
    CHECK(library.find(atoms.intern("asset://models/absent.glb")) == nullptr);

    // A second `set` on the same content replaces rather than appending.
    library.set(second, render::SkeletonLibrary::Entry{});
    CHECK(library.find(second)->joints.empty());

    library.clear();
    CHECK(library.find(first) == nullptr);
}

// --- scene::SkeletonHost -----------------------------------------------------

// A three-joint chain -- root, arm one unit up, hand one unit above that -- so
// that a joint can be overridden with a joint BELOW it that is not, which is the
// case the forward pass exists for.
render::SkeletonLibrary::Entry threeJointChain()
{
    render::SkeletonLibrary::Entry entry = twoJointSkeleton();
    asset::Joint hand;
    hand.name = "hand";
    hand.parent = 1;
    hand.localBind.position = core::DVec3{0.0, 1.0, 0.0};
    hand.inverseBind.m[3][1] = -2.0f;
    entry.joints.push_back(hand);
    return entry;
}

TEST_CASE("a skeleton answers what it is made of")
{
    Fixture fixture;
    (void)fixture.rig(threeJointChain());
    render::AnimationSystem animation(fixture.world, fixture.skeletons);
    scene::SkeletonHost& host = animation;

    CHECK(host.jointCount(fixture.mesh) == 3);
    CHECK(host.findJoint(fixture.mesh, "hand") == 2);
    CHECK(host.findJoint(fixture.mesh, "root") == 0);
    // A name nothing carries is -1 rather than 0, because 0 is a real joint --
    // and a `Bone` that silently resolved to the root would put a sword at a
    // character's feet.
    CHECK(host.findJoint(fixture.mesh, "tail") == -1);
    CHECK(host.jointName(fixture.mesh, 1) == "child");
    CHECK(host.jointParent(fixture.mesh, 2) == 1);
    CHECK(host.jointParent(fixture.mesh, 0) == -1);

    // A mesh with no rig at all, which is most of them.
    const core::InstanceId plain = fixture.world.create(fixture.meshPartClass);
    CHECK(host.jointCount(plain) == 0);
    CHECK(host.findJoint(plain, "root") == -1);
}

TEST_CASE("a joint answers where it is even with nothing playing")
{
    // **The whole reason `jointModel` is a call and not a peek at the palette.**
    // A character standing still has no pose at all, and a socket welded to its
    // hand still has to be somewhere -- at the hand, in the rest chain.
    Fixture fixture;
    (void)fixture.rig(threeJointChain());
    render::AnimationSystem animation(fixture.world, fixture.skeletons);
    scene::SkeletonHost& host = animation;

    core::CFrameD at;
    REQUIRE(host.jointModel(fixture.mesh, 2, at));
    // Two units up: one for the arm and one for the hand above it.
    CHECK(at.position.y == doctest::Approx(2.0));
    CHECK(at.position.x == doctest::Approx(0.0));

    REQUIRE(host.jointModel(fixture.mesh, 0, at));
    CHECK(at.position.y == doctest::Approx(0.0));

    CHECK_FALSE(host.jointModel(fixture.mesh, 7, at));
}

TEST_CASE("a joint answers where the clip put it once one is playing")
{
    Fixture fixture;
    const core::InstanceId player = fixture.rig(threeJointChain());
    render::SkeletonLibrary::Entry posed = threeJointChain();
    posed.clips.push_back(slideClip("slide"));
    fixture.skeletons.set(fixture.content, posed);

    render::AnimationSystem animation(fixture.world, fixture.skeletons);
    const scene::TrackId track = animation.createTrack(player, "slide");
    animation.play(track, 0.0f, 1.0f, 1.0f);
    // Half a second into a clip that slides the child from y = 1 to y = 3.
    animation.sample(0.5);

    core::CFrameD at;
    REQUIRE(animation.jointModel(fixture.mesh, 1, at));
    CHECK(at.position.y == doctest::Approx(2.0).epsilon(0.01));
    // And the hand rode along, one unit above wherever the arm ended up.
    REQUIRE(animation.jointModel(fixture.mesh, 2, at));
    CHECK(at.position.y == doctest::Approx(3.0).epsilon(0.01));
}

TEST_CASE("an override moves the joints below it")
{
    // A ragdoll simulates a dozen bones and a hand has twenty. The ones it does
    // NOT simulate take their parent's new transform and their own unchanged
    // local -- which is what makes them ride along rather than stay where the
    // clip left them.
    Fixture fixture;
    (void)fixture.rig(threeJointChain());
    render::AnimationSystem animation(fixture.world, fixture.skeletons);
    scene::SkeletonHost& host = animation;

    core::CFrameD moved;
    moved.position = core::DVec3{5.0, 1.0, 0.0};
    host.setJointOverride(fixture.mesh, 1, moved);
    host.commitOverrides();

    core::CFrameD at;
    REQUIRE(host.jointModel(fixture.mesh, 1, at));
    CHECK(at.position.x == doctest::Approx(5.0));
    // The hand was never overridden and followed anyway.
    REQUIRE(host.jointModel(fixture.mesh, 2, at));
    CHECK(at.position.x == doctest::Approx(5.0));
    CHECK(at.position.y == doctest::Approx(2.0));
    // And the root above it did not move, because an override reaches down and
    // not up.
    REQUIRE(host.jointModel(fixture.mesh, 0, at));
    CHECK(at.position.x == doctest::Approx(0.0));
}

TEST_CASE("going limp keeps the pose the animation left, joint by joint")
{
    // **The repair that is invisible until a ragdoll takes over a character
    // that was moving.** A mesh whose tracks stop contributing used to have its
    // pose ERASED -- and `commitOverrides` then rebuilds from the REST chain,
    // so every joint the ragdoll does not simulate snaps out of the animation
    // it was in and into bind pose, on the exact frame the character goes limp.
    //
    // Here the clip has slid the child to y = 3 and the hand rides one above it.
    // The ragdoll then drives the CHILD and leaves the hand alone: the hand must
    // keep the local the clip gave it, not the rest one.
    Fixture fixture;
    const core::InstanceId player = fixture.rig(threeJointChain());
    render::SkeletonLibrary::Entry posed = threeJointChain();
    // The hand is bent out along X by the clip, which the rest chain does not do.
    asset::AnimationChannel bend;
    bend.joint = 2;
    bend.target = asset::AnimationChannel::Target::Translation;
    bend.stride = 3;
    bend.times = {0.0f, 1.0f};
    bend.values = {3.0f, 1.0f, 0.0f, 3.0f, 1.0f, 0.0f};
    asset::AnimationClip clip = slideClip("slide");
    clip.channels.push_back(bend);
    posed.clips.push_back(clip);
    fixture.skeletons.set(fixture.content, posed);

    render::AnimationSystem animation(fixture.world, fixture.skeletons);
    scene::SkeletonHost& host = animation;
    const scene::TrackId track = animation.createTrack(player, "slide");
    animation.play(track, 0.0f, 1.0f, 1.0f);
    animation.sample(1.0);

    core::CFrameD handWhilePlaying;
    REQUIRE(host.jointModel(fixture.mesh, 2, handWhilePlaying));
    // Bent three units out along X by the clip.
    CHECK(handWhilePlaying.position.x == doctest::Approx(3.0).epsilon(0.01));

    // The character goes limp: the clip stops contributing, and the ragdoll
    // starts driving the joint above the hand in the same tick.
    animation.stop(track, 0.0f);
    core::CFrameD driven;
    driven.position = core::DVec3{0.0, 6.0, 0.0};
    host.setJointOverride(fixture.mesh, 1, driven);
    animation.sample(1.0 / 60.0);
    host.commitOverrides();

    core::CFrameD hand;
    REQUIRE(host.jointModel(fixture.mesh, 2, hand));
    // It followed the driven joint up...
    CHECK(hand.position.y == doctest::Approx(7.0).epsilon(0.01));
    // ...and it is STILL BENT, which is the whole point. Rebuilt from rest it
    // would be at x = 0, straight, and the character's hand would snap open.
    CHECK(hand.position.x == doctest::Approx(3.0).epsilon(0.01));
}

TEST_CASE("an override survives a tick in which no clip contributes")
{
    // **The repair that is invisible until a ragdoll goes limp.** A mesh with
    // nothing playing used to have its pose ERASED -- and a mesh with no track
    // was never even visited -- so a ragdoll driving a character with no
    // animation had nowhere to write and the character snapped to bind pose on
    // exactly the frame it must not.
    Fixture fixture;
    (void)fixture.rig(threeJointChain());
    render::AnimationSystem animation(fixture.world, fixture.skeletons);
    scene::SkeletonHost& host = animation;

    core::CFrameD moved;
    moved.position = core::DVec3{0.0, 9.0, 0.0};
    host.setJointOverride(fixture.mesh, 1, moved);
    host.commitOverrides();

    // A tick with no track at all. The pose must still be there afterwards.
    animation.sample(1.0 / 60.0);

    REQUIRE(animation.pose(fixture.mesh) != nullptr);
    core::CFrameD at;
    REQUIRE(host.jointModel(fixture.mesh, 1, at));
    CHECK(at.position.y == doctest::Approx(9.0));
}

TEST_CASE("overrides last one tick and no longer")
{
    // An override that outlived the tick that set it is a ragdoll still driving
    // a character nobody is simulating.
    Fixture fixture;
    (void)fixture.rig(threeJointChain());
    render::AnimationSystem animation(fixture.world, fixture.skeletons);
    scene::SkeletonHost& host = animation;

    core::CFrameD moved;
    moved.position = core::DVec3{4.0, 1.0, 0.0};
    host.setJointOverride(fixture.mesh, 1, moved);
    host.commitOverrides();

    core::CFrameD at;
    REQUIRE(host.jointModel(fixture.mesh, 1, at));
    CHECK(at.position.x == doctest::Approx(4.0));

    // A second commit with nothing set changes nothing: the palette holds the
    // last committed pose rather than being rebuilt from an override that is
    // gone.
    host.commitOverrides();
    REQUIRE(host.jointModel(fixture.mesh, 1, at));
    CHECK(at.position.x == doctest::Approx(4.0));
}

TEST_CASE("clearing an override takes the mesh out of the drive entirely")
{
    Fixture fixture;
    (void)fixture.rig(threeJointChain());
    render::AnimationSystem animation(fixture.world, fixture.skeletons);
    scene::SkeletonHost& host = animation;

    core::CFrameD moved;
    moved.position = core::DVec3{4.0, 1.0, 0.0};
    host.setJointOverride(fixture.mesh, 1, moved);
    host.clearJointOverrides(fixture.mesh);
    host.commitOverrides();

    // Never committed, so there is no pose -- and a null pose means bind pose,
    // which is the honest answer for a mesh nothing is driving.
    CHECK(animation.pose(fixture.mesh) == nullptr);
    core::CFrameD at;
    REQUIRE(host.jointModel(fixture.mesh, 1, at));
    CHECK(at.position.x == doctest::Approx(0.0));
}

TEST_CASE("an override on a joint the rig does not have is refused")
{
    Fixture fixture;
    (void)fixture.rig(threeJointChain());
    render::AnimationSystem animation(fixture.world, fixture.skeletons);
    scene::SkeletonHost& host = animation;

    host.setJointOverride(fixture.mesh, 9, core::CFrameD{});
    host.commitOverrides();
    // Nothing was driven, so nothing was built.
    CHECK(animation.pose(fixture.mesh) == nullptr);
}

TEST_CASE("the pose keeps the model transforms it used to throw away")
{
    Fixture fixture;
    const core::InstanceId player = fixture.rig(threeJointChain());
    render::SkeletonLibrary::Entry posed = threeJointChain();
    posed.clips.push_back(slideClip("slide"));
    fixture.skeletons.set(fixture.content, posed);

    render::AnimationSystem animation(fixture.world, fixture.skeletons);
    const scene::TrackId track = animation.createTrack(player, "slide");
    animation.play(track, 0.0f, 1.0f, 1.0f);
    animation.sample(0.5);

    const render::Pose* pose = animation.pose(fixture.mesh);
    REQUIRE(pose != nullptr);
    REQUIRE(pose->model.size() == 3);
    REQUIRE(pose->local.size() == 3);
    REQUIRE(pose->palette.size() == 3);

    // `model` is joint space to MODEL space, and `palette` is that with
    // `inverseBind` folded in -- two different matrices, and confusing them is
    // what puts a socket at the origin.
    // Compared at f32. `doctest::Approx` takes a double, and letting a matrix
    // entry promote into it is a `-Wdouble-promotion` error under Clang.
    const auto nearF = [](f32 value, f32 expected) { return std::fabs(value - expected) < 0.01f; };
    CHECK(nearF(pose->model[1].m[3][1], 2.0f));
    CHECK(nearF(pose->palette[1].m[3][1], 1.0f));
    // `local` is relative to the parent: the child slid to two units above a
    // root that did not move.
    CHECK(nearF(pose->local[1].m[3][1], 2.0f));
    CHECK(nearF(pose->local[2].m[3][1], 1.0f));
}

// --- One player over several meshes -------------------------------------------

namespace {

// The same two-joint skeleton, with the joints in the OTHER order -- which is
// what two files exported separately look like. Matching by index would drive
// the child with the root's channel and twist the sleeve.
render::SkeletonLibrary::Entry shirtSkeleton()
{
    render::SkeletonLibrary::Entry entry;

    asset::Joint child;
    child.name = "child";
    child.parent = asset::Joint::NoParent;
    child.localBind.position = core::DVec3{0.0, 1.0, 0.0};
    child.inverseBind.m[3][1] = -1.0f;
    entry.joints.push_back(child);

    asset::Joint root;
    root.name = "root";
    root.parent = asset::Joint::NoParent;
    entry.joints.push_back(root);

    return entry;
}

} // namespace

TEST_CASE("one player over a Model drives every skinned mesh under it")
{
    // **A character is a body, a shirt and a pair of trousers.** Several skinned
    // meshes wearing the same skeleton, and one clip has to move all of them --
    // parented to the `Model` rather than to any one piece.
    Fixture fixture;

    render::SkeletonLibrary::Entry body = twoJointSkeleton();
    body.clips.push_back(slideClip("slide"));
    const core::NameAtom bodyContent = fixture.atoms.intern("asset://models/body.glb");
    const core::NameAtom shirtContent = fixture.atoms.intern("asset://models/shirt.glb");
    fixture.skeletons.set(bodyContent, body);
    // The shirt has NO clips of its own, which is the point: only one piece
    // needs to carry the animation.
    fixture.skeletons.set(shirtContent, shirtSkeleton());

    const core::InstanceId model = fixture.world.create(fixture.instanceClass);
    const core::InstanceId bodyMesh = fixture.world.create(fixture.meshPartClass);
    fixture.world.meshParts().find(bodyMesh)->meshContent = bodyContent;
    REQUIRE(fixture.world.setParent(bodyMesh, model) == std::nullopt);
    const core::InstanceId shirtMesh = fixture.world.create(fixture.meshPartClass);
    fixture.world.meshParts().find(shirtMesh)->meshContent = shirtContent;
    REQUIRE(fixture.world.setParent(shirtMesh, model) == std::nullopt);

    // Parented to the MODEL, not to a mesh.
    const core::InstanceId player = fixture.world.create(fixture.instanceClass);
    REQUIRE(fixture.world.setParent(player, model) == std::nullopt);

    render::AnimationSystem animation(fixture.world, fixture.skeletons);
    const scene::TrackId track = animation.createTrack(player, "slide");
    REQUIRE(track != 0);
    animation.play(track, 0.0f, 1.0f, 1.0f);
    animation.sample(0.5);

    // The body moved: the clip slides its `child` from y = 1 to y = 3, so half a
    // second in it is at 2.
    core::CFrameD at;
    REQUIRE(animation.jointModel(bodyMesh, 1, at));
    CHECK(at.position.y == doctest::Approx(2.0).epsilon(0.01));

    // **And the shirt moved with it**, through the joint called `child` -- which
    // is index 0 in its file rather than index 1. Matched by index it would have
    // been the root that moved, and the sleeve would twist.
    REQUIRE(animation.jointModel(shirtMesh, 0, at));
    CHECK(at.position.y == doctest::Approx(2.0).epsilon(0.01));
    // The shirt's own root did not move, which is what says the remap landed on
    // the right joint rather than on all of them.
    REQUIRE(animation.jointModel(shirtMesh, 1, at));
    CHECK(at.position.y == doctest::Approx(0.0).epsilon(0.01));
}

TEST_CASE("a joint the other rig does not have is skipped rather than guessed")
{
    // A shirt with no fingers keeps its own sleeve rather than inheriting a
    // finger's rotation.
    Fixture fixture;
    render::SkeletonLibrary::Entry body = twoJointSkeleton();
    body.clips.push_back(slideClip("slide"));
    const core::NameAtom bodyContent = fixture.atoms.intern("asset://models/body.glb");
    const core::NameAtom hatContent = fixture.atoms.intern("asset://models/hat.glb");
    fixture.skeletons.set(bodyContent, body);

    // One joint, and not the one the clip drives.
    render::SkeletonLibrary::Entry hat;
    asset::Joint only;
    only.name = "root";
    only.parent = asset::Joint::NoParent;
    hat.joints.push_back(only);
    fixture.skeletons.set(hatContent, hat);

    const core::InstanceId model = fixture.world.create(fixture.instanceClass);
    const core::InstanceId bodyMesh = fixture.world.create(fixture.meshPartClass);
    fixture.world.meshParts().find(bodyMesh)->meshContent = bodyContent;
    REQUIRE(fixture.world.setParent(bodyMesh, model) == std::nullopt);
    const core::InstanceId hatMesh = fixture.world.create(fixture.meshPartClass);
    fixture.world.meshParts().find(hatMesh)->meshContent = hatContent;
    REQUIRE(fixture.world.setParent(hatMesh, model) == std::nullopt);

    const core::InstanceId player = fixture.world.create(fixture.instanceClass);
    REQUIRE(fixture.world.setParent(player, model) == std::nullopt);

    render::AnimationSystem animation(fixture.world, fixture.skeletons);
    const scene::TrackId track = animation.createTrack(player, "slide");
    animation.play(track, 0.0f, 1.0f, 1.0f);
    animation.sample(0.5);

    core::CFrameD at;
    REQUIRE(animation.jointModel(hatMesh, 0, at));
    CHECK(at.position.y == doctest::Approx(0.0).epsilon(0.01));
}

TEST_CASE("a player parented straight to a mesh still drives only that mesh")
{
    // The older shape, and the one a character made of one mesh uses. Widening
    // the rule must not widen it past what somebody asked for.
    Fixture fixture;
    render::SkeletonLibrary::Entry body = twoJointSkeleton();
    body.clips.push_back(slideClip("slide"));
    const core::NameAtom bodyContent = fixture.atoms.intern("asset://models/body.glb");
    const core::NameAtom shirtContent = fixture.atoms.intern("asset://models/shirt.glb");
    fixture.skeletons.set(bodyContent, body);
    fixture.skeletons.set(shirtContent, shirtSkeleton());

    const core::InstanceId model = fixture.world.create(fixture.instanceClass);
    const core::InstanceId bodyMesh = fixture.world.create(fixture.meshPartClass);
    fixture.world.meshParts().find(bodyMesh)->meshContent = bodyContent;
    REQUIRE(fixture.world.setParent(bodyMesh, model) == std::nullopt);
    const core::InstanceId shirtMesh = fixture.world.create(fixture.meshPartClass);
    fixture.world.meshParts().find(shirtMesh)->meshContent = shirtContent;
    REQUIRE(fixture.world.setParent(shirtMesh, model) == std::nullopt);

    // Parented to the BODY.
    const core::InstanceId player = fixture.world.create(fixture.instanceClass);
    REQUIRE(fixture.world.setParent(player, bodyMesh) == std::nullopt);

    render::AnimationSystem animation(fixture.world, fixture.skeletons);
    const scene::TrackId track = animation.createTrack(player, "slide");
    animation.play(track, 0.0f, 1.0f, 1.0f);
    animation.sample(0.5);

    core::CFrameD at;
    REQUIRE(animation.jointModel(bodyMesh, 1, at));
    CHECK(at.position.y == doctest::Approx(2.0).epsilon(0.01));
    // The shirt is untouched: it has no pose at all.
    CHECK(animation.pose(shirtMesh) == nullptr);
}
