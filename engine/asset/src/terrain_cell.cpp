#include "luaug/asset/terrain_cell.h"

#include "luaug/core/i18n.h"

#include <cstring>

namespace luaug::asset {
namespace {

using core::f32;
using core::I18nArg;
using core::i32;
using core::u32;
using core::u64;
using core::u8;
using core::usize;

// `LGTF`, little-endian: LuauG Terrain Field. Read as four bytes rather than as
// an integer literal so the file's first four characters are legible in a hex
// dump, which is the only debugging tool available when a format goes wrong.
constexpr u32 Magic = 'L' | ('G' << 8) | ('T' << 16) | ('F' << 24);

void writeU32(std::vector<std::byte>& out, u32 value)
{
    for (usize i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::byte>((value >> (i * 8)) & 0xFFu));
    }
}

void writeF32(std::vector<std::byte>& out, f32 value)
{
    u32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    writeU32(out, bits);
}

void writeBytes(std::vector<std::byte>& out, const void* data, usize count)
{
    const auto* const first = static_cast<const std::byte*>(data);
    out.insert(out.end(), first, first + count);
}

class Reader
{
public:
    explicit Reader(std::span<const std::byte> bytes) : m_bytes(bytes) {}

    [[nodiscard]] bool ok() const noexcept { return m_ok; }
    [[nodiscard]] usize remaining() const noexcept { return m_ok ? m_bytes.size() - m_at : 0; }
    // Where the header ended, so the body can be handed to a coder as a span
    // rather than read through this.
    [[nodiscard]] usize at() const noexcept { return m_at; }

    u32 u32v()
    {
        if (!m_ok || m_bytes.size() - m_at < 4) {
            m_ok = false;
            return 0;
        }
        u32 value = 0;
        for (usize i = 0; i < 4; ++i) {
            value |= static_cast<u32>(static_cast<unsigned char>(m_bytes[m_at + i])) << (i * 8);
        }
        m_at += 4;
        return value;
    }

