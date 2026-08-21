#include "luaug/scene/components.h"
#include "luaug/scene/streaming_glue.h"
#include "luaug/scene/world.h"

#include <doctest/doctest.h>
#include <string>
#include <vector>

#include "scene_fixture.h"

using namespace luaug;
using namespace luaug::scene;

namespace {

[[nodiscard]] asset::Chunk chunkWith(asset::ChunkId id, int parts, bool withMesh = false)
{
    asset::Chunk chunk;
    chunk.id = id;
    chunk.bounds = asset::chunkBounds(id, asset::DefaultChunkSize);
    chunk.strings = {"Rock", "asset://models/rock.glb"};

    for (int i = 0; i < parts; ++i) {
        asset::ChunkInstance instance;
        instance.kind = withMesh ? asset::ChunkInstance::Kind::MeshPart : asset::ChunkInstance::Kind::Part;
        instance.cframe.position = core::DVec3{static_cast<core::f64>(i) * 4.0, 1.0, 0.0};
        instance.size = core::Vec3{2.0f, 3.0f, 4.0f};
        instance.color = core::Color3{0.25f, 0.5f, 0.75f};
        instance.transparency = 0.5f;
        instance.shape = 1;
        instance.anchored = true;
        instance.name = 0;
        instance.meshContent = withMesh ? 1u : asset::ChunkInstance::NoString;
        chunk.instances.push_back(instance);
    }
    return chunk;
}

struct Fixture
{
    testing::Fixture scene;
    core::InstanceId root;
    StreamingGlue glue;

    Fixture() : root(scene.folder("StreamedWorld")), glue(scene.world, root) {}
};

} // namespace

TEST_CASE("a chunk becomes instances under a folder of its own")
{
    Fixture fixture;
    const asset::Chunk chunk = chunkWith(asset::ChunkId{2, -3, 0}, 4);

    CHECK(fixture.glue.materialize(chunk.id, chunk) >= 0.0);
    CHECK(fixture.glue.residentChunks() == 1);
    CHECK(fixture.glue.residentInstances() == 4);

    // The folder is what makes a streamed world navigable in the explorer, and
    // it is named for the cell so a person looking at a wrong part can tell
    // which chunk put it there.
    const core::InstanceId folder =
        fixture.scene.world.findFirstChild(fixture.root, fixture.scene.world.atoms().intern("Chunk_2_-3_0"));
    REQUIRE(folder.valid());
    CHECK(fixture.scene.world.childCount(folder) == 4);
}

TEST_CASE("every property the chunk carries reaches the instance")
{
    Fixture fixture;
    const asset::Chunk chunk = chunkWith(asset::ChunkId{}, 1);
    fixture.glue.materialize(chunk.id, chunk);

    const core::InstanceId folder =
        fixture.scene.world.findFirstChild(fixture.root, fixture.scene.world.atoms().intern("Chunk_0_0_0"));
    REQUIRE(folder.valid());
    const core::InstanceId part =
        fixture.scene.world.findFirstChild(folder, fixture.scene.world.atoms().intern("Rock"));
    REQUIRE(part.valid());

    const PartComponent* component = fixture.scene.world.parts().find(part);
    REQUIRE(component != nullptr);
    CHECK(component->cframe.position == core::DVec3{0.0, 1.0, 0.0});
    CHECK(component->size == core::Vec3{2.0f, 3.0f, 4.0f});
    CHECK(component->color == core::Color3{0.25f, 0.5f, 0.75f});
    CHECK(component->transparency == 0.5f);
    CHECK(component->shape == 1);

    // `anchored` is not asserted here, and the reason is the fixture rather
    // than the glue: `scene_fixture` models the M2 surface and its `BasePart`
    // attaches only a `PartComponent`, where the real registry also attaches a
    // `RigidBodyComponent` (`native_accessors.cpp`). Teaching the fixture to
    // attach one would collapse the distinction the physics-mirror tests rely
    // on -- a part WITH a body and a part without. The write is one guarded
    // line and it is covered end to end by `examples/05-streaming`, where an
    // unanchored chunk instance falls.
    CHECK(fixture.scene.world.rigidBodies().find(part) == nullptr);
}

