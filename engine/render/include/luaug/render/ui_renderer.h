// Drawing what `ui` laid out (roadmap M6).
//
// One pipeline, one transient vertex buffer, and one draw call per scissor run
// -- the same shape `DebugRenderer` has, and for the same reason: a 2D pass has
// no materials, no lighting and no depth, so the only thing that can break a
// batch is a change of clip rectangle.
//
// **It takes a `ui::DrawList` rather than a world.** `render` is L4 and `ui` is
// L5, so this class cannot include `ui`'s header -- and it does not need to. The
// quad type below is `render`'s own, and `app` converts. That is one copy per
// frame over a few hundred quads, and it is what keeps the layering honest
// (architecture.md §2 rule 3).
#pragma once

#include "luaug/core/error.h"
#include "luaug/core/math.h"
#include "luaug/render/shader_library.h"
#include "luaug/rhi/device.h"

#include <optional>
#include <span>
#include <vector>

namespace luaug::render {

// Matches `shaders/src/ui2d.hlsl`: a float2 in window pixels and four
// normalised bytes. Twelve bytes, and the colour travels packed for the same
// reason `DebugVertex`'s does -- the hardware expands it for free.
struct UiVertex
{
    core::f32 x = 0.0f;
    core::f32 y = 0.0f;
    core::u8 r = 255;
    core::u8 g = 255;
    core::u8 b = 255;
    core::u8 a = 255;
    // The rounded-corner three (D030). `UICorner` was declared, stored, read
    // back and consumed by nothing at all for a whole milestone; these are what
    // consumes it.
    //
    // Per VERTEX rather than per draw, because there is one draw per scissor run
    // and a run is hundreds of quads with different radii. Twelve more bytes on
    // a UI vertex is nothing against a second pipeline or a draw per element,
    // and a radius of zero costs the fragment stage one `max` -- which is why
    // this is one shader rather than two.
    //
    // `localX`/`localY` are the vertex's offset from the quad's centre and
    // `halfX`/`halfY` its half-extent, so the fragment stage has a signed
    // distance without needing the quad's screen position.
    core::f32 localX = 0.0f;
    core::f32 localY = 0.0f;
    core::f32 halfX = 0.0f;
    core::f32 halfY = 0.0f;
    core::f32 radius = 0.0f;

    // Where this vertex samples the run's texture, normalised.
    //
    // On EVERY vertex rather than on a second pipeline, and for the reason the
    // corner radius is: a run is hundreds of quads and most of them sample
    // nothing, so a textured pipeline and an untextured one would be two
    // pipelines, two sorts and a state change per element. An untextured quad
    // carries the same eight bytes and samples a one-pixel white texture, which
    // multiplies by one.
    core::f32 u = 0.0f;
    core::f32 v = 0.0f;
};

static_assert(sizeof(UiVertex) == 40, "the ui2d vertex layout is an ABI decision the shader shares");

// One run of quads sharing a clip rectangle. The draw list is already ordered,
// so a run is a contiguous span rather than a bucket -- which is what makes
// this one `draw` call per clip change and not one per element.
struct UiScissorRun
{
    rhi::Rect scissor;
    core::u32 firstVertex = 0;
    core::u32 vertexCount = 0;

    // What this run samples. Invalid -- the common case -- binds the renderer's
    // own one-pixel white texture, so an untextured run costs one bind of a
    // texture that multiplies by one rather than a branch in the shader.
    //
    // A run breaks on a texture change as well as on a scissor change: a draw
    // binds one texture, so two textures is two draws whatever the clip does.
    rhi::TextureHandle texture{};
};

class UiRenderer
{
public:
    UiRenderer() = default;
    ~UiRenderer() = default;

    UiRenderer(const UiRenderer&) = delete;
    UiRenderer& operator=(const UiRenderer&) = delete;

    // Built against one colour target format, like every other pipeline: a
    // caller rendering into two formats needs two of these, which is honest.
    [[nodiscard]] std::optional<core::EngineError> create(rhi::IDevice& device, const ShaderLibrary& shaders,
                                                          rhi::TextureFormat colorFormat);

    void destroy(rhi::IDevice& device);

    // Uploads this frame's geometry. No render pass may be open -- the seam says
    // so and the SDL_GPU backend enforces it with a keyed error.
    void upload(rhi::IDevice& device, rhi::ICmdList& cmd, std::span<const UiVertex> vertices,
                std::span<const UiScissorRun> runs);

    // Records the draws. Call inside a render pass, after `upload`.
    //
    // `viewport` is the target's full size in pixels; the projection is derived
    // from it, and the scissor is restored to it afterwards so that a later pass
    // does not inherit a UI element's clip.
    void render(rhi::ICmdList& cmd, core::Vec2 viewport);

    [[nodiscard]] bool valid() const noexcept { return pipeline_.valid(); }

private:
    rhi::ShaderHandle vertexShader_{};
    rhi::ShaderHandle fragmentShader_{};
    rhi::PipelineHandle pipeline_{};
    rhi::BufferHandle vertices_{};

    // Grown, never shrunk, like the debug renderer's. A HUD's vertex count is
    // stable frame to frame and a menu opening doubles it once.
    // The one-pixel white every untextured run samples, and the sampler every
    // run uses. See `create` for why they exist at all.
    rhi::TextureHandle whitePixel_{};
    rhi::SamplerHandle sampler_{};
    bool whiteUploaded_ = false;

    core::u32 capacityVertices_ = 0;
    core::u32 pendingVertices_ = 0;
    std::vector<UiScissorRun> runs_;
};

} // namespace luaug::render
