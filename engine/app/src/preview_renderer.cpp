#include "luaug/app/preview_renderer.h"

#include "luaug/core/log.h"
#include "luaug/scene/components.h"
#include "luaug/scene/pivot.h"
#include "luaug/scene/scene_file.h"

#include <algorithm>
#include <string>

namespace luaug::app {
namespace {

using core::f32;
using core::u32;

// A preview's own light, and the project's `Lighting` is deliberately not
// consulted. A preview that inherited the scene's sun would change when somebody
// dragged a slider in a panel three tabs away, which is the same defect as a
// preview that follows the camera: the picture of a file has to be a function of
// the file.
//
// One directional light from over the camera's shoulder and slightly to the
// other side, so the three-quarter view has a lit face, a shaded face and a
// visible terminator between them -- which is what makes a shape read as a
// shape.
void lightIt(render::RenderWorld& world)
{
    // **No point or spot lights at all.** A preview is lit by the environment's
    // sun, which is the same thing that lights a scene outdoors -- and adding
    // local lights would spend shadow-atlas tiles on a 128-pixel tile.
    world.lights.clear();

    // Over the camera's shoulder and slightly to the other side, so the
    // three-quarter view has a lit face, a shaded face and a visible terminator
    // between them, which is what makes a shape read as a shape.
    world.environment.sunDirection = core::normalize(core::Vec3{-0.35f, 0.62f, -0.70f});
    world.environment.sunBrightness = 2.8f;

    // A neutral ambient, and the project's `Lighting` is deliberately not
    // consulted: a preview that inherited the scene's sun would change when
    // somebody dragged a slider in a panel three tabs away, which is the same
    // defect as a preview that follows the camera. The picture of a file has to
    // be a function of the file.
    world.environment.ambient = core::Color3{0.20f, 0.21f, 0.24f};
    // No fog: a thumbnail is not standing anywhere.
    world.environment.fogEnd = 0.0f;
    world.environment.exposureCompensation = 0.0f;
}

// The text of an interned name, which `setProperty` wants and the atom table
// holds. A small function so the two spellings of "this URN" cannot drift.
[[nodiscard]] std::string urnText(const core::AtomTable& atoms, core::NameAtom urn)
{
    return std::string(atoms.text(urn));
}

} // namespace

HostPreviewRenderer::HostPreviewRenderer(scene::ClassRegistry& classes, scene::EnumRegistry& enums,
                                         core::AtomTable& atoms, std::filesystem::path contentRoot,
                                         const asset::ContentMounts& mounts, render::IRenderer& renderer)
    : classes_(classes), enums_(enums), atoms_(atoms), contentRoot_(std::move(contentRoot)), renderer_(renderer)
{
    // One seed, and it never matters: nothing in a preview is random and this
    // world is never hashed. Named rather than zero so it does not read as a
    // forgotten argument.
    scratch_ = std::make_unique<scene::World>(classes_, enums_, atoms_, 1u);

    meshPartClass_ = classes_.findId(atoms_.intern("MeshPart"));
    folderClass_ = classes_.findId(atoms_.intern("Folder"));
    meshContentProperty_ = atoms_.intern("MeshContent");
    partClass_ = classes_.findId(atoms_.intern("Part"));
    shapeProperty_ = atoms_.intern("Shape");
    sizeProperty_ = atoms_.intern("Size");
    materialProperty_ = atoms_.intern("Material");
    partShapeEnum_ = enums_.findId(atoms_.intern("PartShape"));

    loader_.setContentRoot(contentRoot_);
    loader_.setContentMounts(&mounts);
    // **Synchronous, and only the subtree path uses it.** A mesh arrives
    // already parsed -- see `drawPreview` -- so the only thing left to read on
    // the frame is whatever a scene or a stamp names, which the cache budgets at
    // `MaxPreviewsPerFrame`. The seam has no "busy" answer by design: false
    // means there will never be a picture, so a preview that could not be drawn
    // in one call would have to lie.
    loader_.setDeferredTextures(false);
    loader_.setDeferredMeshes(false);

    resetScratch();
}

HostPreviewRenderer::~HostPreviewRenderer() = default;

void HostPreviewRenderer::resetScratch()
{
    if (workspace_.valid() && scratch_->alive(workspace_))
        (void)scratch_->destroy(workspace_);

    workspace_ = scratch_->create(folderClass_);
    if (!workspace_.valid())
        return;
    scratch_->setName(workspace_, atoms_.intern("Workspace"));
    // The component rather than a class, exactly as the scene tests do: a scene
    // is rooted at a Workspace and this world has to look like one to `extract`.
    scratch_->workspaces().add(workspace_, scene::WorkspaceComponent{});
}

bool HostPreviewRenderer::drawPreview(rhi::IDevice& device, rhi::ICmdList& cmd, const PreviewJob& job,
                                      PreviewResult& out)
{
    if (job.edge == 0 || !renderer_.valid())
        return false;

    resetScratch();
    if (!workspace_.valid())
        return false;

    // --- Build the thing to look at ------------------------------------------
    if (job.kind == PreviewKind::Mesh) {
        if (job.model == nullptr || meshPartClass_ == scene::InvalidClass)
            return false;

        // **The model arrives parsed, and that is the seam's whole point.** A
        // 3 MB glTF parsed on the frame thread is D118 exactly -- the defect
        // that made the editor feel like it reloaded the world whenever
        // anybody touched anything -- so the cache does it on the job pool and
        // this uploads what it produced.
        //
        // Keyed by the path so two rows of the same file share one upload, and
        // so a `MeshPart` naming it finds it below.
        const core::NameAtom urn = atoms_.intern(job.path.generic_string());
        if (library_.find(urn) == nullptr) {
            if (!loader_.uploadModel(device, cmd, *job.model, urn, meshes_, library_))
                return false;
        }

        const core::InstanceId part = scratch_->create(meshPartClass_);
        if (!part.valid())
            return false;
        scratch_->setName(part, atoms_.intern("Preview"));
        if (scratch_->setParent(part, workspace_).has_value())
            return false;
        // The property rather than the component, so whatever else a `MeshPart`
        // does on a write happens here too.
        (void)scratch_->setProperty(part, meshContentProperty_, scene::Value{urnText(atoms_, urn)});
        if (scene::MeshPartComponent* mesh = scratch_->meshParts().find(part); mesh != nullptr)
            mesh->meshContent = urn;
    }
    else if (job.kind == PreviewKind::Subtree) {
        if (job.text.empty())
            return false;

        // A stamp is one subtree and a scene is a whole one. Tried in that
        // order rather than decided from the extension a second time -- the
        // browser and this cache already share `contentKindOf`, and a third
        // opinion is a third thing to keep in step.
        scene::SceneIoReport report;
        const core::InstanceId root = scene::readStamp(*scratch_, job.text, workspace_, "preview", &report);
        if (!root.valid()) {
            if (scene::readScene(*scratch_, job.text, &report).has_value())
                return false;
            // `readScene` builds its own Workspace; frame that one, since ours
            // is empty beside it.
            for (core::InstanceId child = scratch_->firstChild(core::InstanceId{}); child.valid();
                 child = scratch_->nextSibling(child)) {
                if (scratch_->workspaces().find(child) != nullptr) {
                    workspace_ = child;
                    break;
                }
            }
        }

        // **A material is shared as a stamp** (ADR 0060), so a `.stamp.json`
        // holding one arrives here like any other subtree -- and a lone
        // `Material` has no geometry, which would frame as an empty box and draw
        // a picture of nothing. A swatch is what somebody wants instead: the
        // material on a curved surface, because roughness, metalness and a
        // normal map are all about how light moves ACROSS a curvature and a
        // square of colour shows none of them. The stage's own material preview
        // makes the same argument in the same words.
        if (!swatchIfMaterial())
            return false;

        // A subtree names its meshes by URN, and those have to be read. This is
        // the one place a preview touches the disk on the frame, and it is
        // bounded by the cache's per-frame budget above.
        (void)loader_.sync(device, cmd, *scratch_, workspace_, meshes_, library_, nullptr, nullptr);
        (void)loader_.syncTextures(device, cmd, *scratch_, textures_);
    }
    else {
        return false;
    }

    // --- The view, which is a pure function of the bounds --------------------
    const core::AABB bounds = [&]() -> core::AABB {
        core::DVec3 min;
        core::DVec3 max;
        if (!scene::worldExtents(*scratch_, workspace_, min, max))
            return core::AABB{};
        return core::AABB{core::Vec3{static_cast<f32>(min.x), static_cast<f32>(min.y), static_cast<f32>(min.z)},
                          core::Vec3{static_cast<f32>(max.x), static_cast<f32>(max.y), static_cast<f32>(max.z)}};
    }();
    const render::ViewOverride view = previewView(bounds);

    render::RenderWorld snapshot;
    render::extract(*scratch_, workspace_, core::InstanceId{}, library_, 1.0f, renderer_.shadowRadius(), nullptr, 0.0f,
                    nullptr, snapshot, &view, {}, &textures_);
    lightIt(snapshot);

    // --- The target, which belongs to the CACHE from the moment this returns --
    const rhi::TextureHandle target = device.createTexture({
        .format = rhi::TextureFormat::Rgba8UnormSrgb,
        .usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::ColorTarget,
        .width = job.edge,
        .height = job.edge,
        .debugName = "thumbnail-preview",
    });
    if (!target.valid())
        return false;

    renderer_.render(
        device, cmd,
        {.color = target, .colorFormat = rhi::TextureFormat::Rgba8UnormSrgb, .width = job.edge, .height = job.edge},
        snapshot, meshes_);

    out.texture = target;
    out.width = job.edge;
    out.height = job.edge;
    return true;
}

bool HostPreviewRenderer::swatchIfMaterial()
{
    // Only when there is nothing to look at. A stamp of a lamp post that happens
    // to contain a material is a lamp post, and drawing a ball instead would be
    // this function deciding what a file is.
    core::DVec3 min;
    core::DVec3 max;
    if (scene::worldExtents(*scratch_, workspace_, min, max))
        return true;

    // The material to wear: the first one anywhere under the root, which for a
    // material stamp is its own root.
    core::InstanceId material;
    std::vector<core::InstanceId> subtree;
    subtree.push_back(workspace_);
    scratch_->collectDescendants(workspace_, subtree);
    for (const core::InstanceId id : subtree) {
        if (scratch_->materials().find(id) != nullptr) {
            material = id;
            break;
        }
    }
    if (!material.valid())
        return true; // Nothing to look at and no material either: a picture of nothing.

    if (partClass_ == scene::InvalidClass)
        return true;
    const core::InstanceId ball = scratch_->create(partClass_);
    if (!ball.valid())
        return false;
    scratch_->setName(ball, atoms_.intern("Swatch"));
    if (scratch_->setParent(ball, workspace_).has_value())
        return false;

    // A ball of a metre, at the origin. The size is arbitrary and the framing
    // does not care -- `previewView` fits whatever it is given -- but a round
    // number keeps the picture the same if somebody changes the view later.
    (void)scratch_->setProperty(ball, shapeProperty_, scene::Value{scene::EnumValue{partShapeEnum_, 1}});
    (void)scratch_->setProperty(ball, sizeProperty_, scene::Value{core::Vec3{1.0f, 1.0f, 1.0f}});
    (void)scratch_->setProperty(ball, materialProperty_, scene::Value{material});
    return true;
}

void HostPreviewRenderer::destroy(rhi::IDevice& device)
{
    loader_.destroy(device);
    meshes_.destroy(device);
    library_.clear();
    textures_.clear();
    resetScratch();
}

} // namespace luaug::app
