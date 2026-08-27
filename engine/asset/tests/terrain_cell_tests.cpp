// The `.lterrain` cell format (ADR 0067, F1 B3).
//
// **What a codec's tests are actually for is the malformed input**, not the
// round trip. A round trip proves the writer and the reader agree with each
// other, which they would even if both were wrong; what decides whether the
// format is safe is what happens to a file somebody truncated, corrupted, or
// wrote with a different version of this engine.
#include "luaug/asset/terrain_cell.h"
#include "luaug/core/i18n.h"

#include <doctest/doctest.h>
#include <vector>

using namespace luaug;
using namespace luaug::asset;

namespace {

void seedCatalog()
{
    const auto result = core::engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
}

[[nodiscard]] TerrainCell sampleCell()
{
    TerrainCell cell;
    cell.x = -3;
    cell.z = 11;
    cell.settings = FieldSettings{.voxelSize = 0.25f, .giveUpColumns = 12};
    cell.field = TerrainField(cell.settings);

    std::vector<float> heights(TileArea);
    std::vector<core::u8> materials(TileArea);
    for (core::u32 at = 0; at < TileArea; ++at) {
        heights[at] = 0.5f * static_cast<float>(at % 17);
        materials[at] = static_cast<core::u8>(at % 5);
    }
    cell.field.setTile(TileKey{0, 0}, heights, materials);
    cell.field.setTile(TileKey{-1, 2}, heights, materials);

    std::vector<core::u8> distances(BrickVolume);
    std::vector<core::u8> brickMaterials(BrickVolume);
    for (core::u32 at = 0; at < BrickVolume; ++at) {
        distances[at] = static_cast<core::u8>(at % 251);
        brickMaterials[at] = static_cast<core::u8>(at % 7);
    }
    cell.field.setBrick(BrickKey{0, -1, 0}, distances, brickMaterials);
    return cell;
}

} // namespace

TEST_CASE("a cell round-trips, and the field that comes back has the same digest")
{
    seedCatalog();

    const TerrainCell original = sampleCell();
    const std::vector<std::byte> encoded = encodeTerrainCell(original);
    REQUIRE_FALSE(encoded.empty());

    TerrainCell decoded;
    REQUIRE_FALSE(decodeTerrainCell(encoded, decoded).has_value());

    CHECK(decoded.x == original.x);
    CHECK(decoded.z == original.z);
    CHECK(decoded.settings.voxelSize == original.settings.voxelSize);
    CHECK(decoded.settings.giveUpColumns == original.settings.giveUpColumns);
    CHECK(decoded.field.tileCount() == original.field.tileCount());
    CHECK(decoded.field.brickCount() == original.field.brickCount());

    // **The digest is the assertion that matters.** Comparing counts proves the
    // right number of objects arrived; comparing digests proves the right BYTES
    // did, keys included -- which is what a world hash over reloaded terrain
    // depends on.
    CHECK(decoded.field.digest() == original.field.digest());
}

TEST_CASE("encoding is a pure function of the cell")
{
    seedCatalog();

    // The same cell encodes to the same bytes on every machine, which is what
    // makes a content hash over one mean anything. A writer that walked a hash
    // map, or that wrote uninitialised padding, would fail here.
    const TerrainCell cell = sampleCell();
    CHECK(encodeTerrainCell(cell) == encodeTerrainCell(cell));

    // And a re-encode after a round trip is byte-identical, which is the
    // stronger statement: the decoder reconstructed the field exactly rather
    // than equivalently.
    TerrainCell decoded;
    REQUIRE_FALSE(decodeTerrainCell(encodeTerrainCell(cell), decoded).has_value());
    CHECK(encodeTerrainCell(decoded) == encodeTerrainCell(cell));
}

TEST_CASE("an empty cell is a legal cell")
{
    seedCatalog();

    // A cell nobody has sculpted yet. The path exists because a streaming system
    // that had to special-case "no terrain here" would special-case it in
    // several places.
    TerrainCell empty;
    empty.settings = FieldSettings{.voxelSize = 0.5f};
    empty.field = TerrainField(empty.settings);

    TerrainCell decoded;
    REQUIRE_FALSE(decodeTerrainCell(encodeTerrainCell(empty), decoded).has_value());
    CHECK(decoded.field.tileCount() == 0);
    CHECK(decoded.field.brickCount() == 0);
    CHECK(decoded.field.digest() == empty.field.digest());
}

