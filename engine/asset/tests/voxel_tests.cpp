// The block world's storage and its mesher (V1).
#include "luaug/asset/voxel.h"
#include "luaug/asset/voxel_mesher.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <doctest/doctest.h>
#include <vector>

using namespace luaug;
using namespace luaug::asset;

namespace {

constexpr BlockId Stone = 1;
constexpr BlockId Dirt = 2;

// Every triangle's winding agrees with its vertices' normal: the face points out
// of the block it belongs to, which is what back-face culling needs.
[[nodiscard]] bool windingAgrees(const VoxelMesh& mesh)
{
    for (core::usize at = 0; at + 2 < mesh.mesh.indices.size(); at += 3) {
        const Vertex& a = mesh.mesh.vertices[mesh.mesh.indices[at]];
        const Vertex& b = mesh.mesh.vertices[mesh.mesh.indices[at + 1]];
        const Vertex& c = mesh.mesh.vertices[mesh.mesh.indices[at + 2]];
        const core::Vec3 ab = b.position - a.position;
        const core::Vec3 ac = c.position - a.position;
        const core::Vec3 face = core::cross(ab, ac);
        if (core::dot(face, a.normal) <= 0.0f)
            return false;
    }
    return true;
}

} // namespace

TEST_CASE("a block is read back where it was written, across chunk boundaries")
{
    VoxelGrid grid;
    CHECK(grid.set(0, 0, 0, Stone));
    CHECK(grid.set(-1, -1, -1, Dirt));
    CHECK(grid.set(15, 16, 31, Stone));
    CHECK(grid.get(0, 0, 0) == Stone);
    CHECK(grid.get(-1, -1, -1) == Dirt);
    CHECK(grid.get(15, 16, 31) == Stone);
    CHECK(grid.get(1, 0, 0) == AirBlock);
    // Three blocks in three different chunks.
    CHECK(grid.chunkCount() == 3);
    // Writing what is already there changes nothing.
    CHECK_FALSE(grid.set(0, 0, 0, Stone));
}

TEST_CASE("a chunk emptied of blocks is dropped, and air never creates one")
{
    VoxelGrid grid;
    CHECK_FALSE(grid.set(4, 4, 4, AirBlock));
    CHECK(grid.chunkCount() == 0);
    (void)grid.set(4, 4, 4, Stone);
    CHECK(grid.chunkCount() == 1);
    (void)grid.set(4, 4, 4, AirBlock);
    CHECK(grid.chunkCount() == 0);
}

TEST_CASE("a fill touches each chunk once and counts what it changed")
{
    VoxelGrid grid;
    CHECK(grid.fill(0, 0, 0, 31, 0, 31, Stone) == 32u * 32u);
    CHECK(grid.chunkCount() == 4);
    CHECK(grid.fill(0, 0, 0, 31, 0, 31, Stone) == 0u);
    CHECK(grid.fill(0, 0, 0, 31, 0, 31, AirBlock) == 32u * 32u);
    CHECK(grid.chunkCount() == 0);
}

TEST_CASE("a snapshot is untouched by an edit made after it")
{
    // Undo keeps whole worlds by value; this is what makes that cheap.
    VoxelGrid grid;
    (void)grid.fill(0, 0, 0, 7, 7, 7, Stone);
    const VoxelGrid snapshot = grid;
    const core::u64 before = snapshot.digest();
    (void)grid.set(3, 3, 3, Dirt);
    CHECK(snapshot.get(3, 3, 3) == Stone);
    CHECK(snapshot.digest() == before);
    CHECK(grid.digest() != before);
}

TEST_CASE("one block is six faces, wound outwards")
{
    VoxelGrid grid;
    (void)grid.set(2, 3, 4, Stone);
    const VoxelMesh mesh = meshVoxelChunk(grid, VoxelChunkKey{0, 0, 0}, {}, 1.0f);
    CHECK(mesh.mesh.vertices.size() == 24);
    CHECK(mesh.mesh.indices.size() == 36);
    CHECK(windingAgrees(mesh));
    CHECK(mesh.mesh.bounds.min.x == doctest::Approx(2.0));
    CHECK(mesh.mesh.bounds.max.y == doctest::Approx(4.0));
    CHECK(mesh.colliderIndices.size() == mesh.mesh.indices.size());
}

