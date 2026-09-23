#include "luaug/asset/terrain_mesher.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <unordered_map>
#include <vector>

namespace luaug::asset {
namespace {

using core::i32;
using core::u16;
using core::u32;
using core::u8;
using core::usize;
using core::Vec3;

// The eight corners of a lattice cell: bit 0 is x, bit 1 is y, bit 2 is z.
constexpr std::array<std::array<i32, 3>, 8> CornerOffsets{{
    {0, 0, 0},
    {1, 0, 0},
    {0, 1, 0},
    {1, 1, 0},
    {0, 0, 1},
    {1, 0, 1},
    {0, 1, 1},
    {1, 1, 1},
}};

// The twelve edges of a cell, as corner pairs.
constexpr std::array<std::array<int, 2>, 12> CellEdges{{
    {0, 1},
    {2, 3},
    {4, 5},
    {6, 7}, // along x
    {0, 2},
    {1, 3},
    {4, 6},
    {5, 7}, // along y
    {0, 4},
    {1, 5},
    {2, 6},
    {3, 7}, // along z
}};

// Where along an edge occupancy passes one half. Guarded for two equal ends,
// which cannot straddle the half -- but a caller that asks should get the
// middle, not a division by zero.
[[nodiscard]] float crossingAt(float a, float b) noexcept
{
    const float delta = b - a;
    if (std::abs(delta) < 1e-6f)
        return 0.5f;
    return std::clamp((0.5f - a) / delta, 0.0f, 1.0f);
}

// **The voxels of a box, at one level, read a chunk at a time.** Sampling voxel
// by voxel through the field is a binary search per sample; a node is tens of
// thousands of samples, and this is one search per chunk the box touches.
void gather(const TerrainField& field, u32 level, i32 x0, i32 y0, i32 z0, i32 sizeX, i32 sizeY, i32 sizeZ,
            std::vector<u16>& out)
{
    out.assign(static_cast<usize>(sizeX) * static_cast<usize>(sizeY) * static_cast<usize>(sizeZ), 0);
    const i32 edge = static_cast<i32>(ChunkEdge) >> level;
    const auto slot = [&](i32 x, i32 y, i32 z) {
        return (static_cast<usize>(z - z0) * static_cast<usize>(sizeY) + static_cast<usize>(y - y0)) *
                   static_cast<usize>(sizeX) +
               static_cast<usize>(x - x0);
    };
    std::array<u16, ChunkEdge> row{};
    for (i32 cz = floorDiv(z0, edge); cz <= floorDiv(z0 + sizeZ - 1, edge); ++cz) {
        for (i32 cx = floorDiv(x0, edge); cx <= floorDiv(x0 + sizeX - 1, edge); ++cx) {
            // One column of chunks: its entries in y order, walked alongside.
            const std::span<const TerrainField::Entry> column = field.column(cx, cz);
            for (const TerrainField::Entry& entry : column) {
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
                if (chunk.uniform()) {
                    const u16 value = packVoxel(chunk.value());
                    for (i32 z = lowZ; z < highZ; ++z) {
                        for (i32 y = lowY; y < highY; ++y) {
                            u16* first = &out[slot(lowX, y, z)];
                            std::fill(first, first + (highX - lowX), value);
                        }
                    }
                    continue;
                }
                if (level > 0)
                    chunk.prepareMip(level);
                for (i32 z = lowZ; z < highZ; ++z) {
                    for (i32 y = lowY; y < highY; ++y) {
                        const auto ly = static_cast<u32>(y - cy * edge);
                        const auto lz = static_cast<u32>(z - cz * edge);
                        if (level == 0) {
                            chunk.readRow(ly, lz, row);
                            for (i32 x = lowX; x < highX; ++x)
                                out[slot(x, y, z)] = row[static_cast<usize>(x - cx * edge)];
                        }
                        else {
                            for (i32 x = lowX; x < highX; ++x)
                                out[slot(x, y, z)] =
                                    packVoxel(chunk.mip(level, static_cast<u32>(x - cx * edge), ly, lz));
                        }
                    }
                }
            }
        }
    }
}

} // namespace

namespace {

// Whether one face layer of a chunk is ground all the way across: `axis` 0, 1
// or 2 for x, y or z, and `high` for the layer at 31 rather than 0.
[[nodiscard]] bool faceSolid(const TerrainChunk& chunk, u32 axis, bool high) noexcept
{
    if (chunk.uniform())
        return chunk.value().occupancy >= 128;
    const u32 at = high ? ChunkEdge - 1 : 0;
    for (u32 a = 0; a < ChunkEdge; ++a) {
        for (u32 b = 0; b < ChunkEdge; ++b) {
            const Voxel voxel =
                axis == 0 ? chunk.get(at, a, b) : (axis == 1 ? chunk.get(b, at, a) : chunk.get(b, a, at));
            if (voxel.occupancy < 128)
                return false;
        }
    }
    return true;
}

} // namespace

std::vector<std::pair<i32, i32>> activeRuns(const TerrainField& field, i32 chunkX, i32 chunkZ, i32 across)
{
    constexpr auto edge = static_cast<i32>(ChunkEdge);
    // **Which layers of chunks can hold a surface**: one that is not all one
    // value, or one that is solid and has something other than solid ground
    // against any of its six faces -- air above it, the air past the edge of
    // the terrain beside it, or nothing under it. The last two are what give
    // the terrain walls and a bottom of the same kind all round; without them
    // an edge showed a wall where its chunks happened to be rows and an open
    // side where they happened to be whole.
    std::vector<i32> layers;
    for (i32 cz = chunkZ - 1; cz <= chunkZ + across; ++cz) {
        for (i32 cx = chunkX - 1; cx <= chunkX + across; ++cx) {
            for (const TerrainField::Entry& entry : field.column(cx, cz)) {
                const TerrainChunk& chunk = *entry.second;
                bool interesting = !chunk.uniform();
                if (!interesting && chunk.value().occupancy >= 128) {
                    static constexpr std::array<std::array<i32, 3>, 6> Faces{{
                        {-1, 0, 0},
                        {1, 0, 0},
                        {0, -1, 0},
                        {0, 1, 0},
                        {0, 0, -1},
                        {0, 0, 1},
                    }};
                    for (const std::array<i32, 3>& face : Faces) {
                        const TerrainChunk* near = field.findChunk(
                            ChunkKey{entry.first.x + face[0], entry.first.y + face[1], entry.first.z + face[2]});
                        const u32 axis = face[0] != 0 ? 0u : (face[1] != 0 ? 1u : 2u);
                        const bool nearHigh = (face[0] + face[1] + face[2]) < 0;
                        if (near == nullptr || !faceSolid(*near, axis, nearHigh)) {
                            interesting = true;
                            break;
                        }
                    }
                }
                if (interesting)
                    layers.push_back(entry.first.y);
            }
        }
    }
    std::sort(layers.begin(), layers.end());
    layers.erase(std::unique(layers.begin(), layers.end()), layers.end());

    // Each layer's rows, a voxel either side -- a surface on a chunk's face is
    // between its last voxel and the next chunk's first -- merged where they
    // touch.
    std::vector<std::pair<i32, i32>> runs;
    for (const i32 layer : layers) {
        const i32 low = layer * edge - 2;
        const i32 high = layer * edge + edge + 1;
        if (!runs.empty() && low <= runs.back().second + 1)
            runs.back().second = std::max(runs.back().second, high);
        else
            runs.emplace_back(low, high);
    }
    return runs;
}

std::optional<std::pair<i32, i32>> activeRows(const TerrainField& field, i32 chunkX, i32 chunkZ, i32 across)
{
    const std::vector<std::pair<i32, i32>> runs = activeRuns(field, chunkX, chunkZ, across);
    if (runs.empty())
        return std::nullopt;
    return std::pair{runs.front().first, runs.back().second};
}

void appendMesh(TerrainMesh& into, const TerrainMesh& from)
{
    if (from.mesh.indices.empty())
        return;
    if (into.mesh.indices.empty()) {
        into = from;
        return;
    }
    // Sections stay one per material, in id order: both meshes' triangles of a
    // material are gathered into one run.
    const auto offset = static_cast<u32>(into.mesh.vertices.size());
    std::map<u8, std::vector<u32>> buckets;
    const auto gather = [&](const TerrainMesh& mesh, u32 shift) {
        for (usize section = 0; section < mesh.mesh.submeshes.size(); ++section) {
            const Submesh& sub = mesh.mesh.submeshes[section];
            std::vector<u32>& bucket = buckets[mesh.sectionMaterials[section]];
            for (u32 at = 0; at < sub.indexCount; ++at)
                bucket.push_back(mesh.mesh.indices[sub.firstIndex + at] + shift);
        }
    };
    gather(into, 0);
    gather(from, offset);
    into.mesh.vertices.insert(into.mesh.vertices.end(), from.mesh.vertices.begin(), from.mesh.vertices.end());
    into.mesh.indices.clear();
    into.mesh.submeshes.clear();
    into.sectionMaterials.clear();
    for (const auto& [material, indices] : buckets) {
        Submesh section;
        section.firstIndex = static_cast<u32>(into.mesh.indices.size());
        section.indexCount = static_cast<u32>(indices.size());
        into.mesh.indices.insert(into.mesh.indices.end(), indices.begin(), indices.end());
        into.mesh.submeshes.push_back(section);
        into.sectionMaterials.push_back(material);
    }
    const auto colliderOffset = static_cast<u32>(into.colliderPoints.size());
    into.colliderPoints.insert(into.colliderPoints.end(), from.colliderPoints.begin(), from.colliderPoints.end());
    for (const u32 index : from.colliderIndices)
        into.colliderIndices.push_back(index + colliderOffset);
    into.mesh.bounds.min.x = std::min(into.mesh.bounds.min.x, from.mesh.bounds.min.x);
    into.mesh.bounds.min.y = std::min(into.mesh.bounds.min.y, from.mesh.bounds.min.y);
    into.mesh.bounds.min.z = std::min(into.mesh.bounds.min.z, from.mesh.bounds.min.z);
    into.mesh.bounds.max.x = std::max(into.mesh.bounds.max.x, from.mesh.bounds.max.x);
    into.mesh.bounds.max.y = std::max(into.mesh.bounds.max.y, from.mesh.bounds.max.y);
    into.mesh.bounds.max.z = std::max(into.mesh.bounds.max.z, from.mesh.bounds.max.z);
}

TerrainMesh meshField(const TerrainField& field, const MeshRegion& region)
{
    TerrainMesh out;
    if (region.cellsX == 0 || region.cellsY == 0 || region.cellsZ == 0 || region.level >= ChunkLevels)
        return out;
    const u32 level = region.level;
    const float step = field.settings().voxelSize * static_cast<float>(1u << level);
    const auto nx = static_cast<i32>(region.cellsX);
    const auto ny = static_cast<i32>(region.cellsY);
    const auto nz = static_cast<i32>(region.cellsZ);

    // **The samples: the owned points, a cell ring below them, and one more on
    // every side for the gradient.** Owned points are `[min, min + n)`; cells
    // run from `min - 1` to `min + n - 1` and read points `min - 1` to
    // `min + n`; the gradient at those reads one further out.
    const i32 sx0 = region.minX - 2;
    const i32 sy0 = region.minY - 2;
    const i32 sz0 = region.minZ - 2;
    const i32 sizeX = nx + 4;
    const i32 sizeY = ny + 4;
    const i32 sizeZ = nz + 4;
    std::vector<u16> samples;
    gather(field, level, sx0, sy0, sz0, sizeX, sizeY, sizeZ, samples);
    const auto sampleIndex = [&](i32 sx, i32 sy, i32 sz) {
        return (static_cast<usize>(sz) * static_cast<usize>(sizeY) + static_cast<usize>(sy)) *
                   static_cast<usize>(sizeX) +
               static_cast<usize>(sx);
    };
    // Local sample coordinates throughout: `s = lattice - (min - 2)`.
    const auto occupancy = [&](i32 sx, i32 sy, i32 sz) {
        return static_cast<float>(samples[sampleIndex(sx, sy, sz)] & 0xFF) / static_cast<float>(FullOccupancy);
    };
    const auto materialAt = [&](i32 sx, i32 sy, i32 sz) {
        return static_cast<u8>(samples[sampleIndex(sx, sy, sz)] >> 8);
    };
    // The surface normal at a sample: the negative gradient of occupancy, by
    // central differences. Read from the field rather than from the triangles,
    // so it is smooth across them -- and the same in two neighbouring regions,
    // which read the same samples.
    const auto normalAt = [&](i32 sx, i32 sy, i32 sz) {
        return Vec3{occupancy(sx - 1, sy, sz) - occupancy(sx + 1, sy, sz),
                    occupancy(sx, sy - 1, sz) - occupancy(sx, sy + 1, sz),
                    occupancy(sx, sy, sz - 1) - occupancy(sx, sy, sz + 1)};
    };

    // **The column tops the sky term reads**, one per level column over the
    // region and far enough round it for the longest sky ray. Built once, where
    // a search per ray step per vertex would be most of a region's cost.
    //
    // A sky ray is followed `SkyReach` metres across, looking at the columns
    // `SkyStops` along it: close together near the point, where a wall beside
    // it decides most, and further apart out where only a hill can.
    constexpr float SkyReach = 12.0f;
    static constexpr std::array<float, 8> SkyStops{1.0f, 2.0f, 3.0f, 4.5f, 6.0f, 8.0f, 10.0f, SkyReach};
    const i32 margin = static_cast<i32>(std::ceil((SkyReach + 2.5f) / step)) + 1;
    const i32 mapX0 = region.minX - margin;
    const i32 mapZ0 = region.minZ - margin;
    const i32 mapW = nx + 2 * margin;
    const i32 mapD = nz + 2 * margin;
    std::vector<float> tops(static_cast<usize>(mapW) * static_cast<usize>(mapD));
    std::vector<float> bottoms(tops.size());
    const auto half = static_cast<i32>((1u << level) / 2);
    for (i32 z = 0; z < mapD; ++z) {
        for (i32 x = 0; x < mapW; ++x) {
            const i32 columnX = (mapX0 + x) * static_cast<i32>(1u << level) + half;
            const i32 columnZ = (mapZ0 + z) * static_cast<i32>(1u << level) + half;
            const usize slot = static_cast<usize>(z) * static_cast<usize>(mapW) + static_cast<usize>(x);
            tops[slot] = field.columnTop(columnX, columnZ).value_or(std::numeric_limits<float>::quiet_NaN());
            bottoms[slot] =
                std::isnan(tops[slot])
                    ? tops[slot]
                    : field.columnBottom(columnX, columnZ).value_or(std::numeric_limits<float>::quiet_NaN());
        }
    }
    const auto slotAt = [&](float x, float z) {
        const i32 kx = std::clamp(static_cast<i32>(std::floor(x / step)) - mapX0, 0, mapW - 1);
        const i32 kz = std::clamp(static_cast<i32>(std::floor(z / step)) - mapZ0, 0, mapD - 1);
        return static_cast<usize>(kz) * static_cast<usize>(mapW) + static_cast<usize>(kx);
    };
    const auto topAt = [&](float x, float z) { return tops[slotAt(x, z)]; };
    // Each column's bottom and top side by side, for the sky rays' march.
    std::vector<std::array<float, 2>> spans(tops.size());
    for (usize at = 0; at < tops.size(); ++at)
        spans[at] = {bottoms[at], tops[at]};
    const auto bottomAt = [&](float x, float z) { return bottoms[slotAt(x, z)]; };

    // **The highest top within a sky ray's reach of each column** -- a max
    // filter over the tops, one axis at a time. A point above it sees the whole
    // sky without marching a ray, and that is almost every point of open
    // ground: the rays are paid for on walls and in caves, where they matter.
    std::vector<float> nearTops(tops.size(), -std::numeric_limits<float>::infinity());
    {
        const i32 reach = static_cast<i32>(std::ceil(SkyReach / step)) + 1;
        std::vector<float> across(tops.size(), -std::numeric_limits<float>::infinity());
        for (i32 z = 0; z < mapD; ++z) {
            for (i32 x = 0; x < mapW; ++x) {
                float highest = -std::numeric_limits<float>::infinity();
                for (i32 k = std::max(x - reach, 0); k <= std::min(x + reach, mapW - 1); ++k) {
                    const float top = tops[static_cast<usize>(z) * static_cast<usize>(mapW) + static_cast<usize>(k)];
                    if (!std::isnan(top))
                        highest = std::max(highest, top);
                }
                across[static_cast<usize>(z) * static_cast<usize>(mapW) + static_cast<usize>(x)] = highest;
            }
        }
        for (i32 z = 0; z < mapD; ++z) {
            for (i32 x = 0; x < mapW; ++x) {
                float highest = -std::numeric_limits<float>::infinity();
                for (i32 k = std::max(z - reach, 0); k <= std::min(z + reach, mapD - 1); ++k)
                    highest = std::max(
                        highest, across[static_cast<usize>(k) * static_cast<usize>(mapW) + static_cast<usize>(x)]);
                nearTops[static_cast<usize>(z) * static_cast<usize>(mapW) + static_cast<usize>(x)] = highest;
            }
        }
    }

    // **How much of the sky a vertex sees**, from 0 to 1: a fixed fan of rays
    // over the sky, from a point lifted off the surface along its normal, each
    // marched across the column map and blocked where it passes between a
    // column's bottom and its top. Weighted by how squarely each faces the
    // normal, so an open wall sees all of the sky it can face, as open ground
    // does. Deep in a tunnel every ray meets roof; at its mouth the ones that
    // leave by it do not.
    //
    // **Rays, not the ground straight above**, and the owner's picture is why:
    // a ball added to the side of the terrain stood a dark stripe down the
    // whole wall under it, because every point below a column's ground counted
    // as under a roof -- however far below, and however small the roof. A ray
    // from twenty metres down the wall leaves past a four-metre ball.
    //
    // A fixed pattern with written-out constants (no `std::cos`), so the same
    // field meshes to the same bytes on every platform.
    const auto skyVisibility = [&](Vec3 position, Vec3 normal) {
        // Eight bearings, and three rings up from the horizon: 60, 30 and 10
        // degrees -- cosine, sine and rise per metre across. Plus the zenith.
        struct Bearing
        {
            float x;
            float z;
        };
        static constexpr float D = 0.70710678f;
        static constexpr std::array<Bearing, 8> Bearings{{
            {1.0f, 0.0f},
            {D, D},
            {0.0f, 1.0f},
            {-D, D},
            {-1.0f, 0.0f},
            {-D, -D},
            {0.0f, -1.0f},
            {D, -D},
        }};
        struct Ring
        {
            float across;
            float up;
            float rise;
        };
        static constexpr std::array<Ring, 3> Rings{{
            {0.5f, 0.8660254f, 1.7320508f},
            {0.8660254f, 0.5f, 0.5773503f},
            {0.9848078f, 0.1736482f, 0.1763270f},
        }};
        constexpr float Lift = 1.5f;
        const Vec3 from{position.x + normal.x * Lift, position.y + normal.y * Lift, position.z + normal.z * Lift};
        // A point with no ground above it anywhere in reach sees the sky.
        const float highest = nearTops[slotAt(from.x, from.z)];
        if (highest <= from.y)
            return 1.0f;
        const float own = topAt(from.x, from.z);
        const float ownBottom = bottomAt(from.x, from.z);

        // Each ray weighed by how squarely the surface faces it; a surface
        // that faces down faces none of the sky, and is weighed over it as
        // ground is.
        const auto facingOf = [&](Bearing bearing, Ring ring) {
            return std::max(
                bearing.x * ring.across * normal.x + ring.up * normal.y + bearing.z * ring.across * normal.z, 0.0f);
        };
        float facing = std::max(normal.y, 0.0f);
        for (const Bearing& bearing : Bearings) {
            for (const Ring& ring : Rings)
                facing += facingOf(bearing, ring);
        }
        const bool down = facing < 0.25f;

        float seen = 0.0f;
        float total = 0.0f;
        // The zenith: open where nothing in this column is above the point.
        const float zenith = down ? 1.0f : std::max(normal.y, 0.0f);
        if (zenith > 0.0f) {
            total += zenith;
            if (std::isnan(own) || own <= from.y)
                seen += zenith;
        }
        // **One march per bearing, all three rings at once**: a column is
        // looked up once per stop, and each ring still unsettled is checked
        // against it -- blocked where it passes between the column's bottom and
        // top, free once it is above every top in reach, since it only rises.
        // In map cells, so a stop is an add and a truncation rather than a
        // division and a floor: the margin keeps every stop inside the map
        // and on its positive side.
        const float cellX = from.x / step - static_cast<float>(mapX0);
        const float cellZ = from.z / step - static_cast<float>(mapZ0);
        const float perCell = 1.0f / step;
        for (const Bearing& bearing : Bearings) {
            std::array<float, 3> weight{};
            std::array<bool, 3> settled{};
            int open = 0;
            for (usize r = 0; r < Rings.size(); ++r) {
                weight[r] = down ? Rings[r].up : facingOf(bearing, Rings[r]);
                settled[r] = weight[r] <= 0.0f;
                open += settled[r] ? 0 : 1;
                total += weight[r];
            }
            for (usize at = 0; at < SkyStops.size() && open > 0; ++at) {
                const float d = SkyStops[at];
                const i32 kx = std::clamp(static_cast<i32>(cellX + bearing.x * d * perCell), 0, mapW - 1);
                const i32 kz = std::clamp(static_cast<i32>(cellZ + bearing.z * d * perCell), 0, mapD - 1);
                const std::array<float, 2>& span =
                    spans[static_cast<usize>(kz) * static_cast<usize>(mapW) + static_cast<usize>(kx)];
                const float top = span[1];
                const float bottom = span[0];
                for (usize r = 0; r < Rings.size(); ++r) {
                    if (settled[r])
                        continue;
                    const float y = from.y + Rings[r].rise * d;
                    if (y > highest) {
                        settled[r] = true;
                        seen += weight[r];
                        open -= 1;
                    }
                    else if (!std::isnan(top) && y <= top && y >= bottom) {
                        settled[r] = true;
                        open -= 1;
                    }
                }
            }
            // A ray that met nothing within reach leaves.
            for (usize r = 0; r < Rings.size(); ++r) {
                if (!settled[r])
                    seen += weight[r];
            }
        }
        const float visible = total > 0.0f ? seen / total : 1.0f;
        // **Under all the ground in its column** -- the underside of the
        // terrain or of a ledge, not the roof of a cave, which has a floor under
        // it -- a point sees the world below and the horizon: at least half,
        // which draws it as shade rather than as a black hole.
        constexpr float Underside = 0.5f;
        if (!std::isnan(ownBottom) && from.y < ownBottom)
            return std::max(visible, Underside);
        return visible;
    };

    // --- Vertices: one per cell the surface passes through ---------------
    //
    // Cells `[min - 1, min + n)` on each axis, n + 1 of them; a cell's local
    // index `c` has its low corner at local sample `c + 1`.
    const i32 cellsX = nx + 1;
    const i32 cellsY = ny + 1;
    const i32 cellsZ = nz + 1;
    constexpr u32 NoVertex = 0xFFFFFFFFu;
    std::vector<u32> cellVertex(static_cast<usize>(cellsX) * static_cast<usize>(cellsY) * static_cast<usize>(cellsZ),
                                NoVertex);
    const auto cellIndex = [&](i32 x, i32 y, i32 z) {
        return (static_cast<usize>(z) * static_cast<usize>(cellsY) + static_cast<usize>(y)) *
                   static_cast<usize>(cellsX) +
               static_cast<usize>(x);
    };
    // Which of the region's four sides a vertex's cell is on the outer ring
    // of: 1 low x, 2 high x, 4 low z, 8 high z. What the skirts hang from.
    std::vector<u8> vertexRing;
    std::vector<u8> vertexMaterial;

    for (i32 cz = 0; cz < cellsZ; ++cz) {
        for (i32 cy = 0; cy < cellsY; ++cy) {
            for (i32 cx = 0; cx < cellsX; ++cx) {
                std::array<float, 8> corner{};
                int inside = 0;
                for (int at = 0; at < 8; ++at) {
                    const auto& offset = CornerOffsets[static_cast<usize>(at)];
                    corner[static_cast<usize>(at)] =
                        occupancy(cx + 1 + offset[0], cy + 1 + offset[1], cz + 1 + offset[2]);
                    if (corner[static_cast<usize>(at)] >= 0.5f)
                        inside |= 1 << at;
                }
                if (inside == 0 || inside == 0xFF)
                    continue;

                float sumX = 0.0f;
                float sumY = 0.0f;
                float sumZ = 0.0f;
                Vec3 normalSum{0.0f, 0.0f, 0.0f};
                int crossings = 0;
                for (const std::array<int, 2>& edgeCorners : CellEdges) {
                    const auto a = static_cast<usize>(edgeCorners[0]);
                    const auto b = static_cast<usize>(edgeCorners[1]);
                    if (((inside >> a) & 1) == ((inside >> b) & 1))
                        continue;
                    const float t = crossingAt(corner[a], corner[b]);
                    const auto& oa = CornerOffsets[a];
                    const auto& ob = CornerOffsets[b];
                    sumX += static_cast<float>(oa[0]) + static_cast<float>(ob[0] - oa[0]) * t;
                    sumY += static_cast<float>(oa[1]) + static_cast<float>(ob[1] - oa[1]) * t;
                    sumZ += static_cast<float>(oa[2]) + static_cast<float>(ob[2] - oa[2]) * t;
                    const Vec3 na = normalAt(cx + 1 + oa[0], cy + 1 + oa[1], cz + 1 + oa[2]);
                    const Vec3 nb = normalAt(cx + 1 + ob[0], cy + 1 + ob[1], cz + 1 + ob[2]);
                    normalSum.x += na.x + (nb.x - na.x) * t;
                    normalSum.y += na.y + (nb.y - na.y) * t;
                    normalSum.z += na.z + (nb.z - na.z) * t;
                    ++crossings;
                }
                const float inverse = 1.0f / static_cast<float>(crossings);
                // The cell's low corner is lattice point `min - 1 + c`, whose
                // centre is half a step in.
                const Vec3 position{
                    (static_cast<float>(region.minX - 1 + cx) + sumX * inverse + 0.5f) * step,
                    (static_cast<float>(region.minY - 1 + cy) + sumY * inverse + 0.5f) * step,
                    (static_cast<float>(region.minZ - 1 + cz) + sumZ * inverse + 0.5f) * step,
                };
                const float normalLength =
                    std::sqrt(normalSum.x * normalSum.x + normalSum.y * normalSum.y + normalSum.z * normalSum.z);
                const Vec3 normal = normalLength < 1e-8f ? Vec3{0.0f, 1.0f, 0.0f}
                                                         : Vec3{normalSum.x / normalLength, normalSum.y / normalLength,
                                                                normalSum.z / normalLength};

                // **The commonest material among the cell's SOLID corners**,
                // lowest id on a tie. An air corner has no material.
                std::array<u8, 8> seen{};
                std::array<int, 8> votes{};
                int kinds = 0;
                for (int at = 0; at < 8; ++at) {
                    if ((inside & (1 << at)) == 0)
                        continue;
                    const auto& offset = CornerOffsets[static_cast<usize>(at)];
                    const u8 material = materialAt(cx + 1 + offset[0], cy + 1 + offset[1], cz + 1 + offset[2]);
                    int slot = 0;
                    while (slot < kinds && seen[static_cast<usize>(slot)] != material)
                        ++slot;
                    if (slot == kinds) {
                        seen[static_cast<usize>(kinds)] = material;
                        ++kinds;
                    }
                    ++votes[static_cast<usize>(slot)];
                }
                u8 material = seen[0];
                int best = votes[0];
                for (int slot = 1; slot < kinds; ++slot) {
                    const u8 candidate = seen[static_cast<usize>(slot)];
                    const int count = votes[static_cast<usize>(slot)];
                    if (count > best || (count == best && candidate < material)) {
                        material = candidate;
                        best = count;
                    }
                }

                Vertex vertex;
                vertex.position = position;
                vertex.normal = normal;
                // The world position on the two axes the normal is least
                // aligned with. The terrain shader lays its own triplanar
                // detail; this is for whatever reads a mesh's UVs.
                const float ax = std::abs(normal.x);
                const float ay = std::abs(normal.y);
                const float az = std::abs(normal.z);
                if (ay >= ax && ay >= az) {
                    vertex.uv[0] = position.x;
                    vertex.uv[1] = position.z;
                }
                else if (ax >= az) {
                    vertex.uv[0] = position.z;
                    vertex.uv[1] = position.y;
                }
                else {
                    vertex.uv[0] = position.x;
                    vertex.uv[1] = position.y;
                }
                // **The material rides in the tangent's x and the sky in its
                // y.** A terrain has no tangent frame of its own -- its shader
                // builds one -- and `Vertex` is a GPU layout whose size is
                // asserted, so this is where they fit without a second stream.
                vertex.tangent[0] = static_cast<float>(material);
                vertex.tangent[1] = skyVisibility(position, normal);
                vertex.tangent[2] = 0.0f;
                vertex.tangent[3] = 1.0f;

                u8 ring = 0;
                if (cx == 0)
                    ring |= 1;
                if (cx == cellsX - 1)
                    ring |= 2;
                if (cz == 0)
                    ring |= 4;
                if (cz == cellsZ - 1)
                    ring |= 8;

                cellVertex[cellIndex(cx, cy, cz)] = static_cast<u32>(out.mesh.vertices.size());
                out.mesh.vertices.push_back(vertex);
                out.colliderPoints.push_back(position);
                vertexMaterial.push_back(material);
                vertexRing.push_back(ring);
            }
        }
    }

    // --- Quads: one round each crossed edge that starts on an owned point --
    std::map<u8, std::vector<u32>> buckets;
    // **Winding from the field, not from a table**: the triangle is emitted,
    // its face normal compared with the field's, and its order reversed when
    // they disagree. A table can have a case backwards; this cannot.
    const auto emitTriangle = [&](u32 a, u32 b, u32 c) {
        if (a == b || b == c || a == c)
            return;
        const Vec3& pa = out.mesh.vertices[a].position;
        const Vec3& pb = out.mesh.vertices[b].position;
        const Vec3& pc = out.mesh.vertices[c].position;
        const Vec3 ab{pb.x - pa.x, pb.y - pa.y, pb.z - pa.z};
        const Vec3 ac{pc.x - pa.x, pc.y - pa.y, pc.z - pa.z};
        const Vec3 face{ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x};
        const Vec3 reference{
            out.mesh.vertices[a].normal.x + out.mesh.vertices[b].normal.x + out.mesh.vertices[c].normal.x,
            out.mesh.vertices[a].normal.y + out.mesh.vertices[b].normal.y + out.mesh.vertices[c].normal.y,
            out.mesh.vertices[a].normal.z + out.mesh.vertices[b].normal.z + out.mesh.vertices[c].normal.z};
        const float agreement = face.x * reference.x + face.y * reference.y + face.z * reference.z;
        const u32 second = agreement < 0.0f ? c : b;
        const u32 third = agreement < 0.0f ? b : c;

        // The majority of the three vertices' materials, lowest on a three-way
        // tie: a rule, so the visit order never reaches the mesh (R10).
        const u8 ma = vertexMaterial[a];
        const u8 mb = vertexMaterial[second];
        const u8 mc = vertexMaterial[third];
        u8 material = ma;
        if (mb == mc && mb != ma)
            material = mb;
        else if (ma != mb && ma != mc && mb != mc)
            material = std::min({ma, mb, mc});

        std::vector<u32>& bucket = buckets[material];
        bucket.push_back(a);
        bucket.push_back(second);
        bucket.push_back(third);
        out.colliderIndices.push_back(a);
        out.colliderIndices.push_back(second);
        out.colliderIndices.push_back(third);
    };
    const auto quad = [&](std::array<i32, 3> c0, std::array<i32, 3> c1, std::array<i32, 3> c2, std::array<i32, 3> c3) {
        const u32 a = cellVertex[cellIndex(c0[0], c0[1], c0[2])];
        const u32 b = cellVertex[cellIndex(c1[0], c1[1], c1[2])];
        const u32 c = cellVertex[cellIndex(c2[0], c2[1], c2[2])];
        const u32 d = cellVertex[cellIndex(c3[0], c3[1], c3[2])];
        if (a == NoVertex || b == NoVertex || c == NoVertex || d == NoVertex)
            return;
        emitTriangle(a, b, c);
        emitTriangle(a, c, d);
    };
    // Owned point `p` (local `o` in [0, n)) is sample `o + 2`, and the cell
    // whose low corner is point `p - 1 + k` has local index `o + k`.
    for (i32 oz = 0; oz < nz; ++oz) {
        for (i32 oy = 0; oy < ny; ++oy) {
            for (i32 ox = 0; ox < nx; ++ox) {
                const bool here = occupancy(ox + 2, oy + 2, oz + 2) >= 0.5f;
                // Along x: cells (x) by (y-1, y) by (z-1, z).
                if (here != (occupancy(ox + 3, oy + 2, oz + 2) >= 0.5f))
                    quad({ox + 1, oy, oz}, {ox + 1, oy + 1, oz}, {ox + 1, oy + 1, oz + 1}, {ox + 1, oy, oz + 1});
                // Along y: (x-1, x) by (y) by (z-1, z).
                if (here != (occupancy(ox + 2, oy + 3, oz + 2) >= 0.5f))
                    quad({ox, oy + 1, oz}, {ox + 1, oy + 1, oz}, {ox + 1, oy + 1, oz + 1}, {ox, oy + 1, oz + 1});
                // Along z: (x-1, x) by (y-1, y) by (z).
                if (here != (occupancy(ox + 2, oy + 2, oz + 3) >= 0.5f))
                    quad({ox, oy, oz + 1}, {ox + 1, oy, oz + 1}, {ox + 1, oy + 1, oz + 1}, {ox, oy + 1, oz + 1});
            }
        }
    }

    // --- Skirts ------------------------------------------------------------
    //
    // A boundary edge is one a single triangle uses. One whose two ends are on
    // the same side's outer ring of cells is where this region meets its
    // neighbour; a strip hung from it along the negative normal covers a crack
    // to a neighbour meshed at another level. Both windings, because which face
    // a camera sees depends on which neighbour is the coarse one.
    if (region.skirt > 0.0f && !buckets.empty()) {
        std::unordered_map<core::u64, u32> uses;
        const auto edgeKey = [](u32 a, u32 b) {
            const u32 lo = std::min(a, b);
            const u32 hi = std::max(a, b);
            return (static_cast<core::u64>(lo) << 32) | hi;
        };
        for (const auto& entry : buckets) {
            const std::vector<u32>& list = entry.second;
            for (usize at = 0; at + 2 < list.size(); at += 3) {
                ++uses[edgeKey(list[at], list[at + 1])];
                ++uses[edgeKey(list[at + 1], list[at + 2])];
                ++uses[edgeKey(list[at + 2], list[at])];
            }
        }
        // Lowered copies, made once per vertex and looked up by index only.
        std::unordered_map<u32, u32> lowered;
        const auto lowerOf = [&](u32 index) {
            if (const auto at = lowered.find(index); at != lowered.end())
                return at->second;
            Vertex copy = out.mesh.vertices[index];
            copy.position.x -= copy.normal.x * region.skirt;
            copy.position.y -= copy.normal.y * region.skirt;
            copy.position.z -= copy.normal.z * region.skirt;
            const auto made = static_cast<u32>(out.mesh.vertices.size());
            out.mesh.vertices.push_back(copy);
            vertexMaterial.push_back(vertexMaterial[index]);
            vertexRing.push_back(0);
            lowered.emplace(index, made);
            return made;
        };
        for (auto& entry : buckets) {
            std::vector<u32>& list = entry.second;
            const usize triangles = list.size();
            for (usize at = 0; at + 2 < triangles; at += 3) {
                const u32 corners[3] = {list[at], list[at + 1], list[at + 2]};
                for (int edgeIndex = 0; edgeIndex < 3; ++edgeIndex) {
                    const u32 a = corners[edgeIndex];
                    const u32 b = corners[(edgeIndex + 1) % 3];
                    if ((vertexRing[a] & vertexRing[b]) == 0 || uses[edgeKey(a, b)] != 1)
                        continue;
                    const u32 la = lowerOf(a);
                    const u32 lb = lowerOf(b);
                    list.insert(list.end(), {a, b, lb, a, lb, la});
                    list.insert(list.end(), {a, lb, b, a, la, lb});
                }
            }
        }
    }

    if (!out.mesh.vertices.empty()) {
        Vec3 min = out.mesh.vertices.front().position;
        Vec3 max = min;
        for (const Vertex& vertex : out.mesh.vertices) {
            min.x = std::min(min.x, vertex.position.x);
            min.y = std::min(min.y, vertex.position.y);
            min.z = std::min(min.z, vertex.position.z);
            max.x = std::max(max.x, vertex.position.x);
            max.y = std::max(max.y, vertex.position.y);
            max.z = std::max(max.z, vertex.position.z);
        }
        out.mesh.bounds = core::AABB{min, max};
    }

    // **One section per material, in id order** -- a `std::map`, because the
    // section order reaches a GPU buffer and a world hash (R10).
    for (const auto& entry : buckets) {
        if (entry.second.empty())
            continue;
        Submesh section;
        section.firstIndex = static_cast<u32>(out.mesh.indices.size());
        section.indexCount = static_cast<u32>(entry.second.size());
        out.mesh.indices.insert(out.mesh.indices.end(), entry.second.begin(), entry.second.end());
        out.mesh.submeshes.push_back(section);
        out.sectionMaterials.push_back(entry.first);
    }
    return out;
}

} // namespace luaug::asset
