// The terrain mesher (ADR 0082).
//
// **What is worth asserting is placement, watertightness and winding**, because
// each fails silently in its own way: a surface a voxel off looks like a
// modelling mistake, a crack between two regions is a hole somebody falls
// through, and a backwards triangle is a floor you fall through while looking at
// it. And terraces: a steep heightmap meshed as a staircase is what the first
// voxel grid did before its ramp was widened, so it has a test of its own.
#include "luaug/asset/terrain_mesher.h"

#include <algorithm>
#include <cmath>
#include <doctest/doctest.h>
#include <map>
#include <set>
#include <tuple>
#include <vector>

using namespace luaug;
using namespace luaug::asset;

namespace {

[[nodiscard]] FieldSettings settingsOf(float voxel = 1.0f)
{
    return FieldSettings{.voxelSize = voxel, .minHeight = -64.0f, .maxHeight = 64.0f};
}

[[nodiscard]] TerrainField flatGround(float height, float voxel = 1.0f)
{
    TerrainField field(settingsOf(voxel));
    (void)fillFlat(field, core::DVec3{0.0, 0.0, 0.0}, 128.0f * voxel, height, 1);
    return field;
}

// One chunk's worth of region, owning lattice points from `(x, y, z)`.
[[nodiscard]] MeshRegion regionAt(core::i32 x, core::i32 y, core::i32 z, core::u32 cells = 32, core::u32 level = 0)
{
    return MeshRegion{
        .minX = x, .minY = y, .minZ = z, .cellsX = cells, .cellsY = cells, .cellsZ = cells, .level = level};
}

// Every edge of every triangle, counted by the positions at its ends -- so two
// regions' copies of the same vertex count as one.
using Point = std::tuple<float, float, float>;
[[nodiscard]] Point pointOf(const core::Vec3& p)
{
    const auto snap = [](float v) { return std::round(v * 1024.0f) / 1024.0f; };
    return {snap(p.x), snap(p.y), snap(p.z)};
}

void countEdges(const TerrainMesh& meshed, std::map<std::pair<Point, Point>, int>& uses)
{
    const std::vector<core::Vec3>& points = meshed.colliderPoints;
    const std::vector<core::u32>& indices = meshed.colliderIndices;
    for (core::usize at = 0; at + 2 < indices.size(); at += 3) {
        for (int e = 0; e < 3; ++e) {
            const Point a = pointOf(points[indices[at + static_cast<core::usize>(e)]]);
            const Point b = pointOf(points[indices[at + static_cast<core::usize>((e + 1) % 3)]]);
            uses[{std::min(a, b), std::max(a, b)}] += 1;
        }
    }
}

} // namespace

TEST_CASE("a field with no surface produces no triangles")
{
    const TerrainField empty(settingsOf());
    CHECK(meshField(empty, regionAt(0, 0, 0)).mesh.indices.empty());
    // All ground is no surface either: the rows between the slab's bottom at
    // 0 and its top at 40.
    const TerrainField buried = flatGround(40.0f);
    CHECK(meshField(buried, regionAt(0, 6, 0, 16)).mesh.indices.empty());
}

TEST_CASE("flat ground meshes as a plane at the height it was given, facing up")
{
    const TerrainField field = flatGround(3.3f);
    const TerrainMesh meshed = meshField(field, regionAt(0, 0, 0));
    REQUIRE_FALSE(meshed.mesh.vertices.empty());
    for (const Vertex& vertex : meshed.mesh.vertices) {
        CHECK(static_cast<double>(vertex.position.y) == doctest::Approx(3.3).epsilon(0.002));
        CHECK(static_cast<double>(vertex.normal.y) > 0.999);
    }
    // And the triangles face up too, by their winding.
    const std::vector<Vertex>& v = meshed.mesh.vertices;
    for (core::usize at = 0; at + 2 < meshed.mesh.indices.size(); at += 3) {
        const core::Vec3 a = v[meshed.mesh.indices[at]].position;
        const core::Vec3 b = v[meshed.mesh.indices[at + 1]].position;
        const core::Vec3 c = v[meshed.mesh.indices[at + 2]].position;
        const float ny = (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z);
        CHECK(ny > 0.0f);
    }
}

