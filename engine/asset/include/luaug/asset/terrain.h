#pragma once

// The terrain field: one signed-distance function, stored two ways (ADR 0067).
//
// **The whole design in one paragraph.** Terrain is a volume rather than a
// height function, because the owner's answer to "height field or voxel" was
// voxel and the consequence of that answer is caves. But a 64 m cell at half a
// metre is 16.7 million samples if it is voxels all the way down, which is not a
// streaming cell and never will be. So the columns that ARE a single-valued
// height function -- which is most ground -- are stored as heights, and only the
// columns where that stops being true carry voxel bricks.
//
// **One mesher sits above this and cannot tell which encoding answered.** The
// two never meet in the mesher; they meet here, in `sample`. For
// `sd(p) = p.y - H(x, z)` the vertical-edge crossing is at exactly `y = H(x, z)`,
// which is the height grid's own vertex -- so the boundary between the encodings
// is an equality rather than a stitch.
//
// **Immutable and shared, which is what keeps undo affordable.** `UndoStack`
// snapshots the whole world by value and keeps 64 of them; a tile or a brick is
// a `shared_ptr<const T>`, so a snapshot copies pointers and bumps refcounts
// rather than copying megabytes. An edit clones the objects it touches and
// leaves every other snapshot's view of them alone.
//
// **The samples are integers and that is not an implementation detail.**
// `world_hash.cpp`'s `Hasher::pod` static_asserts
// `has_unique_object_representations`, which excludes floating point -- so a
// float array cannot be hashed as bytes. Quantising the distance field into
// `u8` lets every tile and brick carry a digest computed once at construction,
// which is what keeps the world hash O(objects) instead of O(bytes).

