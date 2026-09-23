// Terrain and block worlds cut into streaming cells (ADR 0075, ADR 0082).
//
// The two properties the streamer rests on are asserted here rather than
// trusted: a field cut into cells and put back is the same field, and an edit
// to a streamed-in cell is visible to the question "may this cell be dropped",
// with no flag anybody could forget to raise.
#include "luaug/asset/field_cells.h"
#include "luaug/asset/terrain.h"
#include "luaug/core/i18n.h"

#include <doctest/doctest.h>
#include <memory>
#include <vector>

using namespace luaug;
using namespace luaug::asset;

namespace {

void seedCatalog()
{
    const auto result = core::engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
}

// Flat ground over a square `metres` wide, and a cave dug under its middle.
[[nodiscard]] TerrainField groundWithCave(double metres)
{
    TerrainField field(FieldSettings{.voxelSize = 1.0f, .minHeight = -32.0f, .maxHeight = 32.0f});
    (void)fillBlock(field, core::DVec3{0.0, -20.0, 0.0},
                    core::Vec3{static_cast<float>(metres), 40.0f, static_cast<float>(metres)}, 1);
    (void)fillBall(field, core::DVec3{0.0, -6.0, 0.0}, 3.0, 0);
    return field;
}

} // namespace

TEST_CASE("a cell is the whole number of chunk columns nearest sixty-four metres")
{
    CHECK(terrainCellChunks(0.5f) == 4);
    CHECK(terrainCellChunks(1.0f) == 2);
    CHECK(terrainCellChunks(2.0f) == 1);
    // A chunk wider than a cell is a cell.
    CHECK(terrainCellChunks(4.0f) == 1);
    CHECK(voxelCellChunks(1.0f) == 4);
    CHECK(voxelCellChunks(4.0f) == 1);
}

TEST_CASE("a field cut into cells and put back together is the same field")
{
    const TerrainField field = groundWithCave(256.0);
    const std::vector<TerrainCell> cells = splitTerrain(field);
    // 256 m of 32 m chunk columns, two to a cell: at least four cells a side,
    // and the ramp a fill writes past its edge can add a row.
    CHECK(cells.size() >= 16);

    TerrainField rebuilt(field.settings());
    for (const TerrainCell& cell : cells) {
        // Through the file, because that is the way a cell actually travels.
        TerrainCell decoded;
        REQUIRE_FALSE(decodeTerrainCell(encodeTerrainCell(cell), decoded).has_value());
        rebuilt.shareFrom(decoded.field);
    }
    CHECK(rebuilt.chunkCount() == field.chunkCount());
    CHECK(rebuilt.digest() == field.digest());
}

TEST_CASE("every chunk is filed in the cell of its column")
{
    const TerrainField field = groundWithCave(256.0);
    const core::u32 across = terrainCellChunks(field.settings().voxelSize);
    for (const TerrainCell& cell : splitTerrain(field)) {
        const ChunkId id{cell.x, cell.z, FieldLayerTerrain};
        for (const ChunkKey key : cell.field.chunkKeys())
            CHECK(terrainCellOf(key, across) == id);
    }
}

TEST_CASE("a cell nobody touched may be dropped, and one somebody dug into may not")
{
    const TerrainField source = groundWithCave(256.0);
    const core::u32 across = terrainCellChunks(source.settings().voxelSize);
    const std::vector<TerrainCell> cells = splitTerrain(source);

    TerrainField world(source.settings());
    for (const TerrainCell& cell : cells)
        world.shareFrom(cell.field);
    for (const TerrainCell& cell : cells)
        CHECK(terrainCellUntouched(world, cell, across));

    // **An edit is visible because the streamer holds a second reference**:
    // the write clones the chunk rather than changing the one the cell shares.
    const TerrainCell& far = cells.back();
    const ChunkKey key = far.field.chunkKeys().front();
    const core::i32 x = key.x * static_cast<core::i32>(ChunkEdge);
    const core::i32 y = key.y * static_cast<core::i32>(ChunkEdge);
    const core::i32 z = key.z * static_cast<core::i32>(ChunkEdge);
    const Voxel was = world.voxel(x, y, z);
    (void)world.setVoxel(x, y, z, Voxel{static_cast<core::u8>(was.occupancy == 0 ? 200 : 0), 2});
    CHECK_FALSE(terrainCellUntouched(world, far, across));
    CHECK(terrainCellUntouched(world, cells.front(), across));

    // And the one that may be dropped goes cleanly.
    removeTerrainCell(world, cells.front());
    for (const ChunkKey gone : cells.front().field.chunkKeys())
        CHECK(world.findChunk(gone) == nullptr);
}

