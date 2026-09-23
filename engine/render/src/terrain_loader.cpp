#include "luaug/render/terrain_loader.h"

#include "luaug/asset/terrain_mesher.h"
#include "luaug/asset/terrain_palette.h"
#include "luaug/core/log.h"
#include "luaug/jobs/jobs.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace luaug::render {
namespace {

using core::f32;
using core::f64;
using core::i32;
using core::u32;
using core::u64;
using core::usize;

// How many frames a node may go undrawn and unwanted before its mesh is let
// go. Long enough that looking away and back does not rebuild anything.
constexpr u64 EvictAfterFrames = 180;

// At most this many EDITED nodes rebuilt in one frame. An edit rebuilds every
// node it changed in the frame it lands (`sync`), and this is only the ceiling
// that keeps a brush the size of the world from stalling one frame for all of
// it; the rest follow in the next.
constexpr u32 MaxEditBuildsPerSync = 64;

// Order-sensitive, which is what a key built from an ordered walk wants.
[[nodiscard]] u64 combine(u64 seed, u64 value) noexcept
{
    u64 z = seed ^ (value + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2));
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

[[nodiscard]] i32 across(u32 level) noexcept
{
    return static_cast<i32>(1u << level);
}

// Walks the chunks of the columns `[x0, x1] x [z0, z1]`, inclusive, in key
// order: one search per x, then along the sorted list.
template <class Visit>
void forChunksIn(const asset::TerrainField& field, i32 x0, i32 x1, i32 z0, i32 z1, Visit&& visit)
{
    const std::span<const asset::TerrainField::Entry> chunks = field.chunks();
    for (i32 x = x0; x <= x1; ++x) {
        const asset::ChunkKey low{x, std::numeric_limits<i32>::min(), z0};
        auto at = std::lower_bound(
            chunks.begin(), chunks.end(), low,
            [](const asset::TerrainField::Entry& entry, const asset::ChunkKey& probe) { return entry.first < probe; });
        for (; at != chunks.end() && at->first.x == x && at->first.z <= z1; ++at)
            visit(*at);
    }
}

// **Whether a node can have anything to draw**: a chunk in its footprint or in
// the column just past its high sides, whose surface against this node's air is
// still this node's to mesh.
[[nodiscard]] bool occupied(const asset::TerrainField& field, TerrainNodeKey key) noexcept
{
    const i32 n = across(key.level);
    bool found = false;
    forChunksIn(field, key.x * n, key.x * n + n, key.z * n, key.z * n + n,
                [&](const asset::TerrainField::Entry&) { found = true; });
    return found;
}

// **The chunk columns a node's mesh reads**, on one axis, inclusive: its own,
// and as far past each side as the mesher reaches (`asset::meshReach`) -- one
// column at full detail, two at the coarsest, more for small voxels. The key a
// node is rebuilt by, and what is prepared before meshing in parallel.
struct ChunkSpan
{
    i32 low = 0;
    i32 high = -1;
};

[[nodiscard]] ChunkSpan readSpan(const asset::TerrainField& field, TerrainNodeKey key, i32 index) noexcept
{
    const i32 reach = asset::meshReach(field.settings(), key.level);
    const auto cells = static_cast<i32>(asset::ChunkEdge);
    const auto scale = static_cast<i32>(1u << key.level);
    const i32 first = index * cells - reach;
    const i32 last = index * cells + cells - 1 + reach;
    return ChunkSpan{asset::floorDiv(first * scale, cells), asset::floorDiv((last + 1) * scale - 1, cells)};
}

// What a node's mesh reads (`readSpan`), **whole**.
//
// The ring used to count at full detail only by the two layers of voxels that
// face the node, which is all the triangles read. The openness baked into every
// vertex reads further: the tops and bottoms of the columns up to 12 m round it
// (`terrain_mesher.cpp`). So an edit a few metres into the next column changed
// what a node's vertices should look like and left the node as it was -- dark
// where the ground had since gone, bright where it had since come -- until
// something else rebuilt it: the owner's "the shadows go wrong while I edit".
// Whole costs the eight nodes round an edit a rebuild, which `sync` does in
// parallel and in the same frame.
[[nodiscard]] u64 contentOf(const asset::TerrainField& field, TerrainNodeKey key) noexcept
{
    const ChunkSpan xs = readSpan(field, key, key.x);
    const ChunkSpan zs = readSpan(field, key, key.z);
    u64 content = combine(combine(key.level, static_cast<u64>(static_cast<u32>(key.x))),
                          static_cast<u64>(static_cast<u32>(key.z)));
    forChunksIn(field, xs.low, xs.high, zs.low, zs.high, [&](const asset::TerrainField::Entry& entry) {
        content = combine(content, static_cast<u64>(static_cast<u32>(entry.first.x)));
        content = combine(content, static_cast<u64>(static_cast<u32>(entry.first.y)));
        content = combine(content, static_cast<u64>(static_cast<u32>(entry.first.z)));
        content = combine(content, entry.second->digest());
    });
    return content;
}

// Registers a meshed node under its URN, one material per section tinted
// from the palette. Answers the handle, invalid when there was nothing.
[[nodiscard]] MeshHandle upload(rhi::IDevice& device, rhi::ICmdList& cmd, MeshCache& cache, MeshLibrary& library,
                                core::NameAtom urn, const asset::TerrainMesh& meshed)
{
    if (meshed.mesh.indices.empty())
        return {};
    core::EngineError uploadError;
    const MeshHandle handle = cache.create(device, cmd, meshed.mesh, MeshUsage::Static, &uploadError);
    if (!handle.valid()) {
        core::logText(core::LogLevel::Warn, uploadError.message);
        return {};
    }
    MeshLibrary::Entry entry;
    entry.mesh = handle;
    entry.bounds = meshed.mesh.bounds;
    entry.sectionCount = static_cast<u32>(meshed.mesh.submeshes.size());
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
        material.uniforms.metallicRoughnessNormalCutoff[0] = 0.0f;
        material.uniforms.metallicRoughnessNormalCutoff[1] = 0.92f;
        entry.sectionMaterial[section] = section;
        entry.materials.push_back(material);
    }
    library.set(urn, std::move(entry));
    return handle;
}

