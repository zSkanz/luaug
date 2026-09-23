#pragma once

// Terrain on the GPU (ADR 0067, ADR 0071).
//
// **The ground is not a mesh.** Each terrain's height tiles are copied into an
// `R32Float` atlas -- one 32 by 32 slot per tile -- with a second `R8Unorm`
// atlas of material ids and a small table saying which slot each tile key has.
// The renderer draws the terrain as a quadtree of one shared grid, lifted to the
// atlas's heights in the vertex shader (`terrain_lod.h`,
// `shaders/include/luaug_terrain.hlsli`). A brush stroke therefore costs the
// upload of the tiles it touched -- a few kilobytes -- and nothing is meshed.
//
// This is the architecture the reference heightmap terrains converge on
// (Terrain3D's clipmap over region textures, Strugar's CDLOD), and it replaced
// an earlier one that meshed every tile on the CPU with marching tetrahedra,
// hung skirts under every edge to hide the cracks between levels, and spent
// most of a frame doing it during an edit.
//
// **Caves are the exception, and they are meshes.** A column carrying voxel
// bricks is not a height function, so near the viewer each bricked column is
// meshed from the field on the CPU, filed in `MeshLibrary` under
// `terrainCaveUrn`, and drawn like any mesh; its material byte in the atlas
// gets the cave flag, and the terrain shader discards the ground's triangles
// there so the mesh shows through. Far away there is no cave mesh, no flag and
// no hole -- a cave mouth two hundred metres off is drawn as the hillside
// around it, which at that distance is what it looks like.

#include "luaug/asset/terrain.h"
#include "luaug/asset/terrain_mesher.h"
#include "luaug/render/mesh_cache.h"
#include "luaug/render/render_world.h"
#include "luaug/render/terrain_lod.h"
#include "luaug/rhi/device.h"
#include "luaug/scene/world.h"

#include <string>
#include <vector>

namespace luaug::render {

// The URN one bricked column's cave mesh is filed under, so `extract` and this
// agree about one name. `terrain://<instance>/cave/<x>,<z>`, in brick keys.
[[nodiscard]] std::string terrainCaveUrn(core::InstanceId terrain, asset::TileKey column);

// The mesh one bricked column's cave is drawn with: the region `sync` meshes,
// with the same rims, snapped sides and skirt, on the CPU. It is here so a test
// can hold the surface that is DRAWN against the field and the collider (F1's
// seam gate). Empty when the column has nothing to draw.
[[nodiscard]] asset::TerrainMesh meshCaveColumn(const asset::TerrainField& field, asset::TileKey column);

class TerrainLoader
{
public:
    // Brings the GPU copy of every terrain up to date: uploads the tiles whose
    // content changed, and meshes the cave columns near the viewer. Answers how
    // many tiles and caves it rebuilt.
    //
    // **Budgeted by a COUNT rather than a clock**, the rule the collider mirror
    // follows: a tile that appears "when the machine got round to it" is a
    // different amount of missing ground on every machine.
    core::u32 sync(rhi::IDevice& device, rhi::ICmdList& cmd, const scene::World& world, core::AtomTable& atoms,
                   MeshCache& cache, MeshLibrary& library);

    // Appends one `RenderTerrain` per terrain in `world` under `root`, naming
    // the textures this loader owns. Called after `extract`, into the same
    // snapshot.
    void appendRenderTerrains(const scene::World& world, core::InstanceId root, RenderWorld& out) const;

    // **Where the viewer is**: which caves are meshed, and which tiles upload
    // first. Set once a frame from the camera the last frame was drawn
    // through. With no focus, no cave is meshed -- a headless run has nobody
    // to stand in one.
    void setFocus(core::DVec3 focus) noexcept
    {
        m_focus = focus;
        m_hasFocus = true;
    }
    void clearFocus() noexcept { m_hasFocus = false; }

    // The level-of-detail bands the renderer draws with. Caves are meshed only
    // inside level 0's morph start, where the grid is one vertex per lattice
    // step and a hole in it lines up with the cave mesh exactly.
    void setLodSettings(const TerrainLodSettings& settings) noexcept { m_lod = settings; }
    [[nodiscard]] const TerrainLodSettings& lodSettings() const noexcept { return m_lod; }

    // Releases everything this uploaded. Called once, by whoever owns it.
    void destroy(rhi::IDevice& device, MeshCache& cache, MeshLibrary& library);

    // How many tiles have an atlas slot, across every terrain.
    [[nodiscard]] core::usize residentCount() const noexcept;
    // How many cave columns are meshed.
    [[nodiscard]] core::usize caveCount() const noexcept { return m_caves.size(); }
    // Tiles uploaded by the last `sync`, for tests and the perf overlay.
    [[nodiscard]] core::u32 lastTileUploads() const noexcept { return m_lastTileUploads; }

private:
    struct Slot
    {
        asset::TileKey key;
        core::u32 slot = 0;
        // What was uploaded: the tile's digest and its cave flags, folded.
        core::u64 content = 0;
        float minHeight = 0.0f;
        float maxHeight = 0.0f;
        bool seen = false;
    };

    struct GpuTerrain
    {
        core::InstanceId terrain;
        rhi::TextureHandle heights;
        rhi::TextureHandle materials;
        rhi::TextureHandle tileTable;
        core::u32 rows = 0;
        core::u32 tableEdge = 0;
        core::i32 tableOriginX = 0;
        core::i32 tableOriginZ = 0;
        float voxelSize = 0.5f;
        // Sorted by key (R10: never a hash map -- this decides upload order).
        std::vector<Slot> slots;
        std::vector<core::u32> freeSlots;
        std::vector<float> table;
        bool tableDirty = false;
        bool seen = false;
    };

    struct Cave
    {
        core::InstanceId terrain;
        asset::TileKey column;
        core::NameAtom urn;
        MeshHandle mesh;
        core::u64 content = 0;
        bool seen = false;
    };

    void releaseGpu(rhi::IDevice& device, GpuTerrain& gpu);
    [[nodiscard]] bool caveResident(core::InstanceId terrain, asset::TileKey column) const noexcept;

    core::DVec3 m_focus;
    bool m_hasFocus = false;
    TerrainLodSettings m_lod;
    core::u32 m_lastTileUploads = 0;

    // Sorted by terrain index.
    std::vector<GpuTerrain> m_terrains;
    // Sorted by (terrain, column).
    std::vector<Cave> m_caves;
};

} // namespace luaug::render
