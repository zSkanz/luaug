#include "luaug/render/render_world.h"

#include "luaug/render/lighting.h"
#include "luaug/render/shader_types.h"
#include "luaug/scene/components.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace luaug::render {
namespace {

constexpr f32 kPi = 3.14159265358979323846f;
constexpr f32 kDegreesToRadians = kPi / 180.0f;

// Walks up rather than down. A downward walk from the root would visit every
// instance in the world to find the parts; this visits only the parts, and pays
// the depth of each. A world where that is the wrong trade is a world with deep
// trees and few parts, which is not the shape any of this is built for.
[[nodiscard]] bool inWorld(const scene::World& world, core::InstanceId id, core::InstanceId root) noexcept
{
    for (core::InstanceId cursor = id; cursor.valid(); cursor = world.parentOf(cursor)) {
        if (cursor == root)
            return true;
    }
    return false;
}

// A light hangs off a `BasePart` or an `Attachment` and takes its position from
// it (api-design.md §2.2). A light with no such ancestor lights nothing, which
// is why this returns false rather than defaulting to the origin -- a lamp that
// silently moved to (0,0,0) is worse than a lamp that is off.
[[nodiscard]] bool lightAnchor(const scene::World& world, core::InstanceId id, CFrameD& out,
                               core::InstanceId& anchorId) noexcept
{
    for (core::InstanceId cursor = world.parentOf(id); cursor.valid(); cursor = world.parentOf(cursor)) {
        if (const scene::PartComponent* part = world.parts().find(cursor); part != nullptr) {
            out = part->cframe;
            anchorId = cursor;
            return true;
        }
    }
    return false;
}

// The generated mesh for a `Part`'s shape, or null before the loader has
// uploaded them -- which is the first frame of any run, and is why the debug
// wire box is still reachable.
[[nodiscard]] const MeshLibrary::Entry* primitiveEntry(const scene::World& world, const MeshLibrary& meshes,
                                                       core::i32 shape) noexcept
{
    const char* name = primitiveContent(shape);
    if (name == nullptr)
        return nullptr;
    const MeshLibrary::Entry* entry = meshes.find(world.atoms().lookup(name));
    return entry != nullptr && entry->mesh.valid() ? entry : nullptr;
}

// What one unit mesh has to be scaled by to become this part.
//
// **Not simply `Size`**, and the two exceptions are both about agreeing with the
// collider rather than with the size box:
//
//   `Ball` -- the physics shape is a SPHERE of the largest half-extent
//   (jolt_physics.cpp), so a non-uniform `Size` gives a ball that sticks out of
//   its own box. Rendering an ellipsoid there would mean seeing one thing and
//   colliding with another, which is the worse of the two wrongs.
//
//   `Capsule` -- the unit mesh is built at the character aspect (radius 0.5,
//   total height 2), so Y is halved. Its caps still stretch away from
//   `Size.y == 2 * max(Size.x, Size.z)`; primitives.h records why and what the
//   fix would be.
[[nodiscard]] Vec3 primitiveScale(core::i32 shape, Vec3 size) noexcept
{
    switch (shape) {
    case 1: {
        const f32 diameter = std::fmax(size.x, std::fmax(size.y, size.z));
        return Vec3{diameter, diameter, diameter};
    }
    case 2: {
        const f32 diameter = std::fmax(size.x, size.z);
        return Vec3{diameter, size.y, diameter};
    }
    case 3: {
        const f32 diameter = std::fmax(size.x, size.z);
        return Vec3{diameter, size.y * 0.5f, diameter};
    }
    default:
        return size;
    }
}

} // namespace

