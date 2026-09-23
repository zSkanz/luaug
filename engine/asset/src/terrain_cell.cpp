#include "luaug/asset/terrain_cell.h"

#include "luaug/core/i18n.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <memory>

namespace luaug::asset {
namespace {

using core::f32;
using core::I18nArg;
using core::i32;
using core::u16;
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
// The smallest run coder that does not make incompressible data twice its size.
// Over the voxel runs it mostly finds repeated keys and counts.
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
// reason the header is never compressed: the counts are checked against the
// format's ceilings first, so `plainSize` here is a number this
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

void writeU16(std::vector<std::byte>& out, u16 value)
{
    out.push_back(static_cast<std::byte>(value & 0xFFu));
    out.push_back(static_cast<std::byte>((value >> 8) & 0xFFu));
}

// Upper bound of one chunk's voxel runs: a run per voxel, four bytes each.
constexpr u64 MaxChunkPayload = static_cast<u64>(ChunkVolume) * 4;

// One chunk's voxels as (packed, count) runs in storage order -- y, then z, then
// x innermost, the order a chunk's rows lie in.
void encodeChunk(const TerrainChunk& chunk, std::vector<std::byte>& out)
{
    if (chunk.uniform()) {
        writeU16(out, packVoxel(chunk.value()));
        // A run is at most 65,535; a chunk is 32,768 voxels, so one run is it.
        writeU16(out, static_cast<u16>(ChunkVolume));
        return;
    }
    std::array<u16, ChunkEdge> row{};
    u16 current = 0;
    u32 run = 0;
    for (u32 y = 0; y < ChunkEdge; ++y) {
        for (u32 z = 0; z < ChunkEdge; ++z) {
            chunk.readRow(y, z, row);
            for (const u16 value : row) {
                if (run > 0 && value == current && run < 0xFFFFu) {
                    ++run;
                    continue;
                }
                if (run > 0) {
                    writeU16(out, current);
                    writeU16(out, static_cast<u16>(run));
                }
                current = value;
                run = 1;
            }
        }
    }
    writeU16(out, current);
    writeU16(out, static_cast<u16>(run));
}

// The inverse, into a fresh chunk. False when the runs do not cover exactly one
// chunk or name a voxel that is not canonical -- air with a material, which no
// writer produces and a reader must not invent.
[[nodiscard]] bool decodeChunk(std::span<const std::byte> bytes, std::shared_ptr<TerrainChunk>& out)
{
    if (bytes.size() % 4 != 0 || bytes.empty())
        return false;
    const auto read16 = [&](usize at) {
        return static_cast<u16>(static_cast<unsigned>(static_cast<u8>(bytes[at])) |
                                (static_cast<unsigned>(static_cast<u8>(bytes[at + 1])) << 8));
    };
    if (bytes.size() == 4) {
        const u16 value = read16(0);
        if (read16(2) != ChunkVolume || packVoxel(canonical(unpackVoxel(value))) != value)
            return false;
        out = std::make_shared<TerrainChunk>(unpackVoxel(value));
        return true;
    }
    auto chunk = std::make_shared<TerrainChunk>();
    std::array<u16, ChunkEdge> row{};
    u32 written = 0;
    for (usize at = 0; at < bytes.size(); at += 4) {
        const u16 value = read16(at);
        const u32 count = read16(at + 2);
        if (count == 0 || written + count > ChunkVolume || packVoxel(canonical(unpackVoxel(value))) != value)
            return false;
        for (u32 n = 0; n < count; ++n) {
            const u32 index = written + n;
            row[index % ChunkEdge] = value;
            if (index % ChunkEdge == ChunkEdge - 1) {
                const u32 rowIndex = index / ChunkEdge;
                chunk->writeRow(rowIndex / ChunkEdge, rowIndex % ChunkEdge, row);
            }
        }
        written += count;
    }
    if (written != ChunkVolume)
        return false;
    chunk->normalize();
    out = std::move(chunk);
    return true;
}

// --- Version 2: the hybrid, read and resampled --------------------------------
//
// ADR 0067's field -- height tiles of 32 by 32 columns and voxel bricks of 16
// cubed holding a quantised distance -- exactly as its reader understood it,
// kept only to turn a saved world into voxels. Its lattice sits ON multiples of
// the voxel size where the grid's centres sit half a voxel in; each new voxel's
// fullness is the old field at its centre, which is the mean of the eight
// lattice samples round it.
namespace legacy {

constexpr u32 TileEdge = 32;
constexpr u32 TileArea = TileEdge * TileEdge;
constexpr u32 BrickEdge = 16;
constexpr u32 BrickVolume = BrickEdge * BrickEdge * BrickEdge;
constexpr u64 TileBytes = static_cast<u64>(TileArea) * 5;
constexpr u64 BrickBytes = static_cast<u64>(BrickVolume) * 2;
constexpr u32 MaxTiles = 1u << 20;
constexpr u32 MaxBricks = 1u << 22;

struct Key2
{
    i32 x = 0;
    i32 z = 0;
    [[nodiscard]] constexpr auto operator<=>(const Key2&) const noexcept = default;
};

struct Key3
{
    i32 x = 0;
    i32 y = 0;
    i32 z = 0;
    [[nodiscard]] constexpr auto operator<=>(const Key3&) const noexcept = default;
};

struct Tile
{
    std::array<float, TileArea> height{};
    std::array<u8, TileArea> material{};
};

struct Brick
{
    std::array<u8, BrickVolume> distance{};
    std::array<u8, BrickVolume> material{};
};

template <class Vector, class Key>
[[nodiscard]] const auto* findIn(const Vector& entries, const Key& key)
{
    const auto at = std::lower_bound(entries.begin(), entries.end(), key,
                                     [](const auto& entry, const Key& probe) { return entry.first < probe; });
    return (at != entries.end() && at->first == key) ? &at->second : nullptr;
}

struct Field
{
    float voxel = 0.5f;
    std::vector<std::pair<Key2, Tile>> tiles;
    std::vector<std::pair<Key3, Brick>> bricks;

