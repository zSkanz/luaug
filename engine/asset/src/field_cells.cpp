#include "luaug/asset/field_cells.h"

#include "luaug/core/i18n.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace luaug::asset {
namespace {

using core::f32;
using core::f64;
using core::i32;
using core::u32;
using core::usize;

[[nodiscard]] i32 floorDivide(i32 value, i32 divisor) noexcept
{
    const i32 quotient = value / divisor;
    return (value % divisor != 0 && (value < 0) != (divisor < 0)) ? quotient - 1 : quotient;
}

[[nodiscard]] u32 cellsAcross(f64 unitMetres, f64 cellMetres) noexcept
{
    if (!(unitMetres > 0.0) || !(cellMetres > 0.0))
        return 1;
    const f64 across = std::round(cellMetres / unitMetres);
    return across < 1.0 ? 1u : static_cast<u32>(across);
}

// --- The `.lvoxel` writer and reader ----------------------------------------

constexpr u32 VoxelCellMagic = 0x4356474Cu; // "LGVC", little-endian

void putWord(std::vector<std::byte>& out, u32 value)
{
    for (u32 shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<std::byte>((value >> shift) & 0xFFu));
}

class Reader
{
public:
    explicit Reader(std::span<const std::byte> bytes) noexcept : m_bytes(bytes) {}

    [[nodiscard]] u32 word() noexcept
    {
        if (m_at + 4 > m_bytes.size()) {
            m_ok = false;
            return 0;
        }
        u32 value = 0;
        for (u32 index = 0; index < 4; ++index)
            value |= static_cast<u32>(m_bytes[m_at + index]) << (8u * index);
        m_at += 4;
        return value;
    }

    [[nodiscard]] std::span<const std::byte> take(usize count) noexcept
    {
        if (count > m_bytes.size() - std::min(m_at, m_bytes.size())) {
            m_ok = false;
            return {};
        }
        const std::span<const std::byte> taken = m_bytes.subspan(m_at, count);
        m_at += count;
        return taken;
    }

    [[nodiscard]] bool ok() const noexcept { return m_ok; }
    [[nodiscard]] bool finished() const noexcept { return m_at == m_bytes.size(); }

private:
    std::span<const std::byte> m_bytes;
    usize m_at = 0;
    bool m_ok = true;
};

[[nodiscard]] core::EngineError malformedVoxels()
{
    return core::makeError(LUAUG_TR("asset.voxel.err.cell_malformed"));
}

} // namespace

// --- Terrain -----------------------------------------------------------------

u32 terrainCellChunks(f32 voxelSize, f64 cellMetres) noexcept
{
    return cellsAcross(static_cast<f64>(ChunkEdge) * static_cast<f64>(voxelSize), cellMetres);
}

ChunkId terrainCellOf(ChunkKey key, u32 cellChunks) noexcept
{
    const auto across = static_cast<i32>(cellChunks);
    return ChunkId{floorDivide(key.x, across), floorDivide(key.z, across), FieldLayerTerrain};
}

std::vector<TerrainCell> splitTerrain(const TerrainField& field, f64 cellMetres)
{
    const u32 cellChunks = terrainCellChunks(field.settings().voxelSize, cellMetres);
    std::map<ChunkId, TerrainField> cells;
    // Chunks are shared into the cells rather than copied: they are immutable
    // to everyone but a field that holds them alone.
    for (const TerrainField::Entry& entry : field.chunks()) {
        const ChunkId id = terrainCellOf(entry.first, cellChunks);
        auto at = cells.find(id);
        if (at == cells.end())
            at = cells.emplace(id, TerrainField(field.settings())).first;
        at->second.setChunk(entry.first, entry.second);
    }

    std::vector<TerrainCell> out;
    out.reserve(cells.size());
    for (auto& entry : cells) {
        TerrainCell cell;
        cell.x = entry.first.x;
        cell.z = entry.first.z;
        cell.settings = field.settings();
        cell.field = std::move(entry.second);
        out.push_back(std::move(cell));
    }
    return out;
}