u64 drawSortKey(u32 pass, u32 pipeline, u32 material, u32 geometry, f32 depth) noexcept
{
    // 8 bits of pass, 8 of pipeline, 16 of material, 16 of mesh, 16 of depth.
    // Most significant first, so a single integer compare orders a frame by the
    // thing that costs most to change: the pass, then the pipeline, then the
    // bind set, then the vertex buffers.
    //
    // The geometry field -- mesh and section together -- is what makes an
    // instanced run contiguous (ADR 0043). Its cost is that the opaque pass no
    // longer walks strictly front-to-back within one material, which was worth
    // something for early-Z and is now worth nothing, because the depth prepass
    // provides it exactly.
    const u64 passBits = static_cast<u64>(pass & 0xFFu) << 56;
    const u64 pipelineBits = static_cast<u64>(pipeline & 0xFFu) << 48;
    const u64 materialBits = static_cast<u64>(material & 0xFFFFu) << 32;
    const u64 geometryBits = static_cast<u64>(geometry & 0xFFFFu) << 16;

    // Quantized deliberately. A sub-millimetre wobble in a camera position must
    // not be able to reorder two draws, because the capture golden hashes the
    // order -- and a gate that fails on the last bit of a float is a gate people
    // switch off. One unit is a centimetre out to 655 metres, and saturates
    // beyond, which is far past where ordering within a pass matters.
    const f32 clamped = depth < 0.0f ? 0.0f : (depth > 655.0f ? 655.0f : depth);
    const u64 depthBits = static_cast<u64>(clamped * 100.0f);

    return passBits | pipelineBits | materialBits | geometryBits | (depthBits & 0xFFFFu);
}

const char* primitiveContent(core::i32 shape) noexcept
{
    // Indexed by `Enum.PartShape`'s own values, which enums.api.luau's header
    // makes the contract.
    switch (shape) {
    case 0:
        return "luaug://primitive/block";
    case 1:
        return "luaug://primitive/ball";
    case 2:
        return "luaug://primitive/cylinder";
    case 3:
        return "luaug://primitive/capsule";
    case 4:
        return "luaug://primitive/wedge";
    default:
        return nullptr;
    }
}

void MeshLibrary::set(core::NameAtom content, const Entry& entry)
{
    const auto position =
        std::lower_bound(entries_.begin(), entries_.end(), content,
                         [](const Slot& slot, core::NameAtom value) { return slot.content.id < value.id; });
    if (position != entries_.end() && position->content == content) {
        position->entry = entry;
        return;
    }
    entries_.insert(position, Slot{content, entry});
}

