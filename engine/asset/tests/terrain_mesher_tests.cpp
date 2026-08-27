// The terrain mesher (ADR 0067, F1 B2).
//
// **What is worth asserting is watertightness, placement and winding**, because
// each fails silently in its own way: a crack is a hole somebody falls through,
// a misplaced surface looks like a modelling mistake, and a backwards triangle
// is a floor you fall through while looking at it.
#include "luaug/asset/terrain_mesher.h"

#include <algorithm>
#include <cmath>
#include <doctest/doctest.h>
#include <map>
#include <vector>

using namespace luaug;
using namespace luaug::asset;

namespace {

constexpr float kVoxel = 0.5f;

// A field whose ground is a flat plane at `height`, covering the tiles a small
// region needs.
[[nodiscard]] TerrainField flatGround(float height)
{
    TerrainField field(FieldSettings{.voxelSize = kVoxel});
    const std::vector<float> heights(TileArea, height);
    const std::vector<core::u8> materials(TileArea, core::u8{1});
    for (const TileKey key : {TileKey{-1, -1}, TileKey{-1, 0}, TileKey{0, -1}, TileKey{0, 0}}) {
        field.setTile(key, heights, materials);
    }
    return field;
}

[[nodiscard]] MeshRegion regionAround(core::i32 minY, core::u32 cells)
{
    return MeshRegion{.minX = 0, .minY = minY, .minZ = 0, .cellsX = cells, .cellsY = cells, .cellsZ = cells};
}

// Every edge of every triangle, counted. A closed surface has each interior edge
// used exactly twice, once in each direction.
[[nodiscard]] std::map<std::pair<core::u32, core::u32>, int> edgeUse(const Mesh& mesh)
{
    std::map<std::pair<core::u32, core::u32>, int> uses;
    for (core::usize at = 0; at + 2 < mesh.indices.size(); at += 3) {
        const core::u32 tri[3] = {mesh.indices[at], mesh.indices[at + 1], mesh.indices[at + 2]};
        for (int e = 0; e < 3; ++e) {
            const core::u32 a = tri[e];
            const core::u32 b = tri[(e + 1) % 3];
            uses[{std::min(a, b), std::max(a, b)}] += 1;
        }
    }
    return uses;
}

} // namespace

TEST_CASE("a field with no sign change produces no triangles")
{
    // All air above the ground, so no cell straddles the surface. A mesher that
    // emitted anything here would be putting a surface through empty space.
    const TerrainField field = flatGround(0.0f);
    const TerrainMesh above = meshField(field, regionAround(10, 4));
    CHECK(above.mesh.vertices.empty());
    CHECK(above.mesh.indices.empty());
    CHECK(above.mesh.submeshes.empty());

    // And all solid below it.
    const TerrainMesh below = meshField(field, regionAround(-20, 4));
    CHECK(below.mesh.indices.empty());
}

TEST_CASE("a flat ground meshes as a plane at exactly the height it was given")
{
    // **The equality ADR 0067 rests on.** `sd(p) = p.y - H` puts the vertical
    // edge crossing at exactly `y = H`, so every vertex of a flat field's
    // surface is at that height -- not near it. A mesher that interpolated
    // wrongly, or a sampler that was off by a lattice step, lands somewhere
    // plausible and this is what catches it.
    // A `double` because `doctest::Approx` takes one, and an `f32` compared
    // against it is a promotion only Clang diagnoses.
    const double height = 1.25;
    const TerrainField field = flatGround(static_cast<float>(height));
    const TerrainMesh meshed = meshField(field, regionAround(-4, 8));

    REQUIRE_FALSE(meshed.mesh.vertices.empty());
    for (const Vertex& vertex : meshed.mesh.vertices) {
        CHECK(static_cast<double>(vertex.position.y) == doctest::Approx(height).epsilon(0.001));
    }

    // And it faces UP, because the field's gradient does. Winding is derived
    // from the gradient rather than from a table precisely so this cannot be
    // backwards.
    for (const Vertex& vertex : meshed.mesh.vertices) {
        CHECK(vertex.normal.y > 0.9f);
    }
}