TEST_CASE("a flat floor of one block type is one quad on top")
{
    // **The whole point of greedy meshing**: two hundred and fifty-six top faces
    // with identical blocks and identical (unoccluded) corners merge into one.
    VoxelGrid grid;
    (void)grid.fill(0, 0, 0, 15, 0, 15, Stone);
    const VoxelMesh mesh = meshVoxelChunk(grid, VoxelChunkKey{0, 0, 0}, {}, 1.0f);
    // Top, bottom and four sides: six quads for a whole slab.
    CHECK(mesh.mesh.indices.size() == 6u * 6u);
    CHECK(windingAgrees(mesh));
}

TEST_CASE("faces between two blocks are not drawn, even across a chunk boundary")
{
    VoxelGrid grid;
    (void)grid.set(15, 0, 0, Stone);
    (void)grid.set(16, 0, 0, Stone);
    const VoxelMesh left = meshVoxelChunk(grid, VoxelChunkKey{0, 0, 0}, {}, 1.0f);
    // Five faces: the one touching its neighbour in the next chunk is hidden.
    CHECK(left.mesh.indices.size() == 5u * 6u);
}

TEST_CASE("different block types do not merge, and occluded corners are darker")
{
    VoxelGrid grid;
    (void)grid.fill(0, 0, 0, 3, 0, 0, Stone);
    (void)grid.set(4, 0, 0, Dirt);
    const VoxelMesh row = meshVoxelChunk(grid, VoxelChunkKey{0, 0, 0}, {}, 1.0f);
    // The top: stone merged into one quad, dirt its own.
    std::vector<float> topIds;
    for (core::usize at = 0; at < row.mesh.vertices.size(); at += 4) {
        if (row.mesh.vertices[at].normal.y > 0.5f)
            topIds.push_back(row.mesh.vertices[at].tangent[0]);
    }
    CHECK(topIds.size() == 2);

    // A block beside a wall: its top face's corners against the wall are
    // occluded, the ones away from it are not.
    VoxelGrid corner;
    (void)corner.set(0, 0, 0, Stone);
    (void)corner.set(1, 1, 0, Stone);
    const VoxelMesh lit = meshVoxelChunk(corner, VoxelChunkKey{0, 0, 0}, {}, 1.0f);
    float darkest = 1.0f;
    float brightest = 0.0f;
    for (const Vertex& vertex : lit.mesh.vertices) {
        if (vertex.normal.y > 0.5f && vertex.position.y < 1.5f) {
            darkest = std::min(darkest, vertex.tangent[1]);
            brightest = std::max(brightest, vertex.tangent[1]);
        }
    }
    CHECK(darkest < brightest);
    CHECK(brightest == doctest::Approx(1.0));
}

TEST_CASE("the block size scales the mesh and nothing else")
{
    VoxelGrid grid;
    (void)grid.set(1, 1, 1, Stone);
    const VoxelMesh mesh = meshVoxelChunk(grid, VoxelChunkKey{0, 0, 0}, {}, 0.5f);
    CHECK(mesh.mesh.bounds.min.x == doctest::Approx(0.5));
    CHECK(mesh.mesh.bounds.max.x == doctest::Approx(1.0));
}

