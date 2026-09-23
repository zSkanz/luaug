// The brushes (ADR 0082).
//
// Every verb here is the same shape: walk the voxels in the brush's box, work
// out a new occupancy from the old one, and write it through a `FieldWriter`.
// Nothing examines a column, promotes anything or re-derives an encoding --
// that was where every defect of the hybrid lived (D161, D162, D163).
#include "luaug/asset/terrain.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace luaug::asset {
namespace {

using core::DVec3;
using core::i32;
using core::u32;
using core::u8;
using core::usize;

// **The ramp**: how full a voxel is when its centre is `distance` metres from a
// surface (negative inside) -- `rampOccupancy`, which says why it is four voxels
// wide. Every brush's shape is a signed distance fed through this.
[[nodiscard]] float ramp(double distance, double voxel) noexcept
{
    return rampOccupancy(distance, voxel);
}

// How many voxels past a surface a write has to reach for the ramp to be whole:
// its half width, and one more for the voxel the surface is in.
constexpr i32 RampReach = static_cast<i32>(RampVoxels / 2.0f) + 1;

// 1 at the centre, 0 at the rim, flat at both ends.
[[nodiscard]] float falloff(double distance, double radius) noexcept
{
    if (!(radius > 0.0) || distance >= radius)
        return 0.0f;
    const double s = 1.0 - distance / radius;
    return static_cast<float>(s * s * (3.0 - 2.0 * s));
}

// The voxels whose centres lie in the world's floor-to-ceiling band.
struct Band
{
    i32 low = 0;
    i32 high = -1;
};

[[nodiscard]] Band bandOf(const TerrainField& field) noexcept
{
    const double voxel = static_cast<double>(field.settings().voxelSize);
    // Centre `(i + 0.5) v` inside `[min, max]`.
    return Band{static_cast<i32>(std::ceil(static_cast<double>(field.settings().minHeight) / voxel - 0.5)),
                static_cast<i32>(std::floor(static_cast<double>(field.settings().maxHeight) / voxel - 0.5))};
}

// Where ground laid on empty columns starts, as a voxel row: `LaidDepth` under
// `lowest`, rounded down to a chunk boundary so the slab's bottom is whole
// chunks, and never under the floor.
[[nodiscard]] i32 laidBase(const TerrainField& field, double lowest, const Band& band) noexcept
{
    constexpr auto edge = static_cast<i32>(ChunkEdge);
    const i32 row = field.voxelIndex(lowest - static_cast<double>(LaidDepth));
    return std::max(floorDiv(row, edge) * edge, band.low);
}

// The inclusive voxel box around `[low, high]` in metres, widened by the ramp's
// reach so its outer half is written too, and clamped to the band on y.
struct Box
{
    i32 minX = 0;
    i32 minY = 0;
    i32 minZ = 0;
    i32 maxX = -1;
    i32 maxY = -1;
    i32 maxZ = -1;

    [[nodiscard]] bool empty() const noexcept { return maxX < minX || maxY < minY || maxZ < minZ; }
};

[[nodiscard]] Box boxOf(const TerrainField& field, DVec3 low, DVec3 high) noexcept
{
    const double reach = static_cast<double>(field.settings().voxelSize) * static_cast<double>(RampReach);
    const Band band = bandOf(field);
    Box box;
    box.minX = field.voxelIndex(low.x - reach);
    box.minY = std::max(field.voxelIndex(low.y - reach), band.low);
    box.minZ = field.voxelIndex(low.z - reach);
    box.maxX = field.voxelIndex(high.x + reach);
    box.maxY = std::min(field.voxelIndex(high.y + reach), band.high);
    box.maxZ = field.voxelIndex(high.z + reach);
    return box;
}

// **Walks a box in chunk-then-row order**: z, then y, then x innermost, so
// consecutive voxels share a chunk and a row and the writer's cached chunk
// stays warm.
template <class Visit>
void walk(const Box& box, Visit&& visit)
{
    for (i32 z = box.minZ; z <= box.maxZ; ++z) {
        for (i32 y = box.minY; y <= box.maxY; ++y) {
            for (i32 x = box.minX; x <= box.maxX; ++x)
                visit(x, y, z);
        }
    }
}

// Adds (material not zero) or removes (material zero) a shape given as a signed
// distance at each voxel centre.
template <class Distance>
EditReport applyShape(TerrainField& field, const Box& box, u8 material, Distance&& distanceAt)
{
    EditReport report;
    if (box.empty())
        return report;
    const double voxel = static_cast<double>(field.settings().voxelSize);
    FieldWriter writer(field);
    walk(box, [&](i32 x, i32 y, i32 z) {
        const DVec3 centre{field.voxelCenter(x), field.voxelCenter(y), field.voxelCenter(z)};
        const float inside = ramp(distanceAt(centre), voxel);
        if (inside <= 0.0f && material != 0)
            return;
        const Voxel old = writer.get(x, y, z);
        if (material != 0) {
            // A union: the larger occupancy, and the brush's material where the
            // brush is what made it larger.
            const u8 occupancy = quantiseOccupancy(inside);
            if (occupancy > old.occupancy)
                writer.set(x, y, z, Voxel{occupancy, material});
        }
        else {
            // A subtraction: never fuller than the brush leaves room for.
            const u8 room = quantiseOccupancy(1.0f - inside);
            if (room < old.occupancy)
                writer.set(x, y, z, Voxel{room, old.material});
        }
    });
    writer.finish();
    report.touched = writer.changed();
    return report;
}

[[nodiscard]] double length(double x, double y, double z) noexcept
{
    return std::sqrt(x * x + y * y + z * z);
}

// **The voxels of a box, read a chunk at a time**, x fastest, then y, then z.
// Read voxel by voxel it is a search of the chunk table per voxel, and a big
// brush's box is a hundred thousand of them: the owner's "smooth stalls".
void readBox(const TerrainField& field, i32 x0, i32 y0, i32 z0, i32 sizeX, i32 sizeY, i32 sizeZ,
             std::vector<Voxel>& out)
{
    out.assign(static_cast<usize>(sizeX) * static_cast<usize>(sizeY) * static_cast<usize>(sizeZ), Voxel{});
    constexpr auto edge = static_cast<i32>(ChunkEdge);
    const auto slot = [&](i32 x, i32 y, i32 z) {
        return (static_cast<usize>(z - z0) * static_cast<usize>(sizeY) + static_cast<usize>(y - y0)) *
                   static_cast<usize>(sizeX) +
               static_cast<usize>(x - x0);
    };
    std::array<core::u16, ChunkEdge> row{};
    for (i32 cz = floorDiv(z0, edge); cz <= floorDiv(z0 + sizeZ - 1, edge); ++cz) {
        for (i32 cx = floorDiv(x0, edge); cx <= floorDiv(x0 + sizeX - 1, edge); ++cx) {
            for (const TerrainField::Entry& entry : field.column(cx, cz)) {
                const i32 cy = entry.first.y;
                const i32 lowY = std::max(y0, cy * edge);
                const i32 highY = std::min(y0 + sizeY, (cy + 1) * edge);
                if (lowY >= highY)
                    continue;
                const i32 lowX = std::max(x0, cx * edge);
                const i32 highX = std::min(x0 + sizeX, (cx + 1) * edge);
                const i32 lowZ = std::max(z0, cz * edge);
                const i32 highZ = std::min(z0 + sizeZ, (cz + 1) * edge);
                const TerrainChunk& chunk = *entry.second;
                for (i32 z = lowZ; z < highZ; ++z) {
                    for (i32 y = lowY; y < highY; ++y) {
                        Voxel* first = &out[slot(lowX, y, z)];
                        if (chunk.uniform()) {
                            std::fill(first, first + (highX - lowX), chunk.value());
                            continue;
                        }
                        chunk.readRow(static_cast<u32>(y - cy * edge), static_cast<u32>(z - cz * edge), row);
                        for (i32 x = lowX; x < highX; ++x)
                            first[x - lowX] = unpackVoxel(row[static_cast<usize>(x - cx * edge)]);
                    }
                }
            }
        }
    }
}

// The occupancies of one voxel column over `[low, high]`, read once so a brush
// that shifts or blurs reads the field as it was before the stroke.
struct ColumnBuffer
{
    i32 low = 0;
    std::vector<Voxel> voxels;

