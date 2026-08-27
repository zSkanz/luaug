#pragma once

// Terrain on the GPU (ADR 0067, F1 Part D).
//
// **Terrain does not become `MeshPart`s, and the reason is mechanical rather
// than aesthetic.** `attachPartComponents` adds a `RigidBodyComponent` to every
// `BasePart` with no condition and `applyScene` has no skip -- `CanCollide` and
// `CanQuery` are only flags on the resulting body -- so a few hundred generated
// terrain parts would be a few hundred phantom bodies in the broadphase, a few
// hundred more instances in every `World::snapshot`, and a few hundred rows in
// the Explorer. So the meshes go straight into `MeshLibrary` under a URN of
// their own and `extract` emits their draws directly.
//
// This is the terrain half of what `MeshLoader` is for a `MeshPart`: it walks
// the world, meshes what has changed, uploads it, and keeps `MeshLibrary`
// current. It is separate from `MeshLoader` because nothing about it reads a
// file -- the geometry is computed from a field that is already in memory.

#include "luaug/asset/terrain.h"
#include "luaug/render/mesh_cache.h"
#include "luaug/render/render_world.h"
#include "luaug/rhi/device.h"
#include "luaug/scene/world.h"

#include <vector>

namespace luaug::render {

// The URN a terrain tile's mesh is filed under, so `extract` and this agree
// about one name. `terrain://<instance>/<x>,<z>`.
[[nodiscard]] std::string terrainTileUrn(core::InstanceId terrain, asset::TileKey key);

class TerrainLoader
{
public:
    // Meshes and uploads whatever changed, and answers how many tiles it
    // rebuilt.
    //
    // **Budgeted by a COUNT rather than a clock**, the same rule the collider
    // mirror follows and for a related reason: a tile that appears a frame late
    // is a frame of missing ground, and one that appears "when the machine got
    // round to it" is a different amount of missing ground on every machine.
    core::u32 sync(rhi::IDevice& device, rhi::ICmdList& cmd, const scene::World& world, core::AtomTable& atoms,
                   MeshCache& cache, MeshLibrary& library);

    // Releases every mesh this uploaded. Called once, by whoever owns it.
    void destroy(rhi::IDevice& device, MeshCache& cache, MeshLibrary& library);

    // How many tiles are meshed and resident.
    [[nodiscard]] core::usize residentCount() const noexcept { return m_tiles.size(); }

private:
    struct Resident
    {
        core::InstanceId terrain;
        asset::TileKey key;
        core::NameAtom urn;
        MeshHandle mesh;
        core::u64 revision = 0;
        bool seen = false;
    };

    // Sorted by (terrain, key). Never a hash map: this decides the order meshes
    // are uploaded in, and a mesh handle's index reaches a draw's sort key.
    std::vector<Resident> m_tiles;
};

} // namespace luaug::render
