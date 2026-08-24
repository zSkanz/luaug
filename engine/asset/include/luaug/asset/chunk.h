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
inline constexpr u32 ChunkFormatVersion = 2;

// How many size classes a cell's `layer` may name (ADR 0053): 0 is detail,
// 1 is structures, 2 is terrain features. The field is an `i32` and always
// was -- this is what the partitioner writes into it and what the policy
// engine has a radius pair for, not a limit the format enforces.
inline constexpr i32 ChunkLayerCount = 3;

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

// An atomic model and its descendants, materialised together (ADR 0053, rule 3).
//
// **This is the one thing a cell holds that is not a flat list**, and it is
// deliberately one level deep: a group is a `Model` and the parts under it, and
// nothing else. A general nested-instance serialization is the scene format's
// job, and duplicating it in a payload read thousands of times per session is a
// cost with no caller.
//
// A model whose subtree holds anything a record cannot express -- a light, a
// script, another model -- is not a group. It stays in the scene, whole, and
// the partition report counts it. Half a model is worse than an authored one.
struct ChunkGroup
{
    // Index into `Chunk::strings`, or `ChunkInstance::NoString`.
    u32 name = 0xFFFFFFFFu;
};

// What a chunk creates. One record per instance, flat: a chunk's contents are a
// LIST rather than a tree, because a streamed world's instances are placed
// world content, and the one grouping a world genuinely needs is `ChunkGroup`
// above.
//
// **It carries everything a `BasePart` is, and that is not padding.** Until
// this format had a partitioner writing into it, the only author was a
// generator that wrote what the format had; an authored part has a collision
// group, a friction and a `CanCollide` that somebody chose, and a cell that
// dropped them would change the world on its way through the grid.
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
    // `Enum.CollisionFidelity` for a `MeshPart`; ignored for a `Part`.
    u8 collisionFidelity = 0;
    bool anchored = true;
    bool canCollide = true;
    bool canQuery = true;

    core::CFrameD cframe;
    core::Vec3 size{1.0f, 1.0f, 1.0f};
    core::Color3 color{1.0f, 1.0f, 1.0f};
    f32 transparency = 0.0f;
    f32 friction = 0.3f;
    f32 restitution = 0.0f;
    f32 density = 1.0f;

    // Indices into `Chunk::strings`; `NoString` for absent. An absent
    // `collisionGroup` is the world's `Default`, which is what a part that
    // never named one has.
    static constexpr u32 NoString = 0xFFFFFFFFu;
    u32 name = NoString;
    u32 meshContent = NoString;
    u32 collisionGroup = NoString;

    // The group this record belongs to, as an index into `Chunk::groups`.
    static constexpr u32 NoGroup = 0xFFFFFFFFu;
    u32 group = NoGroup;

    // This record's tags, as the range `[firstTag, firstTag + tagCount)` in
    // `Chunk::tagRefs`.
    //
    // **Tags are what makes a streamed world addressable at all** (ADR 0053,
    // rule 5). A path into `Workspace` is sometimes nil in a world that is not
    // all present, so `TagService:GetTagged` and its added and removed signals
    // are the documented way to find things -- and they can only answer for a
    // streamed instance if the cell remembers what it was tagged with.
    u32 firstTag = 0;
    u32 tagCount = 0;
};

struct Chunk
{
    ChunkId id;
    // In world coordinates, so the streaming manager can score a chunk without
    // decoding it -- the index carries a copy for exactly that reason.
    core::DAABB bounds;
    std::vector<ChunkInstance> instances;
    std::vector<ChunkGroup> groups;
    // Every record's tag list, concatenated; each record names a range. One
    // vector rather than a vector per record, because a record is a POD and a
    // heap allocation per instance is what this format exists not to have.
    std::vector<u32> tagRefs;
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

// The same ceiling, for the same reason, on the two arrays a record indexes
// into. A group per instance is already absurd and four tags per instance is
// already generous; what matters is that a corrupted count hits a named limit
// rather than an allocator.
inline constexpr u32 MaxChunkGroups = MaxChunkInstances;
inline constexpr u32 MaxChunkTagRefs = MaxChunkInstances * 4;

[[nodiscard]] std::vector<std::byte> encodeChunk(const Chunk& chunk);
[[nodiscard]] std::optional<core::EngineError> decodeChunk(std::span<const std::byte> bytes, Chunk& out);

// The world's own table of contents: which chunks exist, where they are, and
// what to ask `ContentMounts` for. Written by `assetc` beside the pack and read
// once at startup -- it is small (a row per chunk) where the payloads are not.
struct ChunkIndexEntry
{
    ChunkId id;
    // The cell's REAL extent in world space, which is not its footprint: an
    // atomic model lives in one cell however far it spreads, so a building that
    // overhangs is described by a box wider than the cell it is filed under and
    // is scored on that box. An index that named only the footprint would keep
    // the overhang out of the loading ring until the cell centre came in.
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