    [[nodiscard]] Voxel at(i32 y) const noexcept
    {
        const i32 index = y - low;
        if (index < 0)
            return voxels.empty() ? Voxel{} : voxels.front();
        if (index >= static_cast<i32>(voxels.size()))
            return voxels.empty() ? Voxel{} : voxels.back();
        return voxels[static_cast<usize>(index)];
    }
};

// Where to lay ground under a column's new top, and whether it had one. The
// shared core of `fillFlat` and `writeHeights`.
//
// **The top moves; what is under it stays.** A column whose top is below the
// target gains ground from its top up; one above it loses ground from its top
// down; one with no ground at all is filled from the floor. A tunnel under the
// top is below everything written (D162's promise, kept).
//
// `slope` is how much steeper than flat the ground is here, `sqrt(1 + |grad H|^2)`:
// a height's vertical distance divided by it is the distance to the surface,
// which is what the ramp wants (`RampVoxels`).
void moveColumnTop(FieldWriter& writer, const TerrainField& field, i32 x, i32 z, float target, u8 material,
                   const Band& band, double slope)
{
    const double voxel = static_cast<double>(field.settings().voxelSize);
    const double height = std::clamp(static_cast<double>(target), static_cast<double>(field.settings().minHeight),
                                     static_cast<double>(field.settings().maxHeight));
    const std::optional<float> top = field.columnTop(x, z);
    const i32 targetIndex = field.voxelIndex(height);
    // The ramp's reach along the vertical, which the slope stretches.
    const auto reach = static_cast<i32>(std::ceil(static_cast<double>(RampReach) * slope));
    const i32 oldTop = top.has_value() ? field.voxelIndex(static_cast<double>(*top)) : band.low;
    const i32 from = std::max(std::min(oldTop, targetIndex) - reach, band.low);
    const i32 to = std::min(std::max(oldTop, targetIndex) + reach, band.high);
    const bool filling = !top.has_value() || static_cast<double>(*top) < height;
    for (i32 y = top.has_value() ? from : band.low; y <= to; ++y) {
        const Voxel old = writer.get(x, y, z);
        const u8 wanted = quantiseOccupancy(ramp((field.voxelCenter(y) - height) / slope, voxel));
        // **Within the ramp round the new top, the ramp exactly.** Taking the
        // larger or the smaller of the old ramp and the new one put the top in
        // neither place where their slopes differ: a column levelled between
        // two steep neighbours came out a third of a metre low. Outside it,
        // only ever more ground under the top and less above it, so a tunnel
        // deeper than the ramp is left alone.
        u8 occupancy = old.occupancy;
        if (std::abs(y - targetIndex) <= reach)
            occupancy = wanted;
        else if (filling)
            occupancy = std::max(old.occupancy, wanted);
        else
            occupancy = std::min(old.occupancy, wanted);
        if (occupancy == old.occupancy)
            continue;
        writer.set(x, y, z, Voxel{occupancy, occupancy > old.occupancy || old.material == 0 ? material : old.material});
    }
}

// **Chunks a block of heights covers entirely, written as one value.** A
// heightmap laid on empty ground is otherwise a write per voxel from the floor
// up -- a kilometre square at a metre over a 256 m band is a quarter of a
// billion of them. Here a chunk every column of which is empty and fills past
// its top becomes one uniform value, and only the chunks the surface passes
// through are written voxel by voxel.
//
// Answers the first voxel still to be written in every column of the block,
// which is the floor when nothing was covered.
[[nodiscard]] i32 fillCoveredChunks(TerrainField& field, i32 chunkX, i32 chunkZ, std::span<const float> blockHeights,
                                    u8 material, const Band& band, double steepest, EditReport& report)
{
    constexpr auto edge = static_cast<i32>(ChunkEdge);
    // Only on a chunk-aligned floor: the voxels under an unaligned one would
    // need writing one by one anyway, and the saving is in the chunks.
    if (floorMod(band.low, edge) != 0)
        return band.low;
    float lowest = std::numeric_limits<float>::max();
    for (const float height : blockHeights) {
        if (std::isnan(height))
            return band.low;
        lowest = std::min(lowest, height);
    }
    const double voxel = static_cast<double>(field.settings().voxelSize);
    // The highest voxel that is full in every column: its centre deeper than
    // the ramp reaches under the lowest height, however steep the ground.
    const i32 fullBelow =
        field.voxelIndex(static_cast<double>(lowest) - voxel * static_cast<double>(RampReach) * steepest) - 1;
    i32 next = band.low;
    for (i32 chunkY = band.low / edge;; ++chunkY) {
        const i32 top = chunkY * edge + edge - 1;
        if (top > fullBelow || top > band.high)
            break;
        field.setChunk(ChunkKey{chunkX, chunkY, chunkZ},
                       std::make_shared<TerrainChunk>(Voxel{FullOccupancy, material}));
        report.touched += ChunkVolume;
        next = top + 1;
    }
    return next;
}

// The commonest non-zero value, lowest on a tie: what a chunk laid whole under
// columns of several materials is made of. Nobody sees it until they dig.
[[nodiscard]] u8 majority(std::span<const u8> materials) noexcept
{
    std::array<u32, 256> counts{};
    for (const u8 material : materials)
        counts[material] += 1;
    u8 best = 1;
    u32 most = 0;
    for (u32 id = 1; id < 256; ++id) {
        if (counts[id] > most) {
            most = counts[id];
            best = static_cast<u8>(id);
        }
    }
    return best;
}

} // namespace