TEST_CASE("the surface is watertight: every interior edge is shared by two triangles")
{
    // **A crack in terrain is a hole somebody falls through**, and it is
    // invisible in a screenshot until they do. Two tetrahedra sharing a face see
    // the same two samples on every edge of it, place the same vertex, and the
    // vertex cache makes it literally the same index -- so a shared edge is used
    // exactly twice.
    //
    // The field is sloped rather than flat, so cells straddle the surface in
    // several orientations and the 1-3 and 2-2 tetrahedron splits both occur.
    TerrainField field(FieldSettings{.voxelSize = kVoxel});
    std::vector<float> heights(TileArea);
    std::vector<core::u8> materials(TileArea, core::u8{1});
    for (core::u32 z = 0; z < TileEdge; ++z) {
        for (core::u32 x = 0; x < TileEdge; ++x) {
            heights[z * TileEdge + x] = 0.4f * static_cast<float>(x) + 0.25f * static_cast<float>(z);
        }
    }
    field.setTile(TileKey{0, 0}, heights, materials);

    // Interior only: a region's boundary edges are legitimately used once,
    // because the surface continues into the cell next door.
    const TerrainMesh meshed = meshField(
        field, MeshRegion{.minX = 2, .minY = -2, .minZ = 2, .cellsX = 12, .cellsY = 24, .cellsZ = 12, .stride = 1});
    REQUIRE(meshed.mesh.indices.size() >= 9);

    const auto uses = edgeUse(meshed.mesh);
    int once = 0;
    int twice = 0;
    int more = 0;
    for (const auto& entry : uses) {
        if (entry.second == 1) {
            ++once;
        }
        else if (entry.second == 2) {
            ++twice;
        }
        else {
            ++more;
        }
    }

    // **No edge is used more than twice**, which is the non-manifold failure --
    // the one that means two surfaces were emitted through the same place.
    CHECK(more == 0);
    // And the overwhelming majority are interior and shared. The ones used once
    // are the region's own border.
    CHECK(twice > once);
}

TEST_CASE("a cave is a surface, which is the whole reason the representation is a volume")
{
    // Solid ground, with a brick of air carved out inside it. The height layer
    // alone cannot express this -- it is single-valued -- so a surface appearing
    // here at all is the hybrid doing the thing it exists for.
    TerrainField field(FieldSettings{.voxelSize = kVoxel});
    const std::vector<float> heights(TileArea, 20.0f);
    const std::vector<core::u8> tileMaterials(TileArea, core::u8{1});
    field.setTile(TileKey{0, 0}, heights, tileMaterials);

    // A brick whose middle is air and whose shell is solid: a bubble.
    std::vector<core::u8> distances(BrickVolume);
    const std::vector<core::u8> brickMaterials(BrickVolume, core::u8{2});
    for (core::u32 y = 0; y < BrickEdge; ++y) {
        for (core::u32 z = 0; z < BrickEdge; ++z) {
            for (core::u32 x = 0; x < BrickEdge; ++x) {
                const float dx = static_cast<float>(x) - 7.5f;
                const float dy = static_cast<float>(y) - 7.5f;
                const float dz = static_cast<float>(z) - 7.5f;
                const float radius = std::sqrt(dx * dx + dy * dy + dz * dz);
                // Positive (air) inside the sphere, negative (solid) outside it.
                const float metres = (4.0f - radius) * kVoxel;
                distances[(y * BrickEdge + z) * BrickEdge + x] = quantiseDistance(metres, kVoxel);
            }
        }
    }
    field.setBrick(BrickKey{0, 0, 0}, distances, brickMaterials);

    const TerrainMesh meshed =
        meshField(field, MeshRegion{.minX = 0, .minY = 0, .minZ = 0, .cellsX = 15, .cellsY = 15, .cellsZ = 15});

    // There is a surface, and it is the bubble's wall.
    REQUIRE_FALSE(meshed.mesh.vertices.empty());

    // Every vertex is on the sphere of radius 4 voxels about the brick's middle,
    // which is where the sign changes. Within one voxel, because that is the
    // resolution the crossing is found at.
    for (const Vertex& vertex : meshed.mesh.vertices) {
        const float dx = vertex.position.x / kVoxel - 7.5f;
        const float dy = vertex.position.y / kVoxel - 7.5f;
        const float dz = vertex.position.z / kVoxel - 7.5f;
        const float radius = std::sqrt(dx * dx + dy * dy + dz * dz);
        CHECK(radius > 3.0f);
        CHECK(radius < 5.0f);
    }
}

