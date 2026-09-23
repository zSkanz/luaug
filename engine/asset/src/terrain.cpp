#include "luaug/asset/terrain.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

#define XXH_INLINE_ALL
#include "xxhash.h"

namespace luaug::asset {
namespace {

using core::i32;
using core::u16;
using core::u32;
using core::u64;
using core::u8;
using core::usize;

constexpr u32 DenseFlag = 0x80000000u;
constexpr auto Edge = static_cast<i32>(ChunkEdge);

[[nodiscard]] bool isDense(u32 row) noexcept
{
    return (row & DenseFlag) != 0;
}

// The lower bound of a key in the sorted chunk list.
template <class Vector>
[[nodiscard]] auto lowerBound(Vector& entries, const ChunkKey& key)
{
    return std::lower_bound(entries.begin(), entries.end(), key,
                            [](const auto& entry, const ChunkKey& probe) { return entry.first < probe; });
}

} // namespace

u8 quantiseOccupancy(float fraction) noexcept
{
    if (!(fraction > 0.0f))
        return 0;
    if (fraction >= 1.0f)
        return FullOccupancy;
    return static_cast<u8>(std::lround(fraction * static_cast<float>(FullOccupancy)));
}

// --- TerrainChunk --------------------------------------------------------------

Voxel TerrainChunk::get(u32 x, u32 y, u32 z) const noexcept
{
    if (m_rows.empty())
        return unpackVoxel(m_value);
    const u32 row = m_rows[rowIndex(y, z)];
    if (!isDense(row))
        return unpackVoxel(static_cast<u16>(row));
    return unpackVoxel(m_dense[static_cast<usize>(row & ~DenseFlag) * ChunkEdge + x]);
}

void TerrainChunk::readRow(u32 y, u32 z, std::span<u16, ChunkEdge> out) const noexcept
{
    if (m_rows.empty()) {
        std::fill(out.begin(), out.end(), m_value);
        return;
    }
    const u32 row = m_rows[rowIndex(y, z)];
    if (!isDense(row)) {
        std::fill(out.begin(), out.end(), static_cast<u16>(row));
        return;
    }
    const u16* first = &m_dense[static_cast<usize>(row & ~DenseFlag) * ChunkEdge];
    std::copy(first, first + ChunkEdge, out.begin());
}

void TerrainChunk::invalidate() noexcept
{
    m_digestValid = false;
    m_bordersValid = 0;
    for (std::vector<u16>& level : m_mips)
        level.clear();
}

void TerrainChunk::expand()
{
    if (!m_rows.empty())
        return;
    m_rows.assign(ChunkRows, static_cast<u32>(m_value));
}

bool TerrainChunk::set(u32 x, u32 y, u32 z, Voxel voxel)
{
    const u16 packed = packVoxel(canonical(voxel));
    if (m_rows.empty()) {
        if (packed == m_value)
            return false;
        expand();
    }
    u32& row = m_rows[rowIndex(y, z)];
    if (!isDense(row)) {
        if (static_cast<u16>(row) == packed)
            return false;
        // The row becomes voxel by voxel, appended; `normalize` puts the dense
        // rows back in row order.
        const auto index = static_cast<u32>(m_dense.size() / ChunkEdge);
        m_dense.insert(m_dense.end(), ChunkEdge, static_cast<u16>(row));
        row = DenseFlag | index;
    }
    u16& slot = m_dense[static_cast<usize>(row & ~DenseFlag) * ChunkEdge + x];
    if (slot == packed)
        return false;
    slot = packed;
    invalidate();
    return true;
}

void TerrainChunk::writeRow(u32 y, u32 z, std::span<const u16, ChunkEdge> values)
{
    const u16 first = values[0];
    const bool same = std::all_of(values.begin(), values.end(), [first](u16 value) { return value == first; });
    if (m_rows.empty()) {
        if (same && first == m_value)
            return;
        expand();
    }
    u32& row = m_rows[rowIndex(y, z)];
    if (same) {
        // Left as a stale dense row if it was one; `normalize` drops it.
        row = static_cast<u32>(first);
    }
    else {
        if (!isDense(row)) {
            const auto index = static_cast<u32>(m_dense.size() / ChunkEdge);
            m_dense.insert(m_dense.end(), ChunkEdge, 0);
            row = DenseFlag | index;
        }
        std::copy(values.begin(), values.end(), m_dense.begin() + static_cast<std::ptrdiff_t>(row & ~DenseFlag) * Edge);
    }
    invalidate();
}

void TerrainChunk::normalize()
{
    if (m_rows.empty())
        return;
    std::vector<u16> dense;
    dense.reserve(m_dense.size());
    bool allSame = true;
    u32 firstValue = 0xFFFFFFFFu;
    for (u32& row : m_rows) {
        if (isDense(row)) {
            const u16* voxels = &m_dense[static_cast<usize>(row & ~DenseFlag) * ChunkEdge];
            const u16 head = voxels[0];
            if (std::all_of(voxels, voxels + ChunkEdge, [head](u16 value) { return value == head; })) {
                row = static_cast<u32>(head);
            }
            else {
                const auto index = static_cast<u32>(dense.size() / ChunkEdge);
                dense.insert(dense.end(), voxels, voxels + ChunkEdge);
                row = DenseFlag | index;
            }
        }
        if (isDense(row)) {
            allSame = false;
        }
        else if (firstValue == 0xFFFFFFFFu) {
            firstValue = row;
        }
        else if (row != firstValue) {
            allSame = false;
        }
    }
    if (allSame) {
        m_value = static_cast<u16>(firstValue);
        m_rows.clear();
        m_rows.shrink_to_fit();
        m_dense.clear();
        m_dense.shrink_to_fit();
    }
    else {
        m_dense = std::move(dense);
    }
    // The bytes are the same voxels either way; only the digest's input moved.
    m_digestValid = false;
    m_bordersValid = 0;
}

usize TerrainChunk::bytes() const noexcept
{
    usize total = sizeof(TerrainChunk) + m_rows.capacity() * sizeof(u32) + m_dense.capacity() * sizeof(u16);
    for (const std::vector<u16>& level : m_mips)
        total += level.capacity() * sizeof(u16);
    return total;
}

u64 TerrainChunk::digest() const noexcept
{
    if (!m_digestValid) {
        // Over the canonical form: a uniform chunk is its value, and a rowed one
        // is its row table and its dense rows. Two chunks with the same voxels
        // only share a digest once both are normalised, which every write path
        // does before anything reads this.
        XXH3_state_t state;
        XXH3_64bits_reset(&state);
        const u8 kind = m_rows.empty() ? 0 : 1;
        XXH3_64bits_update(&state, &kind, 1);
        if (m_rows.empty()) {
            XXH3_64bits_update(&state, &m_value, sizeof(m_value));
        }
        else {
            XXH3_64bits_update(&state, m_rows.data(), m_rows.size() * sizeof(u32));
            XXH3_64bits_update(&state, m_dense.data(), m_dense.size() * sizeof(u16));
        }
        m_digest = XXH3_64bits_digest(&state);
        m_digestValid = true;
    }
    return m_digest;
}

u64 TerrainChunk::borderDigest(i32 dx, i32 dy, i32 dz) const noexcept
{
    if (dx < -1 || dx > 1 || dy < -1 || dy > 1 || dz < -1 || dz > 1)
        return 0;
    if (dx == 0 && dy == 0 && dz == 0)
        return digest();
    const auto slot = static_cast<u32>((dx + 1) + 3 * (dy + 1) + 9 * (dz + 1));
    if ((m_bordersValid & (1u << slot)) == 0) {
        XXH3_state_t state;
        XXH3_64bits_reset(&state);
        XXH3_64bits_update(&state, &slot, sizeof(slot));
        if (m_rows.empty()) {
            XXH3_64bits_update(&state, &m_value, sizeof(m_value));
        }
        else {
            // The layers the neighbour at that offset reads: the two against
            // it on an axis it is offset along -- low layers for a neighbour on
            // the low side -- and every layer on the others. Read voxel by
            // voxel in a fixed order, never the row table, so the answer does
            // not depend on how they happen to be stored.
            const auto range = [](i32 offset) {
                if (offset < 0)
                    return std::pair<u32, u32>{0, 2};
                if (offset > 0)
                    return std::pair<u32, u32>{ChunkEdge - 2, ChunkEdge};
                return std::pair<u32, u32>{0, ChunkEdge};
            };
            const auto [x0, x1] = range(dx);
            const auto [y0, y1] = range(dy);
            const auto [z0, z1] = range(dz);
            std::vector<u16> part;
            part.reserve(static_cast<usize>(x1 - x0) * (y1 - y0) * (z1 - z0));
            for (u32 y = y0; y < y1; ++y) {
                for (u32 z = z0; z < z1; ++z) {
                    for (u32 x = x0; x < x1; ++x)
                        part.push_back(packVoxel(get(x, y, z)));
                }
            }
            XXH3_64bits_update(&state, part.data(), part.size() * sizeof(u16));
        }
        m_borders[slot] = XXH3_64bits_digest(&state);
        m_bordersValid |= 1u << slot;
    }
    return m_borders[slot];
}

void TerrainChunk::prepareMip(u32 level) const
{
    if (level == 0 || level >= ChunkLevels || m_rows.empty() || !m_mips[level].empty())
        return;
    const u32 edge = ChunkEdge >> level;
    const u32 span = 1u << level;
    std::vector<u16>& out = m_mips[level];
    out.assign(static_cast<usize>(edge) * edge * edge, 0);
    // Sums per level cell, read row by row so the rows' own storage is walked
    // once in order.
    std::vector<u32> sums(out.size(), 0);
    std::vector<u8> best(out.size(), 0);
    std::vector<u8> material(out.size(), 0);
    std::array<u16, ChunkEdge> row{};
    for (u32 y = 0; y < ChunkEdge; ++y) {
        for (u32 z = 0; z < ChunkEdge; ++z) {
            readRow(y, z, row);
            const usize base = (static_cast<usize>(y / span) * edge + z / span) * edge;
            for (u32 x = 0; x < ChunkEdge; ++x) {
                const Voxel voxel = unpackVoxel(row[x]);
                const usize cell = base + x / span;
                sums[cell] += voxel.occupancy;
                // The fullest voxel's material, the first one met on a tie: the
                // walk order is fixed, so so is the answer.
                if (voxel.occupancy > best[cell]) {
                    best[cell] = voxel.occupancy;
                    material[cell] = voxel.material;
                }
            }
        }
    }
    const u32 count = span * span * span;
    for (usize at = 0; at < out.size(); ++at) {
        const auto occupancy = static_cast<u8>((sums[at] + count / 2) / count);
        out[at] = packVoxel(canonical(Voxel{occupancy, material[at]}));
    }
}

Voxel TerrainChunk::mip(u32 level, u32 x, u32 y, u32 z) const noexcept
{
    if (level == 0)
        return get(x, y, z);
    if (m_rows.empty())
        return unpackVoxel(m_value);
    if (m_mips[level].empty())
        prepareMip(level);
    const u32 edge = ChunkEdge >> level;
    return unpackVoxel(m_mips[level][(static_cast<usize>(y) * edge + z) * edge + x]);
}

// --- TerrainField --------------------------------------------------------------

i32 TerrainField::voxelIndex(double metres) const noexcept
{
    return static_cast<i32>(std::floor(metres / static_cast<double>(m_settings.voxelSize)));
}

const TerrainChunk* TerrainField::findChunk(ChunkKey key) const noexcept
{
    const auto at = lowerBound(m_chunks, key);
    return (at != m_chunks.end() && at->first == key) ? at->second.get() : nullptr;
}

Voxel TerrainField::voxel(i32 x, i32 y, i32 z) const noexcept
{
    const TerrainChunk* chunk = findChunk(chunkOf(x, y, z));
    if (chunk == nullptr)
        return Voxel{};
    return chunk->get(static_cast<u32>(floorMod(x, Edge)), static_cast<u32>(floorMod(y, Edge)),
                      static_cast<u32>(floorMod(z, Edge)));
}

FieldSample TerrainField::sample(i32 x, i32 y, i32 z) const noexcept
{
    const Voxel got = voxel(x, y, z);
    return FieldSample{(0.5f - occupancyOf(got)) * RampVoxels * m_settings.voxelSize, got.material};
}

Voxel TerrainField::voxelAt(u32 level, i32 x, i32 y, i32 z) const noexcept
{
    if (level == 0)
        return voxel(x, y, z);
    const i32 edge = Edge >> level;
    const TerrainChunk* chunk = findChunk(ChunkKey{floorDiv(x, edge), floorDiv(y, edge), floorDiv(z, edge)});
    if (chunk == nullptr)
        return Voxel{};
    return chunk->mip(level, static_cast<u32>(floorMod(x, edge)), static_cast<u32>(floorMod(y, edge)),
                      static_cast<u32>(floorMod(z, edge)));
}

std::vector<ChunkKey> TerrainField::chunkKeys() const
{
    std::vector<ChunkKey> keys;
    keys.reserve(m_chunks.size());
    for (const Entry& entry : m_chunks)
        keys.push_back(entry.first);
    return keys;
}

std::span<const TerrainField::Entry> TerrainField::column(i32 chunkX, i32 chunkZ) const noexcept
{
    const ChunkKey low{chunkX, std::numeric_limits<i32>::min(), chunkZ};
    const auto first = lowerBound(m_chunks, low);
    auto last = first;
    while (last != m_chunks.end() && last->first.x == chunkX && last->first.z == chunkZ)
        ++last;
    return std::span<const Entry>(first, last);
}

std::optional<float> TerrainField::columnTop(i32 x, i32 z) const noexcept
{
    const std::span<const Entry> chunks = column(floorDiv(x, Edge), floorDiv(z, Edge));
    const auto localX = static_cast<u32>(floorMod(x, Edge));
    const auto localZ = static_cast<u32>(floorMod(z, Edge));
    const float voxel = m_settings.voxelSize;
    // Walked top down: the first voxel at least half full is the top, and the
    // crossing is between it and the one above it.
    float above = 0.0f; // Occupancy of the voxel above the one being read.
    for (auto at = chunks.rbegin(); at != chunks.rend(); ++at) {
        const TerrainChunk& chunk = *at->second;
        const i32 baseY = at->first.y * Edge;
        // A chunk directly under a gap in the column starts from air above.
        const auto next = at.base();
        if (next == chunks.end() || next->first.y != at->first.y + 1)
            above = 0.0f;
        if (chunk.uniform() && chunk.value().occupancy < 128) {
            above = occupancyOf(chunk.value());
            continue;
        }
        for (i32 y = Edge - 1; y >= 0; --y) {
            const float here = occupancyOf(chunk.get(localX, static_cast<u32>(y), localZ));
            if (here >= 0.5f) {
                // Between this centre and the one above, where occupancy
                // passes one half.
                const float t = here - above > 1e-6f ? (here - 0.5f) / (here - above) : 0.0f;
                return (static_cast<float>(baseY + y) + 0.5f + t) * voxel;
            }
            above = here;
        }
    }
    return std::nullopt;
}

std::optional<float> TerrainField::columnBottom(i32 x, i32 z) const noexcept
{
    const std::span<const Entry> chunks = column(floorDiv(x, Edge), floorDiv(z, Edge));
    const auto localX = static_cast<u32>(floorMod(x, Edge));
    const auto localZ = static_cast<u32>(floorMod(z, Edge));
    const float voxel = m_settings.voxelSize;
    // `columnTop` upside down: walked bottom up, the first voxel at least half
    // full is the bottom, and the crossing is between it and the one below.
    float below = 0.0f;
    for (auto at = chunks.begin(); at != chunks.end(); ++at) {
        const TerrainChunk& chunk = *at->second;
        const i32 baseY = at->first.y * Edge;
        if (at == chunks.begin() || std::prev(at)->first.y != at->first.y - 1)
            below = 0.0f;
        if (chunk.uniform() && chunk.value().occupancy < 128) {
            below = occupancyOf(chunk.value());
            continue;
        }
        for (i32 y = 0; y < Edge; ++y) {
            const float here = occupancyOf(chunk.get(localX, static_cast<u32>(y), localZ));
            if (here >= 0.5f) {
                const float t = here - below > 1e-6f ? (here - 0.5f) / (here - below) : 0.0f;
                return (static_cast<float>(baseY + y) + 0.5f - t) * voxel;
            }
            below = here;
        }
    }
    return std::nullopt;
}

u64 TerrainField::digest() const noexcept
{
    XXH3_state_t state;
    XXH3_64bits_reset(&state);
    for (const Entry& entry : m_chunks) {
        XXH3_64bits_update(&state, &entry.first, sizeof(entry.first));
        const u64 chunk = entry.second->digest();
        XXH3_64bits_update(&state, &chunk, sizeof(chunk));
    }
    return XXH3_64bits_digest(&state);
}

usize TerrainField::bytes() const noexcept
{
    usize total = m_chunks.capacity() * sizeof(Entry);
    for (const Entry& entry : m_chunks)
        total += entry.second->bytes();
    return total;
}

TerrainChunk* TerrainField::chunkFor(ChunkKey key)
{
    const auto at = lowerBound(m_chunks, key);
    if (at != m_chunks.end() && at->first == key) {
        if (at->second.use_count() > 1)
            at->second = std::make_shared<TerrainChunk>(*at->second);
        return at->second.get();
    }
    return m_chunks.insert(at, {key, std::make_shared<TerrainChunk>()})->second.get();
}

void TerrainField::finishChunk(ChunkKey key)
{
    const auto at = lowerBound(m_chunks, key);
    if (at == m_chunks.end() || at->first != key)
        return;
    at->second->normalize();
    if (at->second->empty())
        m_chunks.erase(at);
}

bool TerrainField::setVoxel(i32 x, i32 y, i32 z, Voxel voxel)
{
    const ChunkKey key = chunkOf(x, y, z);
    if (canonical(voxel) == Voxel{} && findChunk(key) == nullptr)
        return false;
    TerrainChunk* chunk = chunkFor(key);
    const bool changed = chunk->set(static_cast<u32>(floorMod(x, Edge)), static_cast<u32>(floorMod(y, Edge)),
                                    static_cast<u32>(floorMod(z, Edge)), voxel);
    finishChunk(key);
    return changed;
}

void TerrainField::setChunk(ChunkKey key, std::shared_ptr<TerrainChunk> chunk)
{
    const auto at = lowerBound(m_chunks, key);
    const bool exists = at != m_chunks.end() && at->first == key;
    if (chunk == nullptr || chunk->empty()) {
        if (exists)
            m_chunks.erase(at);
        return;
    }
    if (exists)
        at->second = std::move(chunk);
    else
        m_chunks.insert(at, {key, std::move(chunk)});
}

void TerrainField::removeChunk(ChunkKey key)
{
    const auto at = lowerBound(m_chunks, key);
    if (at != m_chunks.end() && at->first == key)
        m_chunks.erase(at);
}

void TerrainField::shareFrom(const TerrainField& from)
{
    // One merge of the two sorted lists rather than an insertion per chunk:
    // a cell streamed into a large field would otherwise shift the tail of the
    // vector once per chunk it brings.
    if (from.m_chunks.empty())
        return;
    std::vector<Entry> merged;
    merged.reserve(m_chunks.size() + from.m_chunks.size());
    auto held = m_chunks.begin();
    for (const Entry& entry : from.m_chunks) {
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

void TerrainField::removeAll(std::span<const ChunkKey> keys)
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

void TerrainField::setHeightRange(float minHeight, float maxHeight) noexcept
{
    if (!(maxHeight > minHeight))
        return;
    m_settings.minHeight = minHeight;
    m_settings.maxHeight = maxHeight;
}

// --- FieldWriter ---------------------------------------------------------------

bool FieldWriter::set(i32 x, i32 y, i32 z, Voxel voxel)
{
    const ChunkKey key = chunkOf(x, y, z);
    if (m_last == nullptr || !(key == m_lastKey)) {
        const bool air = canonical(voxel) == Voxel{};
        if (air && m_field.findChunk(key) == nullptr)
            return false;
        m_last = m_field.chunkFor(key);
        m_lastKey = key;
        const auto at = std::lower_bound(m_touched.begin(), m_touched.end(), key);
        if (at == m_touched.end() || !(*at == key))
            m_touched.insert(at, key);
    }
    const bool changed = m_last->set(static_cast<u32>(floorMod(x, Edge)), static_cast<u32>(floorMod(y, Edge)),
                                     static_cast<u32>(floorMod(z, Edge)), voxel);
    if (changed)
        m_changed += 1;
    return changed;
}

void FieldWriter::finish()
{
    // `chunkFor` may have inserted chunks and moved the others, so the cached
    // pointer is dropped before anything else.
    m_last = nullptr;
    for (const ChunkKey key : m_touched)
        m_field.finishChunk(key);
}

} // namespace luaug::asset
