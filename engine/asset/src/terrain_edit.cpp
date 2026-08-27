#include "luaug/asset/terrain.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <vector>

namespace luaug::asset {
namespace {

using core::DVec3;
using core::i32;
using core::u32;
using core::u8;
using core::usize;
using core::Vec3;

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

// **How far above and below the brush a column is examined.** The promotion test
// asks whether the resulting column is single-valued, and it can only answer
// about the range it looks at -- so it looks at the brush plus a margin on each
// side, which is where a second surface would have to appear to matter.
//
// Sixteen voxels is one brick, which is also the granularity a promotion writes
// at, so a narrower margin could not produce a different answer.
constexpr i32 ColumnMargin = static_cast<i32>(BrickEdge);

// One lattice column's worth of the edit, gathered before anything is written.
struct Column
{
    i32 x = 0;
    i32 z = 0;
};

// A brush, as the one thing every edit reduces to: how far inside the shape a
// point is, in metres. Positive inside.
using Depth = float (*)(const DVec3& at, const void* shape);

struct BallShape
{
    DVec3 center;
    double radius = 0.0;
};

struct BlockShape
{
    DVec3 center;
    Vec3 half;
};

[[nodiscard]] float ballDepth(const DVec3& at, const void* shape) noexcept
{
    const auto& ball = *static_cast<const BallShape*>(shape);
    const double dx = at.x - ball.center.x;
    const double dy = at.y - ball.center.y;
    const double dz = at.z - ball.center.z;
    return static_cast<float>(ball.radius - std::sqrt(dx * dx + dy * dy + dz * dz));
}

[[nodiscard]] float blockDepth(const DVec3& at, const void* shape) noexcept
{
    const auto& block = *static_cast<const BlockShape*>(shape);
    // The distance to the box, negated so that inside is positive. The MINIMUM
    // over the three axes, because a point is only as deep inside a box as its
    // nearest face -- taking the maximum would round the corners off every block
    // somebody placed.
    const auto dx = static_cast<float>(std::abs(at.x - block.center.x));
    const auto dy = static_cast<float>(std::abs(at.y - block.center.y));
    const auto dz = static_cast<float>(std::abs(at.z - block.center.z));
    return std::min({block.half.x - dx, block.half.y - dy, block.half.z - dz});
}

// One column's storage, resolved once.
//
// **Every sample of a column asks the same two questions**: which brick covers
// it, and which tile holds it. Answering them per sample means two binary
// searches per lattice step, and a column is walked dozens of steps deep -- so
// an eight-metre brush spent most of its time looking up storage it had already
// found. Resolved once, a non-bricked column's sample is one subtraction.
struct ColumnView
{
    // Null when the column carries voxels, which is the case that still needs a
    // lookup per step because a brick covers sixteen of them vertically.
    const HeightTile* tile = nullptr;
    u32 index = 0;
    bool bricked = false;
};

[[nodiscard]] ColumnView viewOf(const TerrainField& field, i32 x, i32 z)
{
    ColumnView view;
    view.bricked = field.isBricked(x, z);
    if (view.bricked) {
        return view;
    }
    const TileKey key{floorDiv(x, static_cast<i32>(TileEdge)), floorDiv(z, static_cast<i32>(TileEdge))};
    view.tile = field.findTile(key);
    const auto localX = static_cast<u32>(floorMod(x, static_cast<i32>(TileEdge)));
    const auto localZ = static_cast<u32>(floorMod(z, static_cast<i32>(TileEdge)));
    view.index = localZ * TileEdge + localX;
    return view;
}

// What the field says at one lattice point of a column already resolved.
//
// The same answer `TerrainField::sample` gives; it just does not look the
// storage up again. A bricked column falls back, because a brick covers sixteen
// steps and the right one changes as the walk climbs.
[[nodiscard]] FieldSample sampleThrough(const TerrainField& field, const ColumnView& view, i32 x, i32 y, i32 z)
{
    if (view.bricked) {
        return field.sample(x, y, z);
    }
    if (view.tile == nullptr || view.tile->material[view.index] == 0) {
        return FieldSample{DistanceRange * field.settings().voxelSize, 0};
    }
    return FieldSample{static_cast<float>(y) * field.settings().voxelSize - view.tile->height[view.index],
                       view.tile->material[view.index]};
}

// The field with the brush applied, at one lattice point, without writing
// anything. **The whole edit is expressed through this**: the promotion test
// needs to know what the column WILL look like, and computing that twice -- once
// to decide and once to write -- is how the two come to disagree.
[[nodiscard]] FieldSample afterEdit(const TerrainField& field, const ColumnView& view, i32 x, i32 y, i32 z, Depth depth,
                                    const void* shape, u8 material)
{
    // **R9's f32/f64 split, made explicit.** A lattice coordinate is an integer,
    // a voxel size is `f32` and a world position is `f64`, so every place they
    // multiply is a promotion Clang diagnoses and MSVC does not. Widened once
    // rather than cast per operand.
    const auto voxel = static_cast<double>(field.settings().voxelSize);
    const DVec3 at{static_cast<double>(x) * voxel, static_cast<double>(y) * voxel, static_cast<double>(z) * voxel};
    const float inside = depth(at, shape);
    const FieldSample existing = sampleThrough(field, view, x, y, z);

    if (material == 0) {
        // **Removing: the union with the brush's INSIDE becomes air.** A carve
        // is `max(existing, insideness)` on a field where positive is air, so a
        // point already in the air stays in the air and one inside the brush
        // becomes air by however deep it was.
        return FieldSample{std::max(existing.distance, inside), existing.material};
    }

    // Adding: the union of ground. A point inside the brush is ground, at the
    // depth the brush says, and keeps whichever material is nearer the surface.
    //
    // **`>=`, and the equal case is the one that mattered** (D153). A sample
    // exactly ON the brush's face has `inside == 0`, and with a strict `>` it
    // fell through to `return existing` -- so the surface sample of a flat-topped
    // block kept the material of whatever was there before, which for a fresh
    // column is nothing. The height layer reads its material from exactly that
    // sample, so every block placed with a flat top wrote material zero. The
    // boundary belongs to the brush, which is the same convention `distance <= 0`
    // already states everywhere else.
    if (inside >= 0.0f) {
        return FieldSample{std::min(existing.distance, -inside), material};
    }
    return existing;
}

// **The promotion test, and it is the whole hybrid in one function.**
//
// Walks the column and counts how many times the field crosses from ground to
// air. Exactly one crossing is a height function -- solid below, air above --
// and the crossing's height is what the cheap encoding stores. Zero crossings is
// a column that is all air or all ground, which is also expressible. Anything
// else is a cave, an overhang or an arch, and needs voxels.
struct ColumnVerdict
{
    bool singleValued = false;
    float height = 0.0f;
    u8 material = 0;
    i32 lowY = 0;
    i32 highY = 0;
};

[[nodiscard]] ColumnVerdict examineColumn(const TerrainField& field, const ColumnView& view, i32 x, i32 z, i32 lowY,
                                          i32 highY, Depth depth, const void* shape, u8 material)
{
    ColumnVerdict verdict;
    verdict.lowY = lowY;
    verdict.highY = highY;

    const float voxel = field.settings().voxelSize;
    int crossings = 0;
    FieldSample previous = afterEdit(field, view, x, lowY, z, depth, shape, material);
    bool previousSolid = previous.distance <= 0.0f;
    // A column whose bottom sample is air has nothing below it either, and one
    // whose bottom is ground is the ordinary case.
    verdict.material = previous.material;

    for (i32 y = lowY + 1; y <= highY; ++y) {
        const FieldSample current = afterEdit(field, view, x, y, z, depth, shape, material);
        const bool solid = current.distance <= 0.0f;
        if (solid != previousSolid) {
            ++crossings;
            if (crossings == 1 && previousSolid) {
                // Ground below, air above: the surface. Interpolated between the
                // two samples so the height is where the field says rather than
                // at the lattice point above it -- the same linear crossing the
                // mesher and the raycast both use, so all three agree.
                const float span = current.distance - previous.distance;
                const float t = std::abs(span) < 1e-12f ? 0.5f : std::clamp(-previous.distance / span, 0.0f, 1.0f);
                verdict.height = (static_cast<float>(y - 1) + t) * voxel;
                verdict.material = previous.material;
            }
        }
        previous = current;
        previousSolid = solid;
    }

    // **One crossing, and it has to be ground-then-air.** A column that starts in
    // the air and ends in the ground is an overhang seen from below, and it is
    // not a height function however few crossings it has.
    // **Solid at the bottom, air at the top, and one transition between them.**
    // That is exactly what `sd = y - H` describes, and the bottom of the range
    // is the world's floor rather than an arbitrary depth -- so "solid below"
    // means solid as far as the world goes.
    verdict.singleValued = crossings == 0 || (crossings == 1 && !previousSolid);
    if (crossings == 0) {
        // All air or all ground. All ground has no surface in range, which means
        // the surface is above the range examined -- so the column keeps whatever
        // height it had rather than being given one.
        verdict.singleValued = true;
        verdict.height = previousSolid ? static_cast<float>(highY + 1) * voxel : static_cast<float>(lowY) * voxel;
        if (!previousSolid) {
            // **All air is stated, not implied** (D153). Material zero is what
            // the height layer reads as "no ground in this column", so a column
            // carved away entirely has to say zero rather than keep the material
            // of the ground that used to be there.
            verdict.material = 0;
        }
    }
    return verdict;
}

// Writes one column into the height layer.
//
// **One column, not one tile**, and the difference is the whole reason
// `setColumn` exists. This used to clone the tile's five kilobytes and hash them
// to change four bytes, so a stroke touching two hundred columns moved and
// hashed a megabyte -- and generating a 128 m square took 285 milliseconds,
// which a person feels as the editor stopping.
void writeHeight(TerrainField& field, i32 x, i32 z, float height, u8 material)
{
    field.setColumn(x, z, height, material);
}

// Writes one column as bricks, cloning each brick it spans.
void writeBricks(TerrainField& field, i32 x, i32 z, i32 lowY, i32 highY, Depth depth, const void* shape, u8 material)
{
    const float voxel = field.settings().voxelSize;
    const i32 lowBrick = floorDiv(lowY, static_cast<i32>(BrickEdge));
    const i32 highBrick = floorDiv(highY, static_cast<i32>(BrickEdge));
    const i32 brickX = floorDiv(x, static_cast<i32>(BrickEdge));
    const i32 brickZ = floorDiv(z, static_cast<i32>(BrickEdge));
    const auto localX = static_cast<u32>(floorMod(x, static_cast<i32>(BrickEdge)));
    const auto localZ = static_cast<u32>(floorMod(z, static_cast<i32>(BrickEdge)));

    for (i32 brickY = lowBrick; brickY <= highBrick; ++brickY) {
        const BrickKey key{brickX, brickY, brickZ};
        const Brick* existingBrick = field.findBrick(key);

        // **A brick that would only repeat the height layer is not created.**
        //
        // The examined column runs a whole brick above and below the brush, so
        // promoting a cave used to allocate four bricks where one carried the
        // cave and three mirrored the ground either side of it. `compact` would
        // reclaim them afterwards -- and did, which is how this was found -- but
        // paying four times the memory on every dig and relying on a verb nobody
        // calls automatically is the wrong shape.
        //
        // A partly-bricked column is a legal column: `sample` prefers a brick
        // where one exists and falls back to the height layer everywhere else,
        // so the levels that agree can simply be absent.
        if (existingBrick == nullptr) {
            bool differs = false;
            for (u32 y = 0; y < BrickEdge && !differs; ++y) {
                const i32 worldY = brickY * static_cast<i32>(BrickEdge) + static_cast<i32>(y);
                if (worldY < lowY || worldY > highY) {
                    continue;
                }
                const FieldSample before = field.sample(x, worldY, z);
                const FieldSample after = afterEdit(field, viewOf(field, x, z), x, worldY, z, depth, shape, material);
                if ((before.distance <= 0.0f) != (after.distance <= 0.0f)) {
                    differs = true;
                }
            }
            if (!differs) {
                continue;
            }
        }

        std::vector<u8> distances(BrickVolume, quantiseDistance(DistanceRange * voxel, voxel));
        std::vector<u8> materials(BrickVolume, 0);
        if (const Brick* existing = existingBrick; existing != nullptr) {
            std::copy(std::begin(existing->sd), std::end(existing->sd), distances.begin());
            std::copy(std::begin(existing->material), std::end(existing->material), materials.begin());
        }
        else {
            // **A brick being created has to be filled from the height layer it
            // is replacing**, not from air. Promoting a column and writing only
            // the edited voxels would delete the ground either side of the cave,
            // because the brick now answers for the whole column and it would be
            // answering "air".
            for (u32 y = 0; y < BrickEdge; ++y) {
                for (u32 z2 = 0; z2 < BrickEdge; ++z2) {
                    for (u32 x2 = 0; x2 < BrickEdge; ++x2) {
                        const i32 worldX = brickX * static_cast<i32>(BrickEdge) + static_cast<i32>(x2);
                        const i32 worldY = brickY * static_cast<i32>(BrickEdge) + static_cast<i32>(y);
                        const i32 worldZ = brickZ * static_cast<i32>(BrickEdge) + static_cast<i32>(z2);
                        const FieldSample was = field.sample(worldX, worldY, worldZ);
                        const u32 index = (y * BrickEdge + z2) * BrickEdge + x2;
                        distances[index] = quantiseDistance(was.distance, voxel);
                        materials[index] = was.material;
                    }
                }
            }
        }

        // Resolved after the brick above is created, because creating one is
        // what makes the column bricked.
        const ColumnView brickView = viewOf(field, x, z);
        for (u32 y = 0; y < BrickEdge; ++y) {
            const i32 worldY = brickY * static_cast<i32>(BrickEdge) + static_cast<i32>(y);
            if (worldY < lowY || worldY > highY) {
                continue;
            }
            const FieldSample edited = afterEdit(field, brickView, x, worldY, z, depth, shape, material);
            const u32 index = (y * BrickEdge + localZ) * BrickEdge + localX;
            distances[index] = quantiseDistance(edited.distance, voxel);
            materials[index] = edited.material;
        }

        field.setBrick(key, distances, materials);
    }
}

// The shared core of every brush.
EditReport applyBrush(TerrainField& field, const core::AABB& bounds, Depth depth, const void* shape, u8 material)
{
    EditReport report;
    const float voxel = field.settings().voxelSize;
    if (!(voxel > 0.0f)) {
        return report;
    }

    const auto low = [voxel](float metres) { return static_cast<i32>(std::floor(metres / voxel)) - 1; };
    const auto high = [voxel](float metres) { return static_cast<i32>(std::ceil(metres / voxel)) + 1; };

    const i32 minX = low(bounds.min.x);
    const i32 maxX = high(bounds.max.x);
    const i32 minZ = low(bounds.min.z);
    const i32 maxZ = high(bounds.max.z);
    // **Clamped to the world's floor and ceiling, and the floor is what makes
    // ground creatable at all.**
    //
    // The height encoding means "solid for every y below H", so a column is only
    // height-encodable when everything under its surface is solid. Without this
    // clamp the examination always reaches below whatever was filled, always
    // finds air there, and always concludes the column is a floating slab -- so
    // the FIRST fill into an empty world promoted to voxels, and so did every
    // fill after it. The example that found this had a hill of 76 bricked cells
    // and a `HeightAt` of zero.
    //
    // Below the floor is not air; it is outside the world. A column solid down
    // to the floor is solid as far as anything can ask.
    const i32 floorY = static_cast<i32>(std::floor(field.settings().minHeight / voxel));
    const i32 ceilingY = static_cast<i32>(std::ceil(field.settings().maxHeight / voxel));
    const i32 minY = std::max(low(bounds.min.y) - ColumnMargin, floorY);
    const i32 maxY = std::min(high(bounds.max.y) + ColumnMargin, ceilingY);
    if (maxY <= minY) {
        return report;
    }

    // **Every column is examined before any is written**, and the columns are
    // visited in a fixed order (R10). A brush that promoted as it went would put
    // the order it happened to visit voxels into the world's observable state,
    // because a promotion decides which encoding a column is in and that is
    // hashed.
    std::vector<Column> columns;
    columns.reserve(static_cast<usize>(maxX - minX + 1) * static_cast<usize>(maxZ - minZ + 1));
    for (i32 z = minZ; z <= maxZ; ++z) {
        for (i32 x = minX; x <= maxX; ++x) {
            columns.push_back(Column{x, z});
        }
    }

    for (const Column& column : columns) {
        // One resolution per column, shared by the examination and the write.
        const ColumnView view = viewOf(field, column.x, column.z);
        const ColumnVerdict verdict =
            examineColumn(field, view, column.x, column.z, minY, maxY, depth, shape, material);
        const bool wasBricked = view.bricked;

        // A column that is already bricked stays bricked even when the edit made
        // it single-valued again. Demotion is `compact` and nothing else, because
        // the representation is part of the world and a field that recompacted
        // itself would be a world that changed when nobody touched it.
        if (verdict.singleValued && !wasBricked) {
            writeHeight(field, column.x, column.z, verdict.height, verdict.material);
        }
        else {
            if (!wasBricked) {
                report.promoted += 1;
            }
            writeBricks(field, column.x, column.z, minY, maxY, depth, shape, material);
        }
        report.touched += 1;
    }

    return report;
}

} // namespace

EditReport fillBall(TerrainField& field, DVec3 center, double radius, u8 material)
{
    if (!(radius > 0.0)) {
        return EditReport{};
    }
    const BallShape ball{center, radius};
    const auto r = static_cast<float>(radius);
    const core::AABB bounds{
        Vec3{static_cast<float>(center.x) - r, static_cast<float>(center.y) - r, static_cast<float>(center.z) - r},
        Vec3{static_cast<float>(center.x) + r, static_cast<float>(center.y) + r, static_cast<float>(center.z) + r}};
    return applyBrush(field, bounds, ballDepth, &ball, material);
}

EditReport fillBlock(TerrainField& field, DVec3 center, Vec3 size, u8 material)
{
    if (!(size.x > 0.0f) || !(size.y > 0.0f) || !(size.z > 0.0f)) {
        return EditReport{};
    }
    const BlockShape block{center, Vec3{size.x * 0.5f, size.y * 0.5f, size.z * 0.5f}};
    const core::AABB bounds{
        Vec3{static_cast<float>(center.x) - block.half.x, static_cast<float>(center.y) - block.half.y,
             static_cast<float>(center.z) - block.half.z},
        Vec3{static_cast<float>(center.x) + block.half.x, static_cast<float>(center.y) + block.half.y,
             static_cast<float>(center.z) + block.half.z}};
    return applyBrush(field, bounds, blockDepth, &block, material);
}

namespace {

// The columns a ball covers, with the height each currently has.
//
// **Height-layer columns only.** A bricked column has no single height, and a
// smoother that invented one would pull a cave's roof down onto its floor.
struct Column2D
{
    i32 x = 0;
    i32 z = 0;
    float height = 0.0f;
    u8 material = 0;
};

[[nodiscard]] std::vector<Column2D> heightColumnsIn(const TerrainField& field, DVec3 center, double radius)
{
    std::vector<Column2D> columns;
    const auto voxel = static_cast<double>(field.settings().voxelSize);
    if (!(radius > 0.0) || !(voxel > 0.0)) {
        return columns;
    }

    const auto low = [voxel](double metres) { return static_cast<i32>(std::floor(metres / voxel)); };
    const auto high = [voxel](double metres) { return static_cast<i32>(std::ceil(metres / voxel)); };
    const double radiusSquared = radius * radius;

    for (i32 z = low(center.z - radius); z <= high(center.z + radius); ++z) {
        for (i32 x = low(center.x - radius); x <= high(center.x + radius); ++x) {
            const double dx = static_cast<double>(x) * voxel - center.x;
            const double dz = static_cast<double>(z) * voxel - center.z;
            if (dx * dx + dz * dz > radiusSquared) {
                continue;
            }
            if (field.isBricked(x, z)) {
                continue;
            }
            const TileKey key{floorDiv(x, static_cast<i32>(TileEdge)), floorDiv(z, static_cast<i32>(TileEdge))};
            const HeightTile* tile = field.findTile(key);
            if (tile == nullptr) {
                continue;
            }
            const auto localX = static_cast<u32>(floorMod(x, static_cast<i32>(TileEdge)));
            const auto localZ = static_cast<u32>(floorMod(z, static_cast<i32>(TileEdge)));
            const u32 index = localZ * TileEdge + localX;
            if (tile->material[index] == 0) {
                // No ground in this column. Material zero says so (D153), and a
                // smoother that averaged it in would pull a cliff edge down into
                // the empty space beside it.
                continue;
            }
            columns.push_back(Column2D{x, z, tile->height[index], tile->material[index]});
        }
    }
    return columns;
}

// What a column's height is now, or nothing where there is no ground. Used by
// the smoother to average NEIGHBOURS, which may lie outside the brush.
[[nodiscard]] std::optional<float> heightOfColumn(const TerrainField& field, i32 x, i32 z)
{
    if (field.isBricked(x, z)) {
        return std::nullopt;
    }
    const TileKey key{floorDiv(x, static_cast<i32>(TileEdge)), floorDiv(z, static_cast<i32>(TileEdge))};
    const HeightTile* tile = field.findTile(key);
    if (tile == nullptr) {
        return std::nullopt;
    }
    const auto localX = static_cast<u32>(floorMod(x, static_cast<i32>(TileEdge)));
    const auto localZ = static_cast<u32>(floorMod(z, static_cast<i32>(TileEdge)));
    const u32 index = localZ * TileEdge + localX;
    if (tile->material[index] == 0) {
        return std::nullopt;
    }
    return tile->height[index];
}

} // namespace

EditReport fillFlat(TerrainField& field, DVec3 center, float size, float height, u8 material)
{
    EditReport report;
    const float voxel = field.settings().voxelSize;
    if (!(size > 0.0f) || !(voxel > 0.0f) || material == 0) {
        return report;
    }

    const float clamped = std::clamp(height, field.settings().minHeight, field.settings().maxHeight);
    const auto wide = static_cast<double>(voxel);
    const double half = static_cast<double>(size) * 0.5;

    const auto low = [wide](double metres) { return static_cast<i32>(std::floor(metres / wide)); };
    const auto high = [wide](double metres) { return static_cast<i32>(std::ceil(metres / wide)); };

    for (i32 z = low(center.z - half); z <= high(center.z + half); ++z) {
        for (i32 x = low(center.x - half); x <= high(center.x + half); ++x) {
            if (field.isBricked(x, z)) {
                // Voxels are not a height, and this verb has no opinion about
                // them. Counted so a caller can say what it did not do.
                report.promoted += 1;
                continue;
            }
            field.setColumn(x, z, clamped, material);
            report.touched += 1;
        }
    }
    return report;
}

EditReport smoothBall(TerrainField& field, DVec3 center, double radius, float strength)
{
    EditReport report;
    const float amount = std::clamp(strength, 0.0f, 1.0f);
    if (!(amount > 0.0f)) {
        return report;
    }

    const std::vector<Column2D> columns = heightColumnsIn(field, center, radius);
    if (columns.empty()) {
        return report;
    }

    // **Every column is read before any is written**, which is what makes this a
    // blur rather than a smear: writing as it goes would feed each column's new
    // height into its neighbour's average, and the result would depend on the
    // order the columns were visited (R10) as well as looking wrong.
    std::vector<float> targets;
    targets.reserve(columns.size());

    for (const Column2D& column : columns) {
        float sum = column.height;
        int count = 1;
        for (i32 dz = -1; dz <= 1; ++dz) {
            for (i32 dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dz == 0) {
                    continue;
                }
                if (const std::optional<float> neighbour = heightOfColumn(field, column.x + dx, column.z + dz);
                    neighbour.has_value()) {
                    sum += *neighbour;
                    ++count;
                }
            }
        }
        targets.push_back(sum / static_cast<float>(count));
    }