#include "luaug/core/math.h"
#include "luaug/core/types.h"

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace luaug::asset {

// --- Keys --------------------------------------------------------------------
//
// Deliberately `i32` and deliberately padding-free: these are hashed with
// `Hasher::pod`, which refuses a type with padding because padding is
// uninitialised memory. Three `i32` is twelve bytes with no hole in it.

struct TileKey
{
    core::i32 x = 0;
    core::i32 z = 0;

    [[nodiscard]] constexpr auto operator<=>(const TileKey&) const noexcept = default;
    [[nodiscard]] constexpr bool operator==(const TileKey&) const noexcept = default;
};

struct BrickKey
{
    core::i32 x = 0;
    core::i32 y = 0;
    core::i32 z = 0;

    [[nodiscard]] constexpr auto operator<=>(const BrickKey&) const noexcept = default;
    [[nodiscard]] constexpr bool operator==(const BrickKey&) const noexcept = default;
};

// --- Dimensions --------------------------------------------------------------

// A height tile is 32x32 columns. Chosen so a tile is a few kilobytes rather
// than a page-thrashing megabyte, and so a 64 m cell at half a metre is a small
// square number of them rather than one enormous object an edit has to clone
// whole.
inline constexpr core::u32 TileEdge = 32;
inline constexpr core::u32 TileArea = TileEdge * TileEdge;

// A brick is 16 cubed. Smaller than the tile's footprint on purpose: a brick is
// what a cave costs, and a cave is a local thing.
inline constexpr core::u32 BrickEdge = 16;
inline constexpr core::u32 BrickVolume = BrickEdge * BrickEdge * BrickEdge;

// **The distance field, quantised.** 128 is the surface; below it is solid and
// above it is air, which puts the isosurface on an exact integer and means a
// sample can sit exactly ON the surface rather than approaching it.
inline constexpr core::u8 SurfaceLevel = 128;

// How many voxels of distance the `u8` range spans. The field is only ever read
// near the surface -- a mesher looks for a sign change -- so the range is narrow
// and the precision is spent where it is used.
inline constexpr float DistanceRange = 4.0f;

// --- Objects -----------------------------------------------------------------

// One 32x32 patch of single-valued ground.
//
// Shared, and edited through `TerrainField` alone.
//
// **Immutable from the outside**, which is what makes sharing one between
// sixty-four world snapshots safe without a lock or a copy. Inside, the field
// mutates a tile it uniquely owns and clones one it does not -- copy on write,
// so a snapshot taken a moment ago is untouched by the next brush stroke.
struct HeightTile
{
    // Metres, in the field's own space. Kept as `f32` rather than quantised
    // because a height is an absolute position that has to survive a round trip
    // through a save file, where the distance field is a local gradient that
    // does not.
    float height[TileArea] = {};
    core::u8 material[TileArea] = {};

    // xxh3 of the two arrays. **This is the reason the world hash stays
    // affordable**: hashing a tile means reading eight bytes rather than five
    // kilobytes.
    //
    // **Computed lazily, and that was a measured decision rather than a
    // preference.** It used to be computed at construction, which was right
    // while a tile was built once -- and wrong the moment editing wrote one
    // COLUMN at a time, because every column rebuilt the whole tile and hashed
    // five kilobytes to change four bytes. Generating a 128 m square took 285
    // milliseconds, which a person feels as the editor stopping.
    //
    // `valid` says whether it has been computed since the last write. Read it
    // through `digestOf`, never directly.
    mutable core::u64 digest = 0;
    mutable bool digestValid = false;
};

// A tile's digest, computed if it has to be. **Every reader goes through this**
// -- the raw field is stale by design after a write.
[[nodiscard]] core::u64 digestOf(const HeightTile& tile) noexcept;

// One 16-cubed patch of field that is not a height function -- a cave, an
// overhang, an arch.
struct Brick
{
    // Distance to the surface, quantised: `SurfaceLevel` is on it, below is
    // solid, above is air. See `DistanceRange` for the scale.
    core::u8 sd[BrickVolume] = {};
    core::u8 material[BrickVolume] = {};

    // Lazy, for the reason `HeightTile::digest` is.
    mutable core::u64 digest = 0;
    mutable bool digestValid = false;
};

[[nodiscard]] core::u64 digestOf(const Brick& brick) noexcept;

// --- The field ---------------------------------------------------------------

// How coarse the field is and how far it may be dug, decided when a cell is
// created and recorded in its file.
struct FieldSettings
{
    // The lattice spacing, in metres.
    float voxelSize = 0.5f;

    // **How many columns of sustained steepness before a cell gives up on the
    // height encoding and converts to bricks.**
    //
    // A tunable rather than a constant, and the A5 slope survey is why: eight
    // columns at a half-metre voxel is four metres of sustained 45-degree slope,
    // which a near-vertical cliff never reaches (it is one column) and a
    // mountainside exceeds easily. It decides how much of a world pays for the
    // second encoding, it is worth different values on a rolling island and an
    // alpine map, and nothing about the algorithm needs it fixed.
    core::u32 giveUpColumns = 8;

    // **The world's floor and ceiling, in metres, and the floor is what makes
    // ground creatable at all.**
    //
    // The height encoding means "solid for every y below H", so a column is only
    // height-encodable when everything under its surface is solid. Examining a
    // column below the world would always find air there -- and a slab with air
    // under it is genuinely not a height function -- so the very first fill into
    // an empty world would promote to voxels, and every fill after it, for ever.
    //
    // Clamping the examination to this range is what fixes that: below the floor
    // is not air, it is outside the world, and a column that is solid down to
    // the floor is solid as far as anything can ask.
    //
    // It is also the range a collider's height precision is spread across when
    // it is built, which cannot be widened afterwards -- so the two meanings are
    // one number for a reason rather than by coincidence.
    float minHeight = -256.0f;
    float maxHeight = 256.0f;
};

// A sample of the field: the signed distance in metres, and what it is made of.
struct FieldSample
{
    // Negative inside the ground, positive in the air, zero on the surface.
    float distance = 0.0f;
    core::u8 material = 0;
};

// The two encodings, and the sampler that hides which one answered.
//
// **Two SORTED FLAT VECTORS and never a hash map** (R10). A hash map's iteration
// order is a fact about its allocator, and anything a mesher or a serializer
// walks has to be a function of the operation sequence instead. Sorted vectors
// also make a merge, a diff and a digest of the whole field a linear walk.
class TerrainField
{
public:
    TerrainField() = default;
    explicit TerrainField(FieldSettings settings) noexcept : m_settings(settings) {}

    [[nodiscard]] const FieldSettings& settings() const noexcept { return m_settings; }

    // --- Reading ---------------------------------------------------------

    // **The one function the mesher sees.** A lattice point is answered by a
    // brick if one covers it, and by the height layer otherwise -- and the
    // caller cannot tell, which is the whole point of the hybrid.
    [[nodiscard]] FieldSample sample(core::i32 x, core::i32 y, core::i32 z) const noexcept;

    // Whether this column is carrying bricks rather than a height. Part of the
    // field's STATE rather than a derived answer: promotion happens at an edit
    // and demotion only at an explicit compaction, so a save and a reload
    // reproduce the same encoding and therefore the same hash.
    [[nodiscard]] bool isBricked(core::i32 x, core::i32 z) const noexcept;

    [[nodiscard]] const HeightTile* findTile(TileKey key) const noexcept;
    [[nodiscard]] const Brick* findBrick(BrickKey key) const noexcept;

    [[nodiscard]] core::usize tileCount() const noexcept { return m_tiles.size(); }
    [[nodiscard]] core::usize brickCount() const noexcept { return m_bricks.size(); }

    // Every key present, in sorted order. For the serializer and the mesher,
    // both of which need a walk that is the same on every machine.
    [[nodiscard]] std::vector<TileKey> tileKeys() const;
    [[nodiscard]] std::vector<BrickKey> brickKeys() const;

    // xxh3 over every object's digest, in key order. O(objects), not O(bytes).
    [[nodiscard]] core::u64 digest() const noexcept;

    // --- Writing ---------------------------------------------------------
    //
    // Every one of these CLONES what it touches and leaves the rest shared,
    // which is what makes an undo snapshot a vector of pointers.

    // **Writes ONE column, in place where the tile is not shared.**
    //
    // The verb the editor actually needs, and the reason it exists is a
    // measurement rather than a preference: writing a column through `setTile`
    // meant cloning five kilobytes and hashing them to change four bytes, so a
    // brush stroke that touched two hundred columns moved a megabyte and hashed
    // a megabyte. Generating a 128 m square took 285 ms.
    //
    // Copy-on-write: a tile another snapshot still holds is cloned first, so an
    // undo step taken a moment ago is untouched. A tile this field alone owns is
    // mutated where it lies.
    void setColumn(core::i32 x, core::i32 z, float height, core::u8 material);

    // The same, for one voxel of one brick. Creates neither: a brick that does
    // not exist is the caller's to make, because filling a new one from the
    // height layer is a decision `writeBricks` makes and this cannot.
    void setVoxel(core::i32 x, core::i32 y, core::i32 z, core::u8 distance, core::u8 material);

    // A brick to write into, created empty if there is none. Copy-on-write, like
    // `setColumn`.
    //
    // **Returns a mutable pointer into the field**, which is a narrower promise
    // than it looks: it is valid until the next call that inserts a tile or a
    // brick, and the only caller is the edit path, which writes and moves on.
    [[nodiscard]] Brick* brickFor(BrickKey key);

    // Replaces a tile wholesale. `heights` and `materials` are `TileArea` long.
    void setTile(TileKey key, std::span<const float> heights, std::span<const core::u8> materials);

    // Replaces a brick wholesale, and marks its column bricked.
    void setBrick(BrickKey key, std::span<const core::u8> distances, std::span<const core::u8> materials);

    // Drops a brick and, when its column has no bricks left, unmarks the column.
    void removeBrick(BrickKey key);

    // Widens or narrows the world's floor and ceiling. Separate from the
    // constructor because a terrain's range is authored after it exists, and
    // changing it resamples nothing -- unlike `voxelSize`, which would.
    void setHeightRange(float minHeight, float maxHeight) noexcept;

private:
    FieldSettings m_settings;
    // Sorted by key. See the class comment: never a hash map.
    // The tile a key names, created empty if there is none, and cloned first
    // when anything else still holds a reference to it.
    [[nodiscard]] HeightTile* tileFor(TileKey key);

    // **Mutable inside, const outside.** Every accessor hands out a
    // `const HeightTile*`; the edit path reaches these through `setColumn` and
    // friends, which clone before writing when anything else still holds a
    // reference. That is what keeps "a snapshot is free" true while making
    // "writing one column is cheap" true as well.
    std::vector<std::pair<TileKey, std::shared_ptr<HeightTile>>> m_tiles;
    std::vector<std::pair<BrickKey, std::shared_ptr<Brick>>> m_bricks;
};

// --- Construction helpers ----------------------------------------------------
//
// Free functions rather than constructors so that the digest is computed in
// exactly one place: an object built any other way would carry a zero digest and
// hash as though it were empty, which is the silent kind of wrong.

[[nodiscard]] std::shared_ptr<HeightTile> makeHeightTile(std::span<const float> heights,
                                                         std::span<const core::u8> materials);

[[nodiscard]] std::shared_ptr<Brick> makeBrick(std::span<const core::u8> distances,
                                               std::span<const core::u8> materials);

// The quantisation the brick's `sd` array uses, exposed because the mesher and
// the serializer both have to agree with it exactly.
[[nodiscard]] core::u8 quantiseDistance(float metres, float voxelSize) noexcept;
[[nodiscard]] float dequantiseDistance(core::u8 quantised, float voxelSize) noexcept;

// --- Editing the field -------------------------------------------------------
//
// **This is where the hybrid is decided, column by column.** Every edit writes
// the field and then asks each column it touched a single question: is this
// still a height function? A column with one surface -- solid below, air above --
// is, and stays in the cheap encoding. A column with more than one is not, and
// promotes to bricks.
//
// Asking the resulting field rather than predicting from the brush's shape is
// what makes the rule total: a ball dug into a hillside, a block raised beside a
// cliff and an arch cut through a ridge all reach the same test, and none of them
// needs a case of its own.

// How much a column may hold before the promotion test gives up and bricks it,
// and what one edit changed.
struct EditReport
{
    // Columns whose encoding is now bricks, having been heights before.
    core::u32 promoted = 0;
    // Columns written at all, in either encoding.
    core::u32 touched = 0;
};

// Adds or removes a ball of ground. `material` of zero removes; anything else
// adds with that material.
EditReport fillBall(TerrainField& field, core::DVec3 center, double radius, core::u8 material);

// The same, as an axis-aligned box. `size` is the full extent, never a half.
EditReport fillBlock(TerrainField& field, core::DVec3 center, core::Vec3 size, core::u8 material);

// Lays a flat square of ground, from the world's floor up to `height`.
//
// **A generator and not a brush, and the difference is measured.** Making flat
// ground through `fillBlock` means carving a box that reaches below the floor,
// which makes every column's examination walk the whole reserved range -- two
// hundred and fifty-six samples per column, each a pair of binary searches, for
// a result that is one number. A 128 m square took 225 milliseconds, which is a
// person watching the editor stop.
//
// This writes the height layer directly: one column, one float, one material.
// The same result, in about a thousandth of the time.
//
// **Columns that already carry voxels are left alone and counted.** Their shape
// is not a height and this verb has no opinion about it; a generator that
// flattened somebody's cave because they pressed the wrong button would be a
// generator nobody trusts. `EditReport::promoted` carries the count skipped.
EditReport fillFlat(TerrainField& field, core::DVec3 center, float size, float height, core::u8 material);

// Softens the ground under a ball, pulling every column's height towards the
// average of its neighbours.
//
// **The one verb here that is not a union or a subtraction**, and it is why the
// height layer earns its keep: smoothing a height function is a blur over a
// grid, which is cheap and stable. It is defined on the height layer only --
// columns carrying voxels are left alone rather than approximated, because
// there is no single height to pull towards and a smoother that invented one
// would flatten a cave's roof into its floor.
//
// `strength` is how far towards the average each column moves, from 0 (nothing)
// to 1 (all the way). Values outside that are clamped: a slider that overshoots
// is a slider, not an instruction.
EditReport smoothBall(TerrainField& field, core::DVec3 center, double radius, float strength);

// Pulls every column under a ball towards one height.
//
// **The height is given rather than sampled**, which is the difference between
// a tool a person can aim and one that drifts: an editor passes the height the
// stroke started at, so dragging across a hillside levels it to where you first
// clicked instead of to wherever the pointer happens to be.
//
// Height layer only, for the same reason `smoothBall` is.
EditReport flattenBall(TerrainField& field, core::DVec3 center, double radius, float height, float strength);

// Changes what the ground is MADE OF without changing where it is.
//
// **The whole point is that it edits no distance.** A paint brush that nudged
// the field would make every recolour a sculpt, and the two are different
// gestures a person makes for different reasons -- so this writes material where
// there is already ground and nowhere else. It creates no brick and promotes no
// column, so painting a height-encoded hill leaves it height-encoded.
//
// `material` of zero is refused rather than treated as erase, because zero means
// erase to `fillBall` and picking the first entry of a material list must not
// delete a hillside.
EditReport paintBall(TerrainField& field, core::DVec3 center, double radius, core::u8 material);

// The height of the ground at this column, in metres, or nothing where there is
// no ground. **Answers about the height layer and says nothing about caves**,
// which is the honest shape of the question: a column with a cave in it has no
// single height.
[[nodiscard]] std::optional<float> heightAt(const TerrainField& field, double x, double z);

// Converts back to the cheap encoding every column that no longer needs voxels,
// and answers how many it converted.
//
// **Nothing calls this automatically**, deliberately: which columns carry voxels
// is part of the world's state, so a field that quietly recompacted itself would
// be a world that changed when nobody touched it.
core::u32 compact(TerrainField& field);

// --- Raycasting the field directly -------------------------------------------

// Where a ray met the ground.
struct TerrainHit
{
    // World-space, f64 because a ray is cast from a camera that may be a
    // kilometre from the origin and f32 stops being exact there (ADR 0014).
    core::DVec3 position;
    // The field's gradient at the hit, which is the surface normal.
    core::Vec3 normal{0.0f, 1.0f, 0.0f};
    // How far along the ray, in metres.
    double distance = 0.0;
    core::u8 material = 0;
};

// **Casts a ray at the field itself, with no physics involved**, and that is the
// point rather than an optimisation.
//
// `PhysicsSync::mirror` is called from exactly one site, gated on the world
// being paused AND the collision wireframe being on -- so an editor sitting in
// edit mode with that view closed holds no bodies at all, and a brush that
// asked physics where the ground was would find nothing. `pickNearest` cannot
// stand in either: it answers with an instance and a distance, and it tests
// every part as its bounding box.
//
// Marched voxel by voxel along the ray and then refined by bisection between the
// last two samples, so the answer is on the surface rather than at the lattice
// point before it. `maxDistance` bounds the march: a ray fired at the sky must
// terminate, and it must do so in a number of steps a caller can predict.
[[nodiscard]] std::optional<TerrainHit> raycastField(const TerrainField& field, core::DVec3 origin,
                                                     core::Vec3 direction, double maxDistance);

} // namespace luaug::asset