core::DAABB terrainCellBounds(const TerrainCell& cell, u32 cellChunks, core::DVec3 origin) noexcept
{
    const f64 side =
        static_cast<f64>(cellChunks) * static_cast<f64>(ChunkEdge) * static_cast<f64>(cell.settings.voxelSize);
    core::DAABB bounds;
    bounds.min =
        core::DVec3{origin.x + static_cast<f64>(cell.x) * side, origin.y + static_cast<f64>(cell.settings.minHeight),
                    origin.z + static_cast<f64>(cell.z) * side};
    bounds.max =
        core::DVec3{bounds.min.x + side, origin.y + static_cast<f64>(cell.settings.maxHeight), bounds.min.z + side};
    return bounds;
}

bool terrainCellUntouched(const TerrainField& field, const TerrainCell& cell, u32 cellChunks) noexcept
{
    const auto across = static_cast<i32>(cellChunks);
    // Every chunk column of the cell's square: the field holds exactly the
    // chunks the cell brought, each still the very object it shared.
    for (i32 z = cell.z * across; z < (cell.z + 1) * across; ++z) {
        for (i32 x = cell.x * across; x < (cell.x + 1) * across; ++x) {
            const std::span<const TerrainField::Entry> held = field.column(x, z);
            const std::span<const TerrainField::Entry> brought = cell.field.column(x, z);
            if (held.size() != brought.size())
                return false;
            for (usize at = 0; at < held.size(); ++at) {
                if (!(held[at].first == brought[at].first) || held[at].second != brought[at].second)
                    return false;
            }
        }
    }
    return true;
}

void removeTerrainCell(TerrainField& field, const TerrainCell& cell)
{
    const std::vector<ChunkKey> keys = cell.field.chunkKeys();
    field.removeAll(keys);
}

// --- Block worlds ------------------------------------------------------------

u32 voxelCellChunks(f32 blockSize, f64 cellMetres) noexcept
{
    return cellsAcross(static_cast<f64>(VoxelChunkEdge) * static_cast<f64>(blockSize), cellMetres);
}

ChunkId voxelCellOf(VoxelChunkKey key, u32 cellChunks) noexcept
{
    const auto across = static_cast<i32>(cellChunks);
    return ChunkId{floorDivide(key.x, across), floorDivide(key.z, across), FieldLayerVoxels};
}

std::vector<VoxelCell> splitVoxels(const VoxelGrid& grid, f32 blockSize, f64 cellMetres)
{
    const u32 cellChunks = voxelCellChunks(blockSize, cellMetres);
    std::map<ChunkId, VoxelGrid> cells;
    for (const VoxelChunkKey key : grid.chunkKeys()) {
        const VoxelChunk* chunk = grid.findChunk(key);
        cells[voxelCellOf(key, cellChunks)].setChunk(key, std::span<const BlockId>(chunk->blocks, VoxelChunkVolume));
    }
    std::vector<VoxelCell> out;
    out.reserve(cells.size());
    for (auto& entry : cells) {
        VoxelCell cell;
        cell.x = entry.first.x;
        cell.z = entry.first.z;
        cell.blockSize = blockSize;
        cell.grid = std::move(entry.second);
        out.push_back(std::move(cell));
    }
    return out;
}

core::DAABB voxelCellBounds(const VoxelCell& cell, u32 cellChunks) noexcept
{
    const f64 chunkMetres = static_cast<f64>(VoxelChunkEdge) * static_cast<f64>(cell.blockSize);
    const f64 side = static_cast<f64>(cellChunks) * chunkMetres;
    i32 lowest = 0;
    i32 highest = 0;
    bool any = false;
    for (const VoxelChunkKey key : cell.grid.chunkKeys()) {
        lowest = any ? std::min(lowest, key.y) : key.y;
        highest = any ? std::max(highest, key.y) : key.y;
        any = true;
    }
    core::DAABB bounds;
    bounds.min = core::DVec3{static_cast<f64>(cell.x) * side, static_cast<f64>(lowest) * chunkMetres,
                             static_cast<f64>(cell.z) * side};
    bounds.max = core::DVec3{bounds.min.x + side, static_cast<f64>(highest + 1) * chunkMetres, bounds.min.z + side};
    return bounds;
}