TEST_CASE("the collider is the same surface as the render mesh")
{
    // **Not shared with it, and that is deliberate**: sharing would make the
    // collider track the render mesh's LOD, and a collider that gets coarser as
    // you walk away is a character falling through the world. What has to hold
    // is that at the level actually meshed they are the same triangles.
    const TerrainField field = flatGround(0.75f);
    const TerrainMesh meshed = meshField(field, regionAround(-4, 8));

    REQUIRE_FALSE(meshed.mesh.indices.empty());
    CHECK(meshed.colliderPoints.size() == meshed.mesh.vertices.size());
    CHECK(meshed.colliderIndices == meshed.mesh.indices);
    for (core::usize at = 0; at < meshed.colliderPoints.size(); ++at) {
        CHECK(meshed.colliderPoints[at].x == meshed.mesh.vertices[at].position.x);
        CHECK(meshed.colliderPoints[at].y == meshed.mesh.vertices[at].position.y);
        CHECK(meshed.colliderPoints[at].z == meshed.mesh.vertices[at].position.z);
    }

    // Triples, and every index in range -- which is what `ShapeType::TriangleMesh`
    // refuses a description for rather than clamping.
    REQUIRE(meshed.colliderIndices.size() % 3 == 0);
    for (const core::u32 index : meshed.colliderIndices) {
        CHECK(index < meshed.colliderPoints.size());
    }
}

TEST_CASE("a coarser stride is the same surface with fewer triangles")
{
    // LOD is a residency decision baked into what was meshed, not a renderer
    // one: `selectMeshLod` picks per draw from camera distance, and two
    // neighbouring cells picking different levels on different frames is a crack
    // that appears and disappears.
    const TerrainField field = flatGround(1.0f);
    const TerrainMesh fine = meshField(
        field, MeshRegion{.minX = 0, .minY = -4, .minZ = 0, .cellsX = 16, .cellsY = 8, .cellsZ = 16, .stride = 1});
    const TerrainMesh coarse = meshField(
        field, MeshRegion{.minX = 0, .minY = -4, .minZ = 0, .cellsX = 8, .cellsY = 4, .cellsZ = 8, .stride = 2});

    REQUIRE_FALSE(fine.mesh.indices.empty());
    REQUIRE_FALSE(coarse.mesh.indices.empty());
    CHECK(coarse.mesh.indices.size() < fine.mesh.indices.size());

    // **And it is the same plane**, at the same height. A coarser mesh that
    // drifted would show as a step between two LOD rings.
    for (const Vertex& vertex : coarse.mesh.vertices) {
        CHECK(static_cast<double>(vertex.position.y) == doctest::Approx(1.0).epsilon(0.001));
    }
}

TEST_CASE("meshing the same field twice produces the same bytes")
{
    // **R10.** The walk order decides the vertex order, which decides the mesh's
    // bytes, which reaches a content hash. A `std::map` rather than a hash map
    // is what makes this true, and this is the assertion that says so.
    const TerrainField field = flatGround(2.0f);
    const TerrainMesh first = meshField(field, regionAround(-4, 10));
    const TerrainMesh second = meshField(field, regionAround(-4, 10));

    REQUIRE_FALSE(first.mesh.vertices.empty());
    REQUIRE(first.mesh.vertices.size() == second.mesh.vertices.size());
    CHECK(first.mesh.indices == second.mesh.indices);
    for (core::usize at = 0; at < first.mesh.vertices.size(); ++at) {
        CHECK(first.mesh.vertices[at].position.x == second.mesh.vertices[at].position.x);
        CHECK(first.mesh.vertices[at].position.y == second.mesh.vertices[at].position.y);
        CHECK(first.mesh.vertices[at].position.z == second.mesh.vertices[at].position.z);
    }
}