TEST_CASE("a cell that loads after ground was made there keeps the newer ground")
{
    const TerrainField source = groundWithCave(64.0);
    const std::vector<TerrainCell> cells = splitTerrain(source);
    REQUIRE_FALSE(cells.empty());

    TerrainField world(source.settings());
    const ChunkKey key = cells.front().field.chunkKeys().front();
    world.setChunk(key, std::make_shared<TerrainChunk>(Voxel{FullOccupancy, 6}));

    world.shareFrom(cells.front().field);
    REQUIRE(world.findChunk(key) != nullptr);
    CHECK(world.findChunk(key)->value() == Voxel{FullOccupancy, 6});
}

TEST_CASE("a block-world cell round-trips through its file")
{
    seedCatalog();
    VoxelGrid grid;
    (void)grid.fill(-40, -3, -8, 70, 20, 9, 3);
    (void)grid.set(5, 30, 5, 7);
    const std::vector<VoxelCell> cells = splitVoxels(grid, 1.0f);
    REQUIRE(cells.size() > 1);

    VoxelGrid rebuilt;
    for (const VoxelCell& cell : cells) {
        VoxelCell decoded;
        REQUIRE_FALSE(decodeVoxelCell(encodeVoxelCell(cell), decoded).has_value());
        CHECK(decoded.x == cell.x);
        CHECK(decoded.z == cell.z);
        CHECK(decoded.blockSize == doctest::Approx(1.0));
        rebuilt.shareFrom(decoded.grid);
    }
    CHECK(rebuilt.digest() == grid.digest());
}

TEST_CASE("a block-world cell that is not one is refused, not half-read")
{
    seedCatalog();
    VoxelGrid grid;
    (void)grid.fill(0, 0, 0, 20, 4, 20, 1);
    const std::vector<std::byte> bytes = encodeVoxelCell(splitVoxels(grid, 1.0f).front());

    VoxelCell out;
    // Truncated.
    CHECK(decodeVoxelCell(std::span<const std::byte>(bytes.data(), bytes.size() - 3), out).has_value());
    // Trailing bytes.
    std::vector<std::byte> longer = bytes;
    longer.push_back(std::byte{0});
    CHECK(decodeVoxelCell(longer, out).has_value());
    // Wrong magic.
    std::vector<std::byte> wrong = bytes;
    wrong[0] = std::byte{0};
    CHECK(decodeVoxelCell(wrong, out).has_value());
    // A version from elsewhere.
    std::vector<std::byte> future = bytes;
    future[4] = std::byte{9};
    CHECK(decodeVoxelCell(future, out).has_value());
    // A count past the ceiling.
    std::vector<std::byte> huge = bytes;
    huge[20] = std::byte{0xFF};
    huge[21] = std::byte{0xFF};
    huge[22] = std::byte{0xFF};
    CHECK(decodeVoxelCell(huge, out).has_value());
}

TEST_CASE("an edited block-world cell is kept, an untouched one is dropped")
{
    VoxelGrid source;
    (void)source.fill(0, 0, 0, 127, 3, 15, 2);
    const core::u32 across = voxelCellChunks(1.0f);
    const std::vector<VoxelCell> cells = splitVoxels(source, 1.0f);
    REQUIRE(cells.size() == 2);

    VoxelGrid world;
    for (const VoxelCell& cell : cells)
        world.shareFrom(cell.grid);
    CHECK(voxelCellUntouched(world, cells[0], across));

    (void)world.set(100, 1, 1, 9);
    CHECK_FALSE(voxelCellUntouched(world, cells[1], across));
    CHECK(voxelCellUntouched(world, cells[0], across));

    removeVoxelCell(world, cells[0]);
    CHECK(world.get(1, 1, 1) == AirBlock);
    CHECK(world.get(100, 1, 1) == 9);
}