// Whether `id` is `root` or under it.
[[nodiscard]] bool inWorld(const scene::World& world, core::InstanceId id, core::InstanceId root) noexcept
{
    for (core::InstanceId at = id; at.valid(); at = world.parentOf(at)) {
        if (at == root)
            return true;
    }
    return false;
}

} // namespace

std::string terrainNodeUrn(core::InstanceId terrain, TerrainNodeKey node)
{
    return "terrain://" + std::to_string(terrain.index) + "/" + std::to_string(node.level) + "/" +
           std::to_string(node.x) + "," + std::to_string(node.z);
}

asset::TerrainMesh meshTerrainNode(const asset::TerrainField& field, TerrainNodeKey node)
{
    if (node.level > TerrainTopLevel)
        return {};
    const i32 n = across(node.level);
    const i32 step = across(node.level);
    // **One region per run of rows that can hold a surface** (`activeRuns`):
    // the ground's top and the bottom of the terrain far under it are meshed,
    // and the solid rock between is not walked. Runs that meet at this level's
    // coarser rows are merged first, so no two regions own the same row.
    std::vector<std::pair<i32, i32>> runs;
    for (const auto& [low, high] : asset::activeRuns(field, node.x * n, node.z * n, n)) {
        const i32 first = asset::floorDiv(low, step);
        const i32 last = asset::floorDiv(high, step);
        if (!runs.empty() && first <= runs.back().second + 1)
            runs.back().second = std::max(runs.back().second, last);
        else
            runs.emplace_back(first, last);
    }
    asset::TerrainMesh out;
    for (const auto& [first, last] : runs) {
        asset::MeshRegion region;
        region.level = node.level;
        // A node is `n` chunks of 32 voxels, which at its own level is 32
        // voxels: every node, whatever its level, is 32 cells a side.
        region.minX = node.x * static_cast<i32>(asset::ChunkEdge);
        region.minZ = node.z * static_cast<i32>(asset::ChunkEdge);
        region.cellsX = asset::ChunkEdge;
        region.cellsZ = asset::ChunkEdge;
        region.minY = first;
        region.cellsY = static_cast<u32>(last - first + 1);
        // **Two of this level's cells**: enough to reach under the widest
        // crack a coarser neighbour can leave, and hidden in the ground.
        region.skirt = 2.0f * static_cast<f32>(step) * field.settings().voxelSize;
        asset::appendMesh(out, asset::meshField(field, region));
    }
    return out;
}

