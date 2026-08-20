// Drawing what DebugDraw accumulated (roadmap M1).
//
// The whole of M1's rendering: one pipeline, one transient vertex buffer, one
// draw call, no lighting and no materials. M4 brings the real renderer; until
// then this is what every visual feature in M2, M3 and M5 is debugged through,
// which is why it exists this early.
#pragma once

#include "luaug/core/error.h"
#include "luaug/core/math.h"
#include "luaug/render/debug_draw.h"
#include "luaug/render/shader_library.h"
#include "luaug/rhi/device.h"

#include <optional>

namespace luaug::render {

class DebugRenderer
{
public:
    DebugRenderer() = default;
    ~DebugRenderer();

    DebugRenderer(const DebugRenderer&) = delete;
    DebugRenderer& operator=(const DebugRenderer&) = delete;

    // Builds the pipeline for a specific colour target format, because a
    // graphics pipeline is compiled against one. A caller that renders into two
    // formats needs two renderers, which is honest: they are two pipelines
    // whatever the API pretends.
    [[nodiscard]] std::optional<core::EngineError> create(rhi::IDevice& device, const ShaderLibrary& shaders,
                                                          rhi::TextureFormat colorFormat);

    void destroy(rhi::IDevice& device);

    // Uploads this frame's geometry, growing the vertex buffer when the frame
    // needs more than the last one did. Must be called with no render pass open
    // -- the seam says so, and the SDL_GPU backend enforces it with a keyed
    // error rather than a driver crash.
    //
    // Takes the device because growing is a device operation. The alternative,
    // a fixed capacity, would mean silently dropping geometry on the frame
    // someone most wants to look at.
    void upload(rhi::IDevice& device, rhi::ICmdList& cmd, const DebugDraw& draw);

    // Records the draw. Call inside a render pass, after upload().
    void render(rhi::ICmdList& cmd, const core::Mat4& viewProjection);

    [[nodiscard]] bool valid() const noexcept { return pipeline_.valid(); }

private:
    rhi::ShaderHandle vertexShader_{};
    rhi::ShaderHandle fragmentShader_{};
    rhi::PipelineHandle pipeline_{};
    rhi::BufferHandle vertices_{};

    // Grown, never shrunk. A debug frame's vertex count swings wildly between
    // frames, and reallocating a GPU buffer to match is far more expensive than
    // holding the high-water mark.
    core::u32 capacityVertices_ = 0;
    core::u32 pendingVertices_ = 0;
};

} // namespace luaug::render