EditReport fillBall(TerrainField& field, DVec3 center, double radius, u8 material)
{
    if (!(radius > 0.0))
        return {};
    const Box box = boxOf(field, DVec3{center.x - radius, center.y - radius, center.z - radius},
                          DVec3{center.x + radius, center.y + radius, center.z + radius});
    return applyShape(field, box, material,
                      [&](DVec3 p) { return length(p.x - center.x, p.y - center.y, p.z - center.z) - radius; });
}

EditReport fillBlock(TerrainField& field, DVec3 center, core::Vec3 size, u8 material)
{
    const double halfX = static_cast<double>(size.x) * 0.5;
    const double halfY = static_cast<double>(size.y) * 0.5;
    const double halfZ = static_cast<double>(size.z) * 0.5;
    if (!(halfX > 0.0) || !(halfY > 0.0) || !(halfZ > 0.0))
        return {};
    const Box box = boxOf(field, DVec3{center.x - halfX, center.y - halfY, center.z - halfZ},
                          DVec3{center.x + halfX, center.y + halfY, center.z + halfZ});
    return applyShape(field, box, material, [&](DVec3 p) {
        return std::max(
            {std::abs(p.x - center.x) - halfX, std::abs(p.y - center.y) - halfY, std::abs(p.z - center.z) - halfZ});
    });
}

EditReport fillCylinder(TerrainField& field, DVec3 center, double height, double radius, u8 material)
{
    const double half = height * 0.5;
    if (!(radius > 0.0) || !(half > 0.0))
        return {};
    const Box box = boxOf(field, DVec3{center.x - radius, center.y - half, center.z - radius},
                          DVec3{center.x + radius, center.y + half, center.z + radius});
    return applyShape(field, box, material, [&](DVec3 p) {
        const double radial = std::sqrt((p.x - center.x) * (p.x - center.x) + (p.z - center.z) * (p.z - center.z));
        return std::max(radial - radius, std::abs(p.y - center.y) - half);
    });
}

