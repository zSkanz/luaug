#pragma once

// The terrain: one sparse grid of voxels, each a material and an occupancy
// (ADR 0082).
//
// **The whole design in one paragraph.** Space is cut into cubes `VoxelSize`
// on a side. Each one says what it is made of and how full of it it is, from
// empty to full. The surface is wherever that fullness crosses one half, found
// by interpolating between neighbouring voxels -- so a ball carved out of a
// hillside lands where the brush put it rather than on the lattice. Caves,
// arches, overhangs and flat ground are all the same data, and every consumer
// -- the mesher, the colliders, the raycast, the brushes, the save file --
// asks the same grid.
//
// This replaced a hybrid (ADR 0067) that kept most ground as a height layer and
// switched columns to voxel bricks where it stopped being one. Every defect the
// owner found in it was at the join between the two, and there is no join here.
//
// **Voxel `i` covers `[i * v, (i + 1) * v)` and its sample sits at its centre**,
// `(i + 0.5) * v`. That is the convention every function below uses when it
// turns an index into metres, and `voxelCenter` is the one place that says so.
//
// **Immutable and shared, which is what keeps undo affordable.** `UndoStack`
// snapshots the whole world by value; a chunk is a `shared_ptr`, so a snapshot
// copies pointers, and an edit clones only the chunks it writes.

#include "luaug/core/math.h"
#include "luaug/core/types.h"

#include <array>
#include <compare>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace luaug::asset {

// --- Voxels ------------------------------------------------------------------

// A voxel that is entirely ground.
inline constexpr core::u8 FullOccupancy = 255;

// What one voxel holds.
//
// **Air is occupancy zero with material zero, always.** A voxel emptied by a
// dig keeps no memory of what it was made of: the digest is over bytes, and two
// equal worlds must hash equal. Every write path goes through `canonical`.
struct Voxel
{
    core::u8 occupancy = 0;
    core::u8 material = 0;

    [[nodiscard]] constexpr bool operator==(const Voxel&) const noexcept = default;
};

[[nodiscard]] constexpr Voxel canonical(Voxel voxel) noexcept
{
    return voxel.occupancy == 0 || voxel.material == 0 ? Voxel{} : voxel;
}

// Two bytes, occupancy low. What a chunk stores and what the file carries.
[[nodiscard]] constexpr core::u16 packVoxel(Voxel voxel) noexcept
{
    return static_cast<core::u16>(voxel.occupancy | (voxel.material << 8));
}

[[nodiscard]] constexpr Voxel unpackVoxel(core::u16 packed) noexcept
{
    return Voxel{static_cast<core::u8>(packed & 0xFF), static_cast<core::u8>(packed >> 8)};
}

// Occupancy as a fraction, and back. Rounded to nearest, so a round trip of a
// byte is exact.
[[nodiscard]] constexpr float occupancyOf(Voxel voxel) noexcept
{
    return static_cast<float>(voxel.occupancy) / static_cast<float>(FullOccupancy);
}

[[nodiscard]] core::u8 quantiseOccupancy(float fraction) noexcept;

// **How many voxels occupancy takes to go from full to empty across a surface.**
//
// Four, not one, and the difference is measured rather than preferred. A voxel's
// occupancy is written from a distance to the surface, and ground laid from a
// height -- a heightmap, a raised hill, a converted world -- knows only its
// VERTICAL distance. With a one-voxel ramp, the voxel beside one on a slope
// steeper than 45 degrees is already clamped full or empty, the crossing between
// them lands in the wrong place, and every hillside came out as a staircase of
// terraces a voxel high. Across four voxels the field stays linear between
// neighbours on slopes to 63 degrees, which puts every crossing exactly on the
// surface; `writeHeights` also divides by the slope, which covers the rest.
//
// What it costs is a few more rows of partial voxels under each surface, and a
// chunk of flat ground a few kilobytes larger. The reference platform ramps over
// one voxel and its hillsides show the terraces.
inline constexpr float RampVoxels = 4.0f;

