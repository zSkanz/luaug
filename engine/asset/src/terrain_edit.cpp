#include "luaug/asset/terrain.h"

#include <algorithm>
#include <cmath>
#include <map>
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

// The field with the brush applied, at one lattice point, without writing
// anything. **The whole edit is expressed through this**: the promotion test
// needs to know what the column WILL look like, and computing that twice -- once
// to decide and once to write -- is how the two come to disagree.
[[nodiscard]] FieldSample afterEdit(const TerrainField& field, i32 x, i32 y, i32 z, Depth depth, const void* shape,
                                    u8 material)
{
    // **R9's f32/f64 split, made explicit.** A lattice coordinate is an integer,
    // a voxel size is `f32` and a world position is `f64`, so every place they
    // multiply is a promotion Clang diagnoses and MSVC does not. Widened once
    // rather than cast per operand.
    const auto voxel = static_cast<double>(field.settings().voxelSize);
    const DVec3 at{static_cast<double>(x) * voxel, static_cast<double>(y) * voxel, static_cast<double>(z) * voxel};
    const float inside = depth(at, shape);
    const FieldSample existing = field.sample(x, y, z);

    if (material == 0) {
        // **Removing: the union with the brush's INSIDE becomes air.** A carve
        // is `max(existing, insideness)` on a field where positive is air, so a
        // point already in the air stays in the air and one inside the brush
        // becomes air by however deep it was.
        return FieldSample{std::max(existing.distance, inside), existing.material};
    }

    // Adding: the union of ground. A point inside the brush is ground, at the
    // depth the brush says, and keeps whichever material is nearer the surface.
    if (inside > 0.0f) {
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

[[nodiscard]] ColumnVerdict examineColumn(const TerrainField& field, i32 x, i32 z, i32 lowY, i32 highY, Depth depth,
                                          const void* shape, u8 material)
{
    ColumnVerdict verdict;
    verdict.lowY = lowY;
    verdict.highY = highY;

    const float voxel = field.settings().voxelSize;
    int crossings = 0;
    FieldSample previous = afterEdit(field, x, lowY, z, depth, shape, material);
    bool previousSolid = previous.distance <= 0.0f;
    // A column whose bottom sample is air has nothing below it either, and one
    // whose bottom is ground is the ordinary case.
    verdict.material = previous.material;

    for (i32 y = lowY + 1; y <= highY; ++y) {
        const FieldSample current = afterEdit(field, x, y, z, depth, shape, material);
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
    verdict.singleValued = crossings == 0 || (crossings == 1 && !previousSolid);
    if (crossings == 0) {
        // All air or all ground. All ground has no surface in range, which means
        // the surface is above the range examined -- so the column keeps whatever
        // height it had rather than being given one.
        verdict.singleValued = true;
        verdict.height = previousSolid ? static_cast<float>(highY + 1) * voxel : static_cast<float>(lowY) * voxel;
    }
    return verdict;
}

// Writes one column into the height layer, cloning the tile it belongs to.
void writeHeight(TerrainField& field, i32 x, i32 z, float height, u8 material)
{
    const TileKey key{floorDiv(x, static_cast<i32>(TileEdge)), floorDiv(z, static_cast<i32>(TileEdge))};
    std::vector<float> heights(TileArea, 0.0f);
    std::vector<u8> materials(TileArea, 0);
    if (const HeightTile* existing = field.findTile(key); existing != nullptr) {
        std::copy(std::begin(existing->height), std::end(existing->height), heights.begin());
        std::copy(std::begin(existing->material), std::end(existing->material), materials.begin());
    }

    const auto localX = static_cast<u32>(floorMod(x, static_cast<i32>(TileEdge)));
    const auto localZ = static_cast<u32>(floorMod(z, static_cast<i32>(TileEdge)));
    heights[localZ * TileEdge + localX] = height;
    materials[localZ * TileEdge + localX] = material;
    field.setTile(key, heights, materials);
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
                const FieldSample after = afterEdit(field, x, worldY, z, depth, shape, material);
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

        for (u32 y = 0; y < BrickEdge; ++y) {
            const i32 worldY = brickY * static_cast<i32>(BrickEdge) + static_cast<i32>(y);
            if (worldY < lowY || worldY > highY) {
                continue;
            }
            const FieldSample edited = afterEdit(field, x, worldY, z, depth, shape, material);
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
    const i32 minY = low(bounds.min.y) - ColumnMargin;
    const i32 maxY = high(bounds.max.y) + ColumnMargin;

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
        const ColumnVerdict verdict = examineColumn(field, column.x, column.z, minY, maxY, depth, shape, material);
        const bool wasBricked = field.isBricked(column.x, column.z);

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
    return tile->height[localZ * TileEdge + localX];
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