EditReport fillFlat(TerrainField& field, DVec3 center, float size, float height, u8 material)
{
    if (!(size > 0.0f) || material == 0 || std::isnan(height))
        return {};
    const double half = static_cast<double>(size) * 0.5;
    const i32 firstX = field.voxelIndex(center.x - half);
    const i32 firstZ = field.voxelIndex(center.z - half);
    const i32 lastX = field.voxelIndex(center.x + half) - 1;
    const i32 lastZ = field.voxelIndex(center.z + half) - 1;
    if (lastX < firstX || lastZ < firstZ)
        return {};
    // **Chunk column by chunk column, and every one that is whole and empty is
    // the SAME column.** Flat ground over empty columns comes out identical in
    // each (the slab's base and the slope are the same everywhere), so it is
    // laid once and shared, as a snapshot shares it: the first edit to a chunk
    // clones that chunk alone. Written column by column, 5 km of ground was
    // thirty seconds and a quarter of a gigabyte before anybody touched it --
    // the owner's "it froze". Every other column (at the square's edge, or
    // holding ground already) goes through `writeHeights` on its own block,
    // which for flat ground is the same result as one call over the square.
    // Counted wide: 5 km of ground is more voxels than a `u32` holds, and the
    // report says so by saturating rather than wrapping.
    core::u64 touched = 0;
    constexpr auto edge = static_cast<i32>(ChunkEdge);
    std::vector<TerrainField::Entry> column;
    const auto laidColumn = [&]() -> const std::vector<TerrainField::Entry>& {
        if (column.empty()) {
            TerrainField scratch(field.settings());
            const std::vector<float> block(ChunkRows, height);
            (void)writeHeights(scratch, 0, 0, ChunkEdge, block, material);
            column.assign(scratch.chunks().begin(), scratch.chunks().end());
        }
        return column;
    };
    TerrainField shared(field.settings());
    std::vector<float> block;
    for (i32 chunkX = floorDiv(firstX, edge); chunkX <= floorDiv(lastX, edge); ++chunkX) {
        for (i32 chunkZ = floorDiv(firstZ, edge); chunkZ <= floorDiv(lastZ, edge); ++chunkZ) {
            const i32 lowX = std::max(firstX, chunkX * edge);
            const i32 highX = std::min(lastX, chunkX * edge + edge - 1);
            const i32 lowZ = std::max(firstZ, chunkZ * edge);
            const i32 highZ = std::min(lastZ, chunkZ * edge + edge - 1);
            const bool whole = highX - lowX + 1 == edge && highZ - lowZ + 1 == edge;
            if (whole && field.column(chunkX, chunkZ).empty()) {
                // In key order (x, then z, then y), so each lands at the end.
                for (const TerrainField::Entry& laid : laidColumn()) {
                    shared.setChunk(ChunkKey{chunkX, laid.first.y, chunkZ}, laid.second);
                    touched += ChunkVolume;
                }
                continue;
            }
            const auto columns = static_cast<u32>(highX - lowX + 1);
            block.assign(static_cast<usize>(columns) * static_cast<usize>(highZ - lowZ + 1), height);
            touched += writeHeights(field, lowX, lowZ, columns, block, material).touched;
        }
    }
    field.shareFrom(shared);
    return EditReport{static_cast<u32>(std::min<core::u64>(touched, std::numeric_limits<u32>::max()))};
}

EditReport writeHeights(TerrainField& field, i32 firstX, i32 firstZ, u32 columns, std::span<const float> heights,
                        u8 material)
{
    if (material == 0)
        return {};
    const u8 one[1] = {material};
    return writeHeights(field, firstX, firstZ, columns, heights, std::span<const u8>(one));
}

EditReport writeHeights(TerrainField& field, i32 firstX, i32 firstZ, u32 columns, std::span<const float> heights,
                        std::span<const u8> materials)
{
    EditReport report;
    if (columns == 0 || heights.empty() || materials.empty())
        return report;
    const auto rows = static_cast<u32>(heights.size() / columns);
    if (rows == 0)
        return report;
    constexpr auto edge = static_cast<i32>(ChunkEdge);
    // **Empty columns are laid as a slab** (`LaidDepth`) under the lowest height
    // of the whole table, so a heightmap's valleys and its peaks stand on one
    // bottom. Everything below reads the band from there up.
    Band band = bandOf(field);
    {
        double lowest = std::numeric_limits<double>::max();
        for (usize at = 0; at < heights.size(); ++at) {
            if (!std::isnan(heights[at]) && (materials.size() == 1 || (at < materials.size() && materials[at] != 0)))
                lowest = std::min(lowest, static_cast<double>(heights[at]));
        }
        if (lowest == std::numeric_limits<double>::max())
            return report;
        lowest = std::clamp(lowest, static_cast<double>(field.settings().minHeight),
                            static_cast<double>(field.settings().maxHeight));
        band.low = laidBase(field, lowest, band);
    }

    // Chunk column by chunk column, so the covered-chunk shortcut can see a
    // whole block of heights at once.
    const i32 lastX = firstX + static_cast<i32>(columns) - 1;
    const i32 lastZ = firstZ + static_cast<i32>(rows) - 1;
    std::vector<float> blockHeights(ChunkRows);
    std::vector<u8> blockMaterials(ChunkRows);
    const auto materialAt = [&](usize index) {
        return materials.size() == 1 ? materials[0] : (index < materials.size() ? materials[index] : u8{0});
    };
    // **How steep the table is at a column**, `sqrt(1 + |grad H|^2)`, from its
    // neighbours in the table -- one-sided at an edge, flat where there is
    // none. A height is a vertical distance; divided by this it is the
    // distance to the surface, which keeps a steep slope's voxels on the ramp
    // rather than clamped into terraces (`RampVoxels`).
    const double voxel = static_cast<double>(field.settings().voxelSize);
    const auto heightOf = [&](i32 x, i32 z) -> double {
        if (x < firstX || x > lastX || z < firstZ || z > lastZ)
            return std::numeric_limits<double>::quiet_NaN();
        const usize index = static_cast<usize>(z - firstZ) * columns + static_cast<usize>(x - firstX);
        if (materialAt(index) == 0 || std::isnan(heights[index]))
            return std::numeric_limits<double>::quiet_NaN();
        // Clamped as the column will be, so a height past the ceiling does not
        // make its neighbours' slope look steeper than the ground they get.
        return std::clamp(static_cast<double>(heights[index]), static_cast<double>(field.settings().minHeight),
                          static_cast<double>(field.settings().maxHeight));
    };
    const auto slopeAt = [&](i32 x, i32 z) {
        const double here = heightOf(x, z);
        const auto along = [&](i32 dx, i32 dz) {
            const double ahead = heightOf(x + dx, z + dz);
            const double behind = heightOf(x - dx, z - dz);
            if (!std::isnan(ahead) && !std::isnan(behind))
                return (ahead - behind) / (2.0 * voxel);
            if (!std::isnan(ahead))
                return (ahead - here) / voxel;
            if (!std::isnan(behind))
                return (here - behind) / voxel;
            return 0.0;
        };
        const double gx = along(1, 0);
        const double gz = along(0, 1);
        const double slope = std::sqrt(1.0 + gx * gx + gz * gz);
        return std::isfinite(slope) ? slope : 1.0;
    };
    for (i32 chunkZ = floorDiv(firstZ, edge); chunkZ <= floorDiv(lastZ, edge); ++chunkZ) {
        for (i32 chunkX = floorDiv(firstX, edge); chunkX <= floorDiv(lastX, edge); ++chunkX) {
            // The block's heights, NaN where the table does not reach.
            const bool columnEmpty = field.column(chunkX, chunkZ).empty();
            for (i32 localZ = 0; localZ < edge; ++localZ) {
                for (i32 localX = 0; localX < edge; ++localX) {
                    const i32 x = chunkX * edge + localX;
                    const i32 z = chunkZ * edge + localZ;
                    const usize slot = static_cast<usize>(localZ) * ChunkEdge + static_cast<usize>(localX);
                    if (x < firstX || x > lastX || z < firstZ || z > lastZ) {
                        blockHeights[slot] = std::numeric_limits<float>::quiet_NaN();
                        continue;
                    }
                    const usize index = static_cast<usize>(z - firstZ) * columns + static_cast<usize>(x - firstX);
                    blockMaterials[slot] = materialAt(index);
                    // A column with no material is not ground: not written.
                    blockHeights[slot] =
                        blockMaterials[slot] == 0 ? std::numeric_limits<float>::quiet_NaN() : heights[index];
                }
            }
            // **A block of empty ground is laid by chunks first**, where it can
            // be: see `fillCoveredChunks`.
            double steepest = 1.0;
            if (columnEmpty) {
                for (i32 localZ = 0; localZ < edge; ++localZ) {
                    for (i32 localX = 0; localX < edge; ++localX)
                        steepest = std::max(steepest, slopeAt(chunkX * edge + localX, chunkZ * edge + localZ));
                }
            }
            const i32 start = columnEmpty ? fillCoveredChunks(field, chunkX, chunkZ, blockHeights,
                                                              majority(blockMaterials), band, steepest, report)
                                          : band.low;

            FieldWriter writer(field);
            const bool shortcut = columnEmpty;
            for (i32 localZ = 0; localZ < edge; ++localZ) {
                for (i32 localX = 0; localX < edge; ++localX) {
                    const usize slot = static_cast<usize>(localZ) * ChunkEdge + static_cast<usize>(localX);
                    const float height = blockHeights[slot];
                    if (std::isnan(height))
                        continue;
                    const u8 material = blockMaterials[slot];
                    const i32 x = chunkX * edge + localX;
                    const i32 z = chunkZ * edge + localZ;
                    const double slope = slopeAt(x, z);
                    if (shortcut) {
                        // An empty column of chunks: nothing to move, only to
                        // lay, from where the covered chunks stop.
                        const double clamped =
                            std::clamp(static_cast<double>(height), static_cast<double>(field.settings().minHeight),
                                       static_cast<double>(field.settings().maxHeight));
                        const auto reach = static_cast<i32>(std::ceil(static_cast<double>(RampReach) * slope));
                        const i32 to = std::min(field.voxelIndex(clamped) + reach, band.high);
                        for (i32 y = start; y <= to; ++y) {
                            const u8 occupancy =
                                quantiseOccupancy(ramp((field.voxelCenter(y) - clamped) / slope, voxel));
                            if (occupancy > writer.get(x, y, z).occupancy)
                                writer.set(x, y, z, Voxel{occupancy, material});
                        }
                        continue;
                    }
                    moveColumnTop(writer, field, x, z, height, material, band, slope);
                }
            }
            writer.finish();
            report.touched += writer.changed();
        }
    }
    return report;
}