    for (usize at = 0; at < columns.size(); ++at) {
        const Column2D& column = columns[at];
        const float moved = column.height + (targets[at] - column.height) * amount;
        if (moved == column.height) {
            continue;
        }
        writeHeight(field, column.x, column.z, moved, column.material);
        report.touched += 1;
    }
    return report;
}

EditReport flattenBall(TerrainField& field, DVec3 center, double radius, float height, float strength)
{
    EditReport report;
    const float amount = std::clamp(strength, 0.0f, 1.0f);
    if (!(amount > 0.0f)) {
        return report;
    }

    const float floorHeight = field.settings().minHeight;
    const float ceilingHeight = field.settings().maxHeight;
    const float target = std::clamp(height, floorHeight, ceilingHeight);

    for (const Column2D& column : heightColumnsIn(field, center, radius)) {
        const float moved = column.height + (target - column.height) * amount;
        if (moved == column.height) {
            continue;
        }
        writeHeight(field, column.x, column.z, moved, column.material);
        report.touched += 1;
    }
    return report;
}

EditReport paintBall(TerrainField& field, DVec3 center, double radius, u8 material)
{
    EditReport report;
    const float voxel = field.settings().voxelSize;
    if (!(radius > 0.0) || !(voxel > 0.0f) || material == 0) {
        // **Material zero is refused rather than treated as erase.** In `fillBall`
        // zero means remove, and a paint brush that removed ground when somebody
        // picked the first entry of a material list would be the worst possible
        // reading of one shared convention.
        return report;
    }

    const auto radiusSquared = radius * radius;
    const auto inBall = [center, radiusSquared](double x, double y, double z) {
        const double dx = x - center.x;
        const double dy = y - center.y;
        const double dz = z - center.z;
        return dx * dx + dy * dy + dz * dz <= radiusSquared;
    };

    const auto wide = static_cast<double>(voxel);
    const auto lowIndex = [wide](double metres) { return static_cast<i32>(std::floor(metres / wide)); };
    const auto highIndex = [wide](double metres) { return static_cast<i32>(std::ceil(metres / wide)); };

    const i32 minX = lowIndex(center.x - radius);
    const i32 maxX = highIndex(center.x + radius);
    const i32 minZ = lowIndex(center.z - radius);
    const i32 maxZ = highIndex(center.z + radius);
    const i32 minY = lowIndex(center.y - radius);
    const i32 maxY = highIndex(center.y + radius);

    for (i32 z = minZ; z <= maxZ; ++z) {
        for (i32 x = minX; x <= maxX; ++x) {
            if (field.isBricked(x, z)) {
                // **Only bricks that already exist**, and only voxels that are
                // already solid. Painting must never promote a column or create
                // a brick: it changes what the ground is made of, not where the
                // ground is, and the one is exactly as visible as the other.
                for (i32 y = minY; y <= maxY; ++y) {
                    const BrickKey key{floorDiv(x, static_cast<i32>(BrickEdge)),
                                       floorDiv(y, static_cast<i32>(BrickEdge)),
                                       floorDiv(z, static_cast<i32>(BrickEdge))};
                    const Brick* existing = field.findBrick(key);
                    if (existing == nullptr) {
                        continue;
                    }
                    const auto localX = static_cast<u32>(floorMod(x, static_cast<i32>(BrickEdge)));
                    const auto localY = static_cast<u32>(floorMod(y, static_cast<i32>(BrickEdge)));
                    const auto localZ = static_cast<u32>(floorMod(z, static_cast<i32>(BrickEdge)));
                    const u32 index = (localY * BrickEdge + localZ) * BrickEdge + localX;
                    if (existing->material[index] == material) {
                        continue;
                    }
                    if (dequantiseDistance(existing->sd[index], voxel) > 0.0f) {
                        continue;
                    }
                    if (!inBall(static_cast<double>(x) * wide, static_cast<double>(y) * wide,
                                static_cast<double>(z) * wide)) {
                        continue;
                    }

                    std::vector<u8> distances(BrickVolume, 0);
                    std::vector<u8> materials(BrickVolume, 0);
                    std::copy(std::begin(existing->sd), std::end(existing->sd), distances.begin());
                    std::copy(std::begin(existing->material), std::end(existing->material), materials.begin());
                    materials[index] = material;
                    field.setBrick(key, distances, materials);
                    ++report.touched;
                }
                continue;
            }

            // The height layer holds one material per column -- the surface's --
            // so the question is whether the ball reaches THAT point rather than
            // any point of the column.
            const TileKey key{floorDiv(x, static_cast<i32>(TileEdge)), floorDiv(z, static_cast<i32>(TileEdge))};
            const HeightTile* tile = field.findTile(key);
            if (tile == nullptr) {
                continue;
            }
            const auto localX = static_cast<u32>(floorMod(x, static_cast<i32>(TileEdge)));
            const auto localZ = static_cast<u32>(floorMod(z, static_cast<i32>(TileEdge)));
            const u32 index = localZ * TileEdge + localX;
            if (tile->material[index] == material || tile->material[index] == 0) {
                continue;
            }
            if (!inBall(static_cast<double>(x) * wide, static_cast<double>(tile->height[index]),
                        static_cast<double>(z) * wide)) {
                continue;
            }

            std::vector<float> heights(TileArea, 0.0f);
            std::vector<u8> materials(TileArea, 0);
            std::copy(std::begin(tile->height), std::end(tile->height), heights.begin());
            std::copy(std::begin(tile->material), std::end(tile->material), materials.begin());
            materials[index] = material;
            field.setTile(key, heights, materials);
            ++report.touched;
        }
    }

    return report;
}

