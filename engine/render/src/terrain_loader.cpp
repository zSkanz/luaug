#include "luaug/render/terrain_loader.h"

#include "luaug/asset/terrain_mesher.h"
#include "luaug/core/log.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace luaug::render {
namespace {

using core::u32;
using core::usize;

// **How many tiles get meshed in one call.** A count and never a millisecond
// budget: a tile that appears "when the machine got round to it" is a different
// amount of missing ground on every machine, and the same rule keeps the
// collider mirror honest one layer down.
//
// Two rather than one, because a tile's mesh and its neighbour's are what a
// person sees as one edit -- a brush that straddles a tile boundary would
// otherwise show half of itself for a frame.
constexpr u32 TilesPerSync = 2;

// How tall a slab of field one tile's mesh covers, in lattice steps.
//
// **The terrain's own reservation would be the honest answer and it is far too
// tall**: `MinHeight` to `MaxHeight` at half a metre is a thousand cells of
// empty air per column, meshed to find nothing. So a tile is meshed around the
// heights it actually holds, plus a margin for whatever bricks reach above or
// below them.
constexpr core::i32 VerticalMargin = static_cast<core::i32>(asset::BrickEdge) * 2;

} // namespace

std::string terrainTileUrn(core::InstanceId terrain, asset::TileKey key)
{
    return "terrain://" + std::to_string(terrain.index) + "/" + std::to_string(key.x) + "," + std::to_string(key.z);
}

u32 TerrainLoader::sync(rhi::IDevice& device, rhi::ICmdList& cmd, const scene::World& world, core::AtomTable& atoms,
                        MeshCache& cache, MeshLibrary& library)
{
    for (Resident& resident : m_tiles)
        resident.seen = false;

    u32 rebuilt = 0;

    world.terrains().forEach([&](core::InstanceId id, const scene::TerrainComponent& terrain) {
        const float voxel = terrain.field.settings().voxelSize;
        if (!(voxel > 0.0f))
            return;
        const auto edge = static_cast<core::i32>(asset::TileEdge);

        for (const asset::TileKey key : terrain.field.tileKeys()) {
            const asset::HeightTile* tile = terrain.field.findTile(key);
            if (tile == nullptr)
                continue;

            const auto at = std::lower_bound(m_tiles.begin(), m_tiles.end(), key,
                                             [&id](const Resident& entry, const asset::TileKey& probe) {
                                                 if (entry.terrain.index != id.index)
                                                     return entry.terrain.index < id.index;
                                                 return entry.key < probe;
                                             });
            const bool exists = at != m_tiles.end() && at->terrain == id && at->key == key;
            if (exists && at->revision == terrain.fieldRevision) {
                at->seen = true;
                continue;
            }
            if (rebuilt >= TilesPerSync) {
                // Over budget: keep what is there rather than dropping it. A
                // tile that vanished while waiting for its rebuild is a hole in
                // the ground somebody can see through.
                if (exists)
                    at->seen = true;
                continue;
            }

            // The slab this tile's surface actually lives in, rather than the
            // terrain's whole legal range.
            float lowest = tile->height[0];
            float highest = tile->height[0];
            for (usize sample = 1; sample < asset::TileArea; ++sample) {
                lowest = std::min(lowest, tile->height[sample]);
                highest = std::max(highest, tile->height[sample]);
            }

            asset::MeshRegion region;
            region.minX = key.x * edge;
            region.minZ = key.z * edge;
            region.minY = static_cast<core::i32>(std::floor(lowest / voxel)) - VerticalMargin;
            const auto top = static_cast<core::i32>(std::ceil(highest / voxel)) + VerticalMargin;
            region.cellsX = asset::TileEdge;
            region.cellsZ = asset::TileEdge;
            region.cellsY = static_cast<u32>(std::max(top - region.minY, 1));
            region.stride = 1;

            const asset::TerrainMesh meshed = asset::meshField(terrain.field, region);
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
            entry.sectionMaterial.assign(entry.sectionCount, 0u);
            // One material for now, and it is the default block. **A per-material
            // split is what terrain wants eventually and it is not what makes the
            // surface correct**, so it is named as absent rather than half-built:
            // the mesher already carries a material per voxel and nothing yet
            // turns those into sections.
            entry.materials.push_back(RenderMaterial{});
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
                at->seen = true;
            }
            else {
                m_tiles.insert(at, Resident{id, key, urn, handle, terrain.fieldRevision, true});
            }
            library.set(urn, std::move(entry));
            rebuilt += 1;
        }
    });

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