EditReport smoothBall(TerrainField& field, DVec3 center, double radius, float strength)
{
    EditReport report;
    const float amount = std::clamp(strength, 0.0f, 1.0f);
    if (!(radius > 0.0) || amount <= 0.0f)
        return report;
    const Box box = boxOf(field, DVec3{center.x - radius, center.y - radius, center.z - radius},
                          DVec3{center.x + radius, center.y + radius, center.z + radius});
    if (box.empty())
        return report;

    // **A box blur a few voxels wide, separable, from a copy.** The occupancy
    // ramps over four voxels (`RampVoxels`), and the mean of a linear ramp is
    // the ramp: a three-voxel blur only rounded a step's two edges and left the
    // step. The kernel grows with the brush, from one voxel either side to
    // four, so a big brush softens big shapes. Read once, before any write: a
    // blur that read its own writes would smear in the walk's direction.
    const double voxel = static_cast<double>(field.settings().voxelSize);
    const i32 reach = std::clamp(static_cast<i32>(std::lround(radius / (3.0 * voxel))), 1, 4);
    const i32 x0 = box.minX - reach;
    const i32 y0 = box.minY - reach;
    const i32 z0 = box.minZ - reach;
    const i32 sizeX = box.maxX - box.minX + 1 + 2 * reach;
    const i32 sizeY = box.maxY - box.minY + 1 + 2 * reach;
    const i32 sizeZ = box.maxZ - box.minZ + 1 + 2 * reach;
    const auto index = [&](i32 x, i32 y, i32 z) {
        return (static_cast<usize>(z - z0) * static_cast<usize>(sizeY) + static_cast<usize>(y - y0)) *
                   static_cast<usize>(sizeX) +
               static_cast<usize>(x - x0);
    };
    std::vector<Voxel> copy;
    readBox(field, x0, y0, z0, sizeX, sizeY, sizeZ, copy);
    std::vector<float> blurred(copy.size());
    for (usize at = 0; at < copy.size(); ++at)
        blurred[at] = occupancyOf(copy[at]);
    // One axis at a time, each pass reading the last; the copy's own edge is
    // held rather than padded with air, so the blur does not eat into ground
    // at the edge of what was read. **Each line is a running sum**, so a pass
    // costs the same whatever the kernel's width.
    const auto blurAlong = [&](i32 length, usize stride, i32 lines, auto&& lineStart) {
        std::vector<double> sums(static_cast<usize>(length + 2 * reach) + 1);
        const float width = static_cast<float>(2 * reach + 1);
        for (i32 line = 0; line < lines; ++line) {
            float* values = &blurred[lineStart(line)];
            sums[0] = 0.0;
            for (i32 k = 0; k < length + 2 * reach; ++k) {
                const i32 from = std::clamp(k - reach, 0, length - 1);
                sums[static_cast<usize>(k) + 1] =
                    sums[static_cast<usize>(k)] + static_cast<double>(values[static_cast<usize>(from) * stride]);
            }
            for (i32 at = 0; at < length; ++at) {
                const double sum = sums[static_cast<usize>(at + 2 * reach) + 1] - sums[static_cast<usize>(at)];
                values[static_cast<usize>(at) * stride] = static_cast<float>(sum) / width;
            }
        }
    };
    const auto strideY = static_cast<usize>(sizeX);
    const auto strideZ = static_cast<usize>(sizeX) * static_cast<usize>(sizeY);
    blurAlong(sizeX, 1, sizeY * sizeZ, [&](i32 line) { return static_cast<usize>(line) * strideY; });
    blurAlong(sizeY, strideY, sizeX * sizeZ,
              [&](i32 line) { return static_cast<usize>(line / sizeX) * strideZ + static_cast<usize>(line % sizeX); });
    blurAlong(sizeZ, strideZ, sizeX * sizeY, [&](i32 line) { return static_cast<usize>(line); });

    FieldWriter writer(field);
    walk(box, [&](i32 x, i32 y, i32 z) {
        const double distance =
            length(field.voxelCenter(x) - center.x, field.voxelCenter(y) - center.y, field.voxelCenter(z) - center.z);
        const float weight = falloff(distance, radius) * amount;
        if (weight <= 0.0f)
            return;
        const Voxel old = copy[index(x, y, z)];
        const float before = occupancyOf(old);
        const u8 occupancy = quantiseOccupancy(before + (blurred[index(x, y, z)] - before) * weight);
        if (occupancy == old.occupancy)
            return;
        // Ground that appears where there was air takes its fullest
        // neighbour's material: smoothing a grass edge grows grass.
        u8 material = old.material;
        if (material == 0) {
            u8 fullest = 0;
            for (i32 dz = -1; dz <= 1; ++dz) {
                for (i32 dy = -1; dy <= 1; ++dy) {
                    for (i32 dx = -1; dx <= 1; ++dx) {
                        const Voxel near = copy[index(x + dx, y + dy, z + dz)];
                        if (near.occupancy > fullest) {
                            fullest = near.occupancy;
                            material = near.material;
                        }
                    }
                }
            }
        }
        writer.set(x, y, z, Voxel{occupancy, material});
    });
    writer.finish();
    report.touched = writer.changed();
    return report;
}

