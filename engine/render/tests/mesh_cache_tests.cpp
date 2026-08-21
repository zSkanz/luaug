#include "luaug/core/i18n.h"
#include "luaug/render/mesh_cache.h"
#include "luaug/rhi/backends.h"

#include <doctest/doctest.h>
#include <initializer_list>
#include <vector>

using luaug::asset::Mesh;
using luaug::asset::Submesh;
using luaug::asset::Vertex;
using luaug::core::f32;
using luaug::core::u32;
using luaug::render::MeshCache;
using luaug::render::MeshHandle;
using luaug::render::MeshLodRange;
using luaug::render::MeshSection;
using luaug::render::MeshUsage;
using luaug::render::selectMeshLod;

namespace {

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
    for (u32 index = 0; index < vertexCount; ++index) {
        const auto value = static_cast<float>(index);
        mesh.vertices[index].position = {value, value, value};
        luaug::core::expand(mesh.bounds, mesh.vertices[index].position);
    }

    const u32 perSubmesh = submeshCount == 0 ? 0 : indexCount / submeshCount;
    for (u32 slot = 0; slot < submeshCount; ++slot) {
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

// ---------------------------------------------------------------------------
// Runtime LOD selection (roadmap M7: "basic LOD switching").
//
// Tested against the FUNCTION rather than through a frame, because what is worth
// pinning is the arithmetic: a screen-space error is four multiplications and a
// divide, and every one of them is a place to get a unit wrong. A capture golden
// would prove the wiring and say nothing about whether the number is right.

namespace {

// A resolved mesh with `count` levels whose errors double each step. Errors in
// the mesh's own units, which is what `asset::MeshLod::error` stores.
struct LodFixture
{
    std::vector<MeshLodRange> ranges;
    std::vector<MeshSection> sections;
    MeshCache::Resolved resolved;

    explicit LodFixture(std::initializer_list<f32> errors)
    {
        for (const f32 error : errors) {
            ranges.push_back(MeshLodRange{
                .firstSection = static_cast<u32>(sections.size()),
                .sectionCount = 1,
                .error = error,
            });
            sections.push_back(MeshSection{.firstIndex = 0, .indexCount = 3, .material = 0, .bounds = {}});
        }
        resolved.lods = ranges;
        resolved.sections = sections;
    }
};

// A camera-relative transform: uniform scale, translated `distance` down -Z.
[[nodiscard]] luaug::core::Mat4 instanceAt(f32 distance, f32 scale = 1.0f)
{
    luaug::core::Mat4 m;
    m.m[0][0] = scale;
    m.m[1][1] = scale;
    m.m[2][2] = scale;
    m.m[2][3] = -distance;
    return m;
}

// 720 pixels tall at a 60-degree vertical field of view: 0.5 * 720 / tan(30deg).
constexpr f32 PixelsPerUnit = 623.5f;

} // namespace

TEST_CASE("a mesh with one level always draws it")
{
    const LodFixture fixture{0.0f};
    CHECK(selectMeshLod(fixture.resolved, instanceAt(1.0f), PixelsPerUnit) == 0);
    CHECK(selectMeshLod(fixture.resolved, instanceAt(10000.0f), PixelsPerUnit) == 0);
}

TEST_CASE("distance is what moves the level, and it moves it monotonically")
{
    // THE DIFFERENTIAL. A selector that always answered zero would pass every
    // other case in this file; what says it is doing anything is that the same
    // mesh at two distances answers differently, and never goes backwards as it
    // recedes.
    const LodFixture fixture{0.0f, 0.01f, 0.04f, 0.16f};

    const u32 near = selectMeshLod(fixture.resolved, instanceAt(1.0f), PixelsPerUnit);
    const u32 far = selectMeshLod(fixture.resolved, instanceAt(5000.0f), PixelsPerUnit);
    CHECK(near == 0);
    CHECK(far == 3);
    CHECK(near < far);

    u32 previous = 0;
    for (f32 distance = 1.0f; distance < 5000.0f; distance *= 1.5f) {
        const u32 level = selectMeshLod(fixture.resolved, instanceAt(distance), PixelsPerUnit);
        CHECK(level >= previous);
        previous = level;
    }
}

TEST_CASE("the level is the coarsest that stays inside the pixel budget")
{
    // Level 1's error is 0.01 units. At `PixelsPerUnit` it subtends one pixel at
    // 6.235 m, so just inside that distance level 0 is required and just outside
    // level 1 is allowed. The arithmetic is the assertion.
    const LodFixture fixture{0.0f, 0.01f};
    CHECK(selectMeshLod(fixture.resolved, instanceAt(6.0f), PixelsPerUnit) == 0);
    CHECK(selectMeshLod(fixture.resolved, instanceAt(6.5f), PixelsPerUnit) == 1);
}

TEST_CASE("scale counts, because an error is a length")
{
    // The same mesh at the same distance, ten times larger, is ten times more
    // wrong on screen. A selector that ignored scale would draw a scaled-up
    // boulder with a boulder's LOD.
    const LodFixture fixture{0.0f, 0.01f};
    CHECK(selectMeshLod(fixture.resolved, instanceAt(20.0f, 1.0f), PixelsPerUnit) == 1);
    CHECK(selectMeshLod(fixture.resolved, instanceAt(20.0f, 10.0f), PixelsPerUnit) == 0);
}

TEST_CASE("a tighter pixel budget picks a finer level")
{
    // The threshold is a parameter so this is assertable at all, and the
    // relationship is the point: asking for less error can never yield a coarser
    // level.
    const LodFixture fixture{0.0f, 0.01f, 0.04f};
    const u32 loose = selectMeshLod(fixture.resolved, instanceAt(50.0f), PixelsPerUnit, 4.0f);
    const u32 tight = selectMeshLod(fixture.resolved, instanceAt(50.0f), PixelsPerUnit, 0.25f);
    CHECK(tight <= loose);
    CHECK(tight < loose);
}

TEST_CASE("a degenerate camera or a zero distance draws the best level")
{
    // Both are real: a headless run before the first frame has no projection,
    // and an instance at the camera's exact position divides by zero.
    const LodFixture fixture{0.0f, 0.01f, 0.04f};
    CHECK(selectMeshLod(fixture.resolved, instanceAt(1000.0f), 0.0f) == 0);
    CHECK(selectMeshLod(fixture.resolved, instanceAt(0.0f), PixelsPerUnit) == 0);
    CHECK(selectMeshLod(fixture.resolved, instanceAt(1000.0f), PixelsPerUnit, 0.0f) == 0);
}

TEST_CASE("a mesh uploaded with a LOD chain draws fewer indices as it recedes")
{
    // The plumbing, end to end minus the GPU: the loader flattens a chain into
    // ONE index buffer with one section list and a range per level, and this
    // asserts that the range a distance selects really does name fewer indices.
    //
    // Worth its own case because every part of it can be right on its own and
    // still be wrong together: a correct selector indexing into level zero's
    // sections would pass every test above this one and draw full detail
    // forever.
    DeviceFixture fixture;
    MeshCache cache;
    REQUIRE_FALSE(cache.create(*fixture.device).has_value());

    // Two levels flattened the way `mesh_loader` flattens them: level 1's
    // submesh starts where level 0's indices end.
    Mesh mesh;
    mesh.vertices.resize(6);
    mesh.indices = {0, 1, 2, 0, 2, 3, 0, 1, 2};
    mesh.submeshes.push_back(Submesh{.firstIndex = 0, .indexCount = 6, .material = 0, .bounds = {}});
    mesh.submeshes.push_back(Submesh{.firstIndex = 6, .indexCount = 3, .material = 0, .bounds = {}});

    const MeshLodRange ranges[] = {
        {.firstSection = 0, .sectionCount = 1, .error = 0.0f},
        {.firstSection = 1, .sectionCount = 1, .error = 0.05f},
    };

    auto* cmd = fixture.device->beginFrame();
    REQUIRE(cmd != nullptr);
    const MeshHandle handle = cache.create(*fixture.device, *cmd, mesh, MeshUsage::Static, nullptr, ranges);
    REQUIRE(handle.valid());

    const MeshCache::Resolved* resolved = cache.resolve(handle);
    REQUIRE(resolved != nullptr);
    REQUIRE(resolved->lods.size() == 2);

    const auto indicesAt = [&](f32 distance) {
        const u32 level = selectMeshLod(*resolved, instanceAt(distance), PixelsPerUnit);
        const MeshLodRange& range = resolved->lods[level];
        return resolved->sections[range.firstSection].indexCount;
    };

    // Near enough that a 0.05-unit error is more than a pixel; far enough that
    // it is not.
    CHECK(indicesAt(1.0f) == 6);
    CHECK(indicesAt(4000.0f) == 3);

    cache.destroy(*fixture.device);
}
