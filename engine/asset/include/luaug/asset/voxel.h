#pragma once

// A world made of blocks (V1, `VoxelService`).
//
// **This is not the terrain.** `Terrain` (ADR 0067) is a signed-distance field
// meshed into a smooth surface; this is a grid of cubes, each a block TYPE, and
// its surface is axis-aligned faces. The two share nothing but the word "voxel"
// and the copy-on-write trick below, and the owner said so in as many words.
//
// **Chunks of 16 x 16 x 16 blocks, with a y.** A block world is as tall as it is
// wide -- a mine goes down, a tower goes up -- so unlike the streaming grid's
// `ChunkId` the key has three coordinates. Sixteen cubed is 4,096 blocks and
// eight kilobytes of ids: small enough that editing one block re-meshes a
// cheap region, large enough that a chunk's mesh is a useful draw.
//
// **Immutable and shared, which keeps undo affordable**, exactly as the terrain
// does it: a chunk is a `shared_ptr`, a snapshot copies pointers, and an edit
// clones only the chunk it touches.

#include "luaug/core/math.h"
#include "luaug/core/types.h"

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace luaug::asset {

// A block type's number. Zero is air, and is never stored in a chunk as anything
// but the absence of a block.
using BlockId = core::u16;
inline constexpr BlockId AirBlock = 0;

inline constexpr core::u32 VoxelChunkEdge = 16;
inline constexpr core::u32 VoxelChunkVolume = VoxelChunkEdge * VoxelChunkEdge * VoxelChunkEdge;

struct VoxelChunkKey
{
    core::i32 x = 0;
    core::i32 y = 0;
    core::i32 z = 0;

    [[nodiscard]] constexpr auto operator<=>(const VoxelChunkKey&) const noexcept = default;
    [[nodiscard]] constexpr bool operator==(const VoxelChunkKey&) const noexcept = default;
};

// One chunk. Indexed `(y * edge + z) * edge + x`, so a column of blocks is a
// stride and a horizontal slice is contiguous -- the layout the mesher's sweeps
// read in.
struct VoxelChunk
{
    BlockId blocks[VoxelChunkVolume] = {};
    // How many blocks are not air, kept as edits happen so an emptied chunk can
    // be dropped without a scan.
    core::u32 solid = 0;

    // xxh3 of the blocks, computed when first asked for and invalidated by any
    // write. Read it through `digestOf`.
    mutable core::u64 digest = 0;
    mutable bool digestValid = false;
};

[[nodiscard]] core::u64 digestOf(const VoxelChunk& chunk) noexcept;

[[nodiscard]] constexpr core::u32 voxelIndex(core::u32 x, core::u32 y, core::u32 z) noexcept
{
    return (y * VoxelChunkEdge + z) * VoxelChunkEdge + x;
}

// The whole block world: a sorted set of chunks, and nothing else. No hash map
// (R10): what an iteration order decides here -- the mesh order, the save file,
// the world hash -- must be a fact about the blocks, not the allocator.
class VoxelGrid
{
public:
    // The block at a block coordinate. Air where no chunk is.
    [[nodiscard]] BlockId get(core::i32 x, core::i32 y, core::i32 z) const noexcept;

    // Writes one block and answers whether anything changed. Setting air in a
    // chunk that then holds nothing drops the chunk.
    bool set(core::i32 x, core::i32 y, core::i32 z, BlockId id);

    // Fills a box of blocks, inclusive of both corners, and answers how many
    // changed. Clones each chunk it touches once, not once per block.
    core::u32 fill(core::i32 minX, core::i32 minY, core::i32 minZ, core::i32 maxX, core::i32 maxY, core::i32 maxZ,
                   BlockId id);

    void clear() noexcept { m_chunks.clear(); }

    [[nodiscard]] const VoxelChunk* findChunk(VoxelChunkKey key) const noexcept;
    [[nodiscard]] core::usize chunkCount() const noexcept { return m_chunks.size(); }
    // Every chunk key, sorted.
    [[nodiscard]] std::vector<VoxelChunkKey> chunkKeys() const;

    // Replaces a chunk wholesale -- the load path. An all-air chunk is not kept.
    void setChunk(VoxelChunkKey key, std::span<const BlockId> blocks);

    // Drops a chunk, whatever it holds.
    void removeChunk(VoxelChunkKey key);

    // Takes every chunk of `from` this grid does not already hold, SHARING it:
    // the load path of a streamed cell, on `TerrainField::shareFrom`'s terms --
    // what is here wins, and the shared reference makes the first edit clone.
    void shareFrom(const VoxelGrid& from);

    // Drops every chunk named, in one pass. `keys` sorted.
    void removeAll(std::span<const VoxelChunkKey> keys);

    // xxh3 over every chunk's key and digest, in key order.
    [[nodiscard]] core::u64 digest() const noexcept;

private:
    // A chunk to write into, cloned if a snapshot shares it, created if absent.
    [[nodiscard]] VoxelChunk* chunkFor(VoxelChunkKey key);
    void dropIfEmpty(VoxelChunkKey key);

    std::vector<std::pair<VoxelChunkKey, std::shared_ptr<VoxelChunk>>> m_chunks;
};

// A chunk's blocks as bytes, run-length encoded: pairs of little-endian u16
// (id, run length), runs in index order. A chunk of ground is mostly a few long
// runs -- a flat floor is sixteen runs of air and stone per layer at worst -- so
// this is what a scene file and a streamed chunk carry rather than 8 KB of ids.
[[nodiscard]] std::vector<core::u8> encodeVoxelChunk(const VoxelChunk& chunk);

// The inverse. False when the bytes do not describe exactly one chunk -- a run
// past the end, a truncated pair -- in which case `out` is left untouched.
[[nodiscard]] bool decodeVoxelChunk(std::span<const core::u8> bytes, std::vector<BlockId>& out);

// Which chunk a block coordinate is in, and where inside it.
[[nodiscard]] VoxelChunkKey voxelChunkOf(core::i32 x, core::i32 y, core::i32 z) noexcept;

// The first solid block a ray meets, and the face it entered through.
struct VoxelHit
{
    std::array<core::i32, 3> block{};
    // The entered face's outward normal, one axis of +-1 -- or all zero when the
    // ray STARTED inside the block, which it then hits through no face at all.
    // `block + face` is where a block placed against this one goes.
    std::array<core::i32, 3> face{};
    // Metres along the ray to the face.
    core::f64 distance = 0.0;
};

// Walks the grid cell by cell (Amanatides and Woo's DDA), so the answer is exact
// -- the first block the ray actually crosses -- and costs one step per cell
// crossed rather than a sample every few centimetres. `direction` need not be
// unit length; `reach` is in metres. One implementation for the script's
// `Raycast` and the editor's block tool, so a tool and a game can never
// disagree about which block is under the pointer.
[[nodiscard]] std::optional<VoxelHit> raycastVoxels(const VoxelGrid& grid, core::f32 blockSize,
                                                    const core::DVec3& origin, const core::Vec3& direction,
                                                    core::f64 reach) noexcept;

} // namespace luaug::asset