EditReport flattenBall(TerrainField& field, DVec3 center, double radius, float height, float strength)
{
    EditReport report;
    const float amount = std::clamp(strength, 0.0f, 1.0f);
    if (!(radius > 0.0) || amount <= 0.0f || std::isnan(height))
        return report;
    const Box box = boxOf(field, DVec3{center.x - radius, center.y - radius, center.z - radius},
                          DVec3{center.x + radius, center.y + radius, center.z + radius});
    if (box.empty())
        return report;
    const double voxel = static_cast<double>(field.settings().voxelSize);
    // What ground laid under the plane is made of: whatever is under the brush.
    u8 fill = sampleField(field, DVec3{center.x, static_cast<double>(height) - voxel, center.z}).material;
    if (fill == 0)
        fill = sampleField(field, center).material;
    if (fill == 0)
        fill = 1;

    FieldWriter writer(field);
    walk(box, [&](i32 x, i32 y, i32 z) {
        const double cy = field.voxelCenter(y);
        const double distance = length(field.voxelCenter(x) - center.x, cy - center.y, field.voxelCenter(z) - center.z);
        const float weight = falloff(distance, radius) * amount;
        if (weight <= 0.0f)
            return;
        const float target = ramp(cy - static_cast<double>(height), voxel);
        const Voxel old = writer.get(x, y, z);
        const float before = occupancyOf(old);
        const u8 occupancy = quantiseOccupancy(before + (target - before) * weight);
        if (occupancy == old.occupancy)
            return;
        writer.set(x, y, z, Voxel{occupancy, old.material != 0 ? old.material : fill});
    });
    writer.finish();
    report.touched = writer.changed();
    return report;
}

