// The `.lterrain` cell format (ADR 0067, F1 B3; version 3 by ADR 0082).
//
// **What a codec's tests are actually for is the malformed input**, not the
// round trip. A round trip proves the writer and the reader agree with each
// other, which they would even if both were wrong; what decides whether the
// format is safe is what happens to a file somebody truncated, corrupted, or
// wrote with a different version of this engine -- including the last one,
// whose worlds are read and turned into voxels.
#include "luaug/asset/terrain_cell.h"
#include "luaug/core/i18n.h"

#include <cmath>
#include <cstring>
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

[[nodiscard]] FieldSettings settingsOf()
{
    return FieldSettings{.voxelSize = 0.5f, .minHeight = -48.0f, .maxHeight = 40.0f};
}

// Ground, a hill, a tunnel and a painted patch: uniform chunks, rowed chunks,
// several materials and air inside ground, all in one field.
[[nodiscard]] TerrainField sculptedField()
{
    TerrainField field(settingsOf());
    (void)fillFlat(field, core::DVec3{0.0, 0.0, 0.0}, 48.0f, 2.0f, 1);
    (void)raiseBall(field, core::DVec3{4.0, 2.0, -3.0}, 8.0, 5.0f);
    (void)fillBall(field, core::DVec3{-6.0, -1.0, 2.0}, 3.0, 0);
    (void)paintBall(field, core::DVec3{8.0, 2.0, 8.0}, 3.0, 4);
    return field;
}

[[nodiscard]] TerrainCell sampleCell()
{
    TerrainCell cell;
    cell.x = -3;
    cell.z = 11;
    cell.settings = settingsOf();
    cell.field = sculptedField();
    return cell;
}

void putWord(std::vector<std::byte>& bytes, core::usize at, core::u32 value)
{
    for (core::usize i = 0; i < 4; ++i)
        bytes[at + i] = static_cast<std::byte>((value >> (i * 8)) & 0xFFu);
}

void appendWord(std::vector<std::byte>& bytes, core::u32 value)
{
    for (core::usize i = 0; i < 4; ++i)
        bytes.push_back(static_cast<std::byte>((value >> (i * 8)) & 0xFFu));
}

void appendFloat(std::vector<std::byte>& bytes, float value)
{
    core::u32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    appendWord(bytes, bits);
}

} // namespace

TEST_CASE("a cell round-trips, and the field that comes back has the same digest")
{
    seedCatalog();
    const TerrainCell original = sampleCell();
    const std::vector<std::byte> encoded = encodeTerrainCell(original);
    REQUIRE_FALSE(encoded.empty());

    TerrainCell back;
    REQUIRE_FALSE(decodeTerrainCell(encoded, back).has_value());
    CHECK(back.x == original.x);
    CHECK(back.z == original.z);
    CHECK(back.settings.voxelSize == original.settings.voxelSize);
    CHECK(back.settings.minHeight == original.settings.minHeight);
    CHECK(back.settings.maxHeight == original.settings.maxHeight);
    CHECK(back.field.chunkCount() == original.field.chunkCount());
    CHECK(back.field.digest() == original.field.digest());
}

TEST_CASE("encoding is a pure function of the cell")
{
    const TerrainCell cell = sampleCell();
    CHECK(encodeTerrainCell(cell) == encodeTerrainCell(cell));
    TerrainCell copy = cell;
    copy.field = sculptedField();
    CHECK(encodeTerrainCell(copy) == encodeTerrainCell(cell));
}

TEST_CASE("an empty cell is a legal cell")
{
    TerrainCell cell;
    cell.settings = settingsOf();
    cell.field = TerrainField(cell.settings);
    TerrainCell back;
    REQUIRE_FALSE(decodeTerrainCell(encodeTerrainCell(cell), back).has_value());
    CHECK(back.field.empty());
}

TEST_CASE("both codings round-trip, and the coded one is smaller")
{
    const TerrainCell cell = sampleCell();
    const std::vector<std::byte> coded = encodeTerrainCell(cell, TerrainCellCompression::RunLength);
    const std::vector<std::byte> plain = encodeTerrainCell(cell, TerrainCellCompression::None);
    CHECK(coded.size() < plain.size());
    for (const std::vector<std::byte>& bytes : {coded, plain}) {
        TerrainCell back;
        REQUIRE_FALSE(decodeTerrainCell(bytes, back).has_value());
        CHECK(back.field.digest() == cell.field.digest());
    }
}