TEST_CASE("a steep heightmap meshes as a slope, not a staircase")
{
    // Sixty degrees. With a one-voxel ramp this came out as terraces a voxel
    // high: the voxel beside one on the slope was clamped, and the crossing
    // between them landed in the wrong place (`RampVoxels`).
    TerrainField field(settingsOf());
    constexpr core::u32 Columns = 64;
    constexpr float Rise = 1.732f;
    std::vector<float> heights(Columns * Columns);
    for (core::u32 z = 0; z < Columns; ++z) {
        for (core::u32 x = 0; x < Columns; ++x)
            heights[z * Columns + x] = Rise * (static_cast<float>(x) - 32.0f) + 0.5f * Rise;
    }
    (void)writeHeights(field, -32, -32, Columns, heights, 1);
    // A column's height is at its centre, so the plane is y = Rise * x.
    MeshRegion region = regionAt(-8, -32, -8, 16);
    region.cellsY = 64;
    const TerrainMesh meshed = meshField(field, region);
    REQUIRE(meshed.mesh.vertices.size() > 100);
    double worst = 0.0;
    for (const Vertex& vertex : meshed.mesh.vertices) {
        // Distance from the plane, measured along its normal.
        const double off = (static_cast<double>(vertex.position.y) - static_cast<double>(Rise * vertex.position.x)) /
                           std::sqrt(1.0 + static_cast<double>(Rise * Rise));
        worst = std::max(worst, std::abs(off));
    }
    CHECK(worst < 0.1);
}

TEST_CASE("two regions side by side meet with no crack")
{
    TerrainField field = flatGround(0.0f);
    (void)raiseBall(field, core::DVec3{32.0, 0.0, 16.0}, 12.0, 8.0f);
    (void)fillBall(field, core::DVec3{32.0, 2.0, 16.0}, 5.0, 0);

    std::map<std::pair<Point, Point>, int> uses;
    MeshRegion left = regionAt(0, -8, 0);
    MeshRegion right = regionAt(32, -8, 0);
    const TerrainMesh a = meshField(field, left);
    const TerrainMesh b = meshField(field, right);
    countEdges(a, uses);
    countEdges(b, uses);

    // An edge on the seam at x = 32 used once is a crack. The seam's shared
    // ring of vertices is built by both regions from the same samples, so the
    // union uses every interior edge twice.
    int cracks = 0;
    for (const auto& [edge, count] : uses) {
        const float ax = std::get<0>(edge.first);
        const float bx = std::get<0>(edge.second);
        const bool onSeam = ax > 30.0f && ax < 34.0f && bx > 30.0f && bx < 34.0f;
        const float az = std::get<2>(edge.first);
        const bool inside = az > 2.0f && az < 30.0f;
        if (onSeam && inside && count == 1)
            ++cracks;
    }
    CHECK(cracks == 0);
}

TEST_CASE("a cave is a surface, which is the whole reason terrain is a volume")
{
    TerrainField field = flatGround(10.0f);
    (void)fillBall(field, core::DVec3{16.0, -6.0, 16.0}, 5.0, 0);
    MeshRegion region = regionAt(0, -32, 0);
    region.cellsY = 64;
    const TerrainMesh meshed = meshField(field, region);
    bool ceiling = false;
    for (const Vertex& vertex : meshed.mesh.vertices) {
        // A vertex near the ball's top faces down, into the cave.
        if (vertex.position.y > -2.0f && vertex.position.y < 0.0f && std::abs(vertex.position.x - 16.0f) < 1.0f &&
            std::abs(vertex.position.z - 16.0f) < 1.0f && vertex.normal.y < -0.8f)
            ceiling = true;
    }
    CHECK(ceiling);
    // And a vertex deep in a cave sees less sky than one in the open.
    float cave = 1.0f;
    float open = 0.0f;
    for (const Vertex& vertex : meshed.mesh.vertices) {
        if (vertex.position.y < -8.0f)
            cave = std::min(cave, vertex.tangent[1]);
        if (vertex.position.y > 9.0f)
            open = std::max(open, vertex.tangent[1]);
    }
    CHECK(cave < 0.5f);
    CHECK(open > 0.99f);
}

TEST_CASE("the collider is the render surface, without the skirts")
{
    TerrainField field = flatGround(2.0f);
    (void)raiseBall(field, core::DVec3{16.0, 2.0, 16.0}, 8.0, 5.0f);
    MeshRegion region = regionAt(0, -8, 0);
    const TerrainMesh plain = meshField(field, region);
    region.skirt = 2.0f;
    const TerrainMesh skirted = meshField(field, region);
    CHECK(skirted.mesh.indices.size() > plain.mesh.indices.size());
    CHECK(skirted.colliderIndices.size() == plain.colliderIndices.size());
    CHECK(plain.colliderIndices.size() == plain.mesh.indices.size());
}

