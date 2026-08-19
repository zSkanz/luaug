#include "luaug/render/debug_renderer.h"

#include <array>
#include <cstring>

#include "luaug/core/text_key.h"

namespace luaug::render
{
namespace
{

// Matches `shaders/src/debug_line.hlsl`. The shader reads TEXCOORD0 as a float3
// position and TEXCOORD1 as a float4 colour; the colour travels as four
// normalised bytes and the hardware expands it, which is why DebugVertex is 16
// bytes rather than 28.
constexpr core::u32 kVertexStride = sizeof(DebugVertex);

static_assert(offsetof(DebugVertex, position) == 0, "the shader reads position at offset 0");
static_assert(offsetof(DebugVertex, color) == 12, "the shader reads colour immediately after position");

// A first allocation big enough for a few hundred boxes, so the common case
// never reallocates at all.
constexpr core::u32 kInitialVertices = 4096;

} // namespace

DebugRenderer::~DebugRenderer()
{
    // Deliberately not releasing here: these are device resources, and the
    // device is not a member. A destructor that silently leaked would be worse
    // than one that requires destroy() to be called, so this asserts nothing
    // and does nothing -- and destroy() is what engine.cpp calls.
}

std::optional<core::EngineError> DebugRenderer::create(
    rhi::IDevice& device, const ShaderLibrary& shaders, rhi::TextureFormat colorFormat)
{
    core::EngineError error;

    vertexShader_ = shaders.create(device, "debug_line", rhi::ShaderStage::Vertex, &error);
    if (!vertexShader_.valid())
        return error;

    fragmentShader_ = shaders.create(device, "debug_line", rhi::ShaderStage::Fragment, &error);
    if (!fragmentShader_.valid())
        return error;

    const std::array<rhi::VertexBufferLayout, 1> buffers{
        rhi::VertexBufferLayout{.slot = 0, .strideBytes = kVertexStride}};

    const std::array<rhi::VertexAttribute, 2> attributes{
        rhi::VertexAttribute{
            .location = 0,
            .bufferSlot = 0,
            .format = rhi::VertexFormat::Float3,
            .offsetBytes = offsetof(DebugVertex, position),
        },
        rhi::VertexAttribute{
            .location = 1,
            .bufferSlot = 0,
            .format = rhi::VertexFormat::Ubyte4Unorm,
            .offsetBytes = offsetof(DebugVertex, color),
        },
    };

    const std::array<rhi::ColorTargetDesc, 1> targets{rhi::ColorTargetDesc{
        .format = colorFormat,
        // Alpha blending, so a translucent debug shape reads as one. Debug
        // geometry is drawn over a scene, not into it.
        .blend = {.enabled = true},
    }};

    pipeline_ = device.createGraphicsPipeline({
        .vertexShader = vertexShader_,
        .fragmentShader = fragmentShader_,
        .vertexBuffers = buffers,
        .vertexAttributes = attributes,
        .primitive = rhi::PrimitiveType::LineList,
        // No culling: a line has no facing, and a wire box drawn from inside
        // must still be visible -- which is exactly when someone is debugging.
        .rasterizer = {.cullMode = rhi::CullMode::None},
        .colorTargets = targets,
        .debugName = "debug-line",
    });

    if (!pipeline_.valid())
        return core::makeError(LUAUG_TR("render.err.debug_pipeline_failed"));

    return std::nullopt;
}

void DebugRenderer::destroy(rhi::IDevice& device)
{
    if (vertices_.valid())
        device.destroy(vertices_);
    if (pipeline_.valid())
        device.destroy(pipeline_);
    if (fragmentShader_.valid())
        device.destroy(fragmentShader_);
    if (vertexShader_.valid())
        device.destroy(vertexShader_);

    // Field by field, because the type is non-copyable on purpose -- it owns
    // device resources, and a copy would double-free them.
    vertexShader_ = {};
    fragmentShader_ = {};
    pipeline_ = {};
    vertices_ = {};
    capacityVertices_ = 0;
    pendingVertices_ = 0;
}

void DebugRenderer::upload(rhi::IDevice& device, rhi::ICmdList& cmd, const DebugDraw& draw)
{
    const std::span<const DebugVertex> vertices = draw.vertices();
    pendingVertices_ = static_cast<core::u32>(vertices.size());
    if (pendingVertices_ == 0)
        return;

    if (pendingVertices_ > capacityVertices_)
    {
        // Doubling from a generous floor, so a frame that grows steadily
        // reallocates a handful of times rather than once per frame. Releasing
        // the old buffer here is safe even though the previous frame may still
        // be reading it: the backend defers the free until the GPU is done.
        core::u32 capacity = capacityVertices_ > 0 ? capacityVertices_ : kInitialVertices;
        while (capacity < pendingVertices_)
            capacity *= 2;

        if (vertices_.valid())
            device.destroy(vertices_);

        vertices_ = device.createBuffer({
            .usage = rhi::BufferUsage::Vertex,
            .sizeBytes = capacity * kVertexStride,
            .debugName = "debug-line-vertices",
        });

        if (!vertices_.valid())
        {
            capacityVertices_ = 0;
            pendingVertices_ = 0;
            return;
        }
        capacityVertices_ = capacity;
    }

    cmd.upload(vertices_, std::as_bytes(vertices), 0);
}

void DebugRenderer::render(rhi::ICmdList& cmd, const core::Mat4& viewProjection)
{
    if (pendingVertices_ == 0 || !pipeline_.valid() || !vertices_.valid())
        return;

    cmd.pushDebugGroup("debug-draw");
    cmd.setPipeline(pipeline_);

    // Slot 0 in the vertex stage's uniform space, matching the shader's
    // `register(b0, space1)`. The matrix is already combined on the CPU: one
    // multiply per frame rather than one per vertex.
    cmd.bindUniforms(rhi::ShaderStage::Vertex, 0, std::as_bytes(std::span{&viewProjection, 1}));

    const std::array<rhi::BufferHandle, 1> buffers{vertices_};
    cmd.bindVertexBuffers(0, buffers);
    cmd.draw(pendingVertices_, 1, 0, 0);

    cmd.popDebugGroup();
}

} // namespace luaug::render