EditReport raiseBall(TerrainField& field, DVec3 center, double radius, float amount, u8 material)
{
    EditReport report;
    if (!(radius > 0.0) || amount == 0.0f || std::isnan(amount))
        return report;
    const double voxel = static_cast<double>(field.settings().voxelSize);
    const double reach = radius + std::abs(static_cast<double>(amount));
    const Band band = bandOf(field);
    const Box box = boxOf(field, DVec3{center.x - radius, center.y - reach, center.z - radius},
                          DVec3{center.x + radius, center.y + reach, center.z + radius});
    if (box.empty())
        return report;
    const bool raising = amount > 0.0f;

    FieldWriter writer(field);
    ColumnBuffer column;
    for (i32 z = box.minZ; z <= box.maxZ; ++z) {
        for (i32 x = box.minX; x <= box.maxX; ++x) {
            const double dx = field.voxelCenter(x) - center.x;
            const double dz = field.voxelCenter(z) - center.z;
            const double shift =
                static_cast<double>(amount) * static_cast<double>(falloff(std::sqrt(dx * dx + dz * dz), radius));
            if (std::abs(shift) < 1e-6)
                continue;

            // **The reach follows the surface it is moving.** A brush centred on
            // the ground reaches `radius` above and below it -- but a script
            // raising the same spot again and again would otherwise see its
            // hill stop growing at the reach's ceiling and go flat on top. So
            // when the column is solid all the way from inside the reach up to
            // its top, the window runs up to the top; and when lowering finds
            // the top already under the reach, down to it. A column with air
            // between the brush and its top -- a brush in a cave -- keeps the
            // plain reach, so raising a cave's floor does not lift the hill
            // over it.
            const i32 extra = static_cast<i32>(std::ceil(std::abs(shift) / voxel)) + RampReach;
            i32 lowY = box.minY;
            i32 highY = box.maxY;
            if (const std::optional<float> top = field.columnTop(x, z); top.has_value()) {
                const i32 topIndex = field.voxelIndex(static_cast<double>(*top));
                if (raising && topIndex + extra > highY && topIndex >= box.minY) {
                    bool solid = true;
                    for (i32 y = highY; y < topIndex && solid; ++y)
                        solid = field.voxel(x, y, z).occupancy >= 128;
                    if (solid)
                        highY = std::min(topIndex + extra, band.high);
                }
                if (!raising && topIndex - extra < lowY)
                    lowY = std::max(topIndex - extra, band.low);
            }

            // The column as it was, a shift's worth wider than the window on
            // both ends so the shifted read never runs off it.
            column.low = lowY - extra;
            column.voxels.clear();
            bool anyGround = false;
            for (i32 y = column.low; y <= highY + extra; ++y) {
                const Voxel got = field.voxel(x, y, z);
                anyGround = anyGround || got.occupancy > 0;
                column.voxels.push_back(got);
            }

            // **An empty column is laid from `center`'s height up**, when there
            // is a material to lay: the first stroke on an empty terrain makes
            // ground rather than nothing. As the slab every other laying verb
            // makes (`LaidDepth`), so ground extended sideways from generated
            // ground has the same bottom.
            if (!anyGround) {
                if (!raising || material == 0 || field.columnTop(x, z).has_value())
                    continue;
                const double top = center.y + shift;
                const i32 to = std::min(field.voxelIndex(top) + RampReach, band.high);
                for (i32 y = laidBase(field, center.y, band); y <= to; ++y) {
                    const u8 occupancy = quantiseOccupancy(ramp(field.voxelCenter(y) - top, voxel));
                    if (occupancy > writer.get(x, y, z).occupancy)
                        writer.set(x, y, z, Voxel{occupancy, material});
                }
                continue;
            }

            // The column shifted by `shift`: the occupancy at `y - shift`,
            // interpolated between the two voxel centres around it.
            const double steps = shift / voxel;
            for (i32 y = lowY; y <= highY; ++y) {
                const double source = static_cast<double>(y) - steps;
                const auto below = static_cast<i32>(std::floor(source));
                const auto t = static_cast<float>(source - static_cast<double>(below));
                const Voxel a = column.at(below);
                const Voxel b = column.at(below + 1);
                const float shifted = occupancyOf(a) + (occupancyOf(b) - occupancyOf(a)) * t;
                const u8 occupancy = quantiseOccupancy(shifted);
                const Voxel old = column.at(y);
                if (raising && occupancy > old.occupancy) {
                    // The material of whichever source voxel is fuller: raising
                    // grass keeps grass on top.
                    u8 from = a.occupancy >= b.occupancy ? a.material : b.material;
                    if (from == 0)
                        from = a.material != 0 ? a.material : b.material;
                    if (from == 0)
                        from = old.material != 0 ? old.material : (material != 0 ? material : 1);
                    writer.set(x, y, z, Voxel{occupancy, from});
                }
                else if (!raising && occupancy < old.occupancy) {
                    writer.set(x, y, z, Voxel{occupancy, old.material});
                }
            }
        }
    }
    writer.finish();
    report.touched = writer.changed();
    return report;
}

