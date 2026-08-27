#include "luaug/asset/terrain.h"

#include <algorithm>
#include <cmath>

#define XXH_INLINE_ALL
#include "xxhash.h"

namespace luaug::asset {
namespace {

using core::i32;
using core::u32;
using core::u64;
using core::u8;
using core::usize;

// Which brick a lattice point falls in, and where inside it.
//
// **Floor division rather than truncation**, which is the bug this function
// exists to have exactly once: `-1 / 16` is `0` in C++ and the brick containing
// x = -1 is brick -1. A world with an origin in the middle of it has negative
// coordinates everywhere, so getting this wrong would put the left half of every
// cave in the wrong brick.
[[nodiscard]] i32 floorDiv(i32 value, i32 divisor) noexcept
{
    const i32 quotient = value / divisor;
    return (value % divisor != 0 && ((value < 0) != (divisor < 0))) ? quotient - 1 : quotient;
}

[[nodiscard]] i32 floorMod(i32 value, i32 divisor) noexcept
{
    const i32 remainder = value % divisor;
    return remainder < 0 ? remainder + divisor : remainder;
}

[[nodiscard]] u64 digestOf(const void* first, usize firstBytes, const void* second, usize secondBytes) noexcept
{
    XXH3_state_t state;
    XXH3_64bits_reset(&state);
    XXH3_64bits_update(&state, first, firstBytes);
    XXH3_64bits_update(&state, second, secondBytes);
    return XXH3_64bits_digest(&state);
}

// A sorted-vector lookup, which is what this container is instead of a hash map
// (R10: a hash map's iteration order is a fact about its allocator).
template <class Vector, class Key>
[[nodiscard]] auto findEntry(Vector& entries, const Key& key)
{
    const auto at = std::lower_bound(entries.begin(), entries.end(), key,
                                     [](const auto& entry, const Key& probe) { return entry.first < probe; });
    return (at != entries.end() && at->first == key) ? at : entries.end();
}

template <class Vector, class Key, class Value>
void insertOrReplace(Vector& entries, const Key& key, Value value)
{
    const auto at = std::lower_bound(entries.begin(), entries.end(), key,
                                     [](const auto& entry, const Key& probe) { return entry.first < probe; });
    if (at != entries.end() && at->first == key) {
        at->second = std::move(value);
        return;
    }
    entries.insert(at, {key, std::move(value)});
}

} // namespace

u8 quantiseDistance(float metres, float voxelSize) noexcept
{
    if (!(voxelSize > 0.0f)) {
        return SurfaceLevel;
    }
    // In voxels, then into the `u8` range centred on `SurfaceLevel`. The
    // saturating clamp is deliberate: the field is only ever read near the
    // surface, so a sample far from it saturates rather than wrapping, and a
    // saturated sample still has the right SIGN -- which is the only thing a
    // mesher asks of a distant one.
    const float voxels = metres / voxelSize;
    const float scaled = static_cast<float>(SurfaceLevel) + (voxels / DistanceRange) * static_cast<float>(SurfaceLevel);
    return static_cast<u8>(std::clamp(scaled, 0.0f, 255.0f));
}

float dequantiseDistance(u8 quantised, float voxelSize) noexcept
{
    const float voxels =
        ((static_cast<float>(quantised) - static_cast<float>(SurfaceLevel)) / static_cast<float>(SurfaceLevel)) *
        DistanceRange;
    return voxels * voxelSize;
}

std::shared_ptr<const HeightTile> makeHeightTile(std::span<const float> heights, std::span<const u8> materials)
{
    auto tile = std::make_shared<HeightTile>();
    const usize count = std::min<usize>(TileArea, heights.size());
    for (usize at = 0; at < count; ++at) {
        tile->height[at] = heights[at];
    }
    const usize materialCount = std::min<usize>(TileArea, materials.size());
    for (usize at = 0; at < materialCount; ++at) {
        tile->material[at] = materials[at];
    }
    // **Computed here and nowhere else**, which is why this is a free function
    // rather than a constructor: a tile built any other way would carry a zero
    // digest and hash as though it were empty.
    tile->digest = digestOf(tile->height, sizeof(tile->height), tile->material, sizeof(tile->material));
    return tile;
}

std::shared_ptr<const Brick> makeBrick(std::span<const u8> distances, std::span<const u8> materials)
{
    auto brick = std::make_shared<Brick>();
    const usize count = std::min<usize>(BrickVolume, distances.size());
    for (usize at = 0; at < count; ++at) {
        brick->sd[at] = distances[at];
    }
    const usize materialCount = std::min<usize>(BrickVolume, materials.size());
    for (usize at = 0; at < materialCount; ++at) {
        brick->material[at] = materials[at];
    }
    brick->digest = digestOf(brick->sd, sizeof(brick->sd), brick->material, sizeof(brick->material));
    return brick;
}

const HeightTile* TerrainField::findTile(TileKey key) const noexcept
{
    const auto at = findEntry(m_tiles, key);
    return at == m_tiles.end() ? nullptr : at->second.get();
}

const Brick* TerrainField::findBrick(BrickKey key) const noexcept
{
    const auto at = findEntry(m_bricks, key);
    return at == m_bricks.end() ? nullptr : at->second.get();
}

bool TerrainField::isBricked(i32 x, i32 z) const noexcept
{
    // A column is bricked when any brick covers it, at any height. Linear in the
    // bricks of that column rather than a separate marker set, because the
    // bricks ARE the state -- a second structure saying the same thing is a
    // second thing that can disagree after a load.
    const i32 brickX = floorDiv(x, static_cast<i32>(BrickEdge));
    const i32 brickZ = floorDiv(z, static_cast<i32>(BrickEdge));
    for (const auto& entry : m_bricks) {
        if (entry.first.x == brickX && entry.first.z == brickZ) {
            return true;
        }
    }
    return false;
}

FieldSample TerrainField::sample(i32 x, i32 y, i32 z) const noexcept
{
    // **The brick answers first when there is one.** That is the whole
    // resolution rule, and everything above this function is written so it never
    // needs to know which branch ran.
    const BrickKey brickKey{floorDiv(x, static_cast<i32>(BrickEdge)), floorDiv(y, static_cast<i32>(BrickEdge)),
                            floorDiv(z, static_cast<i32>(BrickEdge))};
    if (const Brick* brick = findBrick(brickKey); brick != nullptr) {
        const auto localX = static_cast<u32>(floorMod(x, static_cast<i32>(BrickEdge)));
        const auto localY = static_cast<u32>(floorMod(y, static_cast<i32>(BrickEdge)));
        const auto localZ = static_cast<u32>(floorMod(z, static_cast<i32>(BrickEdge)));
        const u32 index = (localY * BrickEdge + localZ) * BrickEdge + localX;
        return FieldSample{dequantiseDistance(brick->sd[index], m_settings.voxelSize), brick->material[index]};
    }

    // **`sd(p) = p.y - H(x, z)`**, which is the identity the whole hybrid rests
    // on: its vertical-edge crossing is at exactly `y = H`, and that is the
    // height grid's own vertex. So a Marching Cubes edge between a sample below
    // the surface and one above it lands on the height, not near it -- the seam
    // between the two encodings is an equality rather than a stitch.
    const TileKey tileKey{floorDiv(x, static_cast<i32>(TileEdge)), floorDiv(z, static_cast<i32>(TileEdge))};
    const HeightTile* tile = findTile(tileKey);
    if (tile == nullptr) {
        // No tile is air, not a hole. A field with nothing in it is a world with
        // no ground yet, and a mesher walking it finds no sign change and emits
        // nothing -- which is the right picture of an empty cell.
        return FieldSample{DistanceRange * m_settings.voxelSize, 0};
    }

    const auto localX = static_cast<u32>(floorMod(x, static_cast<i32>(TileEdge)));
    const auto localZ = static_cast<u32>(floorMod(z, static_cast<i32>(TileEdge)));
    const u32 index = localZ * TileEdge + localX;
    if (tile->material[index] == 0) {
        // **Material zero means there is no ground in this column** (D153), and
        // without this rule creating a tile creates a plane.
        //
        // A tile is 32 by 32 columns and is written one column at a time, so the
        // moment the first column of one is filled the other 1023 exist with a
        // height of zero and a material of zero. Reading that as a surface put a
        // flat plane at y = 0 across a thousand columns nobody touched -- and
        // made the first column written into a tile behave differently from
        // every column after it, which is how this was found.
        //
        // Zero is the right sentinel rather than a NaN or a separate mask:
        // material zero already means "nothing" to every other verb here --
        // `fillBall` erases with it and `paintBall` refuses it -- so the encoding
        // gains no new state, only a rule it was already implying.
        return FieldSample{DistanceRange * m_settings.voxelSize, 0};
    }
    const float height = tile->height[index];
    return FieldSample{static_cast<float>(y) * m_settings.voxelSize - height, tile->material[index]};
}

std::vector<TileKey> TerrainField::tileKeys() const
{
    std::vector<TileKey> keys;
    keys.reserve(m_tiles.size());
    for (const auto& entry : m_tiles) {
        keys.push_back(entry.first);
    }
    return keys;
}

std::vector<BrickKey> TerrainField::brickKeys() const
{
    std::vector<BrickKey> keys;
    keys.reserve(m_bricks.size());
    for (const auto& entry : m_bricks) {
        keys.push_back(entry.first);
    }
    return keys;
}

u64 TerrainField::digest() const noexcept
{
    // **O(objects), not O(bytes)**, which is the reason every tile and brick
    // carries a digest of its own. A cell of a hundred tiles is a hundred
    // eight-byte reads here rather than half a megabyte.
    //
    // The KEY goes in beside the digest: two fields holding the same tiles at
    // different coordinates are different fields, and a digest over contents
    // alone could not tell them apart.
    XXH3_state_t state;
    XXH3_64bits_reset(&state);
    for (const auto& entry : m_tiles) {
        XXH3_64bits_update(&state, &entry.first, sizeof(entry.first));
        XXH3_64bits_update(&state, &entry.second->digest, sizeof(u64));
    }
    for (const auto& entry : m_bricks) {
        XXH3_64bits_update(&state, &entry.first, sizeof(entry.first));
        XXH3_64bits_update(&state, &entry.second->digest, sizeof(u64));
    }
    return XXH3_64bits_digest(&state);
}

void TerrainField::setTile(TileKey key, std::span<const float> heights, std::span<const u8> materials)
{
    insertOrReplace(m_tiles, key, makeHeightTile(heights, materials));
}

void TerrainField::setBrick(BrickKey key, std::span<const u8> distances, std::span<const u8> materials)
{
    insertOrReplace(m_bricks, key, makeBrick(distances, materials));
}

void TerrainField::setHeightRange(float minHeight, float maxHeight) noexcept
{
    // Refused rather than clamped if it is not a range: an inverted one would
    // make every examination empty, and the symptom would be terrain that
    // cannot be sculpted rather than an error.
    if (!(maxHeight > minHeight))
        return;
    m_settings.minHeight = minHeight;
    m_settings.maxHeight = maxHeight;
}

void TerrainField::removeBrick(BrickKey key)
{
    const auto at = findEntry(m_bricks, key);
    if (at != m_bricks.end()) {
        m_bricks.erase(at);
    }
}

} // namespace luaug::asset