bool voxelCellUntouched(const VoxelGrid& grid, const VoxelCell& cell, u32 cellChunks) noexcept
{
    const ChunkId id{cell.x, cell.z, FieldLayerVoxels};
    for (const VoxelChunkKey key : grid.chunkKeys()) {
        if (!(voxelCellOf(key, cellChunks) == id))
            continue;
        if (grid.findChunk(key) != cell.grid.findChunk(key))
            return false;
    }
    for (const VoxelChunkKey key : cell.grid.chunkKeys()) {
        if (grid.findChunk(key) == nullptr)
            return false;
    }
    return true;
}

void removeVoxelCell(VoxelGrid& grid, const VoxelCell& cell)
{
    const std::vector<VoxelChunkKey> keys = cell.grid.chunkKeys();
    grid.removeAll(keys);
}

std::vector<std::byte> encodeVoxelCell(const VoxelCell& cell)
{
    std::vector<std::byte> out;
    const std::vector<VoxelChunkKey> keys = cell.grid.chunkKeys();
    putWord(out, VoxelCellMagic);
    putWord(out, VoxelCellFormatVersion);
    putWord(out, static_cast<u32>(cell.x));
    putWord(out, static_cast<u32>(cell.z));
    u32 sizeBits = 0;
    std::memcpy(&sizeBits, &cell.blockSize, sizeof(sizeBits));
    putWord(out, sizeBits);
    putWord(out, static_cast<u32>(keys.size()));
    for (const VoxelChunkKey key : keys) {
        const std::vector<u8> runs = encodeVoxelChunk(*cell.grid.findChunk(key));
        putWord(out, static_cast<u32>(key.x));
        putWord(out, static_cast<u32>(key.y));
        putWord(out, static_cast<u32>(key.z));
        putWord(out, static_cast<u32>(runs.size()));
        for (const u8 byte : runs)
            out.push_back(static_cast<std::byte>(byte));
    }
    return out;
}

std::optional<core::EngineError> decodeVoxelCell(std::span<const std::byte> bytes, VoxelCell& out)
{
    Reader reader(bytes);
    if (reader.word() != VoxelCellMagic)
        return malformedVoxels();
    if (const u32 version = reader.word(); version != VoxelCellFormatVersion) {
        const core::I18nArg args[] = {{"expected", static_cast<core::i64>(VoxelCellFormatVersion)},
                                      {"found", static_cast<core::i64>(version)}};
        return core::makeError(LUAUG_TR("asset.voxel.err.cell_version"), args);
    }
    VoxelCell cell;
    cell.x = static_cast<i32>(reader.word());
    cell.z = static_cast<i32>(reader.word());
    const u32 sizeBits = reader.word();
    std::memcpy(&cell.blockSize, &sizeBits, sizeof(sizeBits));
    const u32 count = reader.word();
    if (!reader.ok() || count > MaxVoxelCellChunks || !(cell.blockSize > 0.0f))
        return malformedVoxels();

    std::vector<BlockId> blocks;
    bool first = true;
    VoxelChunkKey previous;
    for (u32 index = 0; index < count; ++index) {
        VoxelChunkKey key;
        key.x = static_cast<i32>(reader.word());
        key.y = static_cast<i32>(reader.word());
        key.z = static_cast<i32>(reader.word());
        const u32 length = reader.word();
        const std::span<const std::byte> runs = reader.take(length);
        if (!reader.ok() || (!first && !(previous < key)))
            return malformedVoxels();
        const std::span<const u8> view(reinterpret_cast<const u8*>(runs.data()), runs.size());
        if (!decodeVoxelChunk(view, blocks))
            return malformedVoxels();
        cell.grid.setChunk(key, blocks);
        previous = key;
        first = false;
    }
    if (!reader.finished())
        return malformedVoxels();
    out = std::move(cell);
    return std::nullopt;
}

} // namespace luaug::asset
