// The chunked world payload, `.lchunk` (architecture.md §10, roadmap M7).
//
// One chunk is one cell of the uniform world grid and the instances inside it:
// what to create, where, and what it looks like. Deliberately NARROW — this is
// not a general scene serialization (api-design.md §2.6 keeps that deferred)
// and it is not a prefab format. It is the list architecture.md calls
// "serialized instance blobs", and the reason it can be narrow is that a chunk
// describes placed WORLD content rather than an authored asset.
//
// **A chunk is its own file, and that is a decision rather than an oversight.**
// A `.lpack` is read whole into memory at mount, which is exactly right for the
// meshes and textures a chunk REFERS to — one tree mesh shared by four hundred
// chunks should be resident once — and exactly wrong for the chunk payloads
// themselves, since a world bigger than memory cannot have its instance lists
// resident. So chunks resolve through `ContentMounts` like anything else and
// are read one at a time with `platform::readFileAsync`, which is the path
// architecture.md §10 describes: an IO read at the chunk's priority, a decode on
// a worker, and a budgeted materialisation at FrameStart.
//
// Ranged reads INTO a pack are the obvious later refinement and are named here
// rather than left to be rediscovered: they would let a chunk live in the pack
// without the pack being resident, and they need an IO service that can read an
// offset, which this one deliberately cannot.
#pragma once

#include "luaug/core/error.h"
#include "luaug/core/math.h"
#include "luaug/core/types.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace luaug::asset {

using core::f32;
using core::i32;
using core::u32;
using core::u8;
using core::usize;

inline constexpr char ChunkMagic[4] = {'L', 'G', 'C', 'H'};
inline constexpr u32 ChunkFormatVersion = 1;

// The default cell edge in metres (architecture.md §10, "uniform world grid,
// default cell 256 m"). A property of the WORLD rather than of a chunk, so it
// is carried in the index below and every chunk in one world agrees.
inline constexpr f32 DefaultChunkSize = 256.0f;

// A cell of the grid. `layer` exists from day one because architecture.md's
// `ChunkId` has it: a second layer is how interiors, or a lower level of detail
// of the same ground, get addressed without a second coordinate system.
struct ChunkId
{
    i32 x = 0;
    i32 z = 0;
    i32 layer = 0;

    [[nodiscard]] constexpr bool operator==(const ChunkId&) const noexcept = default;
    // Ordering so a chunk set can be a sorted vector rather than a hash set:
    // R10 forbids an unordered container's order reaching observable output,
    // and the order chunks materialise in is observable in the instance tree.
    [[nodiscard]] constexpr auto operator<=>(const ChunkId&) const noexcept = default;
};

// What a chunk creates. One record per instance, flat: a chunk's contents are a
// LIST rather than a tree, because a streamed world's instances are placed
// world content and anything that genuinely needs a hierarchy is a `Model`
// loaded through `AssetService` instead.
struct ChunkInstance
{
    enum class Kind : u8
    {
        Part,
        MeshPart,
    };

    Kind kind = Kind::Part;
    // `Enum.PartShape` for a `Part`; ignored for a `MeshPart`.
    u8 shape = 0;
    bool anchored = true;

    core::CFrameD cframe;
    core::Vec3 size{1.0f, 1.0f, 1.0f};
    core::Color3 color{1.0f, 1.0f, 1.0f};
    f32 transparency = 0.0f;

    // Indices into `Chunk::strings`; `NoString` for absent.
    static constexpr u32 NoString = 0xFFFFFFFFu;
    u32 name = NoString;
    u32 meshContent = NoString;
};

struct Chunk
{
    ChunkId id;
    // In world coordinates, so the streaming manager can score a chunk without
    // decoding it -- the index carries a copy for exactly that reason.
    core::DAABB bounds;
    std::vector<ChunkInstance> instances;
    std::vector<std::string> strings;

    [[nodiscard]] std::string_view stringAt(u32 index) const
    {
        return index < strings.size() ? std::string_view(strings[index]) : std::string_view{};
    }
};

// The most instances one chunk may declare. A ceiling for the same reason the
// mesh format has one: a corrupted count must hit a named limit rather than an
// allocator. Sixty-five thousand instances in a 256 m cell is already far past
// anything a frame budget could materialise.
inline constexpr u32 MaxChunkInstances = 65536;

[[nodiscard]] std::vector<std::byte> encodeChunk(const Chunk& chunk);
[[nodiscard]] std::optional<core::EngineError> decodeChunk(std::span<const std::byte> bytes, Chunk& out);

// The world's own table of contents: which chunks exist, where they are, and
// what to ask `ContentMounts` for. Written by `assetc` beside the pack and read
// once at startup -- it is small (a row per chunk) where the payloads are not.
struct ChunkIndexEntry
{
    ChunkId id;
    core::DAABB bounds;
    // `asset://world/chunk_0_0.lchunk`, resolved through the mounts.
    std::string urn;
    u32 instanceCount = 0;
    u32 bytes = 0;
};

struct ChunkIndex
{
    f32 chunkSize = DefaultChunkSize;
    // Sorted by `ChunkId`, which is what makes a lookup a binary search and the
    // file a function of its contents.
    std::vector<ChunkIndexEntry> chunks;

    [[nodiscard]] const ChunkIndexEntry* find(ChunkId id) const noexcept;
};

// JSON, because this one is read once at startup, is a few hundred rows, and is
// the file a person debugging a world will open. The payloads it points at are
// binary for the opposite reasons.
[[nodiscard]] std::string writeChunkIndex(const ChunkIndex& index);
[[nodiscard]] std::optional<core::EngineError> readChunkIndex(std::string_view json, ChunkIndex& out);

// The cell a world position falls in.
[[nodiscard]] ChunkId chunkIdAt(core::DVec3 position, f32 chunkSize, i32 layer = 0) noexcept;
[[nodiscard]] core::DAABB chunkBounds(ChunkId id, f32 chunkSize) noexcept;

} // namespace luaug::asset