TEST_CASE("a file this build cannot read is refused by version rather than misread")
{
    seedCatalog();

    std::vector<std::byte> encoded = encodeTerrainCell(sampleCell());
    // The version word is the second, immediately after the magic.
    encoded[4] = static_cast<std::byte>(TerrainCellFormatVersion + 1);

    TerrainCell decoded;
    const auto error = decodeTerrainCell(encoded, decoded);
    REQUIRE(error.has_value());
    CHECK(error->message.find("format") != std::string::npos);
}

TEST_CASE("a file that is not a terrain cell is refused at the first four bytes")
{
    seedCatalog();

    std::vector<std::byte> encoded = encodeTerrainCell(sampleCell());
    encoded[0] = static_cast<std::byte>('X');

    TerrainCell decoded;
    CHECK(decodeTerrainCell(encoded, decoded).has_value());

    // And so is something far too short to be one.
    const std::vector<std::byte> stub(3, std::byte{0});
    CHECK(decodeTerrainCell(stub, decoded).has_value());
    CHECK(decodeTerrainCell({}, decoded).has_value());
}

TEST_CASE("a reserved bit that is set makes an older reader refuse rather than misread")
{
    seedCatalog();

    // **The cheapest forward-compatibility trap there is.** A future writer that
    // sets a flag bit makes every reader that predates the flag refuse, instead
    // of reading a file whose meaning has changed underneath it.
    std::vector<std::byte> encoded = encodeTerrainCell(sampleCell());
    encoded[8] = static_cast<std::byte>(1);

    TerrainCell decoded;
    CHECK(decodeTerrainCell(encoded, decoded).has_value());
}

TEST_CASE("a truncated file is refused rather than half-loaded")
{
    seedCatalog();

    const std::vector<std::byte> whole = encodeTerrainCell(sampleCell());

    // Cut at several depths: inside the header, inside the directory, and part
    // way through a payload. Every one of them has to fail, and none of them may
    // leave `out` half-populated in a way a caller would use.
    for (const double fraction : {0.1, 0.4, 0.7, 0.99}) {
        const auto keep = static_cast<core::usize>(static_cast<double>(whole.size()) * fraction);
        const std::vector<std::byte> cut(whole.begin(), whole.begin() + static_cast<std::ptrdiff_t>(keep));
        TerrainCell decoded;
        CAPTURE(fraction);
        CHECK(decodeTerrainCell(cut, decoded).has_value());
    }
}

TEST_CASE("a count larger than the ceiling is refused before anything is reserved")
{
    seedCatalog();

    // **This is the case the ceilings exist for**, and `chunk.cpp` states it in
    // its own words: "a bounds-checked reader is not a safe reader, because the
    // allocation happens first". A tile count of four billion would reserve
    // sixteen gigabytes before the first read failed.
    std::vector<std::byte> encoded = encodeTerrainCell(sampleCell());

    // The tile count is the eighth word: magic, version, flags, x, z, voxel,
    // giveUp, tiles.
    const core::usize tileCountAt = 7 * 4;
    for (core::usize i = 0; i < 4; ++i) {
        encoded[tileCountAt + i] = static_cast<std::byte>(0xFF);
    }

    TerrainCell decoded;
    const auto error = decodeTerrainCell(encoded, decoded);
    REQUIRE(error.has_value());
    CHECK(error->message.find("more tiles") != std::string::npos);
}

TEST_CASE("a directory whose keys are out of order is a corrupt file")
{
    seedCatalog();

    // The writer emits sorted keys because the field answers sorted. A reader
    // that accepted any order would accept a file whose directory and payloads
    // disagree -- and the failure would be terrain in the wrong place rather
    // than an error, which is the worst kind.
    std::vector<std::byte> encoded = encodeTerrainCell(sampleCell());

    // Two tiles, at `{-1, 2}` and `{0, 0}` sorted. Swap the first key's x so the
    // directory descends.
    const core::usize firstKeyAt = 9 * 4;
    for (core::usize i = 0; i < 4; ++i) {
        encoded[firstKeyAt + i] = static_cast<std::byte>(0x7F);
    }

    TerrainCell decoded;
    CHECK(decodeTerrainCell(encoded, decoded).has_value());
}

TEST_CASE("a voxel size that cannot describe a field is refused rather than clamped")
{
    seedCatalog();

    // Zero divides by zero in every sampler above this, and a clamp would put a
    // world on a lattice nobody asked for.
    std::vector<std::byte> encoded = encodeTerrainCell(sampleCell());
    const core::usize voxelAt = 5 * 4;
    for (core::usize i = 0; i < 4; ++i) {
        encoded[voxelAt + i] = static_cast<std::byte>(0);
    }

    TerrainCell decoded;
    CHECK(decodeTerrainCell(encoded, decoded).has_value());
}
