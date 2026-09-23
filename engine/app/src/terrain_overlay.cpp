#include "luaug/app/terrain_overlay.h"

#include "luaug/asset/terrain_mesher.h"
#include "luaug/render/debug_draw.h"
#include "luaug/scene/world.h"

#include <cmath>
#include <vector>

namespace luaug::app {

namespace {

// How far out from the camera's voxel the cube reaches on each axis.
constexpr core::i32 HalfExtent = 16;

// How long a normal is drawn, in voxels: long enough to read its direction,
// short enough not to cross the next vertex's.
constexpr float NormalLength = 0.6f;

// One terrain's mesh around the camera, kept until the ground or the camera's
// cell changes.
struct Cached
{
    core::InstanceId terrain;
    core::u64 revision = ~core::u64{0};
    core::i32 x = 0;
    core::i32 y = 0;
    core::i32 z = 0;
    asset::TerrainMesh mesh;
};

} // namespace

void drawTerrainDebug(const scene::World& world, core::DVec3 eye, bool wireframe, bool normals, render::DebugDraw& draw)
{
    if (!wireframe && !normals)
        return;
    // Function-local, like the frame loop's other overlay caches: one editor,
    // one world at a time, and a stale entry is only ever re-meshed.
    static std::vector<Cached> cache;

    const auto agree = render::DebugColor::fromLinear(0.35f, 0.9f, 0.45f);
    const auto inverted = render::DebugColor::fromLinear(1.0f, 0.2f, 0.2f);
    const auto normalColour = render::DebugColor::fromLinear(0.95f, 0.85f, 0.3f);

    world.terrains().forEach([&](core::InstanceId id, const scene::TerrainComponent& terrain) {
        const double voxel = static_cast<double>(terrain.field.settings().voxelSize);
        const auto cell = [&](double world, double origin) {
            return static_cast<core::i32>(std::floor((world - origin) / voxel)) - HalfExtent;
        };
        const core::i32 x = cell(eye.x, terrain.origin.x);
        const core::i32 y = cell(eye.y, terrain.origin.y);
        const core::i32 z = cell(eye.z, terrain.origin.z);

        Cached* entry = nullptr;
        for (Cached& candidate : cache) {
            if (candidate.terrain == id)
                entry = &candidate;
        }
        if (entry == nullptr) {
            cache.push_back(Cached{});
            entry = &cache.back();
            entry->terrain = id;
        }
        if (entry->revision != terrain.fieldRevision || entry->x != x || entry->y != y || entry->z != z) {
            constexpr auto Cells = static_cast<core::u32>(2 * HalfExtent);
            entry->mesh = asset::meshField(
                terrain.field,
                asset::MeshRegion{
                    .minX = x, .minY = y, .minZ = z, .cellsX = Cells, .cellsY = Cells, .cellsZ = Cells, .level = 0});
            entry->revision = terrain.fieldRevision;
            entry->x = x;
            entry->y = y;
            entry->z = z;
        }

        // The field's metres, placed where the terrain stands.
        const auto place = [&](const core::Vec3& p) {
            return core::toVec3(core::DVec3{terrain.origin.x + static_cast<double>(p.x),
                                            terrain.origin.y + static_cast<double>(p.y),
                                            terrain.origin.z + static_cast<double>(p.z)});
        };
        const std::vector<asset::Vertex>& vertices = entry->mesh.mesh.vertices;
        const std::vector<core::u32>& indices = entry->mesh.mesh.indices;
        if (wireframe) {
            for (std::size_t at = 0; at + 2 < indices.size(); at += 3) {
                const asset::Vertex& a = vertices[indices[at]];
                const asset::Vertex& b = vertices[indices[at + 1]];
                const asset::Vertex& c = vertices[indices[at + 2]];
                const core::Vec3 face = core::cross(b.position - a.position, c.position - a.position);
                const core::Vec3 shading = a.normal + b.normal + c.normal;
                const render::DebugColor colour = core::dot(face, shading) >= 0.0f ? agree : inverted;
                const core::Vec3 pa = place(a.position);
                const core::Vec3 pb = place(b.position);
                const core::Vec3 pc = place(c.position);
                draw.line(pa, pb, colour);
                draw.line(pb, pc, colour);
                draw.line(pc, pa, colour);
            }
        }
        if (normals) {
            const float length = NormalLength * static_cast<float>(voxel);
            for (const asset::Vertex& vertex : vertices) {
                const core::Vec3 from = place(vertex.position);
                draw.line(from, from + vertex.normal * length, normalColour);
            }
        }
    });
}

} // namespace luaug::app
