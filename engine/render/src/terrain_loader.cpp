#include "luaug/render/terrain_loader.h"

#include "luaug/asset/terrain_mesher.h"
#include "luaug/asset/terrain_palette.h"
#include "luaug/core/log.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <span>
#include <string>

namespace luaug::render {
namespace {

using core::i32;
using core::u32;
using core::u64;
using core::usize;

// Slots per atlas row. 64 slots of 32 texels is a 2048-texel row, which every
// backend accepts; the atlas grows in rows.
constexpr u32 SlotsPerRow = 64;
constexpr u32 AtlasWidth = SlotsPerRow * asset::TileEdge;
// The tallest atlas: 256 rows of slots is 16,384 tiles, which at half a metre is
// a 2 km square of sculpted ground in one terrain.
constexpr u32 MaxRows = 256;

// How many tiles one `sync` may upload. A count, never a clock. A tile is five
// kilobytes, so this is five megabytes -- a whole 256 m terrain appears in one
// frame, and a 2 km one in a dozen.
constexpr u32 TilesPerSync = 1024;

// How many cave columns one `sync` may mesh. A column is 18 by 18 lattice
// columns and as tall as its surface and its bricks, a few milliseconds at the
// worst; four keeps a sculpting stroke inside one frame.
constexpr u32 CavesPerSync = 4;

// The margin above and below a cave column's surfaces, in lattice steps: a
// crossing needs a cell on each side of it.
constexpr i32 CaveMargin = 2;

// How many lattice cells a cave's mesh reaches past the LOW side of its column
// when that side borders ground drawn from the atlas. The ground's opening runs
// from the lattice point before the column to the one after it: on the low side
// the ground's last vertex is one step outside the column, so the mesh needs a
// ring of cells beyond that to put a vertex on it; on the high side the ground
// resumes on the very next lattice point, which a single rim cell's inner corner
// already is. See `CaveRims` and `MeshRegion::snapSides`.
constexpr i32 CaveRim = 2;

// The flag in a material byte that opens the ground for a cave mesh.
constexpr core::u8 CaveFlag = 0x80;

[[nodiscard]] i32 floorDivide(i32 value, i32 divisor) noexcept
{
    const i32 quotient = value / divisor;
    return (value % divisor != 0 && (value < 0) != (divisor < 0)) ? quotient - 1 : quotient;
}

// Order-sensitive, which is what a key built from an ordered walk wants.
[[nodiscard]] u64 combine(u64 seed, u64 value) noexcept
{
    u64 z = seed ^ (value + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2));
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

[[nodiscard]] u64 tileDigestOr(const asset::TerrainField& field, asset::TileKey key) noexcept
{
    const asset::HeightTile* tile = field.findTile(key);
    return tile == nullptr ? 0x6E6F74696C65ull : asset::digestOf(*tile);
}

[[nodiscard]] u32 nextPowerOfTwo(u32 value) noexcept
{
    u32 result = 1;
    while (result < value)
        result <<= 1;
    return result;
}

[[nodiscard]] bool inWorld(const scene::World& world, core::InstanceId id, core::InstanceId root) noexcept
{
    for (core::InstanceId cursor = id; cursor.valid(); cursor = world.parentOf(cursor)) {
        if (cursor == root)
            return true;
    }
    return false;
}

// **How far from the viewer a cave column is meshed**: to the end of level
// 2's band, a quarter of a kilometre at a metre's voxel.
//
// It was level 0's morph start less a column, about eighteen metres, when the
// height map opened caves per VERTEX: only there was every vertex one lattice
// step apart, so only there did a hole punched at a column's lattice points
// match the column. The ground opens per PIXEL now (`terrainCaveAt`), exactly
// at every level, and eighteen metres was a mountain whose tunnel showed the
// sky through it from anywhere you could see the mouth.
//
// Past this the column is closed ground: its top surface, drawn by the height
// map, which from a quarter of a kilometre is what a cave looks like.
[[nodiscard]] double caveRange(const TerrainLodSettings& lod, float voxel) noexcept
{
    TerrainLodSource source;
    source.voxelSize = voxel;
    return terrainLevelRange(source, lod, 2);
}

// What one cave column's mesh reads: the tiles and bricks within a column of
// its footprint.
[[nodiscard]] u64 caveContentOf(const asset::TerrainField& field, asset::TileKey column,
                                std::span<const asset::BrickKey> bricks) noexcept
{
    const auto edge = static_cast<i32>(asset::TileEdge);
    const auto brickEdge = static_cast<i32>(asset::BrickEdge);
    const i32 minX = column.x * brickEdge - CaveRim;
    const i32 minZ = column.z * brickEdge - CaveRim;
    const i32 span = brickEdge + 2 * CaveRim;
    u64 content = 0;
    for (i32 tz = floorDivide(minZ, edge); tz <= floorDivide(minZ + span, edge); ++tz) {
        for (i32 tx = floorDivide(minX, edge); tx <= floorDivide(minX + span, edge); ++tx)
            content = combine(content, tileDigestOr(field, asset::TileKey{tx, tz}));
    }
    for (const asset::BrickKey brick : bricks) {
        if (brick.x < column.x - 1 || brick.x > column.x + 1 || brick.z < column.z - 1 || brick.z > column.z + 1)
            continue;
        const asset::Brick* found = field.findBrick(brick);
        content = combine(content, (static_cast<u64>(static_cast<u32>(brick.x)) << 40) ^
                                       (static_cast<u64>(static_cast<u32>(brick.y)) << 20) ^ static_cast<u32>(brick.z));
        content = combine(content, found == nullptr ? 0 : asset::digestOf(*found));
    }
    return content;
}

// How far a cave column's mesh reaches past each of its four sides, in cells,
// and which of those sides meet ground drawn from the atlas.
//
// **Next to atlas ground, the mesh ends ON the ground's last vertex**: the
// outer ring of cells snaps to the lattice point where the height field's
// boundary vertex is (`MeshRegion::snapSides`), two cells out on the low side
// and one on the high side, so the cave and the ground meet vertex for vertex.
// It used to overlap the ground by a cell and sink the overlap a few
// centimetres, which could not hide a steep wall -- a pit that was part height
// field and part cave showed a step where the two walls disagreed.
//
// **Next to another cave, the mesh reaches one cell into it and nothing
// snaps**: both meshes then carry an identical strip from the same field, which
// closes the seam between them.
struct CaveRims
{
    i32 lowX = CaveRim;
    i32 highX = 1;
    i32 lowZ = CaveRim;
    i32 highZ = 1;
    core::u8 snapSides = 0;
};

[[nodiscard]] CaveRims caveRimsOf(asset::TileKey column, std::span<const asset::TileKey> columns) noexcept
{
    const auto isCave = [&columns](i32 x, i32 z) {
        return std::binary_search(columns.begin(), columns.end(), asset::TileKey{x, z});
    };
    CaveRims rims;
    const bool caveLowX = isCave(column.x - 1, column.z);
    const bool caveHighX = isCave(column.x + 1, column.z);
    const bool caveLowZ = isCave(column.x, column.z - 1);
    const bool caveHighZ = isCave(column.x, column.z + 1);
    rims.lowX = caveLowX ? 1 : CaveRim;
    rims.lowZ = caveLowZ ? 1 : CaveRim;
    rims.snapSides = static_cast<core::u8>((caveLowX ? 0u : 1u) | (caveHighX ? 0u : 2u) | (caveLowZ ? 0u : 4u) |
                                           (caveHighZ ? 0u : 8u));
    return rims;
}

// The region a cave column is meshed over: its footprint plus its rims, and
// from below the lowest surface in reach to above the highest -- the
// neighbours' bricks included, so a strip two columns share is cut from the
// same slab on both sides.
[[nodiscard]] bool caveRegion(const asset::TerrainField& field, asset::TileKey column, const CaveRims& rims,
                              std::span<const asset::BrickKey> bricks, asset::MeshRegion& region) noexcept
{
    const auto edge = static_cast<i32>(asset::TileEdge);
    const auto brickEdge = static_cast<i32>(asset::BrickEdge);
    const float voxel = field.settings().voxelSize;
    const i32 minX = column.x * brickEdge - rims.lowX;
    const i32 minZ = column.z * brickEdge - rims.lowZ;
    const i32 spanX = brickEdge + rims.lowX + rims.highX;
    const i32 spanZ = brickEdge + rims.lowZ + rims.highZ;

    i32 bottom = std::numeric_limits<i32>::max();
    i32 top = std::numeric_limits<i32>::lowest();
    for (i32 z = 0; z <= spanZ; ++z) {
        for (i32 x = 0; x <= spanX; ++x) {
            const i32 lx = minX + x;
            const i32 lz = minZ + z;
            const asset::HeightTile* tile =
                field.findTile(asset::TileKey{floorDivide(lx, edge), floorDivide(lz, edge)});
            if (tile == nullptr)
                continue;
            const auto localX = static_cast<usize>(lx - floorDivide(lx, edge) * edge);
            const auto localZ = static_cast<usize>(lz - floorDivide(lz, edge) * edge);
            const float height = tile->height[localZ * asset::TileEdge + localX];
            bottom = std::min(bottom, static_cast<i32>(std::floor(height / voxel)) - CaveMargin);
            top = std::max(top, static_cast<i32>(std::ceil(height / voxel)) + CaveMargin);
        }
    }
    for (const asset::BrickKey brick : bricks) {
        if (brick.x < column.x - 1 || brick.x > column.x + 1 || brick.z < column.z - 1 || brick.z > column.z + 1)
            continue;
        bottom = std::min(bottom, brick.y * brickEdge - CaveMargin);
        top = std::max(top, (brick.y + 1) * brickEdge + CaveMargin);
    }
    if (bottom >= top)
        return false;

    region.minX = minX;
    region.minZ = minZ;
    region.minY = bottom;
    region.cellsX = static_cast<u32>(spanX);
    region.cellsZ = static_cast<u32>(spanZ);
    region.cellsY = static_cast<u32>(top - bottom);
    region.stride = 1;
    region.snapSides = rims.snapSides;
    // **Skirts, now that caves are drawn past the finest level.** The ground
    // around a column is drawn coarser with distance, and a coarse surface is
    // a chord of the true one -- on a dome it runs up to a few metres inside
    // it. At the column's edge that is a step with nothing under it, and a
    // grazing ray went through it to the sky. Four metres covers level 2's
    // worst chord on anything a brush makes; hung against the normal, a skirt
    // is always inside rock, so on the sides it is not needed it is not seen.
    region.skirt = 4.0f * voxel;
    return true;
}

[[nodiscard]] std::vector<asset::TileKey> brickColumnsOf(std::span<const asset::BrickKey> bricks)
{
    std::vector<asset::TileKey> columns;
    columns.reserve(bricks.size());
    for (const asset::BrickKey brick : bricks)
        columns.push_back(asset::TileKey{brick.x, brick.z});
    std::sort(columns.begin(), columns.end());
    columns.erase(std::unique(columns.begin(), columns.end()), columns.end());
    return columns;
}

} // namespace

std::string terrainCaveUrn(core::InstanceId terrain, asset::TileKey column)
{
    return "terrain://" + std::to_string(terrain.index) + "/cave/" + std::to_string(column.x) + "," +
           std::to_string(column.z);
}

usize TerrainLoader::residentCount() const noexcept
{
    usize count = 0;
    for (const GpuTerrain& gpu : m_terrains)
        count += gpu.slots.size();
    return count;
}

bool TerrainLoader::caveResident(core::InstanceId terrain, asset::TileKey column) const noexcept
{
    const auto at =
        std::lower_bound(m_caves.begin(), m_caves.end(), column, [&](const Cave& entry, asset::TileKey key) {
            if (entry.terrain.index != terrain.index)
                return entry.terrain.index < terrain.index;
            return entry.column < key;
        });
    return at != m_caves.end() && at->terrain == terrain && at->column == column && at->mesh.valid();
}

void TerrainLoader::releaseGpu(rhi::IDevice& device, GpuTerrain& gpu)
{
    if (gpu.heights.valid())
        device.destroy(gpu.heights);
    if (gpu.materials.valid())
        device.destroy(gpu.materials);
    if (gpu.tileTable.valid())
        device.destroy(gpu.tileTable);
    gpu.heights = {};
    gpu.materials = {};
    gpu.tileTable = {};
    gpu.rows = 0;
    gpu.tableEdge = 0;
    gpu.slots.clear();
    gpu.freeSlots.clear();
    gpu.table.clear();
}

u32 TerrainLoader::sync(rhi::IDevice& device, rhi::ICmdList& cmd, const scene::World& world, core::AtomTable& atoms,
                        MeshCache& cache, MeshLibrary& library)
{
    m_lastTileUploads = 0;
    for (GpuTerrain& gpu : m_terrains)
        gpu.seen = false;
    for (Cave& cave : m_caves)
        cave.seen = false;

    u32 rebuilt = 0;
    u32 cavesBuilt = 0;

    world.terrains().forEach([&](core::InstanceId id, const scene::TerrainComponent& terrain) {
        const asset::TerrainField& field = terrain.field;
        const float voxel = field.settings().voxelSize;
        if (!(voxel > 0.0f))
            return;

        auto gpuAt = std::lower_bound(m_terrains.begin(), m_terrains.end(), id.index,
                                      [](const GpuTerrain& entry, u32 index) { return entry.terrain.index < index; });
        if (gpuAt == m_terrains.end() || gpuAt->terrain != id) {
            GpuTerrain fresh;
            fresh.terrain = id;
            gpuAt = m_terrains.insert(gpuAt, std::move(fresh));
        }
        GpuTerrain& gpu = *gpuAt;
        gpu.seen = true;
        if (gpu.voxelSize != voxel) {
            // A different lattice is different ground: everything re-uploads.
            releaseGpu(device, gpu);
            gpu.voxelSize = voxel;
        }

        // --- Caves, first, because their flags are part of every tile ------
        const std::vector<asset::BrickKey> bricks = field.brickKeys();
        const std::vector<asset::TileKey> columns = brickColumnsOf(bricks);
        const double reach = caveRange(m_lod, voxel);
        const double columnMetres = static_cast<double>(asset::BrickEdge) * static_cast<double>(voxel);
        for (const asset::TileKey column : columns) {
            if (!m_hasFocus)
                break;
            const double minX = terrain.origin.x + static_cast<double>(column.x) * columnMetres;
            const double minZ = terrain.origin.z + static_cast<double>(column.z) * columnMetres;
            const double dx = std::max({minX - m_focus.x, 0.0, m_focus.x - (minX + columnMetres)});
            const double dz = std::max({minZ - m_focus.z, 0.0, m_focus.z - (minZ + columnMetres)});
            if (std::sqrt(dx * dx + dz * dz) > reach)
                continue;

            auto at =
                std::lower_bound(m_caves.begin(), m_caves.end(), column, [&](const Cave& entry, asset::TileKey key) {
                    if (entry.terrain.index != id.index)
                        return entry.terrain.index < id.index;
                    return entry.column < key;
                });
            const bool exists = at != m_caves.end() && at->terrain == id && at->column == column;
            const u64 content = caveContentOf(field, column, bricks);
            if (exists && at->content == content) {
                at->seen = true;
                continue;
            }
            if (cavesBuilt >= CavesPerSync) {
                // Keep what is there while it waits; a column not yet meshed
                // at all stays closed ground until its turn.
                if (exists)
                    at->seen = true;
                continue;
            }

            MeshHandle handle;
            const core::NameAtom urn = atoms.intern(terrainCaveUrn(id, column));
            asset::MeshRegion region;
            const CaveRims rims = caveRimsOf(column, columns);
            if (caveRegion(field, column, rims, bricks, region)) {
                const asset::TerrainMesh meshed = asset::meshField(field, region);
                if (!meshed.mesh.indices.empty()) {
                    core::EngineError uploadError;
                    handle = cache.create(device, cmd, meshed.mesh, MeshUsage::Static, &uploadError);
                    if (!handle.valid()) {
                        core::logText(core::LogLevel::Warn, uploadError.message);
                    }
                    else {
                        MeshLibrary::Entry entry;
                        entry.mesh = handle;
                        entry.bounds = meshed.mesh.bounds;
                        entry.sectionCount = static_cast<u32>(meshed.mesh.submeshes.size());
                        entry.sectionMaterial.resize(entry.sectionCount);
                        entry.materials.reserve(entry.sectionCount);
                        for (u32 section = 0; section < entry.sectionCount; ++section) {
                            const core::u8 materialId =
                                section < meshed.sectionMaterials.size() ? meshed.sectionMaterials[section] : 0;
                            const core::Vec3 tint = asset::terrainColorOf(materialId);
                            RenderMaterial material;
                            material.uniforms.baseColor[0] = tint.x;
                            material.uniforms.baseColor[1] = tint.y;
                            material.uniforms.baseColor[2] = tint.z;
                            material.uniforms.baseColor[3] = 1.0f;
                            material.uniforms.metallicRoughnessNormalCutoff[0] = 0.0f;
                            material.uniforms.metallicRoughnessNormalCutoff[1] = 0.92f;
                            entry.sectionMaterial[section] = section;
                            entry.materials.push_back(material);
                        }
                        library.set(urn, std::move(entry));
                    }
                }
            }
            if (!handle.valid())
                library.remove(urn);

            if (exists) {
                if (at->mesh.valid())
                    cache.release(device, at->mesh);
                at->mesh = handle;
                at->content = content;
                at->seen = true;
            }
            else {
                m_caves.insert(at, Cave{id, column, urn, handle, content, true});
            }
            cavesBuilt += 1;
            rebuilt += 1;
        }
        // This terrain's caves that were not wanted go now, before the tiles
        // are uploaded -- their flags must leave the atlas in the same frame.
        for (usize at = m_caves.size(); at > 0; --at) {
            Cave& cave = m_caves[at - 1];
            if (cave.terrain != id || cave.seen)
                continue;
            if (cave.mesh.valid())
                cache.release(device, cave.mesh);
            library.remove(cave.urn);
            m_caves.erase(m_caves.begin() + static_cast<std::ptrdiff_t>(at - 1));
        }

        // --- The atlas --------------------------------------------------------
        const std::vector<asset::TileKey> keys = field.tileKeys();
        const u32 wanted = static_cast<u32>(std::min<usize>(keys.size(), static_cast<usize>(MaxRows) * SlotsPerRow));
        const u32 rowsNeeded = std::max(1u, nextPowerOfTwo((wanted + SlotsPerRow - 1) / SlotsPerRow));
        if (gpu.rows < rowsNeeded || !gpu.heights.valid()) {
            // Grown by recreating: there is no texture copy in the RHI, and a
            // re-upload of every tile from the field is the same bytes.
            releaseGpu(device, gpu);
            gpu.rows = std::min(rowsNeeded, MaxRows);
            const u32 height = gpu.rows * asset::TileEdge;
            gpu.heights = device.createTexture({.format = rhi::TextureFormat::R32Float,
                                                .usage = rhi::TextureUsage::Sampled,
                                                .width = AtlasWidth,
                                                .height = height,
                                                .debugName = "terrain.heights"});
            gpu.materials = device.createTexture({.format = rhi::TextureFormat::R8Unorm,
                                                  .usage = rhi::TextureUsage::Sampled,
                                                  .width = AtlasWidth,
                                                  .height = height,
                                                  .debugName = "terrain.materials"});
            gpu.tableDirty = true;
        }

        // The tile table: large enough for every key, recentred when a key
        // falls outside it.
        if (!keys.empty()) {
            i32 minX = keys.front().x;
            i32 maxX = keys.front().x;
            i32 minZ = keys.front().z;
            i32 maxZ = keys.front().z;
            for (const asset::TileKey key : keys) {
                minX = std::min(minX, key.x);
                maxX = std::max(maxX, key.x);
                minZ = std::min(minZ, key.z);
                maxZ = std::max(maxZ, key.z);
            }
            const bool fits = gpu.tableEdge > 0 && minX >= gpu.tableOriginX && minZ >= gpu.tableOriginZ &&
                              maxX < gpu.tableOriginX + static_cast<i32>(gpu.tableEdge) &&
                              maxZ < gpu.tableOriginZ + static_cast<i32>(gpu.tableEdge);
            if (!fits) {
                const auto extent = static_cast<u32>(std::max(maxX - minX, maxZ - minZ) + 1);
                const u32 edge = std::max(16u, nextPowerOfTwo(extent + 8));
                if (gpu.tileTable.valid())
                    device.destroy(gpu.tileTable);
                gpu.tableEdge = edge;
                gpu.tableOriginX = minX - static_cast<i32>((edge - static_cast<u32>(maxX - minX + 1)) / 2);
                gpu.tableOriginZ = minZ - static_cast<i32>((edge - static_cast<u32>(maxZ - minZ + 1)) / 2);
                gpu.tileTable = device.createTexture({.format = rhi::TextureFormat::R32Float,
                                                      .usage = rhi::TextureUsage::Sampled,
                                                      .width = edge,
                                                      .height = edge,
                                                      .debugName = "terrain.tiles"});
                gpu.table.assign(static_cast<usize>(edge) * edge, -1.0f);
                for (const Slot& slot : gpu.slots) {
                    const i32 tx = slot.key.x - gpu.tableOriginX;
                    const i32 tz = slot.key.z - gpu.tableOriginZ;
                    if (tx >= 0 && tz >= 0 && tx < static_cast<i32>(edge) && tz < static_cast<i32>(edge))
                        gpu.table[static_cast<usize>(tz) * edge + static_cast<usize>(tx)] =
                            static_cast<float>(slot.slot);
                }
                gpu.tableDirty = true;
            }
        }
        else if (gpu.tableEdge == 0) {
            gpu.tableEdge = 16;
            gpu.tileTable = device.createTexture({.format = rhi::TextureFormat::R32Float,
                                                  .usage = rhi::TextureUsage::Sampled,
                                                  .width = 16,
                                                  .height = 16,
                                                  .debugName = "terrain.tiles"});
            gpu.table.assign(256, -1.0f);
            gpu.tableDirty = true;
        }

        for (Slot& slot : gpu.slots)
            slot.seen = false;

        const auto tableIndex = [&gpu](asset::TileKey key) -> i32 {
            const i32 tx = key.x - gpu.tableOriginX;
            const i32 tz = key.z - gpu.tableOriginZ;
            if (tx < 0 || tz < 0 || tx >= static_cast<i32>(gpu.tableEdge) || tz >= static_cast<i32>(gpu.tableEdge))
                return -1;
            return tz * static_cast<i32>(gpu.tableEdge) + tx;
        };

        std::vector<float> heights(asset::TileArea);
        std::vector<core::u8> materials(asset::TileArea);
        const auto brickEdge = static_cast<i32>(asset::BrickEdge);
        const u32 perTile = asset::TileEdge / asset::BrickEdge;

        for (const asset::TileKey key : keys) {
            const asset::HeightTile* tile = field.findTile(key);
            if (tile == nullptr)
                continue;
            auto at = std::lower_bound(gpu.slots.begin(), gpu.slots.end(), key,
                                       [](const Slot& entry, asset::TileKey probe) { return entry.key < probe; });
            const bool exists = at != gpu.slots.end() && at->key == key;

            // The cave flags this tile's columns carry, folded into its content
            // so a cave appearing or leaving re-uploads the tile.
            u32 flags = 0;
            for (u32 bz = 0; bz < perTile; ++bz) {
                for (u32 bx = 0; bx < perTile; ++bx) {
                    const asset::TileKey column{key.x * static_cast<i32>(perTile) + static_cast<i32>(bx),
                                                key.z * static_cast<i32>(perTile) + static_cast<i32>(bz)};
                    if (caveResident(id, column))
                        flags |= 1u << (bz * perTile + bx);
                }
            }
            const u64 content = combine(asset::digestOf(*tile), flags);
            if (exists && at->content == content) {
                at->seen = true;
                continue;
            }
            if (m_lastTileUploads >= TilesPerSync) {
                if (exists)
                    at->seen = true;
                continue;
            }

            u32 slotIndex = 0;
            if (exists) {
                slotIndex = at->slot;
            }
            else if (!gpu.freeSlots.empty()) {
                slotIndex = gpu.freeSlots.back();
                gpu.freeSlots.pop_back();
            }
            else {
                slotIndex = static_cast<u32>(gpu.slots.size());
                if (slotIndex >= gpu.rows * SlotsPerRow)
                    continue; // past the atlas's ceiling: this tile is not drawn
            }

            float lowest = tile->height[0];
            float highest = tile->height[0];
            for (usize sample = 0; sample < asset::TileArea; ++sample) {
                heights[sample] = tile->height[sample];
                lowest = std::min(lowest, tile->height[sample]);
                highest = std::max(highest, tile->height[sample]);
                const auto x = static_cast<i32>(sample % asset::TileEdge);
                const auto z = static_cast<i32>(sample / asset::TileEdge);
                const u32 bit = static_cast<u32>(z / brickEdge) * perTile + static_cast<u32>(x / brickEdge);
                materials[sample] = static_cast<core::u8>((tile->material[sample] & 0x7F) |
                                                          (((flags >> bit) & 1u) != 0u ? CaveFlag : 0));
            }

            const u32 x = (slotIndex % SlotsPerRow) * asset::TileEdge;
            const u32 y = (slotIndex / SlotsPerRow) * asset::TileEdge;
            cmd.uploadTextureRegion(gpu.heights, x, y, asset::TileEdge, asset::TileEdge,
                                    std::as_bytes(std::span<const float>(heights)));
            cmd.uploadTextureRegion(gpu.materials, x, y, asset::TileEdge, asset::TileEdge,
                                    std::as_bytes(std::span<const core::u8>(materials)));
            m_lastTileUploads += 1;
            rebuilt += 1;

            if (exists) {
                at->content = content;
                at->minHeight = lowest;
                at->maxHeight = highest;
                at->seen = true;
            }
            else {
                gpu.slots.insert(at, Slot{key, slotIndex, content, lowest, highest, true});
                const i32 index = tableIndex(key);
                if (index >= 0) {
                    gpu.table[static_cast<usize>(index)] = static_cast<float>(slotIndex);
                    gpu.tableDirty = true;
                }
            }
        }

        // Tiles the field no longer holds give their slot back.
        for (usize at = gpu.slots.size(); at > 0; --at) {
            const Slot& slot = gpu.slots[at - 1];
            if (slot.seen)
                continue;
            gpu.freeSlots.push_back(slot.slot);
            const i32 index = tableIndex(slot.key);
            if (index >= 0) {
                gpu.table[static_cast<usize>(index)] = -1.0f;
                gpu.tableDirty = true;
            }
            gpu.slots.erase(gpu.slots.begin() + static_cast<std::ptrdiff_t>(at - 1));
        }
        // Deterministic reuse: the lowest free slot first.
        std::sort(gpu.freeSlots.begin(), gpu.freeSlots.end(), std::greater<>());

        if (gpu.tableDirty && gpu.tileTable.valid()) {
            cmd.uploadTexture(gpu.tileTable, std::as_bytes(std::span<const float>(gpu.table)), 0);
            gpu.tableDirty = false;
        }
    });

    // Terrains that are gone take their textures and caves with them.
    for (usize at = m_caves.size(); at > 0; --at) {
        Cave& cave = m_caves[at - 1];
        if (cave.seen)
            continue;
        if (cave.mesh.valid())
            cache.release(device, cave.mesh);
        library.remove(cave.urn);
        m_caves.erase(m_caves.begin() + static_cast<std::ptrdiff_t>(at - 1));
    }
    for (usize at = m_terrains.size(); at > 0; --at) {
        GpuTerrain& gpu = m_terrains[at - 1];
        if (gpu.seen)
            continue;
        releaseGpu(device, gpu);
        m_terrains.erase(m_terrains.begin() + static_cast<std::ptrdiff_t>(at - 1));
    }

    return rebuilt;
}

void TerrainLoader::appendRenderTerrains(const scene::World& world, core::InstanceId root, RenderWorld& out) const
{
    for (const GpuTerrain& gpu : m_terrains) {
        const scene::TerrainComponent* terrain = world.terrains().find(gpu.terrain);
        if (terrain == nullptr || gpu.slots.empty() || !gpu.heights.valid() || !gpu.tileTable.valid())
            continue;
        if (!inWorld(world, gpu.terrain, root))
            continue;

        RenderTerrain entry;
        entry.id = gpu.terrain;
        entry.tileTable = gpu.tileTable;
        entry.heights = gpu.heights;
        entry.materials = gpu.materials;
        entry.atlas[0] = static_cast<f32>(SlotsPerRow);
        entry.atlas[1] = static_cast<f32>(gpu.tableEdge);
        entry.atlas[2] = static_cast<f32>(gpu.tableOriginX);
        entry.atlas[3] = static_cast<f32>(gpu.tableOriginZ);
        entry.atlasSize[0] = static_cast<f32>(AtlasWidth);
        entry.atlasSize[1] = static_cast<f32>(gpu.rows * asset::TileEdge);
        entry.atlasSize[2] = 1.0f / entry.atlasSize[0];
        entry.atlasSize[3] = 1.0f / entry.atlasSize[1];
        entry.voxelSize = gpu.voxelSize;
        entry.origin = terrain->origin;

        i32 minX = gpu.slots.front().key.x;
        i32 maxX = minX;
        i32 minZ = gpu.slots.front().key.z;
        i32 maxZ = minZ;
        for (const Slot& slot : gpu.slots) {
            minX = std::min(minX, slot.key.x);
            maxX = std::max(maxX, slot.key.x);
            minZ = std::min(minZ, slot.key.z);
            maxZ = std::max(maxZ, slot.key.z);
        }
        entry.minTileX = minX;
        entry.minTileZ = minZ;
        entry.tilesX = static_cast<u32>(maxX - minX + 1);
        entry.tilesZ = static_cast<u32>(maxZ - minZ + 1);
        const usize count = static_cast<usize>(entry.tilesX) * entry.tilesZ;
        entry.tileMin.assign(count, 1.0f);
        entry.tileMax.assign(count, -1.0f);
        for (const Slot& slot : gpu.slots) {
            const usize index =
                static_cast<usize>(slot.key.z - minZ) * entry.tilesX + static_cast<usize>(slot.key.x - minX);
            entry.tileMin[index] = slot.minHeight;
            entry.tileMax[index] = slot.maxHeight;
        }

        for (u32 id = 0; id < kTerrainPaletteSize; ++id) {
            const core::Vec3 color = asset::terrainColorOf(static_cast<core::u8>(id));
            entry.palette[id][0] = color.x;
            entry.palette[id][1] = color.y;
            entry.palette[id][2] = color.z;
            entry.palette[id][3] = 1.0f;
        }
        out.terrains.push_back(std::move(entry));
    }
}

void TerrainLoader::destroy(rhi::IDevice& device, MeshCache& cache, MeshLibrary& library)
{
    for (GpuTerrain& gpu : m_terrains)
        releaseGpu(device, gpu);
    m_terrains.clear();
    for (Cave& cave : m_caves) {
        if (cave.mesh.valid())
            cache.release(device, cave.mesh);
        library.remove(cave.urn);
    }
    m_caves.clear();
}

} // namespace luaug::render