// The occupancy of a voxel whose centre is `distance` metres from a surface,
// negative inside: one half on the surface, full at `RampVoxels / 2` voxels in,
// empty as far out. Every brush's shape goes through this.
[[nodiscard]] constexpr float rampOccupancy(double distance, double voxelSize) noexcept
{
    const double value = 0.5 - distance / (static_cast<double>(RampVoxels) * voxelSize);
    return static_cast<float>(value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value));
}

// --- Chunks ------------------------------------------------------------------

// A chunk is 32 voxels on a side: 32 m at the default metre voxel. The
// reference platform's storage chunk, and the size a column of the level-of-
// detail quadtree's leaves is.
inline constexpr core::u32 ChunkEdge = 32;
inline constexpr core::u32 ChunkRows = ChunkEdge * ChunkEdge;
inline constexpr core::u32 ChunkVolume = ChunkRows * ChunkEdge;

// **How deep ground laid on empty terrain goes**, in metres under the lowest
// surface a verb lays: `writeHeights`, `fillFlat` and a first raise on empty
// columns. Rounded down to a chunk boundary and never past the world's floor.
//
// A slab and not a pillar to the floor, and the owner is why: ground laid from
// the floor at -256 m stood as a tower wherever it met the air, and ground
// extended sideways with a brush stood as a different one beside it. A slab
// this deep has walls and a bottom of the same height all round, and room
// under the surface for any cave a person digs by hand.
inline constexpr float LaidDepth = 32.0f;

// Level 0 is the voxels themselves; level L averages 2^L on a side, down to one
// value for the whole chunk at level 5.
inline constexpr core::u32 ChunkLevels = 6;

// Deliberately `i32` and padding-free: hashed with `Hasher::pod`.
//
// **Sorted x, then z, then y**, not in declaration order, so one column of
// chunks is one contiguous run of a sorted list -- which is what `HeightAt`,
// the level-of-detail columns and the colliders walk.
struct ChunkKey
{
    core::i32 x = 0;
    core::i32 y = 0;
    core::i32 z = 0;

    [[nodiscard]] constexpr std::strong_ordering operator<=>(const ChunkKey& other) const noexcept
    {
        if (const auto order = x <=> other.x; order != 0)
            return order;
        if (const auto order = z <=> other.z; order != 0)
            return order;
        return y <=> other.y;
    }
    [[nodiscard]] constexpr bool operator==(const ChunkKey&) const noexcept = default;
};

// Floor division, which `-1 / 32` in C++ is not. Here once so a world with its
// origin in the middle does not put the left half of every cave in the wrong
// chunk.
[[nodiscard]] constexpr core::i32 floorDiv(core::i32 value, core::i32 divisor) noexcept
{
    const core::i32 quotient = value / divisor;
    return (value % divisor != 0 && ((value < 0) != (divisor < 0))) ? quotient - 1 : quotient;
}

[[nodiscard]] constexpr core::i32 floorMod(core::i32 value, core::i32 divisor) noexcept
{
    const core::i32 remainder = value % divisor;
    return remainder < 0 ? remainder + divisor : remainder;
}

[[nodiscard]] constexpr ChunkKey chunkOf(core::i32 x, core::i32 y, core::i32 z) noexcept
{
    constexpr auto edge = static_cast<core::i32>(ChunkEdge);
    return ChunkKey{floorDiv(x, edge), floorDiv(y, edge), floorDiv(z, edge)};
}

// One 32-cubed block of voxels, stored by rows.
//
// **A row is 32 voxels along x, at one (y, z)**, and it is either one value or
// 32 of them. Ground is mostly rows that are all ground or all air: flat ground
// at a metre is one row in thirty-two that is not uniform, and a chunk of it is
// about four kilobytes rather than sixty-four. The reference platform measured
// the same trick at a sixth of the memory. A chunk whose every row is the same
// single value is just that value, with no rows at all.
//
// **Mutable only through a `TerrainField`**, which clones a chunk another
// snapshot still holds before it writes. The lazy digest and mips are dropped
// by every write.
class TerrainChunk
{
public:
    TerrainChunk() = default;
    explicit TerrainChunk(Voxel fill) noexcept : m_value(packVoxel(canonical(fill))) {}

