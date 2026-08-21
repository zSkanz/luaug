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
