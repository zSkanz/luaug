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

    // **Where the viewer is**, which decides every tile's level of detail and
    // whether it is meshed at all.
    //
    // Set once a frame from the camera the LAST frame was drawn through, because
    // `sync` runs before `extract` decides this frame's. A frame of lag in a
    // level-of-detail choice is invisible; a tile meshed at the wrong level for
    // a frame is a tile at the level it had a moment ago.
    //
    // With no focus every tile is meshed at full detail, which is what a
    // headless run with no camera and the first frame of every run both need.
    void setFocus(core::DVec3 focus) noexcept
    {
        m_focus = focus;
        m_hasFocus = true;
    }
    void clearFocus() noexcept { m_hasFocus = false; }

    // **The distances the levels change at, in metres from the viewer.**
    //
    // Level 0 (every lattice sample) inside `nearDistance`; each level after it
    // halves the density and doubles the reach -- the cascade every reference
    // engine converges on, whether it is spelled as a quadtree, a clipmap or
    // godot_voxel's `lod_distance`. Beyond `viewDistance` a tile is not meshed
    // and its GPU mesh is released: that is what streaming means for the
    // renderer, and it is what keeps an unbounded field from costing an
    // unbounded amount of video memory.
    struct Distances
    {
        double nearDistance = 48.0;
        double viewDistance = 768.0;
    };
    void setDistances(Distances distances) noexcept { m_distances = distances; }
    [[nodiscard]] const Distances& distances() const noexcept { return m_distances; }

    // The stride a tile at this distance is meshed with: 1, 2, 4 or 8, or 0 when
    // it is beyond the view distance and should not be meshed at all. Public so
    // the selection can be tested without a GPU.
    [[nodiscard]] core::u32 strideFor(double distance) const noexcept;

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
        // **What the mesh was built FROM**: the digests of the tile, its eight
        // neighbours (the mesher reads one column past every edge, for the
        // seam and for the normals) and every brick in reach. `fieldRevision` is
        // one counter for the whole terrain, so on its own it said "every tile
        // is stale" after any brush stroke, and the loader spent each frame
        // re-meshing whichever tiles were nearest the camera while the one the
        // brush touched waited its turn.
        core::u64 content = 0;
        // The stride the resident mesh was built at, so a change of level is a
        // rebuild exactly as a change of field is.
        core::u32 stride = 0;
        bool seen = false;
    };

    core::DVec3 m_focus;
    bool m_hasFocus = false;
    Distances m_distances;

    // Sorted by (terrain, key). Never a hash map: this decides the order meshes
    // are uploaded in, and a mesh handle's index reaches a draw's sort key.
    std::vector<Resident> m_tiles;
};

} // namespace luaug::render
