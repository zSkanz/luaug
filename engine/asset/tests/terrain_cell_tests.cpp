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

    // The tenth word: magic, version, flags, x, z, voxelSize, minHeight,
    // maxHeight, giveUpColumns, tileCount. It was the eighth until version 2
    // added the reserved range, which is why `TerrainCellHeaderBytes` is now
    // exported -- three hand-counted offsets is three things that go quietly
    // wrong the next time a word is added.
    const core::usize tileCountAt = 9 * 4;
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
    std::vector<std::byte> encoded = encodeTerrainCell(sampleCell(), TerrainCellCompression::None);

    // **Written uncompressed**, because the directory lives in the body and a
    // run-coded body has no fixed offset to poke. That is not a workaround: the
    // format carries both codings, and a decoder whose uncompressed branch no
    // test ever reached would be a branch that rots.
    //
    // Two tiles, at `{-1, 2}` and `{0, 0}` sorted. Swap the first key's x so the
    // directory descends.
    const core::usize firstKeyAt = TerrainCellHeaderBytes;
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

// --- Version 2: the reserved range, and the run coder ------------------------

namespace {

// Ground that reaches the floor -- height tiles -- with a cave through it, which
// is what puts voxel bricks in the field. Both encodings, because the format has
// to carry both and a fixture with only one would pass while half of it was
// broken.
[[nodiscard]] TerrainField sculptedField()
{
    TerrainField field(FieldSettings{.voxelSize = 0.5f, .minHeight = -48.0f, .maxHeight = 48.0f});
    fillBlock(field, core::DVec3{0.0, -20.0, 0.0}, core::Vec3{48.0f, 40.0f, 48.0f}, 1);
    fillBall(field, core::DVec3{0.0, -3.0, 0.0}, 4.0, 0);
    fillBall(field, core::DVec3{6.0, 1.0, 3.0}, 5.0, 2);
    return field;
}

} // namespace

TEST_CASE("the reserved height range survives a round trip")
{
    // **The hole version 2 exists to close.** Version 1 wrote `voxelSize` and
    // `giveUpColumns` and dropped the range, so a saved field came back with the
    // DEFAULT band -- and ADR 0066 says the band is spread across a collider's
    // height precision at construction and cannot be widened afterwards. Every
    // later edit to a reloaded world would have been clamped to a range it was
    // never sculpted under, with no error anywhere.
    //
    // Non-default on purpose, in both directions: a test that used the defaults
    // would pass against a decoder that read nothing at all.
    TerrainCell cell;
    cell.x = 3;
    cell.z = -7;
    cell.settings = FieldSettings{.voxelSize = 0.5f, .minHeight = -48.0f, .maxHeight = 48.0f};
    cell.field = sculptedField();

    TerrainCell back;
    REQUIRE_FALSE(decodeTerrainCell(encodeTerrainCell(cell), back).has_value());

    CHECK(back.settings.minHeight == -48.0f);
    CHECK(back.settings.maxHeight == 48.0f);
    // And the field carries it too, which is what every later edit reads.
    CHECK(back.field.settings().minHeight == -48.0f);
    CHECK(back.field.settings().maxHeight == 48.0f);
    CHECK(back.x == 3);
    CHECK(back.z == -7);
    CHECK(back.field.digest() == cell.field.digest());
}

TEST_CASE("an inverted reserved range is refused rather than clamped")
{
    // Its symptom would be terrain that cannot be sculpted -- every promotion
    // examination empty -- rather than an error, which is the worst shape a
    // corrupt file can take.
    TerrainCell cell;
    cell.settings = FieldSettings{.voxelSize = 0.5f, .minHeight = 8.0f, .maxHeight = 8.0f};
    cell.field = TerrainField(cell.settings);

    std::vector<std::byte> bytes = encodeTerrainCell(cell);
    TerrainCell back;
    CHECK(decodeTerrainCell(bytes, back).has_value());
}

TEST_CASE("the run coder actually pays for itself on ground")
{
    // Not a threshold on a benchmark -- an assertion about the data. A tile of
    // flat ground is one height repeated a thousand times and one material
    // repeated a thousand times, and a coder that did not collapse that would be
    // a coder worth removing. This is what version 1's comment asked for before
    // adding a compressor: a measurement rather than a hope.
    TerrainCell cell;
    cell.settings = FieldSettings{.voxelSize = 0.5f, .minHeight = -48.0f, .maxHeight = 48.0f};
    cell.field = TerrainField(cell.settings);
    fillBlock(cell.field, core::DVec3{0.0, -20.0, 0.0}, core::Vec3{64.0f, 40.0f, 64.0f}, 1);
    REQUIRE(cell.field.tileCount() >= 4);

    const core::usize plain = cell.field.tileCount() * (TileArea * 5) + cell.field.brickCount() * (BrickVolume * 2);
    const core::usize coded = encodeTerrainCell(cell).size();
    CHECK(coded * 8 < plain);
}

TEST_CASE("a coder this build does not have is refused rather than guessed at")
{
    TerrainCell cell;
    cell.settings = FieldSettings{.voxelSize = 0.5f, .minHeight = -48.0f, .maxHeight = 48.0f};
    cell.field = sculptedField();

    std::vector<std::byte> bytes = encodeTerrainCell(cell);
    // The compression word is the tenth: magic, version, flags, x, z, voxelSize,
    // minHeight, maxHeight, giveUpColumns, tileCount, brickCount, compression.
    REQUIRE(bytes.size() > 48);
    bytes[44] = std::byte{0x7F};

    TerrainCell back;
    CHECK(decodeTerrainCell(bytes, back).has_value());
}

TEST_CASE("a truncated cell is an error and never a crash")
{
    TerrainCell cell;
    cell.settings = FieldSettings{.voxelSize = 0.5f, .minHeight = -48.0f, .maxHeight = 48.0f};
    cell.field = sculptedField();
    const std::vector<std::byte> good = encodeTerrainCell(cell);

    // Every prefix. A run coder that walked off its input would be a decoder
    // that read whatever came next in memory.
    for (core::usize length = 0; length < good.size(); length += 1 + length / 16) {
        std::vector<std::byte> cut(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(length));
        TerrainCell back;
        CAPTURE(length);
        CHECK(decodeTerrainCell(cut, back).has_value());
    }
}

TEST_CASE("both codings round-trip, and the coded one is smaller")
{
    // The format carries two, so both are asserted. A decoder with a branch no
    // test reaches is a branch that rots -- and version 1's files are exactly
    // that branch, minus the two words version 2 added.
    TerrainCell cell;
    cell.x = -2;
    cell.z = 5;
    cell.settings = FieldSettings{.voxelSize = 0.5f, .minHeight = -48.0f, .maxHeight = 48.0f};
    cell.field = sculptedField();

    const std::vector<std::byte> coded = encodeTerrainCell(cell, TerrainCellCompression::RunLength);
    const std::vector<std::byte> plain = encodeTerrainCell(cell, TerrainCellCompression::None);
    CHECK(coded.size() < plain.size());

    for (const std::vector<std::byte>& bytes : {coded, plain}) {
        TerrainCell back;
        REQUIRE_FALSE(decodeTerrainCell(bytes, back).has_value());
        CHECK(back.x == cell.x);
        CHECK(back.z == cell.z);
        CHECK(back.field.digest() == cell.field.digest());
        CHECK(back.settings.minHeight == cell.settings.minHeight);
        CHECK(back.settings.maxHeight == cell.settings.maxHeight);
    }
}