    [[nodiscard]] Voxel get(core::u32 x, core::u32 y, core::u32 z) const noexcept;

    // Writes one voxel, answering whether it changed. Leaves the chunk
    // un-normalised: call `normalize` when a batch of writes is done.
    bool set(core::u32 x, core::u32 y, core::u32 z, Voxel voxel);

    // The 32 voxels of one row, packed.
    void readRow(core::u32 y, core::u32 z, std::span<core::u16, ChunkEdge> out) const noexcept;
    // Replaces one row. Stored uniform when it is.
    void writeRow(core::u32 y, core::u32 z, std::span<const core::u16, ChunkEdge> row);

    // Collapses rows that became uniform, compacts the dense storage in row
    // order and collapses a chunk of one value. **Canonical afterwards**, so two
    // chunks holding the same voxels hold the same bytes -- which is what the
    // digest and the save file rely on.
    void normalize();

    [[nodiscard]] bool uniform() const noexcept { return m_rows.empty(); }
    // The one value, when `uniform`.
    [[nodiscard]] Voxel value() const noexcept { return unpackVoxel(m_value); }
    [[nodiscard]] bool empty() const noexcept { return uniform() && m_value == 0; }

    // How many rows are stored voxel by voxel. For the memory figures and tests.
    [[nodiscard]] core::usize denseRows() const noexcept { return m_dense.size() / ChunkEdge; }
    [[nodiscard]] core::usize bytes() const noexcept;

    // xxh3 over the canonical form. Lazy, and dropped by any write.
    [[nodiscard]] core::u64 digest() const noexcept;

    // **The digest of the part of this chunk a neighbour's mesh reads.** The
    // mesher reaches two voxels past a region's side, so a chunk at offset
    // (`dx`, `dy`, `dz`) from this one -- each -1, 0 or 1 -- reads the two
    // layers of this chunk against it on every axis it is offset along, and all
    // of it on the others: a face, an edge or a corner. A mesh keyed on these
    // rather than on its neighbours' whole digests is not rebuilt for an edit
    // in the middle of the chunk next door. (0, 0, 0) is `digest`. Lazy.
    [[nodiscard]] core::u64 borderDigest(core::i32 dx, core::i32 dy, core::i32 dz) const noexcept;

    // **The level-`level` voxel at a level-`level` coordinate**, each axis in
    // `[0, 32 >> level)`. Level 0 is `get`. Above it, the occupancy is the mean
    // of the `2^level` cube underneath and the material is its fullest voxel's
    // -- so a distant wall a voxel thick thins rather than vanishing, which a
    // point sample at a stride would do.
    //
    // Computed the first time a level is asked for and kept until a write.
    // **Not thread-safe**: a caller meshing on workers calls `prepareMip`
    // first, on one thread.
    [[nodiscard]] Voxel mip(core::u32 level, core::u32 x, core::u32 y, core::u32 z) const noexcept;
    void prepareMip(core::u32 level) const;

private:
    [[nodiscard]] core::u32 rowIndex(core::u32 y, core::u32 z) const noexcept { return y * ChunkEdge + z; }
    void expand();
    void invalidate() noexcept;

    // Uniform: `m_rows` empty and `m_value` the value. Otherwise one entry per
    // row: bit 31 set means dense, and the low bits index `m_dense` in whole
    // rows; clear means the low sixteen bits are the row's one packed value.
    core::u16 m_value = 0;
    std::vector<core::u32> m_rows;
    std::vector<core::u16> m_dense;

    mutable core::u64 m_digest = 0;
    mutable bool m_digestValid = false;
    mutable std::array<core::u64, 27> m_borders{};
    mutable core::u32 m_bordersValid = 0;
    mutable std::array<std::vector<core::u16>, ChunkLevels> m_mips;
};

// --- The field ---------------------------------------------------------------