TEST_CASE("the voxel runs actually pay for themselves on ground")
{
    // An assertion about the data rather than a benchmark: a field of flat
    // ground is whole chunks of one value and rows of one value, and a format
    // that did not collapse that would be one worth replacing.
    TerrainCell cell;
    cell.settings = settingsOf();
    cell.field = TerrainField(cell.settings);
    (void)fillFlat(cell.field, core::DVec3{0.0, 0.0, 0.0}, 64.0f, 3.0f, 1);
    const core::usize plain = cell.field.chunkCount() * ChunkVolume * 2;
    CHECK(encodeTerrainCell(cell).size() * 64 < plain);
}

TEST_CASE("a file that is not a terrain cell is refused at the first four bytes")
{
    seedCatalog();
    std::vector<std::byte> bytes = encodeTerrainCell(sampleCell());
    bytes[0] = std::byte{'X'};
    TerrainCell back;
    CHECK(decodeTerrainCell(bytes, back).has_value());
}

TEST_CASE("a version this build does not know is refused rather than misread")
{
    seedCatalog();
    std::vector<std::byte> bytes = encodeTerrainCell(sampleCell());
    putWord(bytes, 4, 99);
    TerrainCell back;
    const std::optional<core::EngineError> error = decodeTerrainCell(bytes, back);
    REQUIRE(error.has_value());
    CHECK(error->message.find("99") != std::string::npos);
}

TEST_CASE("a reserved bit or word that is set makes an older reader refuse")
{
    std::vector<std::byte> flags = encodeTerrainCell(sampleCell());
    putWord(flags, 8, 1);
    TerrainCell back;
    CHECK(decodeTerrainCell(flags, back).has_value());

    std::vector<std::byte> reserved = encodeTerrainCell(sampleCell());
    putWord(reserved, TerrainCellHeaderBytes - 4, 1);
    CHECK(decodeTerrainCell(reserved, back).has_value());
}

TEST_CASE("a count larger than the ceiling is refused before anything is reserved")
{
    seedCatalog();
    std::vector<std::byte> bytes = encodeTerrainCell(sampleCell());
    putWord(bytes, 32, MaxCellChunks + 1);
    TerrainCell back;
    CHECK(decodeTerrainCell(bytes, back).has_value());
    // A whole field is held to a larger ceiling, but still to one.
    putWord(bytes, 32, MaxFieldChunks + 1);
    CHECK(decodeTerrainCell(bytes, back, WholeFieldLimits).has_value());
}

TEST_CASE("a body larger than its chunks could ever code to is refused")
{
    std::vector<std::byte> bytes = encodeTerrainCell(sampleCell());
    putWord(bytes, 40, 0xFFFFFFF0u);
    TerrainCell back;
    CHECK(decodeTerrainCell(bytes, back).has_value());
}

TEST_CASE("a directory whose keys are out of order is a corrupt file")
{
    TerrainCell cell = sampleCell();
    REQUIRE(cell.field.chunkCount() >= 2);
    std::vector<std::byte> bytes = encodeTerrainCell(cell, TerrainCellCompression::None);
    // Swap the first two keys of the directory.
    const core::usize first = TerrainCellHeaderBytes;
    for (core::usize i = 0; i < 12; ++i)
        std::swap(bytes[first + i], bytes[first + 12 + i]);
    TerrainCell back;
    CHECK(decodeTerrainCell(bytes, back).has_value());
}

TEST_CASE("a voxel size or a range that cannot describe a field is refused rather than clamped")
{
    std::vector<std::byte> zero = encodeTerrainCell(sampleCell());
    putWord(zero, 20, 0);
    TerrainCell back;
    CHECK(decodeTerrainCell(zero, back).has_value());

    TerrainCell inverted = sampleCell();
    inverted.settings.minHeight = 8.0f;
    inverted.settings.maxHeight = 8.0f;
    CHECK(decodeTerrainCell(encodeTerrainCell(inverted), back).has_value());
}

TEST_CASE("a coder this build does not have is refused rather than guessed at")
{
    std::vector<std::byte> bytes = encodeTerrainCell(sampleCell());
    putWord(bytes, 36, 0x7F);
    TerrainCell back;
    CHECK(decodeTerrainCell(bytes, back).has_value());
}

TEST_CASE("a truncated cell is an error and never a crash")
{
    const std::vector<std::byte> good = encodeTerrainCell(sampleCell());
    for (core::usize length = 0; length < good.size(); length += 1 + length / 16) {
        const std::vector<std::byte> cut(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(length));
        TerrainCell back;
        CAPTURE(length);
        CHECK(decodeTerrainCell(cut, back).has_value());
    }
}