TEST_CASE("a ray meets the first block it crosses, and names the face it entered")
{
    VoxelGrid grid;
    (void)grid.set(5, 0, 0, Stone);
    (void)grid.set(9, 0, 0, Dirt);

    const std::optional<VoxelHit> hit =
        raycastVoxels(grid, 1.0f, core::DVec3{0.5, 0.5, 0.5}, core::Vec3{1.0f, 0.0f, 0.0f}, 100.0);
    REQUIRE(hit.has_value());
    CHECK(hit->block == std::array<core::i32, 3>{5, 0, 0});
    CHECK(hit->face == std::array<core::i32, 3>{-1, 0, 0});
    CHECK(hit->distance == doctest::Approx(4.5));

    // Negative coordinates floor rather than truncate: the block at -1 is the
    // one whose cube spans -1 to 0.
    (void)grid.set(-1, -3, 0, Stone);
    const std::optional<VoxelHit> down =
        raycastVoxels(grid, 1.0f, core::DVec3{-0.5, 2.0, 0.5}, core::Vec3{0.0f, -1.0f, 0.0f}, 100.0);
    REQUIRE(down.has_value());
    CHECK(down->block == std::array<core::i32, 3>{-1, -3, 0});
    CHECK(down->face == std::array<core::i32, 3>{0, 1, 0});
}

TEST_CASE("a ray stops at its reach, and one that starts inside a block hits it through no face")
{
    VoxelGrid grid;
    (void)grid.set(5, 0, 0, Stone);
    CHECK_FALSE(raycastVoxels(grid, 1.0f, core::DVec3{0.5, 0.5, 0.5}, core::Vec3{1.0f, 0.0f, 0.0f}, 4.0).has_value());

    const std::optional<VoxelHit> inside =
        raycastVoxels(grid, 1.0f, core::DVec3{5.5, 0.5, 0.5}, core::Vec3{0.0f, 1.0f, 0.0f}, 10.0);
    REQUIRE(inside.has_value());
    CHECK(inside->face == std::array<core::i32, 3>{0, 0, 0});

    // The block size scales where a block is: half-metre blocks put block 5 at
    // x = 2.5 m.
    const std::optional<VoxelHit> half =
        raycastVoxels(grid, 0.5f, core::DVec3{0.25, 0.25, 0.25}, core::Vec3{1.0f, 0.0f, 0.0f}, 100.0);
    REQUIRE(half.has_value());
    CHECK(half->distance == doctest::Approx(2.25));
}

TEST_CASE("a fluid is drawn as deep as it is full, see-through, and never collides")
{
    constexpr BlockId Water = 2;
    // Stone, then water: water is the fluid, reaching three blocks.
    std::vector<BlockLook> looks(3);
    looks[Water].fluid = true;
    looks[Water].reach = 3;

    VoxelGrid grid;
    (void)grid.fill(0, 0, 0, 3, 0, 3, Stone);
    (void)grid.set(1, 1, 1, Water);                    // a source on the floor
    (void)grid.set(2, 1, 1, blockWithState(Water, 2)); // two blocks out
    const VoxelMesh mesh = meshVoxelChunk(grid, VoxelChunkKey{0, 0, 0}, looks, 1.0f);

    // **Nothing of the water is solid**: its faces are all in the translucent
    // mesh, and every collider point is on the stone -- none stands above the
    // floor's top at y = 1.
    CHECK(mesh.cutout.vertices.empty());
    REQUIRE_FALSE(mesh.translucent.vertices.empty());
    for (const core::Vec3& point : mesh.colliderPoints)
        CHECK(static_cast<double>(point.y) <= 1.0 + 1e-6);

    // The source's surface stands a ninth short of its block; the one two
    // blocks out is shallower by two shares of four.
    float sourceTop = 0.0f;
    float spreadTop = 0.0f;
    for (const Vertex& vertex : mesh.translucent.vertices) {
        if (vertex.normal.y < 0.5f)
            continue;
        if (vertex.position.x <= 2.0f)
            sourceTop = std::max(sourceTop, vertex.position.y);
        else
            spreadTop = std::max(spreadTop, vertex.position.y);
    }
    CHECK(static_cast<double>(sourceTop) == doctest::Approx(1.0 + 8.0 / 9.0));
    CHECK(static_cast<double>(spreadTop) == doctest::Approx(1.0 + (8.0 / 9.0) * 2.0 / 4.0));
    CHECK(static_cast<double>(fluidSurface(blockWithState(Water, 1), Water, 3)) == doctest::Approx(1.0));

    // Every face is wound outwards, and the step between the two levels is a
    // face of its own on the deeper block's side.
    for (core::usize at = 0; at + 2 < mesh.translucent.indices.size(); at += 3) {
        const Vertex& a = mesh.translucent.vertices[mesh.translucent.indices[at]];
        const Vertex& b = mesh.translucent.vertices[mesh.translucent.indices[at + 1]];
        const Vertex& c = mesh.translucent.vertices[mesh.translucent.indices[at + 2]];
        CHECK(core::dot(core::cross(b.position - a.position, c.position - a.position), a.normal) > 0.0f);
    }
    // A type's id rides in the tangent, never its state.
    for (const Vertex& vertex : mesh.translucent.vertices)
        CHECK(static_cast<double>(vertex.tangent[0]) == doctest::Approx(static_cast<double>(Water)));
}

