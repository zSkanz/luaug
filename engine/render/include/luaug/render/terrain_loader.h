#pragma once

// Terrain on the GPU (ADR 0082).
//
// **The ground is meshes**, built on the CPU from the voxels by the mesher the
// colliders use, and drawn like any static mesh with the terrain's own shader.
// There is no height atlas and no vertex-shader level of detail any more: a cave,
// an overhang and a hillside are the same data, so they are the same draw.
//
// **A quadtree of columns of chunks carries the level of detail.** A leaf is one
// column of 32-voxel chunks, meshed at full detail; a node at level L covers
// `2^L` by `2^L` leaves and is meshed from their level-L mips -- an eighth of
// the triangles per level, and a thin wall thins rather than vanishing. Each
// node spans the whole height its chunks occupy, so two levels only ever meet
// on a vertical side, where the mesher hangs a skirt.
//
// **A change of level never shows a hole**: a node is drawn until all of its
// children are ready, and children until their parent is. Meshes are built
// nearest first, a fixed number a frame -- a count, never a clock -- and an edit
// rebuilds only the nodes whose chunks it changed.

#include "luaug/asset/terrain.h"
#include "luaug/asset/terrain_mesher.h"
#include "luaug/render/mesh_cache.h"
#include "luaug/render/render_world.h"
#include "luaug/rhi/device.h"
#include "luaug/scene/world.h"

#include <compare>
#include <span>
#include <string>
#include <vector>

namespace luaug::render {

// One node of a terrain's quadtree: at `level`, covering the chunk columns
// `[x * 2^level, (x + 1) * 2^level)` by the same in z.
struct TerrainNodeKey
{
    core::u32 level = 0;
    core::i32 x = 0;
    core::i32 z = 0;

    [[nodiscard]] constexpr auto operator<=>(const TerrainNodeKey&) const noexcept = default;
    [[nodiscard]] constexpr bool operator==(const TerrainNodeKey&) const noexcept = default;
};

// The coarsest level: a level-5 node is 32 by 32 chunk columns, a kilometre at
// the default voxel, meshed from each chunk's single level-5 value.
inline constexpr core::u32 TerrainTopLevel = asset::ChunkLevels - 1;

// The URN a node's mesh is filed under: `terrain://<instance>/<level>/<x>,<z>`.
[[nodiscard]] std::string terrainNodeUrn(core::InstanceId terrain, TerrainNodeKey node);

// **The mesh one node is drawn with**, exactly as `sync` builds it -- the
// region, the level and the skirt. Here so a test can hold what is DRAWN against
// the field and the collider. Empty when the node has nothing to draw.
[[nodiscard]] asset::TerrainMesh meshTerrainNode(const asset::TerrainField& field, TerrainNodeKey node);

struct TerrainLodSettings
{
    // A node is split into its children when the viewer is nearer to it than
    // this many times its own width. Two puts full detail out to about 64 m at
    // the default metre voxel, and each level doubles it.
    double splitFactor = 2.0;
    // Nothing further than this is drawn.
    double viewDistance = 4096.0;
};

class TerrainLoader
{
public:
    // Brings the meshes of every terrain in `world` up to date for the current
    // focus, and decides which nodes are drawn this frame. Answers how many
    // meshes it built.
    core::u32 sync(rhi::IDevice& device, rhi::ICmdList& cmd, const scene::World& world, core::AtomTable& atoms,
                   MeshCache& cache, MeshLibrary& library);

    // The nodes to draw for `world`, as the last `sync` of it chose them --
    // what `extract` turns into draws.
    [[nodiscard]] std::vector<TerrainNodeDraw> draws(const scene::World& world) const;

    // Appends one `RenderTerrain` per terrain in `world` under `root`: its
    // palette, which the terrain shader reads.
    void appendRenderTerrains(const scene::World& world, core::InstanceId root, RenderWorld& out) const;

    // **Where the viewer is**, which decides every level. Set once a frame from
    // the camera the last frame was drawn through. With no focus nothing is
    // drawn: a headless run with no camera has nobody to draw for.
    void setFocus(core::DVec3 focus) noexcept
    {
        m_focus = focus;
        m_hasFocus = true;
    }
    void clearFocus() noexcept { m_hasFocus = false; }

    void setLodSettings(const TerrainLodSettings& settings) noexcept { m_lod = settings; }
    [[nodiscard]] const TerrainLodSettings& lodSettings() const noexcept { return m_lod; }

    // How many meshes one `sync` may build. A count, never a clock.
    void setBuildsPerSync(core::u32 count) noexcept { m_buildsPerSync = count == 0 ? 1 : count; }

    // Releases everything this uploaded. Called once, by whoever owns it.
    void destroy(rhi::IDevice& device, MeshCache& cache, MeshLibrary& library);

    // How many nodes hold a mesh, across every terrain.
    [[nodiscard]] core::usize residentCount() const noexcept;
    // Whether one node of one terrain holds a mesh (it may be empty ground).
    [[nodiscard]] bool nodeResident(core::InstanceId terrain, TerrainNodeKey key) const noexcept;
    // Meshes built by the last `sync`, for tests and the perf overlay.
    [[nodiscard]] core::u32 lastBuilds() const noexcept { return m_lastBuilds; }
    // Whether the last `sync` left anything it wanted unbuilt.
    [[nodiscard]] bool pending() const noexcept { return m_pending; }

private:
    struct Node
    {
        const scene::World* world = nullptr;
        core::InstanceId terrain;
        TerrainNodeKey key;
        core::NameAtom urn;
        MeshHandle mesh;
        // What it was built from, and at which revision that was last checked.
        core::u64 content = 0;
        core::u64 revision = ~0ull;
        // Built at least once: a node of empty ground is built and has no mesh.
        bool built = false;
        // The last frame this node was drawn or wanted.
        core::u64 used = 0;
    };

    [[nodiscard]] Node* find(const scene::World* world, core::InstanceId terrain, TerrainNodeKey key) noexcept;
    [[nodiscard]] const Node* find(const scene::World* world, core::InstanceId terrain,
                                   TerrainNodeKey key) const noexcept;
    void release(rhi::IDevice& device, MeshCache& cache, MeshLibrary& library, Node& node);

    core::DVec3 m_focus;
    bool m_hasFocus = false;
    TerrainLodSettings m_lod;
    core::u32 m_buildsPerSync = 4;
    core::u32 m_lastBuilds = 0;
    bool m_pending = false;
    core::u64 m_frame = 0;

    // Sorted by (world, terrain index, key): never a hash map (R10).
    std::vector<Node> m_nodes;
    struct Drawn
    {
        const scene::World* world = nullptr;
        TerrainNodeDraw draw;
    };
    std::vector<Drawn> m_drawn;
};

} // namespace luaug::render