struct FieldSettings
{
    // The side of one voxel, in metres.
    float voxelSize = 1.0f;

    // **The world's floor and ceiling, in metres.** No write lands outside
    // them, and a column with no ground is filled from the floor by the verbs
    // that lay ground rather than add to it (`fillFlat`, `writeHeights`,
    // `raiseBall` on empty ground).
    float minHeight = -256.0f;
    float maxHeight = 256.0f;
};

// A sample of the field: a signed distance in metres, and what it is made of.
//
// **The distance is `(0.5 - occupancy) * RampVoxels * VoxelSize`**: negative
// inside, zero on the surface, and a distance only within the ramp round it.
// That is all the mesher and the raycast need -- they look for where it changes
// sign -- and it keeps one type between them and a grid that stores fullness.
struct FieldSample
{
    float distance = 0.0f;
    core::u8 material = 0;
};

class TerrainField
{
public:
    using Entry = std::pair<ChunkKey, std::shared_ptr<TerrainChunk>>;

    TerrainField() = default;
    explicit TerrainField(FieldSettings settings) noexcept : m_settings(settings) {}

    [[nodiscard]] const FieldSettings& settings() const noexcept { return m_settings; }

    // Where voxel `index` is centred on one axis, in the field's own metres.
    [[nodiscard]] double voxelCenter(core::i32 index) const noexcept
    {
        return (static_cast<double>(index) + 0.5) * static_cast<double>(m_settings.voxelSize);
    }
    // The voxel a coordinate in metres falls in, on one axis.
    [[nodiscard]] core::i32 voxelIndex(double metres) const noexcept;

    // --- Reading ---------------------------------------------------------

    [[nodiscard]] Voxel voxel(core::i32 x, core::i32 y, core::i32 z) const noexcept;
    [[nodiscard]] FieldSample sample(core::i32 x, core::i32 y, core::i32 z) const noexcept;

    // A voxel of level `level`, at a level-`level` coordinate (the level-0
    // index divided by `2^level`). What the level-of-detail mesher reads.
    [[nodiscard]] Voxel voxelAt(core::u32 level, core::i32 x, core::i32 y, core::i32 z) const noexcept;

    [[nodiscard]] const TerrainChunk* findChunk(ChunkKey key) const noexcept;
    [[nodiscard]] core::usize chunkCount() const noexcept { return m_chunks.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_chunks.empty(); }
    [[nodiscard]] std::span<const Entry> chunks() const noexcept { return m_chunks; }
    // Every key, sorted.
    [[nodiscard]] std::vector<ChunkKey> chunkKeys() const;
    // The chunks of one column, lowest first.
    [[nodiscard]] std::span<const Entry> column(core::i32 chunkX, core::i32 chunkZ) const noexcept;

    // **The top of the ground in one voxel column**, in the field's metres: the
    // highest place its occupancy crosses one half, going up. Nothing where the
    // column holds no ground at all. Caves under it do not change the answer.
    [[nodiscard]] std::optional<float> columnTop(core::i32 x, core::i32 z) const noexcept;
    // The bottom of the ground in one voxel column: the lowest place occupancy
    // crosses one half, going down. Nothing where the column holds no ground.
    [[nodiscard]] std::optional<float> columnBottom(core::i32 x, core::i32 z) const noexcept;

    // xxh3 over every chunk's key and digest, in key order.
    [[nodiscard]] core::u64 digest() const noexcept;

    // Bytes held by every chunk. Shared chunks are counted in full.
    [[nodiscard]] core::usize bytes() const noexcept;

    // --- Writing ---------------------------------------------------------
    //
    // A write clones the chunk it touches when another snapshot holds it, and
    // leaves every other chunk shared.

    // One voxel. Normalises its chunk afterwards, so this is for single writes;
    // a brush goes through `FieldWriter`.
    bool setVoxel(core::i32 x, core::i32 y, core::i32 z, Voxel voxel);

