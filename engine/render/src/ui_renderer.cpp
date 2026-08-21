#include "luaug/render/ui_renderer.h"

#include "luaug/core/text_key.h"

#include <array>
#include <cstddef>

namespace luaug::render {
namespace {

constexpr core::u32 kVertexStride = sizeof(UiVertex);

// A first allocation big enough for a HUD of a hundred elements, so the common
// case never reallocates. Six vertices a quad -- two triangles, no index buffer:
// an index buffer would save a third of the bandwidth on geometry that is
// already the smallest thing in the frame, and cost a second upload.
constexpr core::u32 kInitialVertices = 4096;

} // namespace

std::optional<core::EngineError> UiRenderer::create(rhi::IDevice& device, const ShaderLibrary& shaders,
                                                    rhi::TextureFormat colorFormat)
{
    core::EngineError error;

    vertexShader_ = shaders.create(device, "ui2d", rhi::ShaderStage::Vertex, &error);
    if (!vertexShader_.valid())
        return error;

    fragmentShader_ = shaders.create(device, "ui2d", rhi::ShaderStage::Fragment, &error);
    if (!fragmentShader_.valid())
        return error;

    const std::array<rhi::VertexBufferLayout, 1> buffers{
        rhi::VertexBufferLayout{.slot = 0, .strideBytes = kVertexStride}};

    const std::array<rhi::VertexAttribute, 4> attributes{
        rhi::VertexAttribute{
            .location = 0,
            .bufferSlot = 0,
            .format = rhi::VertexFormat::Float2,
            .offsetBytes = offsetof(UiVertex, x),
        },
        rhi::VertexAttribute{
            .location = 1,
            .bufferSlot = 0,
            .format = rhi::VertexFormat::Ubyte4Unorm,
            .offsetBytes = offsetof(UiVertex, r),
        },
        // The rounded-corner frame (D030): the vertex's offset from the quad's
        // centre and the quad's half-extent, packed as one `Float4` because two
        // pairs that always travel together are one attribute.
        rhi::VertexAttribute{
            .location = 2,
            .bufferSlot = 0,
            .format = rhi::VertexFormat::Float4,
            .offsetBytes = offsetof(UiVertex, localX),
        },
        rhi::VertexAttribute{
            .location = 3,
            .bufferSlot = 0,
            .format = rhi::VertexFormat::Float1,
            .offsetBytes = offsetof(UiVertex, radius),
        },
    };

    const std::array<rhi::ColorTargetDesc, 1> targets{rhi::ColorTargetDesc{
        .format = colorFormat,
        // Alpha blending, because `BackgroundTransparency` is a real property
        // and a HUD is mostly translucent panels. The UI is drawn OVER the
        // world, never into it.
        .blend = {.enabled = true},
    }};

    pipeline_ = device.createGraphicsPipeline({
        .vertexShader = vertexShader_,
        .fragmentShader = fragmentShader_,
        .vertexBuffers = buffers,
        .vertexAttributes = attributes,
        .primitive = rhi::PrimitiveType::TriangleList,
        // No culling: quads are emitted in one winding and a UI has no back
        // faces, so a cull mode would be a rule with nothing to enforce and one
        // more thing to get backwards.
        .rasterizer = {.cullMode = rhi::CullMode::None},
        .colorTargets = targets,
        .debugName = "ui2d",
    });

    if (!pipeline_.valid())
        return core::makeError(LUAUG_TR("render.err.ui_pipeline_failed"));

    return std::nullopt;
}

void UiRenderer::destroy(rhi::IDevice& device)
{
    if (vertices_.valid())
        device.destroy(vertices_);
    if (pipeline_.valid())
        device.destroy(pipeline_);
    if (fragmentShader_.valid())
        device.destroy(fragmentShader_);
    if (vertexShader_.valid())
        device.destroy(vertexShader_);

    vertexShader_ = {};
    fragmentShader_ = {};
    pipeline_ = {};
    vertices_ = {};
    capacityVertices_ = 0;
    pendingVertices_ = 0;
    runs_.clear();
}

void UiRenderer::upload(rhi::IDevice& device, rhi::ICmdList& cmd, std::span<const UiVertex> vertices,
                        std::span<const UiScissorRun> runs)
{
    pendingVertices_ = static_cast<core::u32>(vertices.size());
    runs_.assign(runs.begin(), runs.end());
    if (pendingVertices_ == 0)
        return;

    if (pendingVertices_ > capacityVertices_) {
        core::u32 capacity = capacityVertices_ > 0 ? capacityVertices_ : kInitialVertices;
        while (capacity < pendingVertices_)
            capacity *= 2;

        if (vertices_.valid())
            device.destroy(vertices_);

        vertices_ = device.createBuffer({
            .usage = rhi::BufferUsage::Vertex,
            .sizeBytes = capacity * kVertexStride,
            .debugName = "ui2d-vertices",
        });

        if (!vertices_.valid()) {
            capacityVertices_ = 0;
            pendingVertices_ = 0;
            runs_.clear();
            return;
        }
        capacityVertices_ = capacity;
    }

    cmd.upload(vertices_, std::as_bytes(vertices), 0);
}

void UiRenderer::render(rhi::ICmdList& cmd, core::Vec2 viewport)
{
    if (pendingVertices_ == 0 || !pipeline_.valid() || !vertices_.valid())
        return;
    if (viewport.x <= 0.0f || viewport.y <= 0.0f)
        return;

    cmd.pushDebugGroup("ui2d");
    cmd.setPipeline(pipeline_);

    // Pixels to clip space. The negative y is the whole of the UI's y-down
    // convention meeting the API's y-up one, and it happens here rather than in
    // the layout so that `AbsolutePosition` means what a script expects.
    const std::array<core::f32, 4> screenToClip{2.0f / viewport.x, -2.0f / viewport.y, -1.0f, 1.0f};
    cmd.bindUniforms(rhi::ShaderStage::Vertex, 0, std::as_bytes(std::span{screenToClip}));

    const std::array<rhi::BufferHandle, 1> buffers{vertices_};
    cmd.bindVertexBuffers(0, buffers);

    for (const UiScissorRun& run : runs_) {
        if (run.vertexCount == 0)
            continue;
        cmd.setScissor(run.scissor);
        cmd.draw(run.vertexCount, 1, run.firstVertex, 0);
    }

    // Restored, so a later pass does not inherit a UI element's clip. A scissor
    // left in force is the kind of state leak that shows up as "half the screen
    // is missing" three passes later.
    cmd.setScissor(rhi::Rect{0, 0, static_cast<core::i32>(viewport.x), static_cast<core::i32>(viewport.y)});
    cmd.popDebugGroup();
}

} // namespace luaug::render
