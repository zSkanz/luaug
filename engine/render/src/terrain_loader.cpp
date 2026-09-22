#include "luaug/render/terrain_loader.h"

#include "luaug/asset/terrain_mesher.h"
#include "luaug/asset/terrain_palette.h"
#include "luaug/core/log.h"
#include "luaug/jobs/jobs.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <string>

namespace luaug::render {
namespace {

using core::u32;
using core::usize;

// **How much meshing one call may do, in lattice CELLS rather than tiles.**
//
// A count and never a millisecond budget: a tile that appears "when the machine
// got round to it" is a different amount of missing ground on every machine.
// Cells rather than tiles because tiles stopped costing the same once they had
// levels -- a tile at stride 8 is a sixty-fourth of one at stride 1, and a tile
// budget would spend a whole slot on it. This is four full-detail tiles' worth:
// a brush on a tile corner touches four, and all four have to show the stroke
// in the frame it was made. It was two while meshing ran on this thread; the
// tiles now mesh in parallel, so four costs a four-core machine what two used
// to cost, and a camera move re-levels dozens of distant tiles at once.
constexpr core::u64 CellsPerSync = 4ull * asset::TileEdge * asset::TileEdge * 16ull;

// The skirt, in lattice steps of the tile's own stride. Two steps is deeper than
// the largest disagreement two neighbouring levels can have at their seam.
constexpr float SkirtSteps = 2.0f;

// How much further a tile must be before it drops a level than it had to be to
// gain one. Without it a tile at a boundary flips every frame the camera
// breathes, and each flip is a rebuild.
constexpr double Hysteresis = 1.1;

// How far above and below a tile's own heights the mesher looks, in lattice
// steps, when the tile carries NO bricks.
//
// **Two, and it used to be thirty-two.** The terrain's whole reservation would
// be the honest answer and is far too tall -- `MinHeight` to `MaxHeight` at half
// a metre is a thousand cells of empty air per column, meshed to find nothing --
// so a tile is meshed around the heights it holds. The margin exists so the
// surface has a cell of air above it and a cell of ground below it to cross
// between; a height column has exactly one crossing and needs no more than that.
//
// Thirty-two was a guess standing in for "whatever bricks reach", and it cost
// eight times the triangles on flat ground: sixty-four cells of Y where eight
// would do, on every tile, twice a frame, against a sixteen-millisecond budget.
// Bricks are now measured rather than guessed at -- see `brickRangeOf`.
constexpr core::i32 SurfaceMargin = 2;

// Order-sensitive, which is what a key built from an ordered walk wants.
[[nodiscard]] core::u64 combine(core::u64 seed, core::u64 value) noexcept
{
    core::u64 z = seed ^ (value + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2));
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

[[nodiscard]] core::i32 floorDivide(core::i32 value, core::i32 divisor) noexcept
{
    const core::i32 quotient = value / divisor;
    return (value % divisor != 0 && (value < 0) != (divisor < 0)) ? quotient - 1 : quotient;
}

// What one tile's mesh reads: itself and its eight neighbours, and every brick
// whose footprint reaches within a column of it. `bricks` is the field's brick
// keys, fetched once per terrain rather than once per tile.
[[nodiscard]] core::u64 contentOf(const asset::TerrainField& field, asset::TileKey key,
                                  std::span<const asset::BrickKey> bricks) noexcept
{
    core::u64 content = 0;
    for (core::i32 dz = -1; dz <= 1; ++dz) {
        for (core::i32 dx = -1; dx <= 1; ++dx) {
            const asset::HeightTile* tile = field.findTile(asset::TileKey{key.x + dx, key.z + dz});
            content = combine(content, tile == nullptr ? 0x6E6F74696C65ull : asset::digestOf(*tile));
        }
    }
    const auto edge = static_cast<core::i32>(asset::TileEdge);
    const auto brickEdge = static_cast<core::i32>(asset::BrickEdge);
    const core::i32 minX = floorDivide(key.x * edge - 1, brickEdge);
    const core::i32 maxX = floorDivide(key.x * edge + edge + 1, brickEdge);
    const core::i32 minZ = floorDivide(key.z * edge - 1, brickEdge);
    const core::i32 maxZ = floorDivide(key.z * edge + edge + 1, brickEdge);
    for (const asset::BrickKey brick : bricks) {
        if (brick.x < minX || brick.x > maxX || brick.z < minZ || brick.z > maxZ)
            continue;
        const asset::Brick* found = field.findBrick(brick);
        content = combine(content, (static_cast<core::u64>(static_cast<core::u32>(brick.x)) << 40) ^
                                       (static_cast<core::u64>(static_cast<core::u32>(brick.y)) << 20) ^
                                       static_cast<core::u32>(brick.z));
        content = combine(content, found == nullptr ? 0 : asset::digestOf(*found));
    }
    return content;
}

} // namespace

std::string terrainTileUrn(core::InstanceId terrain, asset::TileKey key)
{
    return "terrain://" + std::to_string(terrain.index) + "/" + std::to_string(key.x) + "," + std::to_string(key.z);
}

u32 TerrainLoader::strideFor(double distance) const noexcept
{
    if (!m_hasFocus)
        return 1;
    if (distance > m_distances.viewDistance)
        return 0;
    u32 stride = 1;
    double reach = m_distances.nearDistance;
    // Up to 8: a 32-column tile at stride 8 is four cells a side, and coarser
    // than that the skirt is taller than the tile is wide.
    while (distance > reach && stride < 8) {
        stride *= 2;
        reach *= 2.0;
    }
    return stride;
}

u32 TerrainLoader::sync(rhi::IDevice& device, rhi::ICmdList& cmd, const scene::World& world, core::AtomTable& atoms,
                        MeshCache& cache, MeshLibrary& library)
{
    for (Resident& resident : m_tiles)
        resident.seen = false;

    // **Every tile that wants work, nearest first.**
    //
    // The first version walked tiles in key order and rebuilt the first two
    // dirty ones, which was fine while "dirty" meant "edited". With levels of
    // detail a camera move dirties every tile whose level changed, and walking
    // them in key order would re-level a corner of the world nobody is looking
    // at while the ground under the viewer waited. Sorted by distance, then by
    // terrain and key so two tiles at the same distance cannot trade places
    // between runs.
    struct Want
    {
        double distance = 0.0;
        core::InstanceId terrain;
        asset::TileKey key;
        u32 stride = 1;
    };
    std::vector<Want> wants;

    const auto findResident = [this](core::InstanceId id, asset::TileKey key) {
        return std::lower_bound(m_tiles.begin(), m_tiles.end(), key,
                                [&id](const Resident& entry, const asset::TileKey& probe) {
                                    if (entry.terrain.index != id.index)
                                        return entry.terrain.index < id.index;
                                    return entry.key < probe;
                                });
    };

    world.terrains().forEach([&](core::InstanceId id, const scene::TerrainComponent& terrain) {
        const float voxel = terrain.field.settings().voxelSize;
        if (!(voxel > 0.0f))
            return;
        const auto tileMetres = static_cast<double>(asset::TileEdge) * static_cast<double>(voxel);
        // Fetched lazily: a tick with nothing written never needs them.
        std::vector<asset::BrickKey> bricks;
        bool bricksFetched = false;

        for (const asset::TileKey key : terrain.field.tileKeys()) {
            // Distance in the horizontal plane, to the tile's nearest point
            // rather than its centre -- the viewer standing on a tile's edge is
            // on the tile, and a centre distance would coarsen the ground under
            // their feet.
            const double minX = terrain.origin.x + static_cast<double>(key.x) * tileMetres;
            const double minZ = terrain.origin.z + static_cast<double>(key.z) * tileMetres;
            const double dx = std::max({minX - m_focus.x, 0.0, m_focus.x - (minX + tileMetres)});
            const double dz = std::max({minZ - m_focus.z, 0.0, m_focus.z - (minZ + tileMetres)});
            const double distance = std::sqrt(dx * dx + dz * dz);

            u32 stride = strideFor(distance);
            const auto at = findResident(id, key);
            const bool exists = at != m_tiles.end() && at->terrain == id && at->key == key;

            // Hysteresis: keep a finer level until the tile is clearly past it.
            if (exists && at->stride != 0 && stride > at->stride && strideFor(distance / Hysteresis) <= at->stride)
                stride = at->stride;

            if (stride == 0) {
                // Beyond the view distance. Not marked seen, so the sweep below
                // releases whatever was resident.
                continue;
            }
            if (exists && at->revision == terrain.fieldRevision && at->stride == stride) {
                at->seen = true;
                continue;
            }
            // Something was written somewhere. Whether it was HERE is the
            // content key's question, and a tile it did not reach keeps its mesh.
            core::u64 content = 0;
            if (exists && at->stride == stride) {
                if (!bricksFetched) {
                    bricks = terrain.field.brickKeys();
                    bricksFetched = true;
                }
                content = contentOf(terrain.field, key, bricks);
                if (content == at->content) {
                    at->revision = terrain.fieldRevision;
                    at->seen = true;
                    continue;
                }
            }
            // Keep what is there while it waits: a tile that vanished during its
            // rebuild is a hole in the ground somebody can see through.
            if (exists)
                at->seen = true;
            wants.push_back(Want{distance, id, key, stride});
        }
    });

    std::sort(wants.begin(), wants.end(), [](const Want& a, const Want& b) {
        if (a.distance != b.distance)
            return a.distance < b.distance;
        if (a.terrain.index != b.terrain.index)
            return a.terrain.index < b.terrain.index;
        return a.key < b.key;
    });

    // **Chosen in series, meshed in parallel, applied in series.**
    //
    // Meshing is the whole cost of a tile and reads nothing but a field `sync`
    // holds by const reference, so the budgeted tiles are meshed at once across
    // the job pool, each into its own slot. Everything that has an ORDER -- the
    // atoms interned, the handles `MeshCache` hands out, the residency list --
    // happens afterwards on this thread, walking the slots in the order the
    // tiles were chosen. How many workers the machine has decides how soon the
    // meshes are ready and nothing else, and a serial pool runs the same slots
    // in the same order.
    struct Work
    {
        core::InstanceId terrain;
        asset::TileKey key;
        u32 stride = 1;
        const scene::TerrainComponent* component = nullptr;
        asset::MeshRegion region;
        asset::TerrainMesh meshed;
    };
    std::vector<Work> work;

    core::u64 spent = 0;
    for (const Want& want : wants) {
        if (spent >= CellsPerSync)
            break;

        const scene::TerrainComponent* terrainPtr = world.terrains().find(want.terrain);
        if (terrainPtr == nullptr)
            continue;
        const scene::TerrainComponent& terrain = *terrainPtr;
        const asset::TileKey key = want.key;
        const float voxel = terrain.field.settings().voxelSize;
        const auto edge = static_cast<core::i32>(asset::TileEdge);
        const asset::HeightTile* tile = terrain.field.findTile(key);
        if (tile == nullptr)
            continue;
        const auto stride = static_cast<core::i32>(want.stride);

        // The slab this tile's surface actually lives in, rather than the
        // terrain's whole legal range.
        float lowest = tile->height[0];
        float highest = tile->height[0];
        for (usize sample = 1; sample < asset::TileArea; ++sample) {
            lowest = std::min(lowest, tile->height[sample]);
            highest = std::max(highest, tile->height[sample]);
        }

        auto bottom = static_cast<core::i32>(std::floor(lowest / voxel)) - SurfaceMargin * stride;
        auto top = static_cast<core::i32>(std::ceil(highest / voxel)) + SurfaceMargin * stride;

        // **And whatever bricks this tile's columns actually carry**,
        // measured rather than assumed. A cave is voxels somewhere below the
        // surface, and a mesher that stopped at the height layer would leave
        // its roof and its floor unmeshed -- a hole you can see through. The
        // scan is over the brick keys, which is a handful even in a heavily
        // sculpted world, and it runs once per tile rebuild rather than per
        // cell.
        const core::i32 firstColumn = key.x * edge;
        const core::i32 firstRow = key.z * edge;
        const auto brickEdge = static_cast<core::i32>(asset::BrickEdge);
        for (const asset::BrickKey brick : terrain.field.brickKeys()) {
            const core::i32 brickMinX = brick.x * brickEdge;
            const core::i32 brickMinZ = brick.z * brickEdge;
            if (brickMinX + brickEdge <= firstColumn || brickMinX >= firstColumn + edge)
                continue;
            if (brickMinZ + brickEdge <= firstRow || brickMinZ >= firstRow + edge)
                continue;
            bottom = std::min(bottom, brick.y * brickEdge - SurfaceMargin);
            top = std::max(top, (brick.y + 1) * brickEdge + SurfaceMargin);
        }

        // Snapped down to the stride, so a coarse tile samples the same
        // lattice points its fine neighbour does and their shared edge
        // agrees wherever it can.
        bottom = static_cast<core::i32>(std::floor(static_cast<double>(bottom) / stride)) * stride;

        asset::MeshRegion region;
        region.minX = firstColumn;
        region.minZ = firstRow;
        region.minY = bottom;
        region.cellsX = asset::TileEdge / static_cast<u32>(stride);
        region.cellsZ = asset::TileEdge / static_cast<u32>(stride);
        region.cellsY = static_cast<u32>(std::max((top - region.minY + stride - 1) / stride, 1));
        region.stride = static_cast<u32>(stride);
        region.skirt = SkirtSteps * static_cast<float>(stride) * voxel;

        spent += static_cast<core::u64>(region.cellsX) * region.cellsY * region.cellsZ;
        work.push_back(Work{want.terrain, key, want.stride, terrainPtr, region, {}});
    }

    jobs::parallelFor("terrain.mesh", jobs::Domain::Render, 0, work.size(), 1,
                      [&work](usize begin, usize end, u32) noexcept {
                          for (usize at = begin; at < end; ++at)
                              work[at].meshed = asset::meshField(work[at].component->field, work[at].region);
                      });

    u32 rebuilt = 0;
    for (Work& job : work) {
        const core::InstanceId id = job.terrain;
        const asset::TileKey key = job.key;
        const scene::TerrainComponent& terrain = *job.component;
        const asset::TerrainMesh& meshed = job.meshed;
        const core::u64 content = contentOf(terrain.field, key, terrain.field.brickKeys());
        // Looked up again here rather than carried from the choice: an earlier
        // slot's insert or erase has moved the list since.
        const auto at = findResident(id, key);
        const bool exists = at != m_tiles.end() && at->terrain == id && at->key == key;
        // **Interned through the non-const table the caller owns.** A
        // tile's URN has to exist as an atom for `extract` to look it up,
        // and `sync` holds the world by const reference -- so the atom
        // table is taken separately, which also makes it explicit that this
        // is the one thing here that mutates anything shared.
        const core::NameAtom urn = atoms.intern(terrainTileUrn(id, key));

        if (meshed.mesh.indices.empty()) {
            // Nothing to draw here any more. The entry goes, and so does the
            // GPU mesh -- an empty draw is cheaper than a stale one, and a
            // stale one is ground that is not there.
            if (exists) {
                if (at->mesh.valid())
                    cache.release(device, at->mesh);
                library.remove(urn);
                m_tiles.erase(at);
            }
            rebuilt += 1;
            continue;
        }

        core::EngineError uploadError;
        const MeshHandle handle = cache.create(device, cmd, meshed.mesh, MeshUsage::Static, &uploadError);
        if (!handle.valid()) {
            core::logText(core::LogLevel::Warn, uploadError.message);
            continue;
        }

        MeshLibrary::Entry entry;
        entry.mesh = handle;
        entry.bounds = meshed.mesh.bounds;
        entry.sectionCount = static_cast<u32>(meshed.mesh.submeshes.size());
        // **One material per section, coloured from the palette**, which is
        // what makes a painted hillside look painted rather than uniformly
        // grey. The mesher buckets its triangles by material and hands over
        // `sectionMaterials` parallel to `submeshes`; this turns each id into
        // a colour and points the section at it.
        //
        // A colour and not a texture, deliberately and for now: a terrain
        // texture set is per-material albedo, normal and roughness plus the
        // triplanar blend the mesher's UVs are already laid out for, and
        // that is a texture pipeline rather than a colour lookup. Shipping
        // the colour first means painting is visible today and the textures
        // land later without moving anything here.
        entry.sectionMaterial.resize(entry.sectionCount);
        entry.materials.reserve(entry.sectionCount);
        for (u32 section = 0; section < entry.sectionCount; ++section) {
            const core::u8 materialId = section < meshed.sectionMaterials.size() ? meshed.sectionMaterials[section] : 0;
            const core::Vec3 tint = asset::terrainColorOf(materialId);
            RenderMaterial material;
            material.uniforms.baseColor[0] = tint.x;
            material.uniforms.baseColor[1] = tint.y;
            material.uniforms.baseColor[2] = tint.z;
            material.uniforms.baseColor[3] = 1.0f;
            // x metallic, y roughness. Ground is rough and not metal, and
            // the defaults are the other way round.
            material.uniforms.metallicRoughnessNormalCutoff[0] = 0.0f;
            material.uniforms.metallicRoughnessNormalCutoff[1] = 0.92f;
            entry.sectionMaterial[section] = section;
            entry.materials.push_back(material);
        }
        // **`positions` is left empty deliberately.** It exists for whoever
        // needs a collision hull off a `MeshPart`, and terrain's collider
        // comes from the field through `PhysicsSync` rather than from this --
        // filling it would be a second copy of the same surface with no
        // reader.

        if (exists) {
            if (at->mesh.valid())
                cache.release(device, at->mesh);
            at->mesh = handle;
            at->revision = terrain.fieldRevision;
            at->content = content;
            at->stride = job.stride;
            at->seen = true;
        }
        else {
            m_tiles.insert(at, Resident{id, key, urn, handle, terrain.fieldRevision, content, job.stride, true});
        }
        library.set(urn, std::move(entry));
        rebuilt += 1;
    }

    // A tile the field no longer holds takes its mesh with it.
    for (usize at = m_tiles.size(); at > 0; --at) {
        Resident& resident = m_tiles[at - 1];
        if (resident.seen)
            continue;
        if (resident.mesh.valid())
            cache.release(device, resident.mesh);
        library.remove(resident.urn);
        m_tiles.erase(m_tiles.begin() + static_cast<std::ptrdiff_t>(at - 1));
    }

    return rebuilt;
}

void TerrainLoader::destroy(rhi::IDevice& device, MeshCache& cache, MeshLibrary& library)
{
    for (Resident& resident : m_tiles) {
        if (resident.mesh.valid())
            cache.release(device, resident.mesh);
        library.remove(resident.urn);
    }
    m_tiles.clear();
}

} // namespace luaug::render