TEST_CASE("a ray passes through a fluid to what is under it")
{
    constexpr BlockId Water = 2;
    VoxelGrid grid;
    (void)grid.set(0, 0, 0, Stone);
    (void)grid.set(0, 1, 0, blockWithState(Water, 3));
    const std::array<bool, 3> passable{false, false, true};
    const std::optional<VoxelHit> hit =
        raycastVoxels(grid, 1.0f, core::DVec3{0.5, 5.0, 0.5}, core::Vec3{0.0f, -1.0f, 0.0f}, 10.0, passable);
    REQUIRE(hit.has_value());
    CHECK(hit->block == std::array<core::i32, 3>{0, 0, 0});
    // And without the list, the water is what it meets.
    const std::optional<VoxelHit> blocked =
        raycastVoxels(grid, 1.0f, core::DVec3{0.5, 5.0, 0.5}, core::Vec3{0.0f, -1.0f, 0.0f}, 10.0);
    REQUIRE(blocked.has_value());
    CHECK(blocked->block == std::array<core::i32, 3>{0, 1, 0});
}

TEST_CASE("what meshing a block world costs" * doctest::skip())
{
    // The V1 bench: run by name (`--test-case="what meshing*" --no-skip`).
    // A 64 x 32 x 64 world -- rolling ground three block types deep with a
    // tunnel through it, which is what a block game's surface chunks look like
    // -- meshed chunk by chunk, then one block broken and its chunk re-meshed.
    VoxelGrid grid;
    for (core::i32 z = 0; z < 64; ++z) {
        for (core::i32 x = 0; x < 64; ++x) {
            const auto height = static_cast<core::i32>(12.0 + 4.0 * std::sin(x * 0.2) + 3.0 * std::cos(z * 0.17));
            (void)grid.fill(x, 0, z, x, height - 4, z, Stone);
            (void)grid.fill(x, height - 3, z, x, height, z, Dirt);
        }
    }
    (void)grid.fill(10, 4, 0, 13, 7, 63, AirBlock);

    const auto start = std::chrono::steady_clock::now();
    core::usize triangles = 0;
    const std::vector<VoxelChunkKey> keys = grid.chunkKeys();
    for (const VoxelChunkKey key : keys)
        triangles += meshVoxelChunk(grid, key, {}, 1.0f).mesh.indices.size() / 3;
    const double total = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

    (void)grid.set(20, 10, 20, AirBlock);
    const auto edit = std::chrono::steady_clock::now();
    const VoxelMesh remeshed = meshVoxelChunk(grid, voxelChunkOf(20, 10, 20), {}, 1.0f);
    const double one = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - edit).count();

    MESSAGE("chunks " << keys.size() << ", triangles " << triangles << ", mesh all " << total << " ms ("
                      << total / static_cast<double>(keys.size()) << " ms/chunk), re-mesh after an edit " << one
                      << " ms, " << remeshed.mesh.indices.size() / 3 << " triangles");
    CHECK(triangles > 0);
}
