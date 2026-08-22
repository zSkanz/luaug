// The renderer contract (ADR 0027).
//
// A renderer consumes a `RenderWorld` and an `rhi::IDevice` and nothing else.
// That boundary is the whole decision: the renderer architecture -- forward,
// deferred, GPU-driven -- is a separate axis of replaceability from the graphics
// API, and this is the seam for it. An alternative renderer is a build-time
// selection like any backend (ADR 0023) and may not reach into `scene` or into
// `rhi` internals.
//
// ADR 0027's sketch is `render(IDevice&, const RenderWorld&)`. The signature
// here carries two more things the sketch predates: the command list, because
// `app` owns the frame and a renderer that opened its own could not share one
// with the debug overlay; and the target, because headless renders into an
// offscreen texture and windowed into a swapchain image, and which one is the
// host's business rather than the renderer's.
#pragma once

#include "luaug/core/error.h"
#include "luaug/render/mesh_cache.h"
#include "luaug/render/render_world.h"
#include "luaug/render/settings.h"
#include "luaug/render/shader_library.h"
#include "luaug/rhi/device.h"

#include <optional>

namespace luaug::render {

struct RenderTarget
{
    rhi::TextureHandle color{};
    rhi::TextureFormat colorFormat = rhi::TextureFormat::Undefined;
    core::u32 width = 0;
    core::u32 height = 0;
};

// The counters a frame leaves behind, for `DebugService` and the perf table.
struct RendererStats
{
    // Calls issued: `drawIndexed` and `draw`, across every pass of the frame.
    core::u32 drawCalls = 0;
    // How many of those were instanced, and how many objects they covered. A
    // frame where `instances` is far larger than `instancedDraws` is a frame the
    // instanced path did something in; a frame where they are equal is one where
    // it did not, which is the gate's own test.
    core::u32 instancedDraws = 0;
    core::u32 instances = 0;
};

class IRenderer
{
public:
    virtual ~IRenderer() = default;

    IRenderer(const IRenderer&) = delete;
    IRenderer& operator=(const IRenderer&) = delete;

    // Built against one colour format, because a graphics pipeline is. A caller
    // that renders into two formats needs two renderers, which is honest: they
    // are two sets of pipelines whatever the API pretends.
    [[nodiscard]] virtual std::optional<core::EngineError> create(rhi::IDevice& device, const ShaderLibrary& shaders,
                                                                  rhi::TextureFormat colorFormat) = 0;

    virtual void destroy(rhi::IDevice& device) = 0;

    // Records the frame. Must be called with no render pass open; the renderer
    // opens and closes its own, and leaves none open on return, because the
    // overlay draws after it into the same command list.
    virtual void render(rhi::IDevice& device, rhi::ICmdList& cmd, const RenderTarget& target, const RenderWorld& world,
                        const MeshCache& meshes) = 0;

    // False before `create` succeeds, and after a `create` that failed. A
    // renderer that never started must be skippable rather than fatal: a machine
    // whose content directory is missing its shaders should boot and say why.
    [[nodiscard]] virtual bool valid() const noexcept = 0;

    // The radius around the camera this renderer's shadow map covers, in
    // metres. `extract` needs it to decide which off-screen geometry is still
    // worth keeping as a caster, and the number belongs to the pass list rather
    // than to the host -- a renderer with cascades would answer differently.
    [[nodiscard]] virtual core::f32 shadowRadius() const noexcept = 0;

    // The quality family this renderer honours (roadmap M8, ADR 0044). Applying
    // one may rebuild targets, which is why it is a call rather than a field
    // read every frame: the shadow atlas is sized by it.
    //
    // Here rather than on `RenderWorld` because a settings value is not scene
    // state. `extract` produces what the world contains; this is what the
    // machine can afford, and mixing the two would put a stranger's GPU budget
    // into the scene that gets hashed.
    virtual void setSettings(const GraphicsSettings& settings) = 0;
    [[nodiscard]] virtual const GraphicsSettings& settings() const noexcept = 0;

    // What the last frame actually submitted.
    //
    // Counted by the renderer rather than derived from the snapshot, and that
    // distinction IS the number: with instancing, a run of objects that share a
    // mesh and a material is one call, so "how many objects are visible" and
    // "how many calls were issued" stopped being the same question at M7.5.
    // The roadmap's gate for that item is exactly the two of them side by side.
    [[nodiscard]] virtual RendererStats stats() const noexcept = 0;

protected:
    IRenderer() = default;
};

// The v1 renderer (ADR 0027): sun shadow map -> sky -> forward PBR -> tonemap.
// Single cascade, a bounded light count per draw, and no clustered culling --
// each of those is a named non-goal of M4 rather than an omission.
[[nodiscard]] std::unique_ptr<IRenderer> createDefaultRenderer();

} // namespace luaug::render
