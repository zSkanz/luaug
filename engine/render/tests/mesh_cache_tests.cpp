#include <doctest/doctest.h>

#include "luaug/core/i18n.h"
#include "luaug/render/mesh_cache.h"
#include "luaug/rhi/backends.h"

using luaug::asset::Mesh;
using luaug::asset::Submesh;
using luaug::asset::Vertex;
using luaug::core::u32;
using luaug::render::MeshCache;
using luaug::render::MeshHandle;
using luaug::render::MeshUsage;

namespace
{

// The null device is enough here and the capture device would be worse: what
// these cases assert is `MeshCache`'s own bookkeeping -- which handle resolves,
// to which buffer, at which offset -- and asserting that through a recorded
// command stream would be asserting on the text of a log instead of on the
// thing that produces it.
struct DeviceFixture
{
    luaug::rhi::DeviceResult device = luaug::rhi::createNullDevice({.backend = luaug::rhi::BackendId::Null});
    luaug::rhi::ICmdList* cmd = nullptr;

    DeviceFixture()
    {
        REQUIRE(device != nullptr);
        cmd = device->beginFrame();
        REQUIRE(cmd != nullptr);
    }
};

Mesh makeMesh(u32 vertexCount, u32 indexCount, u32 submeshCount = 1)
{
    Mesh mesh;
    mesh.vertices.resize(vertexCount);
    mesh.indices.resize(indexCount);
    for (u32 index = 0; index < vertexCount; ++index)
    {
        const auto value = static_cast<float>(index);
        mesh.vertices[index].position = {value, value, value};
        luaug::core::expand(mesh.bounds, mesh.vertices[index].position);
    }

    const u32 perSubmesh = submeshCount == 0 ? 0 : indexCount / submeshCount;
    for (u32 slot = 0; slot < submeshCount; ++slot)
    {
        Submesh submesh;
        submesh.firstIndex = slot * perSubmesh;
        submesh.indexCount = perSubmesh;
        submesh.material = slot;
        submesh.bounds = mesh.bounds;
        mesh.submeshes.push_back(submesh);
    }
    return mesh;
}

} // namespace

TEST_CASE_FIXTURE(DeviceFixture, "MeshCache: a static mesh resolves to its own buffers and its submeshes")
{
    MeshCache cache;
    REQUIRE_FALSE(cache.create(*device).has_value());

    const Mesh mesh = makeMesh(8, 12, 3);
    const MeshHandle handle = cache.create(*device, *cmd, mesh, MeshUsage::Static);
    REQUIRE(handle.valid());

    const MeshCache::Resolved* resolved = cache.resolve(handle);
    REQUIRE(resolved != nullptr);
    CHECK(resolved->vertices.valid());
    CHECK(resolved->indices.valid());
    // A static mesh owns its buffers outright, so nothing is offset into a
    // shared one.
    CHECK(resolved->firstIndex == 0u);
    CHECK(resolved->vertexOffset == 0);
    REQUIRE(resolved->sections.size() == 3u);
    CHECK(resolved->sections[1].firstIndex == 4u);
    CHECK(resolved->sections[1].indexCount == 4u);
    CHECK(resolved->sections[2].material == 2u);
    CHECK(cache.staticMeshCount() == 1u);

    cache.destroy(*device);
}

TEST_CASE_FIXTURE(DeviceFixture, "MeshCache: a released handle stops resolving, and its slot does not resurrect it")
{
    MeshCache cache;
    REQUIRE_FALSE(cache.create(*device).has_value());

    const MeshHandle first = cache.create(*device, *cmd, makeMesh(4, 6), MeshUsage::Static);
    REQUIRE(cache.resolve(first) != nullptr);

    cache.release(*device, first);
    CHECK(cache.resolve(first) == nullptr);
    CHECK(cache.staticMeshCount() == 0u);

    // The slot is recycled. The generation is what stops the old handle naming
    // the new mesh -- without it, this is the bug the handle type exists for.
    const MeshHandle second = cache.create(*device, *cmd, makeMesh(4, 6), MeshUsage::Static);
    CHECK(second.index == first.index);
    CHECK(second.generation != first.generation);
    CHECK(cache.resolve(second) != nullptr);
    CHECK(cache.resolve(first) == nullptr);

    // A default handle names nothing, which is why generation starts at 1.
    CHECK(cache.resolve(MeshHandle{}) == nullptr);

    cache.destroy(*device);
}

TEST_CASE_FIXTURE(DeviceFixture, "MeshCache: dynamic meshes share one ring and are packed in order")
{
    MeshCache cache;
    REQUIRE_FALSE(cache.create(*device, 1024, 2048).has_value());
    cache.beginFrame(*device);

    const MeshHandle first = cache.create(*device, *cmd, makeMesh(4, 6), MeshUsage::Dynamic);
    const MeshHandle second = cache.create(*device, *cmd, makeMesh(5, 9), MeshUsage::Dynamic);
    REQUIRE(first.valid());
    REQUIRE(second.valid());

    const MeshCache::Resolved* a = cache.resolve(first);
    const MeshCache::Resolved* b = cache.resolve(second);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);

    // One buffer, two slices: that is the whole point of the ring.
    CHECK(a->vertices == b->vertices);
    CHECK(a->indices == b->indices);
    CHECK(a->firstIndex == 0u);
    CHECK(a->vertexOffset == 0);
    CHECK(b->firstIndex == 6u);
    CHECK(b->vertexOffset == 4);

    // Nothing dynamic counts as a static mesh, so a frame of debug geometry
    // cannot look like a leak.
    CHECK(cache.staticMeshCount() == 0u);

    cache.destroy(*device);
}

