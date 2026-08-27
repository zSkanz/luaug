#include <luaug/iconpatch/ico.h>

namespace luaug::iconpatch {
namespace {

[[nodiscard]] std::uint16_t readU16(std::span<const std::uint8_t> data, std::size_t at)
{
    return static_cast<std::uint16_t>(data[at] | (data[at + 1] << 8));
}

[[nodiscard]] std::uint32_t readU32(std::span<const std::uint8_t> data, std::size_t at)
{
    return static_cast<std::uint32_t>(data[at]) | (static_cast<std::uint32_t>(data[at + 1]) << 8) |
           (static_cast<std::uint32_t>(data[at + 2]) << 16) | (static_cast<std::uint32_t>(data[at + 3]) << 24);
}

} // namespace

std::vector<IconEntry> parseIcon(std::span<const std::uint8_t> data)
{
    // `ICONDIR`: a reserved zero, then a type of 1. Type 2 is a CURSOR, which
    // has the same shape and two of its bytes meaning something else entirely --
    // a hotspot where an icon has colour planes and a bit count.
    if (data.size() < 6 || readU16(data, 0) != 0 || readU16(data, 2) != 1)
        return {};

    const std::uint16_t count = readU16(data, 4);
    if (count == 0 || data.size() < 6u + static_cast<std::size_t>(count) * 16u)
        return {};

    std::vector<IconEntry> entries;
    entries.reserve(count);
    for (std::uint16_t index = 0; index < count; ++index) {
        const std::size_t at = 6u + static_cast<std::size_t>(index) * 16u;
        IconEntry entry;
        entry.width = data[at + 0];
        entry.height = data[at + 1];
        entry.colors = data[at + 2];
        entry.reserved = data[at + 3];
        entry.planes = readU16(data, at + 4);
        entry.bitCount = readU16(data, at + 6);
        entry.bytes = readU32(data, at + 8);
        entry.offset = readU32(data, at + 12);

        // All or nothing: one entry naming bytes that are not there makes the
        // whole file untrustworthy, and copying the ones that happened to be
        // valid is how an executable ends up with a hole where a size should be.
        if (entry.bytes == 0 || static_cast<std::size_t>(entry.offset) + entry.bytes > data.size())
            return {};
        entries.push_back(entry);
    }
    return entries;
}

std::vector<std::uint8_t> buildGroup(std::span<const IconEntry> entries, std::uint16_t firstId)
{
    std::vector<std::uint8_t> group(6u + entries.size() * 14u, 0);
    group[2] = 1; // type: icon
    group[4] = static_cast<std::uint8_t>(entries.size() & 0xFF);
    group[5] = static_cast<std::uint8_t>((entries.size() >> 8) & 0xFF);

    for (std::size_t index = 0; index < entries.size(); ++index) {
        const IconEntry& entry = entries[index];
        std::uint8_t* out = group.data() + 6u + index * 14u;
        out[0] = entry.width;
        out[1] = entry.height;
        out[2] = entry.colors;
        out[3] = entry.reserved;
        out[4] = static_cast<std::uint8_t>(entry.planes & 0xFF);
        out[5] = static_cast<std::uint8_t>((entry.planes >> 8) & 0xFF);
        out[6] = static_cast<std::uint8_t>(entry.bitCount & 0xFF);
        out[7] = static_cast<std::uint8_t>((entry.bitCount >> 8) & 0xFF);
        out[8] = static_cast<std::uint8_t>(entry.bytes & 0xFF);
        out[9] = static_cast<std::uint8_t>((entry.bytes >> 8) & 0xFF);
        out[10] = static_cast<std::uint8_t>((entry.bytes >> 16) & 0xFF);
        out[11] = static_cast<std::uint8_t>((entry.bytes >> 24) & 0xFF);
        // **Where the four bytes go.** The file's 32-bit offset becomes a
        // 16-bit id, and the entry is 14 bytes instead of 16.
        const auto id = static_cast<std::uint16_t>(firstId + index);
        out[12] = static_cast<std::uint8_t>(id & 0xFF);
        out[13] = static_cast<std::uint8_t>((id >> 8) & 0xFF);
    }
    return group;
}

} // namespace luaug::iconpatch