TEST_CASE("a MeshPart carries the URN the chunk named")
{
    Fixture fixture;
    const asset::Chunk chunk = chunkWith(asset::ChunkId{}, 1, true);
    fixture.glue.materialize(chunk.id, chunk);

    const core::InstanceId folder =
        fixture.scene.world.findFirstChild(fixture.root, fixture.scene.world.atoms().intern("Chunk_0_0_0"));
    const core::InstanceId part =
        fixture.scene.world.findFirstChild(folder, fixture.scene.world.atoms().intern("Rock"));
    REQUIRE(part.valid());

    const MeshPartComponent* mesh = fixture.scene.world.meshParts().find(part);
    REQUIRE(mesh != nullptr);
    CHECK(fixture.scene.world.atoms().text(mesh->meshContent) == "asset://models/rock.glb");
}

TEST_CASE("materialising the same chunk twice does not build it twice")
{
    Fixture fixture;
    const asset::Chunk chunk = chunkWith(asset::ChunkId{}, 3);

    fixture.glue.materialize(chunk.id, chunk);
    fixture.glue.materialize(chunk.id, chunk);

    // A world with two of everything and no way to tell is worse than a
    // duplicate request that does nothing.
    CHECK(fixture.glue.residentChunks() == 1);
    CHECK(fixture.glue.residentInstances() == 3);
    CHECK(fixture.scene.world.childCount(fixture.root) == 1);
}

TEST_CASE("eviction destroys what nothing holds")
{
    Fixture fixture;
    const asset::Chunk chunk = chunkWith(asset::ChunkId{1, 1, 0}, 5);
    fixture.glue.materialize(chunk.id, chunk);

    fixture.glue.evict(chunk.id);

    CHECK(fixture.glue.residentChunks() == 0);
    CHECK(fixture.glue.residentInstances() == 0);
    CHECK(fixture.glue.husksCreated() == 0);
    // The folder goes with them: a streamed world that left an empty folder per
    // visited cell would fill the explorer with the history of where the
    // camera had been.
    CHECK(fixture.scene.world.childCount(fixture.root) == 0);
    CHECK(fixture.glue.drainStreamedOut().empty());
}

TEST_CASE("an instance a script still holds becomes a husk rather than a dangling id")
{
    Fixture fixture;
    const asset::Chunk chunk = chunkWith(asset::ChunkId{}, 3);
    fixture.glue.materialize(chunk.id, chunk);

    const core::InstanceId folder =
        fixture.scene.world.findFirstChild(fixture.root, fixture.scene.world.atoms().intern("Chunk_0_0_0"));
    const core::InstanceId held = fixture.scene.world.firstChild(folder);
    REQUIRE(held.valid());

    fixture.glue.setReferenceProbe([held](core::InstanceId id) { return id == held; });
    fixture.glue.evict(chunk.id);

    // §4's contract: reparented to nil rather than destroyed, so the Luau side
    // is left with a handle to something rather than to nothing. This is what
    // `StreamingService.InstanceStreamedOut` reports.
    CHECK(fixture.scene.world.alive(held));
    CHECK_FALSE(fixture.scene.world.parentOf(held).valid());
    CHECK(fixture.glue.husksCreated() == 1);

    const std::vector<core::InstanceId> streamedOut = fixture.glue.drainStreamedOut();
    REQUIRE(streamedOut.size() == 1);
    CHECK(streamedOut[0] == held);
    // Drained rather than accumulated: the host turns each into one deferred
    // signal, and a list that kept growing would fire the same one every frame.
    CHECK(fixture.glue.drainStreamedOut().empty());
}

TEST_CASE("clearing takes the whole streamed world with it")
{
    Fixture fixture;
    for (int x = 0; x < 3; ++x) {
        const asset::Chunk chunk = chunkWith(asset::ChunkId{x, 0, 0}, 2);
        fixture.glue.materialize(chunk.id, chunk);
    }
    CHECK(fixture.glue.residentChunks() == 3);
    CHECK(fixture.scene.world.childCount(fixture.root) == 3);

    fixture.glue.clear();
    CHECK(fixture.glue.residentChunks() == 0);
    CHECK(fixture.glue.residentInstances() == 0);
    CHECK(fixture.scene.world.childCount(fixture.root) == 0);
}

TEST_CASE("evicting a chunk that was never resident is a no-op")
{
    Fixture fixture;
    fixture.glue.evict(asset::ChunkId{7, 7, 0});
    CHECK(fixture.glue.residentChunks() == 0);
}