TEST_CASE("a skirt stays inside the ground behind it, even where the ground is thinner than the skirt is long")
{
    // **The owner's dark lines across a 5 km plain.** At the top level a skirt
    // is two 32 m cells long, and ground laid on an empty world is a slab 32 m
    // deep, so the skirt hung from the slab's bottom -- which hangs UP, along
    // the bottom's negative normal -- stood 32 m out of the plain along every
    // side of every coarse node.
    TerrainField field(FieldSettings{});
    (void)fillFlat(field, core::DVec3{0.0, 0.0, 0.0}, 2048.0f, 0.0f, 1);
    MeshRegion region = regionAt(-32, -8, -32, 32, 5);
    region.cellsY = 16;
    region.skirt = 64.0f;
    const TerrainMesh meshed = meshField(field, region);
    REQUIRE(meshed.mesh.indices.size() > meshed.colliderIndices.size());
    float highest = -1e9f;
    float lowest = 1e9f;
    for (const core::u32 index : meshed.mesh.indices) {
        highest = std::max(highest, meshed.mesh.vertices[index].position.y);
        lowest = std::min(lowest, meshed.mesh.vertices[index].position.y);
    }
    // Nothing above the plain, and nothing under the slab's bottom.
    CHECK(highest <= 0.01f);
    CHECK(lowest >= -32.01f);
}

TEST_CASE("a coarser level is the same surface with fewer triangles")
{
    TerrainField field = flatGround(4.0f);
    (void)raiseBall(field, core::DVec3{32.0, 4.0, 32.0}, 20.0, 10.0f);
    const TerrainMesh fine = meshField(field, regionAt(0, -8, 0, 64));
    const TerrainMesh coarse = meshField(field, regionAt(0, -4, 0, 32, 1));
    REQUIRE_FALSE(coarse.mesh.indices.empty());
    CHECK(coarse.mesh.indices.size() * 3 < fine.mesh.indices.size());
    // Its vertices are on the fine surface to within a coarse cell -- away
    // from the edge of the ground, where the surface turns down its side.
    for (const Vertex& vertex : coarse.mesh.vertices) {
        if (vertex.position.x < 4.0f || vertex.position.x > 60.0f || vertex.position.z < 4.0f ||
            vertex.position.z > 60.0f)
            continue;
        const std::optional<float> height =
            heightAt(field, static_cast<double>(vertex.position.x), static_cast<double>(vertex.position.z));
        REQUIRE(height.has_value());
        CHECK(std::abs(static_cast<double>(vertex.position.y - *height)) < 2.0);
    }
}

TEST_CASE("one section per material, in id order")
{
    TerrainField field = flatGround(0.0f);
    (void)paintBall(field, core::DVec3{8.0, 0.0, 8.0}, 4.0, 3);
    (void)paintBall(field, core::DVec3{24.0, 0.0, 24.0}, 4.0, 2);
    const TerrainMesh meshed = meshField(field, regionAt(0, -8, 0));
    REQUIRE(meshed.sectionMaterials.size() == 3);
    CHECK(std::is_sorted(meshed.sectionMaterials.begin(), meshed.sectionMaterials.end()));
    CHECK(meshed.mesh.submeshes.size() == meshed.sectionMaterials.size());
}

TEST_CASE("meshing the same field twice produces the same bytes")
{
    TerrainField field = flatGround(1.0f);
    (void)fillBall(field, core::DVec3{10.0, 1.0, 10.0}, 6.0, 0);
    const TerrainMesh a = meshField(field, regionAt(0, -8, 0));
    const TerrainMesh b = meshField(field, regionAt(0, -8, 0));
    REQUIRE(a.mesh.vertices.size() == b.mesh.vertices.size());
    CHECK(std::equal(a.mesh.indices.begin(), a.mesh.indices.end(), b.mesh.indices.begin()));
    for (core::usize at = 0; at < a.mesh.vertices.size(); ++at) {
        CHECK(a.mesh.vertices[at].position == b.mesh.vertices[at].position);
        CHECK(a.mesh.vertices[at].tangent[1] == b.mesh.vertices[at].tangent[1]);
    }
}

