#include "luaug/render/terrain_lod.h"

#include <algorithm>
#include <cmath>

namespace luaug::render {
namespace {

using core::i32;
using core::u32;
using core::usize;

[[nodiscard]] i32 floorDivide(i32 value, i32 divisor) noexcept
{
    const i32 quotient = value / divisor;
    return (value % divisor != 0 && (value < 0) != (divisor < 0)) ? quotient - 1 : quotient;
}

// Per-level min/max heights over tile-aligned squares, so a node's bounds are
// one lookup rather than a walk over every tile beneath it.
struct Pyramid
{
    struct Level
    {
        i32 originX = 0;
        i32 originZ = 0;
        u32 sizeX = 0;
        u32 sizeZ = 0;
        std::vector<float> min;
        std::vector<float> max;
    };
    std::vector<Level> levels;

    // False when the node lies outside the pyramid or over nothing.
    [[nodiscard]] bool bounds(u32 level, i32 x, i32 z, float& lo, float& hi) const noexcept
    {
        const Level& at = levels[level];
        const i32 localX = x - at.originX;
        const i32 localZ = z - at.originZ;
        if (localX < 0 || localZ < 0 || localX >= static_cast<i32>(at.sizeX) || localZ >= static_cast<i32>(at.sizeZ))
            return false;
        const usize index = static_cast<usize>(localZ) * at.sizeX + static_cast<usize>(localX);
        lo = at.min[index];
        hi = at.max[index];
        return lo <= hi;
    }
};

void buildPyramid(const TerrainLodSource& source, u32 top, Pyramid& out)
{
    out.levels.resize(top + 1);
    Pyramid::Level& base = out.levels[0];
    base.originX = source.minTileX;
    base.originZ = source.minTileZ;
    base.sizeX = source.tilesX;
    base.sizeZ = source.tilesZ;
    base.min.assign(source.tileMin.begin(), source.tileMin.end());
    base.max.assign(source.tileMax.begin(), source.tileMax.end());

    for (u32 level = 1; level <= top; ++level) {
        const Pyramid::Level& below = out.levels[level - 1];
        Pyramid::Level& here = out.levels[level];
        const i32 lastX = floorDivide(below.originX + static_cast<i32>(below.sizeX) - 1, 2);
        const i32 lastZ = floorDivide(below.originZ + static_cast<i32>(below.sizeZ) - 1, 2);
        here.originX = floorDivide(below.originX, 2);
        here.originZ = floorDivide(below.originZ, 2);
        here.sizeX = static_cast<u32>(lastX - here.originX + 1);
        here.sizeZ = static_cast<u32>(lastZ - here.originZ + 1);
        here.min.assign(static_cast<usize>(here.sizeX) * here.sizeZ, 1.0f);
        here.max.assign(static_cast<usize>(here.sizeX) * here.sizeZ, -1.0f);
        for (u32 z = 0; z < here.sizeZ; ++z) {
            for (u32 x = 0; x < here.sizeX; ++x) {
                float lo = 0.0f;
                float hi = 0.0f;
                bool any = false;
                for (i32 dz = 0; dz < 2; ++dz) {
                    for (i32 dx = 0; dx < 2; ++dx) {
                        float childLo = 0.0f;
                        float childHi = 0.0f;
                        const i32 childX = (here.originX + static_cast<i32>(x)) * 2 + dx;
                        const i32 childZ = (here.originZ + static_cast<i32>(z)) * 2 + dz;
                        if (!out.bounds(level - 1, childX, childZ, childLo, childHi))
                            continue;
                        lo = any ? std::min(lo, childLo) : childLo;
                        hi = any ? std::max(hi, childHi) : childHi;
                        any = true;
                    }
                }
                if (any) {
                    const usize index = static_cast<usize>(z) * here.sizeX + x;
                    here.min[index] = lo;
                    here.max[index] = hi;
                }
            }
        }
    }
}

// The distance from the viewer, who is at the origin, to a box.
[[nodiscard]] double distanceTo(const core::Vec3& lo, const core::Vec3& hi) noexcept
{
    const double dx = std::max({static_cast<double>(lo.x), 0.0, -static_cast<double>(hi.x)});
    const double dy = std::max({static_cast<double>(lo.y), 0.0, -static_cast<double>(hi.y)});
    const double dz = std::max({static_cast<double>(lo.z), 0.0, -static_cast<double>(hi.z)});
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

struct Selector
{
    const TerrainLodSource& source;
    const TerrainLodSettings& settings;
    const Pyramid& pyramid;
    std::vector<TerrainNode>& out;

    // The node at tile-aligned key (x, z) of `level`, with its bounds; false
    // when it covers nothing.
    [[nodiscard]] bool nodeAt(u32 level, i32 x, i32 z, TerrainNode& node) const noexcept
    {
        float lo = 0.0f;
        float hi = 0.0f;
        if (!pyramid.bounds(level, x, z, lo, hi))
            return false;
        const auto span = static_cast<i32>(TerrainGridQuads << level);
        node.latticeX = x * span;
        node.latticeZ = z * span;
        node.level = level;
        const auto voxel = static_cast<double>(source.voxelSize);
        const double minX = source.originFromViewer.x + static_cast<double>(node.latticeX) * voxel;
        const double minZ = source.originFromViewer.z + static_cast<double>(node.latticeZ) * voxel;
        const double width = static_cast<double>(span) * voxel;
        node.boundsMin = core::Vec3{static_cast<float>(minX),
                                    static_cast<float>(source.originFromViewer.y + static_cast<double>(lo)),
                                    static_cast<float>(minZ)};
        node.boundsMax = core::Vec3{static_cast<float>(minX + width),
                                    static_cast<float>(source.originFromViewer.y + static_cast<double>(hi)),
                                    static_cast<float>(minZ + width)};
        return true;
    }

    void add(TerrainNode node) const
    {
        const double end = terrainLevelRange(source, settings, node.level);
        const double previous = node.level == 0 ? 0.0 : terrainLevelRange(source, settings, node.level - 1);
        node.morphEnd = static_cast<float>(end);
        node.morphStart = static_cast<float>(previous + (end - previous) * settings.morphStart);
        out.push_back(node);
    }

    // Strugar's selection. Answers false when the node is beyond its own
    // level's band, so the caller draws it at the caller's level instead.
    bool select(u32 level, i32 x, i32 z) const
    {
        TerrainNode node;
        if (!nodeAt(level, x, z, node))
            return true; // nothing here: handled by drawing nothing
        const double distance = distanceTo(node.boundsMin, node.boundsMax);
        if (distance > terrainLevelRange(source, settings, level))
            return false;
        if (distance > settings.viewDistance)
            return true;
        if (level == 0 || distance > terrainLevelRange(source, settings, level - 1)) {
            add(node);
            return true;
        }
        for (i32 dz = 0; dz < 2; ++dz) {
            for (i32 dx = 0; dx < 2; ++dx) {
                const i32 childX = x * 2 + dx;
                const i32 childZ = z * 2 + dz;
                if (select(level - 1, childX, childZ))
                    continue;
                // Past the child level's band, so drawn at the child level fully
                // morphed -- which puts every vertex where this level's grid
                // has it. Four times the vertices of drawing a quarter of this
                // level's grid, and one grid and one draw path instead of two.
                TerrainNode child;
                if (nodeAt(level - 1, childX, childZ, child))
                    add(child);
            }
        }
        return true;
    }
};

} // namespace

double terrainLevelRange(const TerrainLodSource& source, const TerrainLodSettings& settings, u32 level) noexcept
{
    const double leaf = static_cast<double>(TerrainGridQuads) * static_cast<double>(source.voxelSize);
    const double near = std::max(settings.nearRange, 3.0 * leaf);
    return near * static_cast<double>(1u << level);
}

void selectTerrainNodes(const TerrainLodSource& source, const TerrainLodSettings& settings,
                        std::vector<TerrainNode>& out)
{
    if (source.tilesX == 0 || source.tilesZ == 0 || !(source.voxelSize > 0.0f))
        return;
    if (source.tileMin.size() < static_cast<usize>(source.tilesX) * source.tilesZ ||
        source.tileMax.size() < static_cast<usize>(source.tilesX) * source.tilesZ)
        return;

    // The coarsest level needed: the first whose band reaches the view
    // distance, since nothing past it is drawn.
    u32 top = 0;
    while (top < TerrainMaxLevel && terrainLevelRange(source, settings, top) < settings.viewDistance)
        ++top;

    Pyramid pyramid;
    buildPyramid(source, top, pyramid);
    const Selector selector{source, settings, pyramid, out};

    const Pyramid::Level& roots = pyramid.levels[top];
    for (u32 z = 0; z < roots.sizeZ; ++z) {
        for (u32 x = 0; x < roots.sizeX; ++x)
            (void)selector.select(top, roots.originX + static_cast<i32>(x), roots.originZ + static_cast<i32>(z));
    }
}

} // namespace luaug::render