    f32 f32v()
    {
        const u32 bits = u32v();
        f32 value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    // Copies `count` bytes into `into`, or fails. **Never hands out a pointer
    // into the buffer**: a caller holding one would outlive the span the moment
    // the cell was reloaded, which is the shape `ShapeDesc`'s span rule exists
    // to prevent one layer down.
    bool blob(void* into, usize count)
    {
        if (!m_ok || m_bytes.size() - m_at < count) {
            m_ok = false;
            return false;
        }
        std::memcpy(into, m_bytes.data() + m_at, count);
        m_at += count;
        return true;
    }

private:
    std::span<const std::byte> m_bytes;
    usize m_at = 0;
    bool m_ok = true;
};

[[nodiscard]] core::EngineError malformed()
{
    return core::makeError(LUAUG_TR("asset.terrain.err.malformed"));
}

// PackBits: a control byte below 128 means "the next n + 1 bytes are literal",
// and one at or above it means "the next byte repeats 257 - n times", which is
// 2 to 129.
//
// The smallest run coder that does not make incompressible data twice its size,
// and the shape this data wants: a brick is mostly saturated distance and a
// tile's materials are usually one value.
void packBits(std::span<const std::byte> plain, std::vector<std::byte>& out)
{
    usize at = 0;
    while (at < plain.size()) {
        usize run = 1;
        while (at + run < plain.size() && plain[at + run] == plain[at] && run < 129) {
            ++run;
        }

        if (run >= 2) {
            out.push_back(static_cast<std::byte>(257 - run));
            out.push_back(plain[at]);
            at += run;
            continue;
        }

        // A literal stretch ends where a run of three or more begins -- two is
        // not worth breaking a literal for, because the break costs a control
        // byte of its own.
        usize literal = 1;
        while (at + literal < plain.size() && literal < 128) {
            const bool runAhead = at + literal + 2 < plain.size() && plain[at + literal] == plain[at + literal + 1] &&
                                  plain[at + literal] == plain[at + literal + 2];
            if (runAhead) {
                break;
            }
            ++literal;
        }
        out.push_back(static_cast<std::byte>(literal - 1));
        out.insert(out.end(), plain.begin() + static_cast<std::ptrdiff_t>(at),
                   plain.begin() + static_cast<std::ptrdiff_t>(at + literal));
        at += literal;
    }
}

// **Into a buffer whose size the caller already validated**, which is the whole
// reason the header is never compressed: the counts are checked against
// `MaxCellTiles` and `MaxCellBricks` first, so `plainSize` here is a number this
// format already agreed to allocate rather than one a corrupt file chose.
[[nodiscard]] bool unpackBits(std::span<const std::byte> packed, usize plainSize, std::vector<std::byte>& out)
{
    out.clear();
    out.reserve(plainSize);
    usize at = 0;
    while (at < packed.size() && out.size() < plainSize) {
        const auto control = static_cast<u8>(packed[at++]);
        if (control < 128) {
            const usize literal = static_cast<usize>(control) + 1;
            if (at + literal > packed.size() || out.size() + literal > plainSize) {
                return false;
            }
            out.insert(out.end(), packed.begin() + static_cast<std::ptrdiff_t>(at),
                       packed.begin() + static_cast<std::ptrdiff_t>(at + literal));
            at += literal;
            continue;
        }
        const usize run = 257 - static_cast<usize>(control);
        if (at >= packed.size() || out.size() + run > plainSize) {
            return false;
        }
        out.insert(out.end(), run, packed[at]);
        ++at;
    }
    return out.size() == plainSize;
}

} // namespace

std::vector<std::byte> encodeTerrainCell(const TerrainCell& cell, TerrainCellCompression compression)
{
    std::vector<std::byte> out;

    const std::vector<TileKey> tileKeys = cell.field.tileKeys();
    const std::vector<BrickKey> brickKeys = cell.field.brickKeys();

    // The header. A `flags` word that must decode as exactly zero, which is
    // `chunk.cpp`'s rule and worth keeping: it is the cheapest possible
    // forward-compatibility trap, because a future writer that sets a bit makes
    // every older reader refuse rather than misread.
    writeU32(out, Magic);
    writeU32(out, TerrainCellFormatVersion);
    writeU32(out, 0);
    writeU32(out, static_cast<u32>(cell.x));
    writeU32(out, static_cast<u32>(cell.z));
    writeF32(out, cell.settings.voxelSize);
    // **The reserved range, which version 1 dropped.** It is not a description
    // of the ground -- it is what the ground may ever be dug or raised to, and a
    // collider's height precision is spread across it at construction and cannot
    // be widened afterwards (ADR 0066). A file that omitted it reloaded as the
    // default band and clamped every later edit to it, silently.
    writeF32(out, cell.settings.minHeight);
    writeF32(out, cell.settings.maxHeight);
    writeU32(out, cell.settings.giveUpColumns);
    writeU32(out, static_cast<u32>(tileKeys.size()));
    writeU32(out, static_cast<u32>(brickKeys.size()));
    writeU32(out, static_cast<u32>(compression));

    // **The body is assembled plain and then coded as one stream.** One coder
    // over one stream rather than one per array: the runs that matter cross the
    // boundaries anyway -- a tile of flat ground is a run of heights followed by
    // a run of materials -- and one is one thing to get right.
    //
    // The header stays uncompressed, always, because a reader has to be able to
    // check the counts against this format's ceilings BEFORE it allocates
    // anything to decompress into. That is the same rule the ceilings exist for.
    std::vector<std::byte> body;

    // **The directories come first and are key-sorted**, mirroring `.lchunk`'s
    // table of contents. Sorted because `tileKeys()` answers sorted, and a
    // reader that can rely on that can also detect a file whose keys are not --
    // which is a corrupt file rather than an exotic one.
    for (const TileKey key : tileKeys) {
        writeU32(body, static_cast<u32>(key.x));
        writeU32(body, static_cast<u32>(key.z));
    }
    for (const BrickKey key : brickKeys) {
        writeU32(body, static_cast<u32>(key.x));
        writeU32(body, static_cast<u32>(key.y));
        writeU32(body, static_cast<u32>(key.z));
    }

    // Then the payloads, in the same order.
    for (const TileKey key : tileKeys) {
        const HeightTile* tile = cell.field.findTile(key);
        if (tile == nullptr) {
            continue; // Unreachable: the key came from the field.
        }
        writeBytes(body, tile->height, sizeof(tile->height));
        writeBytes(body, tile->material, sizeof(tile->material));
    }
    for (const BrickKey key : brickKeys) {
        const Brick* brick = cell.field.findBrick(key);
        if (brick == nullptr) {
            continue;
        }
        writeBytes(body, brick->sd, sizeof(brick->sd));
        writeBytes(body, brick->material, sizeof(brick->material));
    }

    if (compression == TerrainCellCompression::RunLength) {
        packBits(body, out);
    }
    else {
        out.insert(out.end(), body.begin(), body.end());
    }
    return out;
}

std::optional<core::EngineError> decodeTerrainCell(std::span<const std::byte> bytes, TerrainCell& out)
{
    Reader reader(bytes);

    if (reader.u32v() != Magic) {
        return malformed();
    }
    const u32 version = reader.u32v();
    if (version != TerrainCellFormatVersion) {
        const I18nArg args[] = {{"found", static_cast<core::i64>(version)},
                                {"expected", static_cast<core::i64>(TerrainCellFormatVersion)}};
        return core::makeError(LUAUG_TR("asset.terrain.err.version"), args);
    }
    if (reader.u32v() != 0) {
        return malformed();
    }

    const auto cellX = static_cast<i32>(reader.u32v());
    const auto cellZ = static_cast<i32>(reader.u32v());
    FieldSettings settings;
    settings.voxelSize = reader.f32v();
    settings.minHeight = reader.f32v();
    settings.maxHeight = reader.f32v();
    settings.giveUpColumns = reader.u32v();

    const u32 tileCount = reader.u32v();
    const u32 brickCount = reader.u32v();
    const u32 compression = reader.u32v();
    if (!reader.ok()) {
        return malformed();
    }
    if (compression != static_cast<u32>(TerrainCellCompression::None) &&
        compression != static_cast<u32>(TerrainCellCompression::RunLength)) {
        // A coder this build does not have is refused rather than guessed at,
        // for the same reason a version is.
        return malformed();
    }

    // **Counted before it is believed.** A corrupt `tileCount` of four billion
    // would otherwise reserve sixteen gigabytes before the first read failed,
    // which is the exact failure `chunk.cpp` documents.
    if (tileCount > MaxCellTiles || brickCount > MaxCellBricks) {
        return core::makeError(LUAUG_TR("asset.terrain.err.too_large"));
    }

    // And checked against what is actually left, so a truncated file is refused
    // at the header rather than partway through a payload.
    constexpr u64 TileBytes = sizeof(HeightTile::height) + sizeof(HeightTile::material);
    constexpr u64 BrickBytes = sizeof(Brick::sd) + sizeof(Brick::material);
    constexpr u64 TileKeyBytes = 8;
    constexpr u64 BrickKeyBytes = 12;
    const u64 needed = static_cast<u64>(tileCount) * (TileKeyBytes + TileBytes) +
                       static_cast<u64>(brickCount) * (BrickKeyBytes + BrickBytes);

    // A settings block that cannot describe a field is refused rather than
    // clamped: a zero voxel size divides by zero in every sampler above this,
    // and an inverted range makes every promotion examination empty -- whose
    // symptom would be terrain that cannot be sculpted rather than an error.
    if (!(settings.voxelSize > 0.0f) || !(settings.maxHeight > settings.minHeight)) {
        return malformed();
    }

    // **The body, decoded before it is read, into a buffer the counts already
    // bounded.** `needed` came from counts checked against `MaxCellTiles` and
    // `MaxCellBricks` above, so this is a size the format already agreed to
    // allocate rather than one a corrupt file chose -- which is the whole reason
    // the header is never itself compressed.
    std::vector<std::byte> body;
    std::span<const std::byte> bodyBytes;
    if (compression == static_cast<u32>(TerrainCellCompression::RunLength)) {
        if (!unpackBits(bytes.subspan(reader.at()), static_cast<usize>(needed), body)) {
            return malformed();
        }
        bodyBytes = body;
    }
    else {
        // Uncompressed, which is what a version-1 file was and what a writer
        // may still choose. Checked against what is actually left, so a
        // truncated file is refused at the header rather than partway through a
        // payload.
        if (needed > reader.remaining()) {
            return malformed();
        }
        bodyBytes = bytes.subspan(reader.at(), static_cast<usize>(needed));
    }

    // From here down the reader walks the decoded body rather than the file, so
    // every offset below is relative to it and neither branch above changes a
    // line of what follows.
    reader = Reader(bodyBytes);

    std::vector<TileKey> tileKeys;
    tileKeys.reserve(tileCount);
    for (u32 at = 0; at < tileCount; ++at) {
        TileKey key;
        key.x = static_cast<i32>(reader.u32v());
        key.z = static_cast<i32>(reader.u32v());
        // **Sorted, or the file is corrupt.** The writer emits sorted keys
        // because the field answers sorted; a reader that accepted any order
        // would accept a file whose directory and payloads disagree, and the
        // failure would be terrain in the wrong place rather than an error.
        if (at > 0 && !(tileKeys[at - 1] < key)) {
            return malformed();
        }
        tileKeys.push_back(key);
    }

    std::vector<BrickKey> brickKeys;
    brickKeys.reserve(brickCount);
    for (u32 at = 0; at < brickCount; ++at) {
        BrickKey key;
        key.x = static_cast<i32>(reader.u32v());
        key.y = static_cast<i32>(reader.u32v());
        key.z = static_cast<i32>(reader.u32v());
        if (at > 0 && !(brickKeys[at - 1] < key)) {
            return malformed();
        }
        brickKeys.push_back(key);
    }
    if (!reader.ok()) {
        return malformed();
    }

    TerrainField field(settings);
    std::vector<float> heights(TileArea);
    std::vector<u8> materials(TileArea);
    for (const TileKey key : tileKeys) {
        if (!reader.blob(heights.data(), sizeof(HeightTile::height)) ||
            !reader.blob(materials.data(), sizeof(HeightTile::material))) {
            return malformed();
        }
        field.setTile(key, heights, materials);
    }

    std::vector<u8> distances(BrickVolume);
    std::vector<u8> brickMaterials(BrickVolume);
    for (const BrickKey key : brickKeys) {
        if (!reader.blob(distances.data(), sizeof(Brick::sd)) ||
            !reader.blob(brickMaterials.data(), sizeof(Brick::material))) {
            return malformed();
        }
        field.setBrick(key, distances, brickMaterials);
    }

    if (!reader.ok()) {
        return malformed();
    }

    out.x = cellX;
    out.z = cellZ;
    out.settings = settings;
    out.field = std::move(field);
    return std::nullopt;
}

} // namespace luaug::asset
