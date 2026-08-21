// Resource and pipeline descriptors.
//
// Every desc is a plain aggregate with defaults that describe the common case,
// so a call site names only what it actually cares about. That is also what
// makes `rhi_capture` able to serialise a desc field by field.
#pragma once

#include "luaug/rhi/types.h"

#include <span>
#include <string_view>

namespace luaug::rhi {

struct BufferDesc
{
    BufferUsage usage = BufferUsage::None;
    u32 sizeBytes = 0;
    // Shows up in RenderDoc and in the backend validation layer. Developer
    // text, never shown to a player, so it is a plain string (R3 does not
    // apply); empty is fine.
    std::string_view debugName{};
};

struct TextureDesc
{
    TextureFormat format = TextureFormat::Undefined;
    TextureUsage usage = TextureUsage::None;
    u32 width = 0;
    u32 height = 0;
    u32 layers = 1;
    u32 mipLevels = 1;
    std::string_view debugName{};
};

struct SamplerDesc
{
    Filter minFilter = Filter::Linear;
    Filter magFilter = Filter::Linear;
    MipmapMode mipmapMode = MipmapMode::Linear;
    AddressMode addressU = AddressMode::Repeat;
    AddressMode addressV = AddressMode::Repeat;
    AddressMode addressW = AddressMode::Repeat;
    std::string_view debugName{};
};

struct ShaderDesc
{
    ShaderStage stage = ShaderStage::Vertex;
    ShaderFormat format = ShaderFormat::Unknown;
    std::span<const std::byte> code{};
    std::string_view entryPoint = "main";

    // Backends need the counts up front to build their descriptor layouts;
    // SDL_GPU rejects a shader whose declared counts do not match its bindings.
    // The shader compiler emits them into the shader manifest, so nothing hand
    // counts these.
    u32 samplerCount = 0;
    u32 uniformBufferCount = 0;

    std::string_view debugName{};
};

struct VertexAttribute
{
    // Matches the shader input location, not the field order.
    u32 location = 0;
    // Which bound vertex buffer this attribute reads from.
    u32 bufferSlot = 0;
    VertexFormat format = VertexFormat::Float3;
    u32 offsetBytes = 0;
};

struct VertexBufferLayout
{
    u32 slot = 0;
    u32 strideBytes = 0;
    // When true this stream advances once per INSTANCE rather than once per
    // vertex, which is what lets one call draw a run of objects that share a
    // mesh and a material. False is the default and is what every stream that
    // existed before instanced rendering was (ADR 0043).
    bool perInstance = false;
};

struct DepthStencilState
{
    bool depthTest = false;
    bool depthWrite = false;
    CompareOp depthCompare = CompareOp::LessOrEqual;
};

struct BlendState
{
    bool enabled = false;
    BlendFactor srcColor = BlendFactor::SrcAlpha;
    BlendFactor dstColor = BlendFactor::OneMinusSrcAlpha;
    BlendOp colorOp = BlendOp::Add;
    BlendFactor srcAlpha = BlendFactor::One;
    BlendFactor dstAlpha = BlendFactor::OneMinusSrcAlpha;
    BlendOp alphaOp = BlendOp::Add;
};

struct ColorTargetDesc
{
    TextureFormat format = TextureFormat::Undefined;
    BlendState blend{};
};

struct RasterizerState
{
    FillMode fillMode = FillMode::Solid;
    CullMode cullMode = CullMode::Back;
    FrontFace frontFace = FrontFace::CounterClockwise;
};

struct GraphicsPipelineDesc
{
    ShaderHandle vertexShader{};
    ShaderHandle fragmentShader{};

    std::span<const VertexBufferLayout> vertexBuffers{};
    std::span<const VertexAttribute> vertexAttributes{};

    PrimitiveType primitive = PrimitiveType::TriangleList;
    RasterizerState rasterizer{};
    DepthStencilState depthStencil{};

    std::span<const ColorTargetDesc> colorTargets{};
    // Undefined means the pipeline renders without a depth attachment.
    TextureFormat depthStencilFormat = TextureFormat::Undefined;

    std::string_view debugName{};
};

struct ColorAttachment
{
    TextureHandle texture{};
    LoadOp loadOp = LoadOp::Clear;
    StoreOp storeOp = StoreOp::Store;
    ColorRgba clearColor{};
};

struct DepthStencilAttachment
{
    TextureHandle texture{};
    LoadOp loadOp = LoadOp::Clear;
    StoreOp storeOp = StoreOp::DontCare;
    f32 clearDepth = 1.0f;
};

struct RenderPassDesc
{
    std::span<const ColorAttachment> colorAttachments{};
    // An invalid texture means no depth attachment this pass.
    DepthStencilAttachment depthStencil{};
    std::string_view debugName{};
};

// A texture is always sampled through a sampler; binding them as a pair is
// what every backend's descriptor model expects, and it makes forgetting one
// impossible rather than a black screen.
struct TextureBinding
{
    TextureHandle texture{};
    SamplerHandle sampler{};
};

} // namespace luaug::rhi