TEST_CASE_FIXTURE(DeviceFixture, "MeshCache: a dynamic handle does not survive the frame that made it")
{
    MeshCache cache;
    REQUIRE_FALSE(cache.create(*device, 1024, 2048).has_value());

    cache.beginFrame(*device);
    const MeshHandle dynamic = cache.create(*device, *cmd, makeMesh(4, 6), MeshUsage::Dynamic);
    const MeshHandle stat = cache.create(*device, *cmd, makeMesh(4, 6), MeshUsage::Static);
    REQUIRE(cache.resolve(dynamic) != nullptr);

    cache.beginFrame(*device);
    // The contract that makes MeshUsage worth having: holding a dynamic handle
    // across a frame boundary yields nothing, rather than yielding whatever
    // geometry now occupies that region of the ring. It holds because
    // `beginFrame` retires the entry, and because the slot -- if reused below --
    // carries a new generation.
    CHECK(cache.resolve(dynamic) == nullptr);
    // A static mesh is untouched by the frame boundary.
    CHECK(cache.resolve(stat) != nullptr);

    // And the ring is rewound, so the next frame starts at zero again.
    const MeshHandle next = cache.create(*device, *cmd, makeMesh(4, 6), MeshUsage::Dynamic);
    const MeshCache::Resolved* resolved = cache.resolve(next);
    REQUIRE(resolved != nullptr);
    CHECK(resolved->firstIndex == 0u);
    CHECK(resolved->vertexOffset == 0);

    // The recycled slot is the same one, and the stale handle must not come
    // back as this mesh. This is the assertion the frame-stamp field was
    // supposed to be protecting; the generation was already doing it, which is
    // why the field is gone.
    CHECK(next.index == dynamic.index);
    CHECK(next.generation != dynamic.generation);
    CHECK(cache.resolve(dynamic) == nullptr);

    cache.destroy(*device);
}

TEST_CASE_FIXTURE(DeviceFixture, "MeshCache: a ring that overflows grows, and what was already written stays drawable")
{
    MeshCache cache;
    // Deliberately tiny, so the second mesh cannot fit.
    REQUIRE_FALSE(cache.create(*device, 8, 8).has_value());
    cache.beginFrame(*device);

    const MeshHandle before = cache.create(*device, *cmd, makeMesh(4, 6), MeshUsage::Dynamic);
    REQUIRE(before.valid());
    const MeshCache::Resolved* first = cache.resolve(before);
    REQUIRE(first != nullptr);
    const luaug::rhi::BufferHandle firstBuffer = first->vertices;

    const MeshHandle after = cache.create(*device, *cmd, makeMesh(64, 96), MeshUsage::Dynamic);
    REQUIRE(after.valid());

    // The grow allocated a new ring, so the two handles name different buffers
    // -- and the earlier one must still resolve, because a caller that already
    // recorded a draw against it is entitled to that geometry for the rest of
    // the frame. Destroying the old ring here is the bug this case exists for.
    const MeshCache::Resolved* stillThere = cache.resolve(before);
    REQUIRE(stillThere != nullptr);
    CHECK(stillThere->vertices == firstBuffer);
    CHECK(cache.resolve(after)->vertices != firstBuffer);

    CHECK(cache.ringVertexHighWater() >= 64u);

    // The old ring is held, not destroyed. A buffer handle says nothing about
    // whether it has been destroyed -- the null device's `destroy` is a no-op
    // and the handle stays comparable -- so the first version of this case
    // passed against an implementation that freed the ring immediately. What
    // makes the rule assertable is the count of pending releases.
    CHECK(cache.pendingRingReleases() == 1u);
    cache.beginFrame(*device);
    // Still held one frame later: the GPU may still be executing the commands
    // that named it.
    CHECK(cache.pendingRingReleases() == 1u);
    cache.beginFrame(*device);
    CHECK(cache.pendingRingReleases() == 0u);

    cache.destroy(*device);
}

TEST_CASE_FIXTURE(DeviceFixture, "MeshCache: an empty mesh is a handle that draws nothing, not an error")
{
    MeshCache cache;
    REQUIRE_FALSE(cache.create(*device).has_value());

    // A generator that produced nothing this frame is not a failure, and the
    // alternative is every caller branching on emptiness before it can draw.
    const MeshHandle handle = cache.create(*device, *cmd, Mesh{}, MeshUsage::Static);
    REQUIRE(handle.valid());
    const MeshCache::Resolved* resolved = cache.resolve(handle);
    REQUIRE(resolved != nullptr);
    CHECK(resolved->sections.empty());

    cache.destroy(*device);
}