    // The old `TerrainField::sample`: a brick if one covers the point, else the
    // height layer's `y - H`, else air.
    [[nodiscard]] std::pair<float, u8> sample(i32 x, i32 y, i32 z) const
    {
        constexpr auto brickEdge = static_cast<i32>(BrickEdge);
        if (const Brick* brick =
                findIn(bricks, Key3{floorDiv(x, brickEdge), floorDiv(y, brickEdge), floorDiv(z, brickEdge)})) {
            const auto index = static_cast<usize>(
                (floorMod(y, brickEdge) * brickEdge + floorMod(z, brickEdge)) * brickEdge + floorMod(x, brickEdge));
            const float voxels = ((static_cast<float>(brick->distance[index]) - 128.0f) / 128.0f) * 4.0f;
            return {voxels * voxel, brick->material[index]};
        }
        const auto [height, material] = column(x, z);
        if (material == 0)
            return {4.0f * voxel, 0};
        return {static_cast<float>(y) * voxel - height, material};
    }

    // The column's height and material; material zero is no ground.
    [[nodiscard]] std::pair<float, u8> column(i32 x, i32 z) const
    {
        constexpr auto tileEdge = static_cast<i32>(TileEdge);
        const Tile* tile = findIn(tiles, Key2{floorDiv(x, tileEdge), floorDiv(z, tileEdge)});
        if (tile == nullptr)
            return {0.0f, 0};
        const auto index = static_cast<usize>(floorMod(z, tileEdge) * tileEdge + floorMod(x, tileEdge));
        return {tile->height[index], tile->material[index]};
    }
};

// Lays the old field into `into`: the height layer first, a tile at a time as
// a block of heights, then every brick's neighbourhood voxel by voxel.
void resample(const Field& old, TerrainField& into)
{
    // A voxel column's centre is between four old columns: its height is the
    // mean of those that hold ground, its material the first that does.
    constexpr auto tileEdge = static_cast<i32>(TileEdge);
    std::vector<float> heights(TileArea);
    std::vector<u8> materials(TileArea);
    for (const auto& entry : old.tiles) {
        const i32 firstX = entry.first.x * tileEdge;
        const i32 firstZ = entry.first.z * tileEdge;
        for (i32 localZ = 0; localZ < tileEdge; ++localZ) {
            for (i32 localX = 0; localX < tileEdge; ++localX) {
                float sum = 0.0f;
                int count = 0;
                u8 material = 0;
                for (int corner = 0; corner < 4; ++corner) {
                    const auto [height, got] =
                        old.column(firstX + localX + (corner & 1), firstZ + localZ + ((corner >> 1) & 1));
                    if (got == 0)
                        continue;
                    sum += height;
                    count += 1;
                    if (material == 0)
                        material = got;
                }
                const auto slot = static_cast<usize>(localZ * tileEdge + localX);
                heights[slot] = count > 0 ? sum / static_cast<float>(count) : 0.0f;
                materials[slot] = count > 0 ? material : u8{0};
            }
        }
        (void)writeHeights(into, firstX, firstZ, TileEdge, heights, materials);
    }

    // Where a brick is, the height layer's answer above was not the old
    // surface: every voxel whose eight lattice corners touch one is the old
    // field at its centre.
    constexpr auto brickEdge = static_cast<i32>(BrickEdge);
    FieldWriter writer(into);
    const float voxel = old.voxel;
    for (const auto& entry : old.bricks) {
        const i32 baseX = entry.first.x * brickEdge;
        const i32 baseY = entry.first.y * brickEdge;
        const i32 baseZ = entry.first.z * brickEdge;
        for (i32 z = baseZ - 1; z < baseZ + brickEdge; ++z) {
            for (i32 y = baseY - 1; y < baseY + brickEdge; ++y) {
                for (i32 x = baseX - 1; x < baseX + brickEdge; ++x) {
                    float sum = 0.0f;
                    float nearest = std::numeric_limits<float>::max();
                    u8 material = 0;
                    for (int corner = 0; corner < 8; ++corner) {
                        const auto [distance, got] =
                            old.sample(x + (corner & 1), y + ((corner >> 1) & 1), z + ((corner >> 2) & 1));
                        sum += distance;
                        if (got != 0 && distance < nearest) {
                            nearest = distance;
                            material = got;
                        }
                    }
                    const float distance = sum / 8.0f;
                    const u8 occupancy =
                        quantiseOccupancy(rampOccupancy(static_cast<double>(distance), static_cast<double>(voxel)));
                    writer.set(x, y, z, Voxel{occupancy, material == 0 ? u8{1} : material});
                }
            }
        }
    }
    writer.finish();
}

} // namespace legacy

[[nodiscard]] std::optional<core::EngineError> decodeLegacyCell(std::span<const std::byte> bytes, TerrainCell& out)
{
    Reader reader(bytes);
    (void)reader.u32v(); // Magic, checked by the caller.
    (void)reader.u32v(); // Version, likewise.
    if (reader.u32v() != 0)
        return malformed();
    const auto cellX = static_cast<i32>(reader.u32v());
    const auto cellZ = static_cast<i32>(reader.u32v());
    FieldSettings settings;
    settings.voxelSize = reader.f32v();
    settings.minHeight = reader.f32v();
    settings.maxHeight = reader.f32v();
    (void)reader.u32v(); // How many steep columns before a promotion: nothing now.
    const u32 tileCount = reader.u32v();
    const u32 brickCount = reader.u32v();
    const u32 compression = reader.u32v();
    if (!reader.ok())
        return malformed();
    if (compression != static_cast<u32>(TerrainCellCompression::None) &&
        compression != static_cast<u32>(TerrainCellCompression::RunLength))
        return malformed();
    if (tileCount > legacy::MaxTiles || brickCount > legacy::MaxBricks)
        return core::makeError(LUAUG_TR("asset.terrain.err.too_large"));
    if (!(settings.voxelSize > 0.0f) || !(settings.maxHeight > settings.minHeight))
        return malformed();

    const u64 needed = static_cast<u64>(tileCount) * (8 + legacy::TileBytes) +
                       static_cast<u64>(brickCount) * (12 + legacy::BrickBytes);
    std::vector<std::byte> body;
    std::span<const std::byte> bodyBytes;
    if (compression == static_cast<u32>(TerrainCellCompression::RunLength)) {
        if (needed > static_cast<u64>(reader.remaining()) * 128u)
            return malformed();
        if (!unpackBits(bytes.subspan(reader.at()), static_cast<usize>(needed), body))
            return malformed();
        bodyBytes = body;
    }
    else {
        if (needed > reader.remaining())
            return malformed();
        bodyBytes = bytes.subspan(reader.at(), static_cast<usize>(needed));
    }
    reader = Reader(bodyBytes);

    legacy::Field old;
    old.voxel = settings.voxelSize;
    old.tiles.resize(tileCount);
    old.bricks.resize(brickCount);
    for (u32 at = 0; at < tileCount; ++at) {
        const auto x = static_cast<i32>(reader.u32v());
        const auto z = static_cast<i32>(reader.u32v());
        old.tiles[at].first = legacy::Key2{x, z};
        if (at > 0 && !(old.tiles[at - 1].first < old.tiles[at].first))
            return malformed();
    }
    for (u32 at = 0; at < brickCount; ++at) {
        const auto x = static_cast<i32>(reader.u32v());
        const auto y = static_cast<i32>(reader.u32v());
        const auto z = static_cast<i32>(reader.u32v());
        old.bricks[at].first = legacy::Key3{x, y, z};
        if (at > 0 && !(old.bricks[at - 1].first < old.bricks[at].first))
            return malformed();
    }
    for (auto& entry : old.tiles) {
        if (!reader.blob(entry.second.height.data(), sizeof(float) * legacy::TileArea) ||
            !reader.blob(entry.second.material.data(), legacy::TileArea))
            return malformed();
    }
    for (auto& entry : old.bricks) {
        if (!reader.blob(entry.second.distance.data(), legacy::BrickVolume) ||
            !reader.blob(entry.second.material.data(), legacy::BrickVolume))
            return malformed();
    }
    if (!reader.ok())
        return malformed();

    TerrainField field(settings);
    legacy::resample(old, field);
    out.x = cellX;
    out.z = cellZ;
    out.settings = settings;
    out.field = std::move(field);
    return std::nullopt;
}

} // namespace

std::vector<std::byte> encodeTerrainCell(const TerrainCell& cell, TerrainCellCompression compression)
{
    std::vector<std::byte> out;

    // The body first, plain, so the header can say how long it is.
    //
    // **The directory comes first and is key-sorted**, mirroring `.lchunk`'s
    // table of contents: a reader can then refuse a file whose keys are not
    // sorted, which is a corrupt file rather than an exotic one.
    std::vector<std::byte> body;
    const std::span<const TerrainField::Entry> chunks = cell.field.chunks();
    for (const TerrainField::Entry& entry : chunks) {
        writeU32(body, static_cast<u32>(entry.first.x));
        writeU32(body, static_cast<u32>(entry.first.y));
        writeU32(body, static_cast<u32>(entry.first.z));
    }
    // **A chunk the field shares is coded once**: flat ground laid on an empty
    // world is one column shared by every column (`fillFlat`), and 5 km of it
    // is fifty thousand entries and two chunks. What the file holds is the
    // same either way, a payload per entry.
    std::map<const TerrainChunk*, std::vector<std::byte>> coded;
    for (const TerrainField::Entry& entry : chunks) {
        auto [at, fresh] = coded.try_emplace(entry.second.get());
        if (fresh)
            encodeChunk(*entry.second, at->second);
        writeU32(body, static_cast<u32>(at->second.size()));
        body.insert(body.end(), at->second.begin(), at->second.end());
    }

    // The header. `flags` and the last word must decode as exactly zero:
    // `chunk.cpp`'s rule, and the cheapest forward-compatibility trap there is.
    writeU32(out, Magic);
    writeU32(out, TerrainCellFormatVersion);
    writeU32(out, 0);
    writeU32(out, static_cast<u32>(cell.x));
    writeU32(out, static_cast<u32>(cell.z));
    writeF32(out, cell.settings.voxelSize);
    writeF32(out, cell.settings.minHeight);
    writeF32(out, cell.settings.maxHeight);
    writeU32(out, static_cast<u32>(chunks.size()));
    writeU32(out, static_cast<u32>(compression));
    writeU32(out, static_cast<u32>(body.size()));
    writeU32(out, 0);

    if (compression == TerrainCellCompression::RunLength)
        packBits(body, out);
    else
        out.insert(out.end(), body.begin(), body.end());
    return out;
}

std::optional<core::EngineError> decodeTerrainCell(std::span<const std::byte> bytes, TerrainCell& out,
                                                   TerrainCellLimits limits)
{
    Reader reader(bytes);

    if (reader.u32v() != Magic)
        return malformed();
    const u32 version = reader.u32v();
    if (version == TerrainCellLegacyVersion && reader.ok())
        return decodeLegacyCell(bytes, out);
    if (version != TerrainCellFormatVersion) {
        const I18nArg args[] = {{"found", static_cast<core::i64>(version)},
                                {"expected", static_cast<core::i64>(TerrainCellFormatVersion)}};
        return core::makeError(LUAUG_TR("asset.terrain.err.version"), args);
    }
    if (reader.u32v() != 0)
        return malformed();

    const auto cellX = static_cast<i32>(reader.u32v());
    const auto cellZ = static_cast<i32>(reader.u32v());
    FieldSettings settings;
    settings.voxelSize = reader.f32v();
    settings.minHeight = reader.f32v();
    settings.maxHeight = reader.f32v();
    const u32 chunkCount = reader.u32v();
    const u32 compression = reader.u32v();
    const u32 bodySize = reader.u32v();
    const u32 reserved = reader.u32v();
    if (!reader.ok() || reserved != 0)
        return malformed();
    if (compression != static_cast<u32>(TerrainCellCompression::None) &&
        compression != static_cast<u32>(TerrainCellCompression::RunLength))
        return malformed();

    // **Counted before it is believed**: a corrupt count must not reserve
    // gigabytes before the first read fails.
    if (chunkCount > limits.chunks)
        return core::makeError(LUAUG_TR("asset.terrain.err.too_large"));
    // A settings block that cannot describe a field is refused rather than
    // clamped: a zero voxel size divides by zero in every sampler above this.
    if (!(settings.voxelSize > 0.0f) || !(settings.maxHeight > settings.minHeight))
        return malformed();
    // The body cannot be longer than its chunks could ever code to, nor shorter
    // than their directory and one run each.
    const u64 ceiling = static_cast<u64>(chunkCount) * (12 + 4 + MaxChunkPayload);
    if (bodySize > ceiling || bodySize < static_cast<u64>(chunkCount) * 20)
        return malformed();

    std::vector<std::byte> body;
    std::span<const std::byte> bodyBytes;
    if (compression == static_cast<u32>(TerrainCellCompression::RunLength)) {
        // PackBits expands a byte into at most 128, so a body larger than that
        // many times what is left cannot be in the file: the allocation is
        // bounded by the bytes that are actually there.
        if (static_cast<u64>(bodySize) > static_cast<u64>(reader.remaining()) * 128u)
            return malformed();
        if (!unpackBits(bytes.subspan(reader.at()), bodySize, body))
            return malformed();
        bodyBytes = body;
    }
    else {
        if (bodySize > reader.remaining())
            return malformed();
        bodyBytes = bytes.subspan(reader.at(), bodySize);
    }
    reader = Reader(bodyBytes);

    std::vector<ChunkKey> keys;
    keys.reserve(chunkCount);
    for (u32 at = 0; at < chunkCount; ++at) {
        ChunkKey key;
        key.x = static_cast<i32>(reader.u32v());
        key.y = static_cast<i32>(reader.u32v());
        key.z = static_cast<i32>(reader.u32v());
        if (at > 0 && !(keys[at - 1] < key))
            return malformed();
        keys.push_back(key);
    }
    if (!reader.ok())
        return malformed();

    // **Chunks with the same bytes come back as one chunk, shared**, as the
    // field that was saved held them: a plain of 5 km reopened as fifty
    // thousand copies of two chunks was a quarter of a gigabyte. Shared chunks
    // are copied on the first write to one, so nothing can tell but the memory.
    TerrainField field(settings);
    std::vector<std::byte> payload;
    std::map<std::vector<std::byte>, std::shared_ptr<TerrainChunk>> decoded;
    for (const ChunkKey key : keys) {
        const u32 size = reader.u32v();
        if (!reader.ok() || size > MaxChunkPayload || size > reader.remaining())
            return malformed();
        payload.resize(size);
        if (!reader.blob(payload.data(), size))
            return malformed();
        auto [at, fresh] = decoded.try_emplace(payload);
        if (fresh) {
            if (!decodeChunk(payload, at->second))
                return malformed();
            // An empty chunk is never written; one in a file is a corrupt file.
            if (at->second->empty())
                return malformed();
        }
        field.setChunk(key, at->second);
    }
    if (reader.remaining() != 0)
        return malformed();

    out.x = cellX;
    out.z = cellZ;
    out.settings = settings;
    out.field = std::move(field);
    return std::nullopt;
}

} // namespace luaug::asset