TEST_CASE("a chunk whose runs do not cover it, or name air with a material, is refused")
{
    // One chunk, written by hand: its runs are one voxel short.
    std::vector<std::byte> body;
    appendWord(body, 0);
    appendWord(body, 0);
    appendWord(body, 0);
    appendWord(body, 4);
    body.push_back(std::byte{255});
    body.push_back(std::byte{1});
    body.push_back(std::byte{0xFF});
    body.push_back(std::byte{0x7F});
    const auto cellOf = [](const std::vector<std::byte>& payload) {
        std::vector<std::byte> bytes;
        appendWord(bytes, 'L' | ('G' << 8) | ('T' << 16) | ('F' << 24));
        appendWord(bytes, TerrainCellFormatVersion);
        appendWord(bytes, 0);
        appendWord(bytes, 0);
        appendWord(bytes, 0);
        appendFloat(bytes, 1.0f);
        appendFloat(bytes, -64.0f);
        appendFloat(bytes, 64.0f);
        appendWord(bytes, 1);
        appendWord(bytes, 0);
        appendWord(bytes, static_cast<core::u32>(payload.size()));
        appendWord(bytes, 0);
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        return bytes;
    };
    TerrainCell back;
    CHECK(decodeTerrainCell(cellOf(body), back).has_value());

    // The whole chunk in one run: legal.
    body[body.size() - 2] = std::byte{0x00};
    body[body.size() - 1] = std::byte{0x80};
    CHECK_FALSE(decodeTerrainCell(cellOf(body), back).has_value());

    // Air with a material: no writer makes it, so no reader accepts it.
    body[body.size() - 4] = std::byte{0};
    CHECK(decodeTerrainCell(cellOf(body), back).has_value());
}

TEST_CASE("a hybrid-era cell is read and turned into voxels (ADR 0082)")
{
    // Version 2, written by hand: one height tile of ground at 3 m and, in
    // another column, one voxel brick of air -- a cave. That is the old
    // encoding, and a world saved before the voxel grid must still open.
    constexpr core::u32 TileArea = 32 * 32;
    constexpr core::u32 BrickVolume = 16 * 16 * 16;
    std::vector<std::byte> body;
    appendWord(body, 0); // tile (0, 0)
    appendWord(body, 0);
    appendWord(body, 0); // brick (0, 0, 0)
    appendWord(body, 0);
    appendWord(body, 0);
    for (core::u32 at = 0; at < TileArea; ++at)
        appendFloat(body, 3.0f);
    for (core::u32 at = 0; at < TileArea; ++at)
        body.push_back(std::byte{1});
    // The brick: air everywhere, as a cave dug under the ground is.
    for (core::u32 at = 0; at < BrickVolume; ++at)
        body.push_back(std::byte{255});
    for (core::u32 at = 0; at < BrickVolume; ++at)
        body.push_back(std::byte{0});

    std::vector<std::byte> bytes;
    appendWord(bytes, 'L' | ('G' << 8) | ('T' << 16) | ('F' << 24));
    appendWord(bytes, TerrainCellLegacyVersion);
    appendWord(bytes, 0);
    appendWord(bytes, 2);
    appendWord(bytes, 5);
    appendFloat(bytes, 0.5f);
    appendFloat(bytes, -32.0f);
    appendFloat(bytes, 32.0f);
    appendWord(bytes, 8);
    appendWord(bytes, 1);
    appendWord(bytes, 1);
    appendWord(bytes, 0);
    bytes.insert(bytes.end(), body.begin(), body.end());

    TerrainCell back;
    REQUIRE_FALSE(decodeTerrainCell(bytes, back).has_value());
    CHECK(back.x == 2);
    CHECK(back.z == 5);
    CHECK(back.settings.voxelSize == 0.5f);
    // The ground is where the tile said, far from the brick...
    const std::optional<float> ground = heightAt(back.field, 12.0, 12.0);
    REQUIRE(ground.has_value());
    CHECK(static_cast<double>(*ground) == doctest::Approx(3.0).epsilon(0.01));
    // ...and the brick's cube of air is air, under ground that is still there.
    CHECK(sampleField(back.field, core::DVec3{2.0, 2.0, 2.0}).distance > 0.0f);
    CHECK(sampleField(back.field, core::DVec3{12.0, 2.0, 12.0}).distance < 0.0f);

    // And it is written back as version 3.
    TerrainCell again;
    const std::vector<std::byte> rewritten = encodeTerrainCell(back);
    REQUIRE_FALSE(decodeTerrainCell(rewritten, again).has_value());
    CHECK(again.field.digest() == back.field.digest());
}