TerrainLoader::Node* TerrainLoader::find(const scene::World* world, core::InstanceId terrain,
                                         TerrainNodeKey key) noexcept
{
    const auto& self = *this;
    return const_cast<Node*>(self.find(world, terrain, key));
}

const TerrainLoader::Node* TerrainLoader::find(const scene::World* world, core::InstanceId terrain,
                                               TerrainNodeKey key) const noexcept
{
    for (const Node& node : m_nodes) {
        if (node.world == world && node.terrain == terrain && node.key == key)
            return &node;
    }
    return nullptr;
}

void TerrainLoader::release(rhi::IDevice& device, MeshCache& cache, MeshLibrary& library, Node& node)
{
    if (node.mesh.valid())
        cache.release(device, node.mesh);
    if (node.urn.valid())
        library.remove(node.urn);
    node.mesh = {};
}

u32 TerrainLoader::sync(rhi::IDevice& device, rhi::ICmdList& cmd, const scene::World& world, core::AtomTable& atoms,
                        MeshCache& cache, MeshLibrary& library)
{
    m_frame += 1;
    m_lastBuilds = 0;
    m_pending = false;
    std::erase_if(m_drawn, [&](const Drawn& drawn) { return drawn.world == &world; });

    struct Request
    {
        core::InstanceId terrain;
        TerrainNodeKey key;
        f64 distance = 0.0;
    };
    std::vector<Request> requests;

    world.terrains().forEach([&](core::InstanceId id, const scene::TerrainComponent& terrain) {
        const asset::TerrainField& field = terrain.field;
        const f64 voxel = static_cast<f64>(field.settings().voxelSize);
        if (field.empty() || !(voxel > 0.0) || !m_hasFocus)
            return;
        const f64 chunkMetres = voxel * static_cast<f64>(asset::ChunkEdge);
        const core::DVec3 focus{m_focus.x - terrain.origin.x, m_focus.y - terrain.origin.y,
                                m_focus.z - terrain.origin.z};

        // The field's extent, in chunks: x from the sorted ends, z and y by a
        // walk. What the roots cover and how tall every node's box is.
        const std::span<const asset::TerrainField::Entry> chunks = field.chunks();
        i32 minZ = chunks.front().first.z;
        i32 maxZ = minZ;
        i32 minY = chunks.front().first.y;
        i32 maxY = minY;
        for (const asset::TerrainField::Entry& entry : chunks) {
            minZ = std::min(minZ, entry.first.z);
            maxZ = std::max(maxZ, entry.first.z);
            minY = std::min(minY, entry.first.y);
            maxY = std::max(maxY, entry.first.y);
        }
        const f64 lowMetres = static_cast<f64>(minY) * chunkMetres;
        const f64 highMetres = static_cast<f64>(maxY + 1) * chunkMetres;

        // A node is current when its content matches what it was built from;
        // checked only when the field has been written since.
        const auto nodeFor = [&](TerrainNodeKey key) -> Node& {
            if (Node* found = find(&world, id, key))
                return *found;
            Node fresh;
            fresh.world = &world;
            fresh.terrain = id;
            fresh.key = key;
            fresh.urn = atoms.intern(terrainNodeUrn(id, key));
            m_nodes.push_back(fresh);
            return m_nodes.back();
        };
        const auto current = [&](Node& node) {
            if (!node.built)
                return false;
            if (node.revision == terrain.fieldRevision)
                return true;
            const u64 content = contentOf(field, node.key);
            if (content != node.content)
                return false;
            node.revision = terrain.fieldRevision;
            return true;
        };
        const auto distanceTo = [&](TerrainNodeKey key) {
            const f64 width = static_cast<f64>(across(key.level)) * chunkMetres;
            const f64 x0 = static_cast<f64>(key.x) * width;
            const f64 z0 = static_cast<f64>(key.z) * width;
            const f64 dx = std::max({x0 - focus.x, 0.0, focus.x - (x0 + width)});
            const f64 dy = std::max({lowMetres - focus.y, 0.0, focus.y - highMetres});
            const f64 dz = std::max({z0 - focus.z, 0.0, focus.z - (z0 + width)});
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        };
        const auto request = [&](TerrainNodeKey key, f64 distance) {
            Node& node = nodeFor(key);
            node.used = m_frame;
            if (!current(node))
                requests.push_back(Request{id, key, distance});
        };
        const auto resident = [&](TerrainNodeKey key) {
            const Node* node = find(&world, id, key);
            return node != nullptr && node->built;
        };
        const auto draw = [&](TerrainNodeKey key) {
            Node& node = nodeFor(key);
            node.used = m_frame;
            if (node.mesh.valid())
                m_drawn.push_back(Drawn{&world, TerrainNodeDraw{id, node.urn}});
        };

        const auto childrenOf = [&](TerrainNodeKey key, std::array<TerrainNodeKey, 4>& out) {
            usize count = 0;
            if (key.level == 0)
                return count;
            for (i32 dz = 0; dz < 2; ++dz) {
                for (i32 dx = 0; dx < 2; ++dx) {
                    const TerrainNodeKey child{key.level - 1, key.x * 2 + dx, key.z * 2 + dz};
                    if (occupied(field, child))
                        out[count++] = child;
                }
            }
            return count;
        };
        // **Whether a node's ground can be drawn without a hole**: its own mesh,
        // or meshes further down that cover all of it. Asking only whether the
        // children had meshes of their own was the flicker the owner saw in the
        // cave: an ancestor let go while its descendants were drawn came back,
        // counted as ready, and was drawn over the whole close-up for the
        // frames its own children took to be rebuilt.
        const auto coverable = [&](const auto& self, TerrainNodeKey key) -> bool {
            if (resident(key))
                return true;
            std::array<TerrainNodeKey, 4> children{};
            const usize count = childrenOf(key, children);
            if (count == 0)
                return false;
            for (usize at = 0; at < count; ++at) {
                if (!self(self, children[at]))
                    return false;
            }
            return true;
        };
        // Draws a node, or whatever is built underneath it.
        const auto drawCovering = [&](const auto& self, TerrainNodeKey key) -> void {
            if (resident(key)) {
                draw(key);
                return;
            }
            std::array<TerrainNodeKey, 4> children{};
            const usize count = childrenOf(key, children);
            for (usize at = 0; at < count; ++at)
                self(self, children[at]);
        };

        // **The selection, depth first in key order** (R10: the order draws and
        // requests are made in is a function of the world and the camera).
        const auto select = [&](const auto& self, TerrainNodeKey key) -> void {
            if (!occupied(field, key))
                return;
            const f64 distance = distanceTo(key);
            if (distance > m_lod.viewDistance)
                return;
            // Every node the walk passes through is in use, drawn or not: an
            // ancestor let go is one the next step back has to rebuild first.
            if (Node* visited = find(&world, id, key))
                visited->used = m_frame;
            const f64 width = static_cast<f64>(across(key.level)) * chunkMetres;
            std::array<TerrainNodeKey, 4> children{};
            const usize childCount = childrenOf(key, children);
            const auto childrenCoverable = [&] {
                for (usize at = 0; at < childCount; ++at) {
                    if (!coverable(coverable, children[at]))
                        return false;
                }
                return true;
            };

            if (key.level > 0 && distance < m_lod.splitFactor * width) {
                for (usize at = 0; at < childCount; ++at) {
                    if (!resident(children[at]))
                        request(children[at], distanceTo(children[at]));
                }
                if (childrenCoverable()) {
                    for (usize at = 0; at < childCount; ++at)
                        self(self, children[at]);
                    return;
                }
                // Not ready: this node stands in for them if it can -- or they
                // draw what they already have.
                if (resident(key)) {
                    draw(key);
                    return;
                }
                for (usize at = 0; at < childCount; ++at)
                    self(self, children[at]);
                return;
            }

            request(key, distance);
            // Its own mesh, or finer ones while it is not there yet.
            drawCovering(drawCovering, key);
        };

        const i32 top = across(TerrainTopLevel);
        const i32 firstX = asset::floorDiv(chunks.front().first.x, top);
        const i32 lastX = asset::floorDiv(chunks.back().first.x + 1, top);
        const i32 firstZ = asset::floorDiv(minZ, top);
        const i32 lastZ = asset::floorDiv(maxZ + 1, top);
        for (i32 rootZ = firstZ; rootZ <= lastZ; ++rootZ) {
            for (i32 rootX = firstX; rootX <= lastX; ++rootX)
                select(select, TerrainNodeKey{TerrainTopLevel, rootX, rootZ});
        }
    });

    // **Nearest first, a fixed count** -- ties by level, terrain and key, so the
    // order is a fact about the world and the camera.
    std::sort(requests.begin(), requests.end(), [](const Request& a, const Request& b) {
        if (a.distance != b.distance)
            return a.distance < b.distance;
        if (a.key.level != b.key.level)
            return a.key.level < b.key.level;
        if (a.terrain.index != b.terrain.index)
            return a.terrain.index < b.terrain.index;
        return a.key < b.key;
    });
    requests.erase(
        std::unique(requests.begin(), requests.end(),
                    [](const Request& a, const Request& b) { return a.terrain == b.terrain && a.key == b.key; }),
        requests.end());
    // **Which to build this frame.** A node that was built and is stale is
    // ground somebody changed, and **every one of those is rebuilt in the frame
    // the change lands**, up to `MaxEditBuildsPerSync`. Rebuilt a few a frame,
    // neighbouring nodes showed two versions of one edit for several frames --
    // a ridge half raised, an old shade beside a new one -- and a held brush
    // changes the ground every frame, so that never settled: the flicker the
    // owner saw while editing. A node that was never built is loading, and
    // loading keeps the per-frame budget, nearest first.
    struct Build
    {
        const asset::TerrainField* field = nullptr;
        u64 revision = 0;
        Node* node = nullptr;
        TerrainNodeKey key;
        asset::TerrainMesh mesh;
    };
    std::vector<Build> builds;
    u32 edits = 0;
    u32 loads = 0;
    for (const Request& next : requests) {
        const scene::TerrainComponent* terrain = world.terrains().find(next.terrain);
        Node* node = find(&world, next.terrain, next.key);
        if (terrain == nullptr || node == nullptr)
            continue;
        if (node->built ? edits >= MaxEditBuildsPerSync : loads >= m_buildsPerSync) {
            m_pending = true;
            continue;
        }
        (node->built ? edits : loads) += 1;
        builds.push_back(Build{&terrain->field, terrain->fieldRevision, node, next.key, {}});
    }

    // **Meshed in parallel**, which is what makes rebuilding a whole edit in
    // one frame affordable. The mesher is a pure function of the field and the
    // key; the one thing in it that writes is a chunk's lazy mip, and that is
    // done here first, on this thread (`TerrainChunk::prepareMip`).
    for (const Build& build : builds) {
        if (build.key.level == 0)
            continue;
        const ChunkSpan xs = readSpan(*build.field, build.key, build.key.x);
        const ChunkSpan zs = readSpan(*build.field, build.key, build.key.z);
        forChunksIn(*build.field, xs.low, xs.high, zs.low, zs.high,
                    [&](const asset::TerrainField::Entry& entry) { entry.second->prepareMip(build.key.level); });
    }
    jobs::parallelFor("terrain.mesh", jobs::Domain::Render, 0, builds.size(), 1,
                      [&builds](usize begin, usize end, u32) noexcept {
                          for (usize at = begin; at < end; ++at)
                              builds[at].mesh = meshTerrainNode(*builds[at].field, builds[at].key);
                      });

    // Uploaded in request order, on this thread: nearest first, as ever.
    for (Build& build : builds) {
        Node& node = *build.node;
        if (node.mesh.valid())
            cache.release(device, node.mesh);
        node.mesh = upload(device, cmd, cache, library, node.urn, build.mesh);
        if (!node.mesh.valid())
            library.remove(node.urn);
        node.content = contentOf(*build.field, build.key);
        node.revision = build.revision;
        node.built = true;
        m_lastBuilds += 1;
    }

    // Nodes of this world nobody has drawn or wanted for a while, or whose
    // terrain is gone, let their meshes go.
    for (usize at = m_nodes.size(); at > 0; --at) {
        Node& node = m_nodes[at - 1];
        if (node.world != &world)
            continue;
        const bool gone = world.terrains().find(node.terrain) == nullptr;
        if (!gone && node.used + EvictAfterFrames >= m_frame)
            continue;
        release(device, cache, library, node);
        m_nodes.erase(m_nodes.begin() + static_cast<std::ptrdiff_t>(at - 1));
    }
    return m_lastBuilds;
}

