// The icon directory, both layouts, and the four bytes between them.
//
// **Nothing tested this at all** (S7.10). `iconpatch` is what puts a game's own
// icon into its packaged executable, and its own header says why that matters:
// nothing fails when an icon is wrong -- the program runs, the window opens, and
// it wears the wrong face. The failure is invisible in every direction, which is
// exactly the kind that needs asserting rather than looking at.
#include "luaug/iconpatch/ico.h"

#include <cstdint>
#include <doctest/doctest.h>
#include <vector>

using luaug::iconpatch::buildGroup;
using luaug::iconpatch::IconEntry;
using luaug::iconpatch::parseIcon;

namespace {

void putU16(std::vector<std::uint8_t>& out, std::size_t at, std::uint16_t value)
{
    out[at] = static_cast<std::uint8_t>(value & 0xFF);
    out[at + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
}

void putU32(std::vector<std::uint8_t>& out, std::size_t at, std::uint32_t value)
{
    out[at] = static_cast<std::uint8_t>(value & 0xFF);
    out[at + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    out[at + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    out[at + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
}

// A well-formed `.ico` with `sizes.size()` entries, each carrying `payload`
// bytes of anything at all -- the parser does not look inside an image and must
// not start.
[[nodiscard]] std::vector<std::uint8_t> makeIcon(const std::vector<std::uint8_t>& sizes, std::uint32_t payload = 16)
{
    const std::size_t header = 6u + sizes.size() * 16u;
    std::vector<std::uint8_t> data(header + sizes.size() * payload, 0xAB);
    for (std::size_t index = 0; index < header; ++index)
        data[index] = 0;

    putU16(data, 0, 0);
    putU16(data, 2, 1);
    putU16(data, 4, static_cast<std::uint16_t>(sizes.size()));
    for (std::size_t index = 0; index < sizes.size(); ++index) {
        const std::size_t at = 6u + index * 16u;
        data[at + 0] = sizes[index];
        data[at + 1] = sizes[index];
        putU16(data, at + 4, 1);  // planes
        putU16(data, at + 6, 32); // bit count
        putU32(data, at + 8, payload);
        putU32(data, at + 12, static_cast<std::uint32_t>(header + index * payload));
    }
    return data;
}

} // namespace

TEST_CASE("a well-formed icon parses to its entries")
{
    const std::vector<std::uint8_t> data = makeIcon({16, 32, 48});
    const std::vector<IconEntry> entries = parseIcon(data);

    REQUIRE(entries.size() == 3);
    CHECK(entries[0].width == 16);
    CHECK(entries[1].width == 32);
    CHECK(entries[2].width == 48);
    CHECK(entries[0].bitCount == 32);
    CHECK(entries[0].planes == 1);
    CHECK(entries[0].bytes == 16);
    CHECK(entries[2].offset == 6u + 3u * 16u + 2u * 16u);
}

TEST_CASE("a 256-pixel entry is stored as zero and is not a bug")
{
    // The one thing every naive icon reader gets wrong, and the size that
    // matters most: 256 does not fit in a byte, so the format spells it 0.
    // Parsing keeps the raw byte -- the meaning belongs to whoever draws it --
    // and what MUST hold is that it survives the round trip to a group entry.
    const std::vector<std::uint8_t> data = makeIcon({0});
    const std::vector<IconEntry> entries = parseIcon(data);
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].width == 0);

    const std::vector<std::uint8_t> group = buildGroup(entries, 1);
    REQUIRE(group.size() == 6u + 14u);
    CHECK(group[6] == 0);
    CHECK(group[7] == 0);
}

TEST_CASE("a cursor is refused, and it is the same shape as an icon")
{
    // Type 2, which has a hotspot exactly where an icon has colour planes and a
    // bit count -- so it parses perfectly and means something else. Refusing on
    // the type is the only thing between that and a build step that applies it.
    std::vector<std::uint8_t> data = makeIcon({32});
    putU16(data, 2, 2);
    CHECK(parseIcon(data).empty());
}

TEST_CASE("a truncated file is refused rather than half-read")
{
    const std::vector<std::uint8_t> whole = makeIcon({16, 32});

    // Cut inside the directory: the second entry's bytes are not all there.
    const std::vector<std::uint8_t> shortDirectory(whole.begin(), whole.begin() + 6 + 16 + 8);
    CHECK(parseIcon(shortDirectory).empty());

    // Header intact, image data missing. **All or nothing**: the first entry is
    // perfectly valid, and keeping it is how an executable gets an icon with a
    // hole where its second size should be.
    const std::vector<std::uint8_t> shortPayload(whole.begin(), whole.begin() + 6 + 32 + 4);
    CHECK(parseIcon(shortPayload).empty());

    CHECK(parseIcon(std::vector<std::uint8_t>{}).empty());
    CHECK(parseIcon(std::vector<std::uint8_t>{0, 0, 1, 0}).empty());
}

TEST_CASE("an entry claiming zero bytes is refused")
{
    std::vector<std::uint8_t> data = makeIcon({32});
    putU32(data, 6 + 8, 0);
    CHECK(parseIcon(data).empty());
}

TEST_CASE("an entry pointing past the end is refused, including one that wraps")
{
    std::vector<std::uint8_t> data = makeIcon({32});
    putU32(data, 6 + 12, 0xFFFFFFF0u);
    CHECK(parseIcon(data).empty());

    // And the overflow shape: an offset near the top plus a length that would
    // wrap a 32-bit sum. The bound is computed in `std::size_t`, which is what
    // stops this being a read past the buffer rather than a refusal.
    data = makeIcon({32});
    putU32(data, 6 + 12, 0xFFFFFF00u);
    putU32(data, 6 + 8, 0x00000200u);
    CHECK(parseIcon(data).empty());
}

TEST_CASE("a group entry is the file entry with an id where the offset was")
{
    // The four bytes this whole tool turns on. Sixteen becomes fourteen, the
    // 32-bit file offset becomes a 16-bit resource id, and every other field is
    // carried across untouched.
    const std::vector<IconEntry> entries = parseIcon(makeIcon({16, 32}, 300));
    REQUIRE(entries.size() == 2);

    const std::vector<std::uint8_t> group = buildGroup(entries, 1);
    REQUIRE(group.size() == 6u + 2u * 14u);

    // The header says icon, and says how many.
    CHECK(group[0] == 0);
    CHECK(group[1] == 0);
    CHECK(group[2] == 1);
    CHECK(group[3] == 0);
    CHECK(group[4] == 2);
    CHECK(group[5] == 0);

    const std::uint8_t* first = group.data() + 6;
    CHECK(first[0] == 16);
    CHECK(first[1] == 16);
    CHECK(first[4] == 1);  // planes
    CHECK(first[6] == 32); // bit count
    // The byte count survives as 32 bits, which is what 300 is here to check:
    // an entry whose size was truncated to 16 bits works for every icon under
    // 64 KiB and fails for a 256-square PNG, which is the one that matters.
    CHECK(static_cast<std::uint32_t>(first[8] | (first[9] << 8) | (first[10] << 16) | (first[11] << 24)) == 300u);
    CHECK(first[12] == 1); // id, not an offset
    CHECK(first[13] == 0);

    // Ids are consecutive from `firstId`, which is the contract the RT_ICON
    // resources are written under.
    const std::uint8_t* second = group.data() + 6 + 14;
    CHECK(second[0] == 32);
    CHECK(second[12] == 2);
    CHECK(second[13] == 0);
}

TEST_CASE("ids continue past 255 without losing their high byte")
{
    const std::vector<IconEntry> entries = parseIcon(makeIcon({16, 32}));
    const std::vector<std::uint8_t> group = buildGroup(entries, 300);
    CHECK(group[6 + 12] == 44); // 300 & 0xFF
    CHECK(group[6 + 13] == 1);  // 300 >> 8
    CHECK(group[6 + 14 + 12] == 45);
    CHECK(group[6 + 14 + 13] == 1);
}

TEST_CASE("no entries is an empty group, not a crash")
{
    const std::vector<std::uint8_t> group = buildGroup({}, 1);
    REQUIRE(group.size() == 6);
    CHECK(group[2] == 1);
    CHECK(group[4] == 0);
}