    // A chunk to write into, cloned if shared and created as air if absent.
    // **Valid until the next call that inserts or removes a chunk.** Whoever
    // writes through it calls `finishChunk` afterwards.
    [[nodiscard]] TerrainChunk* chunkFor(ChunkKey key);
    // Normalises a written chunk, and drops it when it is all air.
    void finishChunk(ChunkKey key);

    // Replaces a chunk wholesale -- the load path. An empty chunk is dropped.
    void setChunk(ChunkKey key, std::shared_ptr<TerrainChunk> chunk);
    void removeChunk(ChunkKey key);

    // **Takes every chunk of `from` this field does not already hold, SHARING
    // it** -- the load path of a streamed cell. What this field holds wins,
    // because it is newer; and because the chunk is shared, whoever keeps
    // `from` keeps a reference, so the first edit here clones it. That is how
    // an evicting streamer tells a cell somebody changed from one nobody did.
    void shareFrom(const TerrainField& from);

    // Drops every chunk named, in one pass. `keys` sorted.
    void removeAll(std::span<const ChunkKey> keys);

    // Widens or narrows the world's floor and ceiling. Resamples nothing.
    void setHeightRange(float minHeight, float maxHeight) noexcept;

private:
    FieldSettings m_settings;
    // Sorted by key. Never a hash map (R10): the mesher, the save file and the
    // world hash all walk this, and the walk must be a fact about the world.
    std::vector<Entry> m_chunks;
};

// A batch of voxel writes: caches the chunk it is in, and normalises every
// chunk it touched when it is done. **Every brush writes through one**, which
// is what makes a stroke clone each chunk once and leave it canonical.
class FieldWriter
{
public:
    explicit FieldWriter(TerrainField& field) noexcept : m_field(field) {}
    ~FieldWriter() { finish(); }
    FieldWriter(const FieldWriter&) = delete;
    FieldWriter& operator=(const FieldWriter&) = delete;

    [[nodiscard]] Voxel get(core::i32 x, core::i32 y, core::i32 z) const noexcept { return m_field.voxel(x, y, z); }
    // Answers whether the voxel changed.
    bool set(core::i32 x, core::i32 y, core::i32 z, Voxel voxel);

    // Normalises and drops what the batch left empty. Idempotent.
    void finish();

    [[nodiscard]] core::u32 changed() const noexcept { return m_changed; }
    // The chunks written, sorted.
    [[nodiscard]] const std::vector<ChunkKey>& touched() const noexcept { return m_touched; }

private:
    TerrainField& m_field;
    std::vector<ChunkKey> m_touched;
    ChunkKey m_lastKey{};
    TerrainChunk* m_last = nullptr;
    core::u32 m_changed = 0;
};

// --- Editing -----------------------------------------------------------------
//
// Every verb writes occupancies over its brush's box with the ramp (`RampVoxels`) at
// the boundary, so the surface lands where the brush says to within the
// interpolation, not on a lattice line. **Additions take the larger occupancy
// and removals the smaller**, so a brush never undoes ground it did not reach.

struct EditReport
{
    // Voxels whose value changed.
    core::u32 touched = 0;
};

// Adds a ball of ground, or removes one when `material` is zero.
EditReport fillBall(TerrainField& field, core::DVec3 center, double radius, core::u8 material);

// The same, as an axis-aligned box. `size` is the full extent.
EditReport fillBlock(TerrainField& field, core::DVec3 center, core::Vec3 size, core::u8 material);

// The same, as an upright cylinder `height` tall.
EditReport fillCylinder(TerrainField& field, core::DVec3 center, double height, double radius, core::u8 material);

// **Moves the top of every column in a square to `height`**, as volume: ground
// is added from the column's top up to it, or taken from the top down to it,
// and whatever is under the top -- a tunnel -- stays. A column with no ground at
// all is filled from the world's floor.
EditReport fillFlat(TerrainField& field, core::DVec3 center, float size, float height, core::u8 material);

