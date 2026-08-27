#include "luaug/render/terrain_loader.h"

#include "luaug/asset/terrain_mesher.h"
#include "luaug/asset/terrain_palette.h"
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

            auto bottom = static_cast<core::i32>(std::floor(lowest / voxel)) - SurfaceMargin;
            auto top = static_cast<core::i32>(std::ceil(highest / voxel)) + SurfaceMargin;

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

            asset::MeshRegion region;
            region.minX = firstColumn;
            region.minZ = firstRow;
            region.minY = bottom;
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
                const core::u8 materialId =
                    section < meshed.sectionMaterials.size() ? meshed.sectionMaterials[section] : 0;
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