void MeshLibrary::remove(core::NameAtom content)
{
    const auto position =
        std::lower_bound(entries_.begin(), entries_.end(), content,
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
    const auto position =
        std::lower_bound(entries_.begin(), entries_.end(), content,
                         [](const Slot& slot, core::NameAtom value) { return slot.content.id < value.id; });
    if (position == entries_.end() || !(position->content == content))
        return nullptr;
    return &position->entry;
}

void extract(const scene::World& world, core::InstanceId root, core::InstanceId lightingHost, const MeshLibrary& meshes,
             f32 viewportAspect, f32 shadowRadius, const AnimationSystem* animation, f32 alpha,
             const TransformHistory* history, RenderWorld& out, const ViewOverride* view,
             std::span<const core::InstanceId> outlined)
{
    out.clear();
    if (!root.valid())
        return;

    // Linear, and it is the cheap answer rather than the lazy one: an editor
    // selection is a handful of instances, the common case is none at all, and
    // a hash lookup per draw would cost more on a list of ten thousand than
    // this costs on a list of four.
    const auto isOutlined = [outlined](core::InstanceId id) {
        for (const core::InstanceId selected : outlined) {
            if (selected == id)
                return true;
        }
        return false;
    };

    // Where a thing is at the fractional time this frame is being drawn at
    // (`transform_history.h`, D047). Every transform below goes through it, so
    // the whole frame -- camera, parts, meshes, the anchors lights hang off --
    // is evaluated at one time rather than at several.
    //
    // Anything with no previous transform is drawn where it is: something that
    // streamed in this tick has no earlier position to come from, and smearing
    // it in from a stale slot would be worse than the step it replaces.
    const auto at = [&](core::InstanceId id, const CFrameD& current) -> CFrameD {
        if (history == nullptr || alpha <= 0.0f)
            return current;
        const CFrameD* earlier = history->previous(id);
        if (earlier == nullptr)
            return current;

        // **Two early outs, and they are the difference between this costing
        // nothing and costing two and a half milliseconds.** `core::lerp` on a
        // CFrame slerps the rotation, which is a quaternion round trip with a
        // trig pair in it -- and an open world is thousands of parts that did
        // not move at all. Measured on `examples/10-open-world`: the median
        // frame at 1080p went from 3.5 ms to 6.1 ms with the slerp on every
        // part, and back with these two comparisons in front of it.
        if (earlier->rotation == current.rotation) {
            if (earlier->position == current.position)
                return current;
            // Moved without turning, which is most of what moves: a lift, a
            // sliding platform, anything driven along a path.
            const core::f64 t = static_cast<core::f64>(alpha);
            const DVec3 delta = current.position - earlier->position;
            CFrameD moved = current;
            moved.position = DVec3{earlier->position.x + delta.x * t, earlier->position.y + delta.y * t,
                                   earlier->position.z + delta.z * t};
            return moved;
        }

        return core::lerp(*earlier, current, static_cast<core::f64>(alpha));
    };

    // --- The camera, and therefore the space everything else is expressed in --
    //
    // Resolved first because `origin` is the camera's position and every f32
    // coordinate below is relative to it.
    core::InstanceId cameraId;
    if (const scene::WorkspaceComponent* workspace = world.workspaces().find(root); workspace != nullptr)
        cameraId = workspace->currentCamera;

    const bool cameraUsable = world.alive(cameraId) && !world.destroyed(cameraId);
    const scene::CameraComponent* worldCamera = cameraUsable ? world.cameras().find(cameraId) : nullptr;

    // An override is a camera the WORLD does not contain, so it is not
    // interpolated and not looked up: there is no previous tick for a transform
    // no tick ever wrote. That is correct rather than a shortcut -- an editor's
    // camera moves on the render clock, so a frame drawn at `t + alpha` from it
    // is already the camera's position at that instant.
    const scene::CameraComponent overrideCamera =
        view != nullptr ? scene::CameraComponent{view->cframe, view->fieldOfView, view->nearPlane, view->farPlane}
                        : scene::CameraComponent{};
    const scene::CameraComponent* camera = view != nullptr ? &overrideCamera : worldCamera;

    if (camera != nullptr) {
        const CFrameD cameraFrame = view != nullptr ? camera->cframe : at(cameraId, camera->cframe);
        out.camera.valid = true;
        out.camera.origin = cameraFrame.position;
        out.camera.nearPlane = camera->nearPlane;
        out.camera.farPlane = camera->farPlane;

        // Rotation only. The camera's position is `origin` and has already been
        // subtracted out of everything else, so leaving it in the view matrix
        // would apply it twice -- which looks correct near the world origin and
        // is catastrophically wrong a kilometre out, i.e. exactly where ADR 0014
        // says the f64 path has to hold.
        CFrameD orientation = cameraFrame;
        orientation.position = DVec3{};
        out.camera.view = core::toRenderMatrix(core::inverse(orientation), DVec3{});
        const f32 aspect = viewportAspect > 0.0f ? viewportAspect : 1.0f;
        out.camera.projection =
            core::perspective(camera->fieldOfView * kDegreesToRadians, aspect, camera->nearPlane, camera->farPlane);
        // The jitter, folded into the projection's translation row -- which is
        // where a sub-pixel offset belongs, because it must move the whole frustum
        // rather than the geometry inside it. Zero everywhere today, so this is a
        // pair of additions of zero and every golden is unchanged.
        out.camera.projection.m[2][0] += out.camera.jitter.x;
        out.camera.projection.m[2][1] += out.camera.jitter.y;
        out.camera.viewProjection = out.camera.projection * out.camera.view;
        out.camera.frustum = core::frustumFromViewProjection(out.camera.viewProjection);
    }

    const DVec3 origin = out.camera.origin;

    // --- The environment ----------------------------------------------------

    if (const scene::LightingComponent* lighting = world.lighting().find(lightingHost); lighting != nullptr) {
        out.environment.sunDirection = sunDirection(lighting->clockTime, lighting->geographicLatitude);
        out.environment.ambient = lighting->ambient;
        out.environment.sunBrightness = lighting->brightness;
        out.environment.fogColor = lighting->fogColor;
        out.environment.fogStart = lighting->fogStart;
        out.environment.fogEnd = lighting->fogEnd;
        out.environment.exposureCompensation = lighting->exposureCompensation;
    }

    // --- Debug parts --------------------------------------------------------
    //
    // Still here, and not culled: the debug path is how anything is seen when
    // the real one is not working, so a bug in the culler must not be able to
    // hide it.
    //
    // A `MeshPart` whose mesh HAS loaded is the exception, and it is the one
    // case where the wire box says something untrue. `BasePart.Size` does not
    // scale a mesh -- the file's own bounds do -- so the box drawn from it is a
    // unit cube at the mesh's origin, describing nothing about what is on
    // screen. When the mesh has not loaded it is the opposite: the only sign
    // the part exists at all, which is exactly what the debug path is for.
    world.parts().forEach([&](core::InstanceId id, const scene::PartComponent& part) {
        if (!inWorld(world, id, root))
            return;
        if (const scene::MeshPartComponent* mesh = world.meshParts().find(id); mesh != nullptr) {
            const MeshLibrary::Entry* loaded = meshes.find(mesh->meshContent);
            if (loaded != nullptr && loaded->mesh.valid())
                return;
        }
        // M6: a `Part` has a solid path now, so the wire box is what it falls
        // back to rather than what it is. The path stays because it is still how
        // anything is seen when the real one is not running -- and the guard is
        // the camera, because a world with no camera is exactly the case the
        // host draws with the debug path instead of with the renderer
        // (`engine.cpp`'s `useRenderer`). Without that guard `examples/00-clear`
        // went from three wire cubes to an empty screen.
        else if (out.camera.valid && primitiveEntry(world, meshes, part.shape) != nullptr) {
            return;
        }
        out.parts.push_back(RenderPart{
            .cframe = at(id, part.cframe),
            .size = part.size,
            .color = part.color,
            .transparency = part.transparency,
            .shape = part.shape,
        });
    });

    // --- Lights -------------------------------------------------------------

    world.pointLights().forEach([&](core::InstanceId id, const scene::PointLightComponent& light) {
        // **Before anything else, including the budget.** That is what makes
        // `Enabled` different from a brightness of zero: a disabled light does
        // not occupy a slot, so turning a room's lights off gives the rest of
        // the scene the slots back.
        if (!light.enabled)
            return;
        if (!inWorld(world, id, root))
            return;
        CFrameD anchor;
        core::InstanceId anchorId;
        if (!lightAnchor(world, id, anchor, anchorId))
            return;
        anchor = at(anchorId, anchor);
        out.lights.push_back(RenderLight{
            .kind = LightKind::Point,
            .position = core::toVec3(anchor.position - origin),
            .direction = Vec3{0.0f, -1.0f, 0.0f},
            .color = light.color,
            .brightness = light.brightness,
            .range = light.range,
            // -1 admits every direction; 1 would be the narrowest cone
            // expressible, which is the opposite of what a point light is.
            .spotCosHalfAngle = -1.0f,
            .shadows = light.shadows,
        });
    });

    world.spotLights().forEach([&](core::InstanceId id, const scene::SpotLightComponent& light) {
        if (!light.enabled)
            return;
        if (!inWorld(world, id, root))
            return;
        CFrameD anchor;
        core::InstanceId anchorId;
        if (!lightAnchor(world, id, anchor, anchorId))
            return;
        anchor = at(anchorId, anchor);
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

    world.meshParts().forEach([&](core::InstanceId id, const scene::MeshPartComponent& meshPart) {
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

        const Mat4 transform = core::toRenderMatrix(at(id, part->cframe), origin);
        const AABB worldBounds = core::transformed(transform, entry->bounds);

        // The palette, appended once per MESH rather than once per section: a
        // character with four submeshes is one skeleton, and uploading its pose
        // four times would be four times the bytes for one answer. Truncated at
        // `kMaxSkinJoints` rather than refused -- a rig past the budget draws
        // its first sixty-four joints posed and the rest in bind, which is
        // visibly wrong in a way that says what happened.
        u32 firstBone = 0;
        u32 boneCount = 0;
        if (animation != nullptr) {
            if (const Pose* pose = animation->pose(id); pose != nullptr && !pose->palette.empty()) {
                firstBone = static_cast<u32>(out.bones.size());
                boneCount = static_cast<u32>(std::min<usize>(pose->palette.size(), kMaxSkinJoints));
                out.bones.insert(out.bones.end(), pose->palette.begin(),
                                 pose->palette.begin() + static_cast<std::ptrdiff_t>(boneCount));
            }
        }

        for (u32 section = 0; section < entry->sectionCount; ++section) {
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
            const bool visible = core::intersects(out.camera.frustum, worldBounds);
            if (!visible) {
                ++out.culledDraws;
                // Kept anyway when it is close enough to cast into view. The
                // first version dropped it, which deleted the shadow of
                // everything behind the camera -- an image that looks right
                // until you notice what is missing from it.
                const Vec3 toCentre = core::center(worldBounds);
                const f32 reach = shadowRadius + 0.5f * core::length(core::size(worldBounds));
                if (core::length(toCentre) > reach)
                    continue;
            }

            u32 materialSlot = 0;
            bool found = false;
            for (u32 index = 0; index < static_cast<u32>(resolved.size()); ++index) {
                if (resolved[index].content == meshPart.meshContent && resolved[index].local == localMaterial) {
                    materialSlot = resolved[index].slot;
                    found = true;
                    break;
                }
            }
            if (!found) {
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

            // The two sources of see-through, multiplied: the part's own
            // `Transparency` and whatever alpha the material arrived with.
            // The shader computes the same product, and it has to -- this is
            // what the draw was sorted by.
            const f32 opacity = (1.0f - part->transparency) * out.materials[materialSlot].uniforms.baseColor[3];
            // Fully invisible draws nothing at all, in either pass. That is
            // the debug path's existing rule (`submitWorld` skips a part at
            // `transparency >= 1`), and consistency with it matters more
            // here than the shadow question the roadmap left closed: a
            // shadow cast by something nobody can see is a defect whoever
            // sees it will report.
            if (opacity <= 0.0f)
                continue;

            const bool transparent = opacity < 1.0f;
            const Vec3 centre = core::center(worldBounds);
            const f32 depth = core::length(centre);
            // Back-to-front for the blended pass, and the inversion happens
            // HERE rather than as a reversed walk in a backend -- that is
            // M4's third design constraint, and a reversed walk is work
            // every future backend would repeat.
            const f32 sortDepth = transparent ? kMaxSortDepth - depth : depth;
            out.draws.push_back(DrawItem{
                // Zero for a transparent draw: see `drawSortKey`.
                .sortKey = drawSortKey(transparent ? kTransparentPass : kOpaquePass,
                                       boneCount > 0 ? kSkinnedPipeline : kStaticPipeline, materialSlot,
                                       transparent ? 0u : drawGeometryKey(entry->mesh.index, section), sortDepth),
                .transform = transform,
                .mesh = entry->mesh,
                .section = section,
                .material = materialSlot,
                .alpha = opacity,
                .transparent = transparent,
                .boundsCenter = core::center(worldBounds),
                .boundsRadius = 0.5f * core::length(core::size(worldBounds)),
                .inCameraFrustum = visible,
                .firstBone = firstBone,
                .boneCount = boneCount,
                .outlined = isOutlined(id),
            });
        }
    });

    // --- Solid parts (M6) ---------------------------------------------------
    //
    // **The renderer changes not at all for this**, which is the answer M4's
    // "engine-generated geometry must be able to reach the renderer" constraint
    // was written to get: these are ordinary `DrawItem`s naming ordinary
    // `MeshHandle`s, and colour, `Transparency` and the blended pass come free
    // because of it.
    //
    // The material is the part's own colour rather than a file's, deduplicated
    // across the frame by that colour so the sort key still groups draws that
    // share a bind set. A linear scan for the same reason the mesh loop uses
    // one: a scene has a handful of distinct colours, and an unordered
    // container's iteration order must not reach observable output (R10).
    struct ResolvedPartMaterial
    {
        Color3 color;
        u32 slot;
    };
    std::vector<ResolvedPartMaterial> partMaterials;

    world.parts().forEach([&](core::InstanceId id, const scene::PartComponent& part) {
        if (!inWorld(world, id, root))
            return;
        // A `MeshPart` is a `BasePart` and is in this pool too; its geometry
        // came from a file and the loop above already drew it.
        if (world.meshParts().find(id) != nullptr)
            return;

        const MeshLibrary::Entry* entry = primitiveEntry(world, meshes, part.shape);
        if (entry == nullptr)
            return;

        const f32 opacity = 1.0f - part.transparency;
        if (opacity <= 0.0f)
            return;

        const Mat4 transform =
            core::toRenderMatrix(at(id, part.cframe), origin) * core::scaling(primitiveScale(part.shape, part.size));
        const AABB worldBounds = core::transformed(transform, entry->bounds);

        ++out.candidateDraws;
        const bool visible = core::intersects(out.camera.frustum, worldBounds);
        if (!visible) {
            ++out.culledDraws;
            const Vec3 toCentre = core::center(worldBounds);
            const f32 reach = shadowRadius + 0.5f * core::length(core::size(worldBounds));
            if (core::length(toCentre) > reach)
                return;
        }

        u32 materialSlot = 0;
        bool found = false;
        for (const ResolvedPartMaterial& candidate : partMaterials) {
            if (candidate.color == part.color) {
                materialSlot = candidate.slot;
                found = true;
                break;
            }
        }
        if (!found) {
            RenderMaterial material;
            material.uniforms.baseColor[0] = part.color.r;
            material.uniforms.baseColor[1] = part.color.g;
            material.uniforms.baseColor[2] = part.color.b;
            material.uniforms.baseColor[3] = 1.0f;
            // Dielectric and fairly rough, which is what an untextured building
            // block looks like. `RenderMaterial`'s own defaults are metallic 1
            // and roughness 1, which is right for a glTF that forgot to say and
            // wrong for a `Part` that has no way to.
            material.uniforms.metallicRoughnessNormalCutoff[0] = 0.0f;
            material.uniforms.metallicRoughnessNormalCutoff[1] = 0.7f;
            materialSlot = static_cast<u32>(out.materials.size());
            out.materials.push_back(material);
            partMaterials.push_back(ResolvedPartMaterial{part.color, materialSlot});
        }

        const bool transparent = opacity < 1.0f;
        const f32 depth = core::length(core::center(worldBounds));
        const f32 sortDepth = transparent ? kMaxSortDepth - depth : depth;
        out.draws.push_back(DrawItem{
            .sortKey = drawSortKey(transparent ? kTransparentPass : kOpaquePass, kStaticPipeline, materialSlot,
                                   transparent ? 0u : drawGeometryKey(entry->mesh.index, 0), sortDepth),
            .transform = transform,
            .mesh = entry->mesh,
            .section = 0,
            .material = materialSlot,
            .alpha = opacity,
            .transparent = transparent,
            .boundsCenter = core::center(worldBounds),
            .boundsRadius = 0.5f * core::length(core::size(worldBounds)),
            .inCameraFrustum = visible,
            .outlined = isOutlined(id),
        });
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