// **A heightmap, written whole**: one height per voxel column, row after row
// along +z, starting at column (`firstX`, `firstZ`), `columns` wide. On
// `fillFlat`'s terms, column by column. Heights clamp into the field's range; a
// NaN is skipped. Chunks the ground covers entirely are written as one value,
// so a square kilometre is not a billion voxel writes.
EditReport writeHeights(TerrainField& field, core::i32 firstX, core::i32 firstZ, core::u32 columns,
                        std::span<const float> heights, core::u8 material);

// The same with a material per column, parallel to `heights`. A column whose
// material is zero is skipped. What a converted hybrid terrain is laid with.
EditReport writeHeights(TerrainField& field, core::i32 firstX, core::i32 firstZ, core::u32 columns,
                        std::span<const float> heights, std::span<const core::u8> materials);

// Softens the ground in a ball: every voxel moves towards the mean of its
// twenty-seven neighbours by `strength`, less towards the rim. Clamped to 0..1.
EditReport smoothBall(TerrainField& field, core::DVec3 center, double radius, float strength);

// Pulls the ground in a ball towards a level plane at `height` -- taking what
// is above it away and filling what is below it -- by `strength`, less towards
// the rim. The height is given rather than sampled so a stroke dragged across
// a slope levels to where it started.
EditReport flattenBall(TerrainField& field, core::DVec3 center, double radius, float height, float strength);

// Raises the ground under a disc by `amount` metres at the centre, falling
// smoothly to nothing at the rim -- or lowers it, when `amount` is negative.
//
// **Moves the surface, not a ball**: each column inside the disc is shifted up
// by its falloff, within the brush's reach above and below `center`, and the
// larger of the old and the shifted occupancy is kept (the smaller when
// lowering). So a hill rises without an overhang at its rim, and a tunnel below
// the reach stays where it is. Where a column in reach holds no ground and
// `material` is not zero, raising lays ground from `center`'s height up.
EditReport raiseBall(TerrainField& field, core::DVec3 center, double radius, float amount, core::u8 material = 0);

// Grows the ground in a ball outwards by `amount` metres at the centre, falling
// smoothly to nothing at the rim -- or wears it away, when `amount` is negative.
//
// **Along the surface's own normal, not up**: a field rises, a cliff comes out
// sideways and an overhang grows down, which is what `raiseBall` cannot do. It
// grows from ground that is there, so over nothing it does nothing, and one call
// moves the surface at most half the ramp (two voxels). New ground takes its
// neighbour's material, or `material` where there is none (1 if that is zero).
EditReport growBall(TerrainField& field, core::DVec3 center, double radius, float amount, core::u8 material = 0);

// Changes what the ground is made of in a ball, without moving it. Zero is
// refused rather than treated as erase.
EditReport paintBall(TerrainField& field, core::DVec3 center, double radius, core::u8 material);

// Every voxel of material `from` in the box between the two corners becomes
// `to`. Zero for either is refused.
EditReport replaceMaterial(TerrainField& field, core::DVec3 minCorner, core::DVec3 maxCorner, core::u8 from,
                           core::u8 to);

// The height of the top of the ground at this point, in metres, interpolated
// between the four columns around it -- or nothing where there is no ground.
[[nodiscard]] std::optional<float> heightAt(const TerrainField& field, double x, double z);

// --- Sampling and raycasting -------------------------------------------------

// The field at any point, trilinear between the eight voxel centres around it.
[[nodiscard]] FieldSample sampleField(const TerrainField& field, core::DVec3 at);

struct TerrainHit
{
    // Field space, f64 (ADR 0014).
    core::DVec3 position;
    // The field's gradient at the hit.
    core::Vec3 normal{0.0f, 1.0f, 0.0f};
    double distance = 0.0;
    core::u8 material = 0;
};

// **Casts a ray at the field itself, with no physics involved.** Marched a half
// voxel at a time and bisected at the crossing; chunks of air are skipped whole.
// A ray starting inside the ground hits at once.
[[nodiscard]] std::optional<TerrainHit> raycastField(const TerrainField& field, core::DVec3 origin,
                                                     core::Vec3 direction, double maxDistance);

} // namespace luaug::asset
