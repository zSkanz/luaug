#include "luaug/render/render_world.h"

#include <algorithm>
#include <cmath>

#include "luaug/render/lighting.h"
#include "luaug/scene/components.h"
#include "luaug/scene/world.h"

namespace luaug::render
{
namespace
{

constexpr f32 kPi = 3.14159265358979323846f;
constexpr f32 kDegreesToRadians = kPi / 180.0f;

// Walks up rather than down. A downward walk from the root would visit every
// instance in the world to find the parts; this visits only the parts, and pays
// the depth of each. A world where that is the wrong trade is a world with deep
// trees and few parts, which is not the shape any of this is built for.
[[nodiscard]] bool inWorld(const scene::World& world, core::InstanceId id, core::InstanceId root) noexcept
{
    for (core::InstanceId cursor = id; cursor.valid(); cursor = world.parentOf(cursor))
    {
        if (cursor == root)
            return true;
    }
    return false;
}

// A light hangs off a `BasePart` or an `Attachment` and takes its position from
// it (api-design.md §2.2). A light with no such ancestor lights nothing, which
// is why this returns false rather than defaulting to the origin -- a lamp that
// silently moved to (0,0,0) is worse than a lamp that is off.
[[nodiscard]] bool lightAnchor(const scene::World& world, core::InstanceId id, CFrameD& out) noexcept
{
    for (core::InstanceId cursor = world.parentOf(id); cursor.valid(); cursor = world.parentOf(cursor))
    {
        if (const scene::PartComponent* part = world.parts().find(cursor); part != nullptr)
        {
            out = part->cframe;
            return true;
        }
    }
    return false;
}

} // namespace

u64 drawSortKey(u32 pass, u32 pipeline, u32 material, f32 depth) noexcept
{
    // 8 bits of pass, 16 of pipeline, 24 of material, 16 of depth. Most
    // significant first, so a single integer compare orders a frame by the thing
    // that costs most to change: the pass, then the pipeline, then the bind set.
    const u64 passBits = static_cast<u64>(pass & 0xFFu) << 56;
    const u64 pipelineBits = static_cast<u64>(pipeline & 0xFFFFu) << 40;
    const u64 materialBits = static_cast<u64>(material & 0xFFFFFFu) << 16;

    // Quantized deliberately. A sub-millimetre wobble in a camera position must
    // not be able to reorder two draws, because the capture golden hashes the
    // order -- and a gate that fails on the last bit of a float is a gate people
    // switch off. One unit is a centimetre out to 655 metres, and saturates
    // beyond, which is far past where ordering within a pass matters.
    const f32 clamped = depth < 0.0f ? 0.0f : (depth > 655.0f ? 655.0f : depth);
    const u64 depthBits = static_cast<u64>(clamped * 100.0f);

    return passBits | pipelineBits | materialBits | (depthBits & 0xFFFFu);
}

void MeshLibrary::set(core::NameAtom content, const Entry& entry)
{
    const auto position = std::lower_bound(entries_.begin(), entries_.end(), content,
        [](const Slot& slot, core::NameAtom value) { return slot.content.id < value.id; });
    if (position != entries_.end() && position->content == content)
    {
        position->entry = entry;
        return;
    }
    entries_.insert(position, Slot{content, entry});
}

void MeshLibrary::remove(core::NameAtom content)
{
    const auto position = std::lower_bound(entries_.begin(), entries_.end(), content,
        [](const Slot& slot, core::NameAtom value) { return slot.content.id < value.id; });
    if (position != entries_.end() && position->content == content)
        entries_.erase(position);
}

void MeshLibrary::clear() noexcept
{
    entries_.clear();
}

const MeshLibrary::Entry* MeshLibrary::find(core::NameAtom content) const noexcept
{
    const auto position = std::lower_bound(entries_.begin(), entries_.end(), content,
        [](const Slot& slot, core::NameAtom value) { return slot.content.id < value.id; });
    if (position == entries_.end() || !(position->content == content))
        return nullptr;
    return &position->entry;
}

void extract(
    const scene::World& world,
    core::InstanceId root,
    core::InstanceId lightingHost,
    const MeshLibrary& meshes,
    f32 viewportAspect,
    RenderWorld& out)
{
    out.clear();
    if (!root.valid())
        return;

    // --- The camera, and therefore the space everything else is expressed in --
    //
    // Resolved first because `origin` is the camera's position and every f32
    // coordinate below is relative to it.
    core::InstanceId cameraId;
    if (const scene::WorkspaceComponent* workspace = world.workspaces().find(root); workspace != nullptr)
        cameraId = workspace->currentCamera;

    const bool cameraUsable = world.alive(cameraId) && !world.destroyed(cameraId);
    const scene::CameraComponent* camera = cameraUsable ? world.cameras().find(cameraId) : nullptr;
    if (camera != nullptr)
    {
        out.camera.valid = true;
        out.camera.origin = camera->cframe.position;
        out.camera.nearPlane = camera->nearPlane;
        out.camera.farPlane = camera->farPlane;

        // Rotation only. The camera's position is `origin` and has already been
        // subtracted out of everything else, so leaving it in the view matrix
        // would apply it twice -- which looks correct near the world origin and
        // is catastrophically wrong a kilometre out, i.e. exactly where ADR 0014
        // says the f64 path has to hold.
        CFrameD orientation = camera->cframe;
        orientation.position = DVec3{};
        out.camera.view = core::toRenderMatrix(core::inverse(orientation), DVec3{});
        const f32 aspect = viewportAspect > 0.0f ? viewportAspect : 1.0f;
        out.camera.projection =
            core::perspective(camera->fieldOfView * kDegreesToRadians, aspect, camera->nearPlane, camera->farPlane);
        out.camera.viewProjection = out.camera.projection * out.camera.view;
        out.camera.frustum = core::frustumFromViewProjection(out.camera.viewProjection);
    }

    const DVec3 origin = out.camera.origin;

    // --- The environment ----------------------------------------------------

    if (const scene::LightingComponent* lighting = world.lighting().find(lightingHost); lighting != nullptr)
    {
        out.environment.sunDirection = sunDirection(lighting->clockTime, lighting->geographicLatitude);
        out.environment.ambient = lighting->ambient;
        out.environment.sunBrightness = lighting->brightness;
        out.environment.fogColor = lighting->fogColor;
        out.environment.fogStart = lighting->fogStart;
        out.environment.fogEnd = lighting->fogEnd;
    }

    // --- Debug parts --------------------------------------------------------
    //
    // Still here, and not culled: the debug path is how anything is seen when
    // the real one is not working, so a bug in the culler must not be able to
    // hide it.
    world.parts().forEach(
        [&](core::InstanceId id, const scene::PartComponent& part)
        {
            if (!inWorld(world, id, root))
                return;
            out.parts.push_back(RenderPart{
                .cframe = part.cframe,
                .size = part.size,
                .color = part.color,
                .transparency = part.transparency,
                .shape = part.shape,
            });
        });

    // --- Lights -------------------------------------------------------------

    world.pointLights().forEach(
        [&](core::InstanceId id, const scene::PointLightComponent& light)
        {
            if (!inWorld(world, id, root))
                return;
            CFrameD anchor;
            if (!lightAnchor(world, id, anchor))
                return;
            out.lights.push_back(RenderLight{
                .kind = LightKind::Point,
                .position = core::toVec3(anchor.position - origin),
                .direction = Vec3{0.0f, -1.0f, 0.0f},
                .color = light.color,
                .brightness = light.brightness,
                .range = light.range,
                .spotCosHalfAngle = 1.0f,
                .shadows = light.shadows,
            });
        });

    world.spotLights().forEach(
        [&](core::InstanceId id, const scene::SpotLightComponent& light)
        {
            if (!inWorld(world, id, root))
                return;
            CFrameD anchor;
            if (!lightAnchor(world, id, anchor))
                return;
            // A spot points along its anchor's LookVector, which is -Z (ADR
            // 0013's convention, stated in core/math.h).
            const Vec3 forward = core::transformDirection(anchor, Vec3{0.0f, 0.0f, -1.0f});
            out.lights.push_back(RenderLight{
                .kind = LightKind::Spot,
                .position = core::toVec3(anchor.position - origin),
                .direction = core::normalize(forward),
                .color = light.color,
                .brightness = light.brightness,
                .range = light.range,
                .spotCosHalfAngle = std::cos(light.angle * 0.5f * kDegreesToRadians),
                .shadows = light.shadows,
            });
        });

    // --- Draws --------------------------------------------------------------

    if (!out.camera.valid)
        return;

    // Frame-local material dedup. Declared here rather than inside the lambda
    // because it spans every MeshPart.
    struct ResolvedMaterial
    {
        core::NameAtom content;
        u32 local = 0;
        u32 slot = 0;
    };
    std::vector<ResolvedMaterial> resolved;

    world.meshParts().forEach(
        [&](core::InstanceId id, const scene::MeshPartComponent& meshPart)
        {
            if (!inWorld(world, id, root))
                return;
            const scene::PartComponent* part = world.parts().find(id);
            if (part == nullptr)
                return;

            const MeshLibrary::Entry* entry = meshes.find(meshPart.meshContent);
            // Skipped rather than substituted. A missing mesh that draws a
            // placeholder cube is a missing mesh nobody notices.
            if (entry == nullptr || !entry->mesh.valid())
                return;

            const Mat4 transform = core::toRenderMatrix(part->cframe, origin);
            const AABB worldBounds = core::transformed(transform, entry->bounds);

            for (u32 section = 0; section < entry->sectionCount; ++section)
            {
                // Resolved before the cull test so that `material` is meaningful
                // on every candidate, and deduplicated across the frame by
                // (content, local index) so the sort key can group draws that
                // share a bind set. A linear scan, because a scene has a handful
                // of materials and an unordered container's iteration order must
                // not reach observable output (R10).
                u32 localMaterial = 0;
                if (section < entry->sectionMaterial.size())
                    localMaterial = entry->sectionMaterial[section];

                ++out.candidateDraws;
                // Culled against the whole mesh's bounds rather than the
                // section's: the section bounds are in the library and this is
                // the loop that would have to fetch them per section. Whole-mesh
                // is conservative in the direction that never drops geometry,
                // and a per-section test is the optimization to make when a
                // profile says the draws it saves are worth the fetch.
                if (!core::intersects(out.camera.frustum, worldBounds))
                {
                    ++out.culledDraws;
                    continue;
                }

                u32 materialSlot = 0;
                bool found = false;
                for (u32 index = 0; index < static_cast<u32>(resolved.size()); ++index)
                {
                    if (resolved[index].content == meshPart.meshContent && resolved[index].local == localMaterial)
                    {
                        materialSlot = resolved[index].slot;
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    materialSlot = static_cast<u32>(out.materials.size());
                    out.materials.push_back(localMaterial < entry->materials.size()
                            ? entry->materials[localMaterial]
                            // A section whose material the importer did not
                            // produce still draws, in the default: a mesh that
                            // vanishes because one primitive lacked a material
                            // is harder to diagnose than a white one.
                            : RenderMaterial{});
                    resolved.push_back(ResolvedMaterial{meshPart.meshContent, localMaterial, materialSlot});
                }

                const Vec3 centre = core::center(worldBounds);
                const f32 depth = core::length(centre);
                out.draws.push_back(DrawItem{
                    .sortKey = drawSortKey(0, 0, materialSlot, depth),
                    .transform = transform,
                    .mesh = entry->mesh,
                    .section = section,
                    .material = materialSlot,
                });
            }
        });

    // `stable_sort`, and the stability is the contract: two draws with equal
    // keys keep their extraction order, which is the pool's dense order and
    // therefore a pure function of the operation sequence. `std::sort` is a
    // quicksort and would order them by whatever the partition happened to do
    // (R10) -- the same trap the api-dump generator hit on the same day.
    std::stable_sort(out.draws.begin(), out.draws.end(),
        [](const DrawItem& a, const DrawItem& b) { return a.sortKey < b.sortKey; });
}

} // namespace luaug::render
