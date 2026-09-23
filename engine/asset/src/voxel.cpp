#include "luaug/asset/voxel.h"

#define XXH_INLINE_ALL
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "xxhash.h"

namespace luaug::asset {
namespace {

using core::i32;
using core::u32;
using core::u64;
using core::usize;

[[nodiscard]] i32 floorDivide(i32 value, i32 divisor) noexcept
{
    const i32 quotient = value / divisor;
    return (value % divisor != 0 && (value < 0) != (divisor < 0)) ? quotient - 1 : quotient;
}

[[nodiscard]] u32 floorModulo(i32 value, i32 divisor) noexcept
{
    const i32 remainder = value % divisor;
    return static_cast<u32>(remainder < 0 ? remainder + divisor : remainder);
}

} // namespace

u64 digestOf(const VoxelChunk& chunk) noexcept
{
    if (!chunk.digestValid) {
        chunk.digest = XXH3_64bits(chunk.blocks, sizeof(chunk.blocks));
        chunk.digestValid = true;
    }
    return chunk.digest;
}

std::vector<core::u8> encodeVoxelChunk(const VoxelChunk& chunk)
{
    std::vector<core::u8> bytes;
    const auto put = [&bytes](core::u16 value) {
        bytes.push_back(static_cast<core::u8>(value & 0xFFu));
        bytes.push_back(static_cast<core::u8>(value >> 8u));
    };
    u32 at = 0;
    while (at < VoxelChunkVolume) {
        const BlockId id = chunk.blocks[at];
        u32 run = 1;
        // A run is capped at the u16 it is stored in; a whole chunk is 4096, so
        // the cap is never reached, and it is here so the format says so.
        while (at + run < VoxelChunkVolume && chunk.blocks[at + run] == id && run < 0xFFFFu)
            ++run;
        put(id);
        put(static_cast<core::u16>(run));
        at += run;
    }
    return bytes;
}

bool decodeVoxelChunk(std::span<const core::u8> bytes, std::vector<BlockId>& out)
{
    if (bytes.size() % 4 != 0)
        return false;
    std::vector<BlockId> blocks;
    blocks.reserve(VoxelChunkVolume);
    for (usize at = 0; at < bytes.size(); at += 4) {
        const auto id = static_cast<BlockId>(bytes[at] | (bytes[at + 1] << 8u));
        const auto run = static_cast<u32>(bytes[at + 2] | (bytes[at + 3] << 8u));
        if (run == 0 || blocks.size() + run > VoxelChunkVolume)
            return false;
        blocks.insert(blocks.end(), run, id);
    }
    if (blocks.size() != VoxelChunkVolume)
        return false;
    out = std::move(blocks);
    return true;
}

VoxelChunkKey voxelChunkOf(i32 x, i32 y, i32 z) noexcept
{
    const auto edge = static_cast<i32>(VoxelChunkEdge);
    return VoxelChunkKey{floorDivide(x, edge), floorDivide(y, edge), floorDivide(z, edge)};
}

const VoxelChunk* VoxelGrid::findChunk(VoxelChunkKey key) const noexcept
{
    const auto at = std::lower_bound(m_chunks.begin(), m_chunks.end(), key,
                                     [](const auto& entry, const VoxelChunkKey& probe) { return entry.first < probe; });
    return at != m_chunks.end() && at->first == key ? at->second.get() : nullptr;
}

BlockId VoxelGrid::get(i32 x, i32 y, i32 z) const noexcept
{
    const VoxelChunk* chunk = findChunk(voxelChunkOf(x, y, z));
    if (chunk == nullptr)
        return AirBlock;
    const auto edge = static_cast<i32>(VoxelChunkEdge);
    return chunk->blocks[voxelIndex(floorModulo(x, edge), floorModulo(y, edge), floorModulo(z, edge))];
}

VoxelChunk* VoxelGrid::chunkFor(VoxelChunkKey key)
{
    const auto at = std::lower_bound(m_chunks.begin(), m_chunks.end(), key,
                                     [](const auto& entry, const VoxelChunkKey& probe) { return entry.first < probe; });
    if (at != m_chunks.end() && at->first == key) {
        // Copy on write: a snapshot holding this chunk keeps the old one.
        if (at->second.use_count() > 1)
            at->second = std::make_shared<VoxelChunk>(*at->second);
        at->second->digestValid = false;
        return at->second.get();
    }
    return m_chunks.insert(at, {key, std::make_shared<VoxelChunk>()})->second.get();
}

void VoxelGrid::dropIfEmpty(VoxelChunkKey key)
{
    const auto at = std::lower_bound(m_chunks.begin(), m_chunks.end(), key,
                                     [](const auto& entry, const VoxelChunkKey& probe) { return entry.first < probe; });
    if (at != m_chunks.end() && at->first == key && at->second->solid == 0)
        m_chunks.erase(at);
}

bool VoxelGrid::set(i32 x, i32 y, i32 z, BlockId id)
{
    const VoxelChunkKey key = voxelChunkOf(x, y, z);
    const auto edge = static_cast<i32>(VoxelChunkEdge);
    const u32 index = voxelIndex(floorModulo(x, edge), floorModulo(y, edge), floorModulo(z, edge));
    // Read before writing, so an edit that changes nothing clones nothing and
    // an air write never creates a chunk.
    if (const VoxelChunk* existing = findChunk(key);
        existing == nullptr ? id == AirBlock : existing->blocks[index] == id)
        return false;

    VoxelChunk* chunk = chunkFor(key);
    const BlockId before = chunk->blocks[index];
    chunk->blocks[index] = id;
    if (before == AirBlock && id != AirBlock)
        ++chunk->solid;
    else if (before != AirBlock && id == AirBlock)
        --chunk->solid;
    if (chunk->solid == 0)
        dropIfEmpty(key);
    return true;
}

u32 VoxelGrid::fill(i32 minX, i32 minY, i32 minZ, i32 maxX, i32 maxY, i32 maxZ, BlockId id)
{
    if (minX > maxX)
        std::swap(minX, maxX);
    if (minY > maxY)
        std::swap(minY, maxY);
    if (minZ > maxZ)
        std::swap(minZ, maxZ);

    const auto edge = static_cast<i32>(VoxelChunkEdge);
    u32 changed = 0;
    // Chunk by chunk, so each touched chunk is cloned once and its counter
    // settled once.
    for (i32 cy = floorDivide(minY, edge); cy <= floorDivide(maxY, edge); ++cy) {
        for (i32 cz = floorDivide(minZ, edge); cz <= floorDivide(maxZ, edge); ++cz) {
            for (i32 cx = floorDivide(minX, edge); cx <= floorDivide(maxX, edge); ++cx) {
                const VoxelChunkKey key{cx, cy, cz};
                if (id == AirBlock && findChunk(key) == nullptr)
                    continue;
                VoxelChunk* chunk = chunkFor(key);
                const i32 lowX = std::max(minX, cx * edge);
                const i32 highX = std::min(maxX, cx * edge + edge - 1);
                const i32 lowY = std::max(minY, cy * edge);
                const i32 highY = std::min(maxY, cy * edge + edge - 1);
                const i32 lowZ = std::max(minZ, cz * edge);
                const i32 highZ = std::min(maxZ, cz * edge + edge - 1);
                for (i32 y = lowY; y <= highY; ++y) {
                    for (i32 z = lowZ; z <= highZ; ++z) {
                        for (i32 x = lowX; x <= highX; ++x) {
                            const u32 index =
                                voxelIndex(static_cast<u32>(x - cx * edge), static_cast<u32>(y - cy * edge),
                                           static_cast<u32>(z - cz * edge));
                            const BlockId before = chunk->blocks[index];
                            if (before == id)
                                continue;
                            chunk->blocks[index] = id;
                            if (before == AirBlock)
                                ++chunk->solid;
                            else if (id == AirBlock)
                                --chunk->solid;
                            ++changed;
                        }
                    }
                }
                if (chunk->solid == 0)
                    dropIfEmpty(key);
            }
        }
    }
    return changed;
}

std::vector<VoxelChunkKey> VoxelGrid::chunkKeys() const
{
    std::vector<VoxelChunkKey> keys;
    keys.reserve(m_chunks.size());
    for (const auto& entry : m_chunks)
        keys.push_back(entry.first);
    return keys;
}

void VoxelGrid::removeChunk(VoxelChunkKey key)
{
    const auto at = std::lower_bound(m_chunks.begin(), m_chunks.end(), key,
                                     [](const auto& entry, const VoxelChunkKey& probe) { return entry.first < probe; });
    if (at != m_chunks.end() && at->first == key)
        m_chunks.erase(at);
}

// One pass, for `TerrainField::shareFrom`'s reason.
void VoxelGrid::shareFrom(const VoxelGrid& from)
{
    if (from.m_chunks.empty())
        return;
    decltype(m_chunks) merged;
    merged.reserve(m_chunks.size() + from.m_chunks.size());
    auto held = m_chunks.begin();
    for (const auto& entry : from.m_chunks) {
        while (held != m_chunks.end() && held->first < entry.first)
            merged.push_back(std::move(*held++));
        if (held != m_chunks.end() && held->first == entry.first)
            continue;
        merged.push_back(entry);
    }
    while (held != m_chunks.end())
        merged.push_back(std::move(*held++));
    m_chunks = std::move(merged);
}

void VoxelGrid::removeAll(std::span<const VoxelChunkKey> keys)
{
    if (keys.empty())
        return;
    auto key = keys.begin();
    auto kept = m_chunks.begin();
    for (auto at = m_chunks.begin(); at != m_chunks.end(); ++at) {
        while (key != keys.end() && *key < at->first)
            ++key;
        if (key != keys.end() && *key == at->first)
            continue;
        if (kept != at)
            *kept = std::move(*at);
        ++kept;
    }
    m_chunks.erase(kept, m_chunks.end());
}

void VoxelGrid::setChunk(VoxelChunkKey key, std::span<const BlockId> blocks)
{
    if (blocks.size() != VoxelChunkVolume)
        return;
    auto chunk = std::make_shared<VoxelChunk>();
    std::copy(blocks.begin(), blocks.end(), chunk->blocks);
    chunk->solid =
        static_cast<u32>(std::count_if(blocks.begin(), blocks.end(), [](BlockId id) { return id != AirBlock; }));
    const auto at = std::lower_bound(m_chunks.begin(), m_chunks.end(), key,
                                     [](const auto& entry, const VoxelChunkKey& probe) { return entry.first < probe; });
    const bool exists = at != m_chunks.end() && at->first == key;
    if (chunk->solid == 0) {
        if (exists)
            m_chunks.erase(at);
        return;
    }
    if (exists)
        at->second = std::move(chunk);
    else
        m_chunks.insert(at, {key, std::move(chunk)});
}

std::optional<VoxelHit> raycastVoxels(const VoxelGrid& grid, core::f32 blockSize, const core::DVec3& origin,
                                      const core::Vec3& direction, core::f64 reach,
                                      std::span<const bool> passable) noexcept
{
    const auto stops = [&](i32 x, i32 y, i32 z) noexcept {
        const BlockId id = grid.get(x, y, z);
        if (id == AirBlock)
            return false;
        const BlockId type = blockTypeOf(id);
        return !(type < passable.size() && passable[type]);
    };
    const auto size = static_cast<core::f64>(blockSize);
    const core::f64 length = std::sqrt(static_cast<core::f64>(direction.x) * static_cast<core::f64>(direction.x) +
                                       static_cast<core::f64>(direction.y) * static_cast<core::f64>(direction.y) +
                                       static_cast<core::f64>(direction.z) * static_cast<core::f64>(direction.z));
    if (!(length > 0.0) || !(size > 0.0) || !(reach > 0.0))
        return std::nullopt;

    // In block units from here on.
    const std::array<core::f64, 3> start{origin.x / size, origin.y / size, origin.z / size};
    const std::array<core::f64, 3> step{static_cast<core::f64>(direction.x) / length,
                                        static_cast<core::f64>(direction.y) / length,
                                        static_cast<core::f64>(direction.z) / length};
    const core::f64 limit = reach / size;

    std::array<i32, 3> block{static_cast<i32>(std::floor(start[0])), static_cast<i32>(std::floor(start[1])),
                             static_cast<i32>(std::floor(start[2]))};
    std::array<i32, 3> stepSign{};
    std::array<core::f64, 3> nextBoundary{};
    std::array<core::f64, 3> delta{};
    for (usize axis = 0; axis < 3; ++axis) {
        if (step[axis] > 0.0) {
            stepSign[axis] = 1;
            nextBoundary[axis] = (static_cast<core::f64>(block[axis]) + 1.0 - start[axis]) / step[axis];
            delta[axis] = 1.0 / step[axis];
        }
        else if (step[axis] < 0.0) {
            stepSign[axis] = -1;
            nextBoundary[axis] = (start[axis] - static_cast<core::f64>(block[axis])) / -step[axis];
            delta[axis] = 1.0 / -step[axis];
        }
        else {
            nextBoundary[axis] = std::numeric_limits<core::f64>::infinity();
            delta[axis] = std::numeric_limits<core::f64>::infinity();
        }
    }

    if (stops(block[0], block[1], block[2]))
        return VoxelHit{block, {0, 0, 0}, 0.0};

    // A ray crosses at most one cell per axis per block of length, so this
    // bounds the walk by the reach -- and the hard cap stops a ray a million
    // blocks long stalling a tick.
    const auto steps = static_cast<i32>(std::min(3.0 * limit + 3.0, 65536.0));
    for (i32 taken = 0; taken < steps; ++taken) {
        usize axis = 0;
        if (nextBoundary[1] < nextBoundary[axis])
            axis = 1;
        if (nextBoundary[2] < nextBoundary[axis])
            axis = 2;
        if (nextBoundary[axis] > limit)
            break;
        const core::f64 along = nextBoundary[axis];
        block[axis] += stepSign[axis];
        nextBoundary[axis] += delta[axis];
        if (stops(block[0], block[1], block[2])) {
            std::array<i32, 3> face{0, 0, 0};
            face[axis] = -stepSign[axis];
            return VoxelHit{block, face, along * size};
        }
    }
    return std::nullopt;
}

u64 VoxelGrid::digest() const noexcept
{
    XXH3_state_t state;
    XXH3_64bits_reset(&state);
    for (const auto& entry : m_chunks) {
        const VoxelChunkKey& key = entry.first;
        XXH3_64bits_update(&state, &key, sizeof(key));
        const u64 chunkDigest = digestOf(*entry.second);
        XXH3_64bits_update(&state, &chunkDigest, sizeof(chunkDigest));
    }
    return XXH3_64bits_digest(&state);
}

} // namespace luaug::asset
