// Terrain's level of detail and residency (F1, the streaming pass).
//
// **The level is a function of distance and nothing else**, which is what lets
// it be asserted here with no device: which stride a tile is meshed at, where
// the cascade doubles, and where a tile stops being meshed at all.
#include "luaug/asset/terrain.h"
#include "luaug/asset/terrain_mesher.h"
#include "luaug/render/terrain_loader.h"

#include <doctest/doctest.h>

using namespace luaug;
using namespace luaug::render;

TEST_CASE("with no viewer every tile is meshed at full detail")
{
    // A headless run with no camera, and the first frame of every run, have no
    // focus yet. Coarsening ground nobody is looking at from nowhere would be a
    // guess; full detail is the answer that is never wrong, only slower.
    TerrainLoader loader;
    CHECK(loader.strideFor(0.0) == 1);
    CHECK(loader.strideFor(10000.0) == 1);
}

TEST_CASE("the stride doubles with each doubling of distance")
{
    TerrainLoader loader;
    loader.setDistances(TerrainLoader::Distances{48.0, 768.0});
    loader.setFocus(core::DVec3{0.0, 0.0, 0.0});

    CHECK(loader.strideFor(0.0) == 1);
    CHECK(loader.strideFor(47.0) == 1);
    CHECK(loader.strideFor(49.0) == 2);
    CHECK(loader.strideFor(95.0) == 2);
    CHECK(loader.strideFor(97.0) == 4);
    CHECK(loader.strideFor(193.0) == 8);
    // Eight is the coarsest: a 32-column tile at stride 8 is four cells a side,
    // and coarser than that the skirt is taller than the tile is wide.
    CHECK(loader.strideFor(700.0) == 8);
}

TEST_CASE("beyond the view distance a tile is not meshed at all")
{
    // **This is what streaming means for the renderer.** An unbounded field
    // must not cost an unbounded amount of video memory, so past the view
    // distance a tile's GPU mesh is released rather than kept at the coarsest
    // level for ever.
    TerrainLoader loader;
    loader.setDistances(TerrainLoader::Distances{48.0, 768.0});
    loader.setFocus(core::DVec3{0.0, 0.0, 0.0});
    CHECK(loader.strideFor(768.0) == 8);
    CHECK(loader.strideFor(769.0) == 0);
}

TEST_CASE("a coarse tile carries skirts and the collider does not")
{
    // The crack between two neighbouring tiles meshed at different strides is
    // filled from below by a strip of wall hung from every side edge. The
    // collider must never see it: a wall of collision under every tile edge
    // would be something a character could stand on inside a cave.
    asset::TerrainField field(asset::FieldSettings{.voxelSize = 0.5f, .minHeight = -32.0f, .maxHeight = 32.0f});
    asset::fillFlat(field, core::DVec3{0.0, 0.0, 0.0}, 64.0f, 0.0f, 1);

    asset::MeshRegion region;
    region.minX = 0;
    region.minY = -8;
    region.minZ = 0;
    region.cellsX = asset::TileEdge / 4;
    region.cellsY = 4;
    region.cellsZ = asset::TileEdge / 4;
    region.stride = 4;

    const asset::TerrainMesh bare = asset::meshField(field, region);
    region.skirt = 4.0f;
    const asset::TerrainMesh skirted = asset::meshField(field, region);

    REQUIRE_FALSE(bare.mesh.indices.empty());
    CHECK(skirted.mesh.indices.size() > bare.mesh.indices.size());
    CHECK(skirted.mesh.bounds.min.y < bare.mesh.bounds.min.y);
    // The collider is identical with or without.
    CHECK(skirted.colliderIndices == bare.colliderIndices);
    CHECK(skirted.colliderPoints.size() == bare.colliderPoints.size());
}

TEST_CASE("a coarse tile has a sixteenth of the triangles or fewer")
{
    // What the level of detail buys, stated as a ratio rather than a timing.
    asset::TerrainField field(asset::FieldSettings{.voxelSize = 0.5f, .minHeight = -32.0f, .maxHeight = 32.0f});
    asset::fillFlat(field, core::DVec3{0.0, 0.0, 0.0}, 64.0f, 0.0f, 1);
    asset::fillBall(field, core::DVec3{8.0, 0.0, 8.0}, 5.0, 1);

    const auto meshAt = [&field](core::u32 stride) {
        asset::MeshRegion region;
        region.minX = 0;
        region.minY = -8;
        region.minZ = 0;
        region.cellsX = asset::TileEdge / stride;
        region.cellsY = 24 / stride;
        region.cellsZ = asset::TileEdge / stride;
        region.stride = stride;
        return asset::meshField(field, region).mesh.indices.size() / 3;
    };

    const auto fine = meshAt(1);
    const auto coarse = meshAt(4);
    REQUIRE(fine > 0);
    CHECK(coarse * 16 <= fine + fine / 4);
}