EditReport growBall(TerrainField& field, DVec3 center, double radius, float amount, u8 material)
{
    EditReport report;
    if (!(radius > 0.0) || amount == 0.0f || std::isnan(amount))
        return report;
    const Box box = boxOf(field, DVec3{center.x - radius, center.y - radius, center.z - radius},
                          DVec3{center.x + radius, center.y + radius, center.z + radius});
    if (box.empty())
        return report;
    const double voxel = static_cast<double>(field.settings().voxelSize);
    const bool growing = amount > 0.0f;
    // **Capped at the ramp's outer half.** Past it the air holds no occupancy
    // to grow from, so a bigger step would only stop at the same place.
    const double step = std::min(std::abs(static_cast<double>(amount)), voxel * static_cast<double>(RampVoxels) * 0.5);

    // Read once, a voxel wider than the box, so every voxel sees its six
    // neighbours as they were before the stamp.
    const i32 x0 = box.minX - 1;
    const i32 y0 = box.minY - 1;
    const i32 z0 = box.minZ - 1;
    const i32 sizeX = box.maxX - box.minX + 3;
    const i32 sizeY = box.maxY - box.minY + 3;
    const i32 sizeZ = box.maxZ - box.minZ + 3;
    const auto index = [&](i32 x, i32 y, i32 z) {
        return (static_cast<usize>(z - z0) * static_cast<usize>(sizeY) + static_cast<usize>(y - y0)) *
                   static_cast<usize>(sizeX) +
               static_cast<usize>(x - x0);
    };
    std::vector<Voxel> copy(static_cast<usize>(sizeX) * static_cast<usize>(sizeY) * static_cast<usize>(sizeZ));
    for (i32 z = z0; z < z0 + sizeZ; ++z) {
        for (i32 y = y0; y < y0 + sizeY; ++y) {
            for (i32 x = x0; x < x0 + sizeX; ++x)
                copy[index(x, y, z)] = field.voxel(x, y, z);
        }
    }

    static constexpr std::array<std::array<i32, 3>, 6> Faces{{
        {-1, 0, 0},
        {1, 0, 0},
        {0, -1, 0},
        {0, 1, 0},
        {0, 0, -1},
        {0, 0, 1},
    }};
    // One voxel's worth of ramp: how much a voxel's occupancy may differ from
    // its neighbour's where the surface is square to the axis between them.
    const float slope = 1.0f / RampVoxels;

    FieldWriter writer(field);
    walk(box, [&](i32 x, i32 y, i32 z) {
        const double distance =
            length(field.voxelCenter(x) - center.x, field.voxelCenter(y) - center.y, field.voxelCenter(z) - center.z);
        const float weight = falloff(distance, radius);
        if (weight <= 0.0f)
            return;
        // **The surface moves along its own normal, by `step` at the centre.**
        // Inside the ramp, occupancy is distance, so adding `step / (4 v)` moves
        // the half crossing out by `step` whichever way the ground faces -- up
        // on a field, sideways on a cliff, down under an overhang. A voxel is
        // first raised to its fullest neighbour less one voxel of ramp, so air
        // next to ground has something to grow from and ground next to air
        // something to wear from; in a ramp already whole, that changes nothing.
        const Voxel old = copy[index(x, y, z)];
        const float before = occupancyOf(old);
        const float delta = static_cast<float>(step / (voxel * static_cast<double>(RampVoxels))) * weight;
        float base = before;
        Voxel fullest{};
        for (const std::array<i32, 3>& face : Faces) {
            const Voxel near = copy[index(x + face[0], y + face[1], z + face[2])];
            if (growing) {
                base = std::max(base, occupancyOf(near) - slope);
                if (near.occupancy > fullest.occupancy)
                    fullest = near;
            }
            else {
                base = std::min(base, occupancyOf(near) + slope);
            }
        }
        if (growing) {
            // Nothing to grow from: air with no ground within a voxel of it.
            if (base <= 0.0f)
                return;
            const u8 occupancy = quantiseOccupancy(base + delta);
            if (occupancy <= old.occupancy)
                return;
            // New ground is made of what it grew from: growing grass grows
            // grass.
            u8 made = old.material != 0 ? old.material : fullest.material;
            if (made == 0)
                made = material != 0 ? material : 1;
            writer.set(x, y, z, Voxel{occupancy, made});
        }
        else {
            if (base >= 1.0f)
                return;
            const u8 occupancy = quantiseOccupancy(base - delta);
            if (occupancy >= old.occupancy)
                return;
            writer.set(x, y, z, Voxel{occupancy, old.material});
        }
    });
    writer.finish();
    report.touched = writer.changed();
    return report;
}

EditReport paintBall(TerrainField& field, DVec3 center, double radius, u8 material)
{
    EditReport report;
    if (!(radius > 0.0) || material == 0)
        return report;
    const Box box = boxOf(field, DVec3{center.x - radius, center.y - radius, center.z - radius},
                          DVec3{center.x + radius, center.y + radius, center.z + radius});
    if (box.empty())
        return report;
    FieldWriter writer(field);
    walk(box, [&](i32 x, i32 y, i32 z) {
        if (length(field.voxelCenter(x) - center.x, field.voxelCenter(y) - center.y, field.voxelCenter(z) - center.z) >
            radius)
            return;
        const Voxel old = writer.get(x, y, z);
        if (old.occupancy > 0 && old.material != material)
            writer.set(x, y, z, Voxel{old.occupancy, material});
    });
    writer.finish();
    report.touched = writer.changed();
    return report;
}

EditReport replaceMaterial(TerrainField& field, DVec3 minCorner, DVec3 maxCorner, u8 from, u8 to)
{
    EditReport report;
    if (from == 0 || to == 0 || from == to)
        return report;
    Box box;
    box.minX = field.voxelIndex(std::min(minCorner.x, maxCorner.x));
    box.minY = field.voxelIndex(std::min(minCorner.y, maxCorner.y));
    box.minZ = field.voxelIndex(std::min(minCorner.z, maxCorner.z));
    box.maxX = field.voxelIndex(std::max(minCorner.x, maxCorner.x));
    box.maxY = field.voxelIndex(std::max(minCorner.y, maxCorner.y));
    box.maxZ = field.voxelIndex(std::max(minCorner.z, maxCorner.z));
    FieldWriter writer(field);
    walk(box, [&](i32 x, i32 y, i32 z) {
        const Voxel old = writer.get(x, y, z);
        if (old.occupancy > 0 && old.material == from)
            writer.set(x, y, z, Voxel{old.occupancy, to});
    });
    writer.finish();
    report.touched = writer.changed();
    return report;
}

std::optional<float> heightAt(const TerrainField& field, double x, double z)
{
    const double voxel = static_cast<double>(field.settings().voxelSize);
    if (!(voxel > 0.0))
        return std::nullopt;
    // Bilinear between the four column centres around the point. The column the
    // point is IN decides whether there is ground here at all; a neighbour with
    // none stands in with its value, so the edge of a plateau does not sag.
    const std::optional<float> own = field.columnTop(field.voxelIndex(x), field.voxelIndex(z));
    if (!own.has_value())
        return std::nullopt;
    const double gridX = x / voxel - 0.5;
    const double gridZ = z / voxel - 0.5;
    const auto lowX = static_cast<i32>(std::floor(gridX));
    const auto lowZ = static_cast<i32>(std::floor(gridZ));
    const auto tx = static_cast<float>(gridX - std::floor(gridX));
    const auto tz = static_cast<float>(gridZ - std::floor(gridZ));
    const auto top = [&](i32 cx, i32 cz) { return field.columnTop(cx, cz).value_or(*own); };
    const float a = top(lowX, lowZ);
    const float b = top(lowX + 1, lowZ);
    const float c = top(lowX, lowZ + 1);
    const float d = top(lowX + 1, lowZ + 1);
    const float near = a + (b - a) * tx;
    const float far = c + (d - c) * tx;
    return near + (far - near) * tz;
}

} // namespace luaug::asset
