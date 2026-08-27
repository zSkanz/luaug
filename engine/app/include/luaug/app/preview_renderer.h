#pragma once

// The render half of a content-browser preview (S5.16).
//
// **`ThumbnailCache` does every part of a preview that does not need a device**
// -- the request, the read off the frame, the parse off the frame, the budget,
// the keying, the cache -- and hands the rest through `IPreviewRenderer`. This
// is that implementation, and it lives in `app` rather than in `render` for the
// reason the seam's own comment gives: building a subtree needs a world, a world
// needs registries, and both belong to the host.
//
// **It draws through the ordinary renderer over a scratch world**, which is the
// whole design. The alternative -- assembling `RenderWorld::draws` by hand from
// an `asset::Model` -- would be a second copy of `render_world.cpp`'s extraction,
// and the two would disagree the first time either learned about a new material
// field. So a `Mesh` job becomes one `MeshPart` in a scratch world and a
// `Subtree` job becomes that text read into the same one, and both then go
// through `extract` and `IRenderer::render` exactly as a frame does.
//
// What that buys, beyond not duplicating anything: a preview is lit, shadowed
// and tonemapped by the same code as the viewport, so a model looks in the
// browser like it will look in the world.

#include "luaug/app/thumbnails.h"
#include "luaug/asset/content.h"
#include "luaug/render/mesh_cache.h"
#include "luaug/render/mesh_loader.h"
#include "luaug/render/render_world.h"
#include "luaug/render/renderer.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/enum_registry.h"
#include "luaug/scene/world.h"

#include <filesystem>
#include <memory>

namespace luaug::app {

class HostPreviewRenderer final : public IPreviewRenderer
{
public:
    // Borrows the registries and the renderer; owns everything else. The
    // registries are the host's because a scratch world built against a second
    // set could not be compared with anything -- a `ClassId` is an index into a
    // registry.
    HostPreviewRenderer(scene::ClassRegistry& classes, scene::EnumRegistry& enums, core::AtomTable& atoms,
                        std::filesystem::path contentRoot, const asset::ContentMounts& mounts,
                        render::IRenderer& renderer);
    ~HostPreviewRenderer() override;

    [[nodiscard]] bool drawPreview(rhi::IDevice& device, rhi::ICmdList& cmd, const PreviewJob& job,
                                   PreviewResult& out) override;

    // The GPU resources the scratch world accumulated. Called once, at shutdown,
    // by whoever owns this -- the meshes and textures a preview uploaded are not
    // the cache's, and the cache destroys only the picture it was handed.
    void destroy(rhi::IDevice& device);

private:
    // Empties the scratch world back to a bare Workspace, so one preview cannot
    // be drawn with the previous one still in it.
    void resetScratch();

    // Puts a ball wearing the material into an otherwise empty scratch world,
    // and leaves a subtree that has geometry alone. False only when the world
    // refused to build one, which is not recoverable.
    [[nodiscard]] bool swatchIfMaterial();

    scene::ClassRegistry& classes_;
    scene::EnumRegistry& enums_;
    core::AtomTable& atoms_;
    std::filesystem::path contentRoot_;
    render::IRenderer& renderer_;

    // A world of its own rather than the game's: a preview must not be able to
    // change what is on screen, and a scene read into the live world to draw a
    // thumbnail of it would do exactly that.
    std::unique_ptr<scene::World> scratch_;
    core::InstanceId workspace_;

    render::MeshCache meshes_;
    render::MeshLibrary library_;
    render::TextureLibrary textures_;
    render::MeshLoader loader_;

    scene::ClassId meshPartClass_ = scene::InvalidClass;
    scene::ClassId folderClass_ = scene::InvalidClass;
    core::NameAtom meshContentProperty_;
    scene::ClassId partClass_ = scene::InvalidClass;
    core::NameAtom shapeProperty_;
    core::NameAtom sizeProperty_;
    core::NameAtom materialProperty_;
    scene::EnumId partShapeEnum_ = scene::InvalidEnum;
};

} // namespace luaug::app