std::optional<float> heightAt(const TerrainField& field, double x, double z)
{
    const auto voxel = static_cast<double>(field.settings().voxelSize);
    if (!(voxel > 0.0)) {
        return std::nullopt;
    }
    const auto latticeX = static_cast<i32>(std::floor(x / voxel));
    const auto latticeZ = static_cast<i32>(std::floor(z / voxel));
    const TileKey key{floorDiv(latticeX, static_cast<i32>(TileEdge)), floorDiv(latticeZ, static_cast<i32>(TileEdge))};
    const HeightTile* tile = field.findTile(key);
    if (tile == nullptr) {
        return std::nullopt;
    }
    const auto localX = static_cast<u32>(floorMod(latticeX, static_cast<i32>(TileEdge)));
    const auto localZ = static_cast<u32>(floorMod(latticeZ, static_cast<i32>(TileEdge)));
    const u32 index = localZ * TileEdge + localX;
    if (tile->material[index] == 0) {
        // Material zero is the height layer's "no ground here", and this has to
        // agree with `sample` or a column would have a height nothing can stand
        // on (D153).
        return std::nullopt;
    }
    return tile->height[index];
}

u32 compact(TerrainField& field)
{
    // A brick whose every sample agrees with what the height layer would say is
    // a brick that is carrying nothing, and dropping it gives the column back to
    // the cheap encoding.
    //
    // **Walked in key order and decided before anything is removed**, so the
    // answer does not depend on the order removals happen to invalidate lookups.
    const std::vector<BrickKey> keys = field.brickKeys();
    std::vector<BrickKey> removable;
    const float voxel = field.settings().voxelSize;
    // Named once rather than cast per operand: a lattice coordinate is an
    // integer, a voxel size is `f32` and a world position is `f64`, so every
    // place they multiply is a promotion Clang diagnoses and MSVC does not.
    const auto wideVoxel = static_cast<double>(voxel);

    for (const BrickKey key : keys) {
        const Brick* brick = field.findBrick(key);
        if (brick == nullptr) {
            continue;
        }
        bool redundant = true;
        for (u32 y = 0; y < BrickEdge && redundant; ++y) {
            for (u32 z = 0; z < BrickEdge && redundant; ++z) {
                for (u32 x = 0; x < BrickEdge && redundant; ++x) {
                    const u32 index = (y * BrickEdge + z) * BrickEdge + x;
                    // Sign is what a mesher reads, so agreeing in sign is
                    // agreeing about the surface. Comparing the quantised value
                    // exactly would keep every brick a rounding apart.
                    const i32 worldX = key.x * static_cast<i32>(BrickEdge) + static_cast<i32>(x);
                    const i32 worldY = key.y * static_cast<i32>(BrickEdge) + static_cast<i32>(y);
                    const i32 worldZ = key.z * static_cast<i32>(BrickEdge) + static_cast<i32>(z);

                    const float brickDistance = dequantiseDistance(brick->sd[index], voxel);
                    const std::optional<float> height = heightAt(field, static_cast<double>(worldX) * wideVoxel,
                                                                 static_cast<double>(worldZ) * wideVoxel);
                    if (!height.has_value()) {
                        redundant = false;
                        break;
                    }
                    const float heightDistance = static_cast<float>(worldY) * voxel - *height;
                    if ((brickDistance <= 0.0f) != (heightDistance <= 0.0f)) {
                        redundant = false;
                    }
                }
            }
        }
        if (redundant) {
            removable.push_back(key);
        }
    }

    for (const BrickKey key : removable) {
        field.removeBrick(key);
    }
    return static_cast<u32>(removable.size());
}

} // namespace luaug::asset