std::vector<TerrainNodeDraw> TerrainLoader::draws(const scene::World& world) const
{
    std::vector<TerrainNodeDraw> out;
    for (const Drawn& drawn : m_drawn) {
        if (drawn.world == &world)
            out.push_back(drawn.draw);
    }
    return out;
}

void TerrainLoader::appendRenderTerrains(const scene::World& world, core::InstanceId root, RenderWorld& out) const
{
    world.terrains().forEach([&](core::InstanceId id, const scene::TerrainComponent& terrain) {
        if (terrain.field.empty() || !inWorld(world, id, root))
            return;
        RenderTerrain entry;
        entry.id = id;
        entry.origin = terrain.origin;
        for (u32 material = 0; material < kTerrainPaletteSize; ++material) {
            const core::Vec3 color = asset::terrainColorOf(static_cast<core::u8>(material));
            entry.palette[material][0] = color.x;
            entry.palette[material][1] = color.y;
            entry.palette[material][2] = color.z;
            entry.palette[material][3] = 1.0f;
        }
        out.terrains.push_back(std::move(entry));
    });
}

void TerrainLoader::destroy(rhi::IDevice& device, MeshCache& cache, MeshLibrary& library)
{
    for (Node& node : m_nodes)
        release(device, cache, library, node);
    m_nodes.clear();
    m_drawn.clear();
}

usize TerrainLoader::residentCount() const noexcept
{
    usize count = 0;
    for (const Node& node : m_nodes) {
        if (node.mesh.valid())
            ++count;
    }
    return count;
}

bool TerrainLoader::nodeResident(core::InstanceId terrain, TerrainNodeKey key) const noexcept
{
    for (const Node& node : m_nodes) {
        if (node.terrain == terrain && node.key == key && node.built)
            return true;
    }
    return false;
}

} // namespace luaug::render