TEST_CASE("the rows a column of chunks can have a surface in")
{
    const TerrainField empty(settingsOf());
    CHECK_FALSE(activeRows(empty, 0, 0, 1).has_value());

    const TerrainField field = flatGround(40.0f);
    const std::optional<std::pair<core::i32, core::i32>> rows = activeRows(field, 0, 0, 1);
    REQUIRE(rows.has_value());
    // The top is in the chunk from 32 to 63 and the slab's bottom is at 0
    // (`LaidDepth` under 40, to a chunk boundary): both are surfaces.
    CHECK(rows->first <= -1);
    CHECK(rows->first >= -2);
    CHECK(rows->second >= 42);

    // Ground laid from a deep floor is two runs, its top and its bottom, and
    // the solid rock between them is not walked.
    TerrainField deep(FieldSettings{.voxelSize = 1.0f, .minHeight = -256.0f, .maxHeight = 64.0f});
    (void)fillBlock(deep, core::DVec3{16.0, -128.0, 16.0}, core::Vec3{128.0f, 256.0f, 128.0f}, 1);
    const std::vector<std::pair<core::i32, core::i32>> runs = activeRuns(deep, 0, 0, 1);
    REQUIRE(runs.size() == 2);
    CHECK(runs[0].second < runs[1].first);
}

TEST_CASE("ground laid on empty terrain has walls and a bottom all round, and the bottom is not black")
{
    // **The owner's screenshot**: ground generated and then extended sideways
    // stood as two different things -- a slab whose sides stopped short, open
    // underneath, and a wall to the world's floor with a black line along its
    // foot. Laid ground is a slab `LaidDepth` deep, and its edges are meshed
    // the same way whatever the chunks under them happen to be stored as.
    TerrainField field(settingsOf());
    (void)fillFlat(field, core::DVec3{0.0, 0.0, 0.0}, 64.0f, 4.0f, 1);
    const std::vector<std::pair<core::i32, core::i32>> runs = activeRuns(field, 0, 0, 1);
    REQUIRE_FALSE(runs.empty());
    MeshRegion region = regionAt(0, runs.front().first, 0);
    region.cellsY = static_cast<core::u32>(runs.back().second - runs.front().first + 1);
    const TerrainMesh meshed = meshField(field, region);
    bool wall = false;
    bool bottom = false;
    float bottomSky = 0.0f;
    for (const Vertex& vertex : meshed.mesh.vertices) {
        // The +x side of the square is at 32, the edge of this region.
        wall = wall || (vertex.normal.x > 0.9f && vertex.position.y < -8.0f);
        if (vertex.normal.y < -0.9f) {
            bottom = true;
            bottomSky = std::max(bottomSky, vertex.tangent[1]);
        }
    }
    CHECK(wall);
    CHECK(bottom);
    // Shade, not the black of a cave's roof.
    CHECK(bottomSky > 0.25f);
}

TEST_CASE("a ball on the side of the terrain shades the wall near it, not all the way down")
{
    // **The owner's picture**: a ball added to the side of the terrain stood a
    // dark stripe down the whole wall under it, because a point below any of a
    // column's ground counted as under a roof. The sky term marches rays now:
    // the wall just under the ball is in its shade, and the wall twenty metres
    // down sees the sky as the open wall beside it does.
    TerrainField field(settingsOf());
    (void)fillFlat(field, core::DVec3{0.0, 0.0, 0.0}, 64.0f, 0.0f, 1);
    (void)fillBall(field, core::DVec3{32.0, -4.0, 16.0}, 4.0, 1);
    const std::vector<std::pair<core::i32, core::i32>> runs = activeRuns(field, 1, 0, 1);
    REQUIRE_FALSE(runs.empty());
    // The region past the wall, which owns the ball's outer half and the
    // cells either side of the wall.
    MeshRegion region = regionAt(32, runs.front().first, 0);
    region.cellsY = static_cast<core::u32>(runs.back().second - runs.front().first + 1);
    const TerrainMesh meshed = meshField(field, region);
    float deepUnder = 1.0f;
    float deepAside = 1.0f;
    float underBall = 1.0f;
    for (const Vertex& vertex : meshed.mesh.vertices) {
        // Where the ball meets the wall, under it: the crease between them.
        if (vertex.position.x < 34.0f && vertex.position.y < -5.0f && vertex.position.y > -10.0f &&
            std::abs(vertex.position.z - 16.0f) < 3.0f)
            underBall = std::min(underBall, vertex.tangent[1]);
        if (vertex.normal.x < 0.9f || std::abs(vertex.position.x - 32.0f) > 1.0f)
            continue;
        const float sky = vertex.tangent[1];
        if (vertex.position.y < -20.0f && vertex.position.y > -26.0f) {
            if (std::abs(vertex.position.z - 16.0f) < 1.0f)
                deepUnder = std::min(deepUnder, sky);
            if (std::abs(vertex.position.z - 28.0f) < 1.0f)
                deepAside = std::min(deepAside, sky);
        }
    }
    CHECK(deepUnder > 0.9f);
    CHECK(static_cast<double>(deepUnder) == doctest::Approx(static_cast<double>(deepAside)).epsilon(0.05));
    CHECK(underBall < deepUnder);
}
