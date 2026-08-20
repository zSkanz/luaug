// LuauG RHI enums translated to SDL_GPU's.
//
// One file, all of it total switches with no default, so that adding an
// enumerator to the seam is a compile error here rather than a silently wrong
// pipeline. That is the whole reason the mapping is not a table of constants.
#pragma once

#include "luaug/rhi/types.h"

#include <SDL3/SDL_gpu.h>

namespace luaug::rhi::sdlgpu {

[[nodiscard]] inline SDL_GPUTextureFormat toSdl(TextureFormat format) noexcept
{
    switch (format) {
    case TextureFormat::Undefined:
        return SDL_GPU_TEXTUREFORMAT_INVALID;
    case TextureFormat::R8Unorm:
        return SDL_GPU_TEXTUREFORMAT_R8_UNORM;
    case TextureFormat::Rg8Unorm:
        return SDL_GPU_TEXTUREFORMAT_R8G8_UNORM;
    case TextureFormat::Rgba8Unorm:
        return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    case TextureFormat::Rgba8UnormSrgb:
        return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
    case TextureFormat::Bgra8Unorm:
        return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    case TextureFormat::Bgra8UnormSrgb:
        return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB;
    case TextureFormat::R32Float:
        return SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
    case TextureFormat::Rgba16Float:
        return SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    case TextureFormat::Rgba32Float:
        return SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    case TextureFormat::D16Unorm:
        return SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    case TextureFormat::D24UnormS8Uint:
        return SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
    case TextureFormat::D32Float:
        return SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    case TextureFormat::D32FloatS8Uint:
        return SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
    case TextureFormat::Bc1RgbaUnorm:
        return SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM;
    case TextureFormat::Bc3RgbaUnorm:
        return SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM;
    case TextureFormat::Bc5RgUnorm:
        return SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM;
    case TextureFormat::Bc7RgbaUnorm:
        return SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM;
    }
    return SDL_GPU_TEXTUREFORMAT_INVALID;
}

[[nodiscard]] inline TextureFormat fromSdl(SDL_GPUTextureFormat format) noexcept
{
    // Only the formats a swapchain can hand back need the reverse direction;
    // anything else came from us in the first place.
    switch (format) {
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:
        return TextureFormat::Rgba8Unorm;
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB:
        return TextureFormat::Rgba8UnormSrgb;
    case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM:
        return TextureFormat::Bgra8Unorm;
    case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB:
        return TextureFormat::Bgra8UnormSrgb;
    case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT:
        return TextureFormat::Rgba16Float;
    default:
        return TextureFormat::Undefined;
    }
}

[[nodiscard]] inline SDL_GPUTextureUsageFlags toSdl(TextureUsage usage) noexcept
{
    SDL_GPUTextureUsageFlags flags = 0;
    if (hasUsage(usage, TextureUsage::Sampled))
        flags |= SDL_GPU_TEXTUREUSAGE_SAMPLER;
    if (hasUsage(usage, TextureUsage::ColorTarget))
        flags |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    if (hasUsage(usage, TextureUsage::DepthStencilTarget))
        flags |= SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    return flags;
}

[[nodiscard]] inline SDL_GPUBufferUsageFlags toSdl(BufferUsage usage) noexcept
{
    SDL_GPUBufferUsageFlags flags = 0;
    if (hasUsage(usage, BufferUsage::Vertex))
        flags |= SDL_GPU_BUFFERUSAGE_VERTEX;
    if (hasUsage(usage, BufferUsage::Index))
        flags |= SDL_GPU_BUFFERUSAGE_INDEX;
    if (hasUsage(usage, BufferUsage::Indirect))
        flags |= SDL_GPU_BUFFERUSAGE_INDIRECT;
    return flags;
}

[[nodiscard]] inline SDL_GPUShaderFormat toSdl(ShaderFormat format) noexcept
{
    switch (format) {
    case ShaderFormat::Unknown:
        return SDL_GPU_SHADERFORMAT_INVALID;
    case ShaderFormat::SpirV:
        return SDL_GPU_SHADERFORMAT_SPIRV;
    case ShaderFormat::Dxil:
        return SDL_GPU_SHADERFORMAT_DXIL;
    case ShaderFormat::Msl:
        return SDL_GPU_SHADERFORMAT_MSL;
    }
    return SDL_GPU_SHADERFORMAT_INVALID;
}

// A device may report several; the first one the engine can supply wins, in the
// order the shader build produces them.
[[nodiscard]] inline ShaderFormat fromSdlShaderFormats(SDL_GPUShaderFormat formats) noexcept
{
    if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0)
        return ShaderFormat::SpirV;
    if ((formats & SDL_GPU_SHADERFORMAT_DXIL) != 0)
        return ShaderFormat::Dxil;
    if ((formats & SDL_GPU_SHADERFORMAT_MSL) != 0)
        return ShaderFormat::Msl;
    return ShaderFormat::Unknown;
}

[[nodiscard]] inline SDL_GPUShaderStage toSdl(ShaderStage stage) noexcept
{
    switch (stage) {
    case ShaderStage::Vertex:
        return SDL_GPU_SHADERSTAGE_VERTEX;
    case ShaderStage::Fragment:
        return SDL_GPU_SHADERSTAGE_FRAGMENT;
    }
    return SDL_GPU_SHADERSTAGE_VERTEX;
}

[[nodiscard]] inline SDL_GPUPrimitiveType toSdl(PrimitiveType type) noexcept
{
    switch (type) {
    case PrimitiveType::TriangleList:
        return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    case PrimitiveType::TriangleStrip:
        return SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
    case PrimitiveType::LineList:
        return SDL_GPU_PRIMITIVETYPE_LINELIST;
    case PrimitiveType::LineStrip:
        return SDL_GPU_PRIMITIVETYPE_LINESTRIP;
    case PrimitiveType::PointList:
        return SDL_GPU_PRIMITIVETYPE_POINTLIST;
    }
    return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
}

[[nodiscard]] inline SDL_GPUIndexElementSize toSdl(IndexType type) noexcept
{
    switch (type) {
    case IndexType::U16:
        return SDL_GPU_INDEXELEMENTSIZE_16BIT;
    case IndexType::U32:
        return SDL_GPU_INDEXELEMENTSIZE_32BIT;
    }
    return SDL_GPU_INDEXELEMENTSIZE_16BIT;
}

[[nodiscard]] inline SDL_GPULoadOp toSdl(LoadOp op) noexcept
{
    switch (op) {
    case LoadOp::Load:
        return SDL_GPU_LOADOP_LOAD;
    case LoadOp::Clear:
        return SDL_GPU_LOADOP_CLEAR;
    case LoadOp::DontCare:
        return SDL_GPU_LOADOP_DONT_CARE;
    }
    return SDL_GPU_LOADOP_CLEAR;
}

[[nodiscard]] inline SDL_GPUStoreOp toSdl(StoreOp op) noexcept
{
    switch (op) {
    case StoreOp::Store:
        return SDL_GPU_STOREOP_STORE;
    case StoreOp::DontCare:
        return SDL_GPU_STOREOP_DONT_CARE;
    }
    return SDL_GPU_STOREOP_STORE;
}

[[nodiscard]] inline SDL_GPUFillMode toSdl(FillMode mode) noexcept
{
    switch (mode) {
    case FillMode::Solid:
        return SDL_GPU_FILLMODE_FILL;
    case FillMode::Wireframe:
        return SDL_GPU_FILLMODE_LINE;
    }
    return SDL_GPU_FILLMODE_FILL;
}

[[nodiscard]] inline SDL_GPUCullMode toSdl(CullMode mode) noexcept
{
    switch (mode) {
    case CullMode::None:
        return SDL_GPU_CULLMODE_NONE;
    case CullMode::Front:
        return SDL_GPU_CULLMODE_FRONT;
    case CullMode::Back:
        return SDL_GPU_CULLMODE_BACK;
    }
    return SDL_GPU_CULLMODE_NONE;
}

[[nodiscard]] inline SDL_GPUFrontFace toSdl(FrontFace face) noexcept
{
    switch (face) {
    case FrontFace::CounterClockwise:
        return SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    case FrontFace::Clockwise:
        return SDL_GPU_FRONTFACE_CLOCKWISE;
    }
    return SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
}

[[nodiscard]] inline SDL_GPUCompareOp toSdl(CompareOp op) noexcept
{
    switch (op) {
    case CompareOp::Never:
        return SDL_GPU_COMPAREOP_NEVER;
    case CompareOp::Less:
        return SDL_GPU_COMPAREOP_LESS;
    case CompareOp::Equal:
        return SDL_GPU_COMPAREOP_EQUAL;
    case CompareOp::LessOrEqual:
        return SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    case CompareOp::Greater:
        return SDL_GPU_COMPAREOP_GREATER;
    case CompareOp::NotEqual:
        return SDL_GPU_COMPAREOP_NOT_EQUAL;
    case CompareOp::GreaterOrEqual:
        return SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
    case CompareOp::Always:
        return SDL_GPU_COMPAREOP_ALWAYS;
    }
    return SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
}

[[nodiscard]] inline SDL_GPUBlendFactor toSdl(BlendFactor factor) noexcept
{
    switch (factor) {
    case BlendFactor::Zero:
        return SDL_GPU_BLENDFACTOR_ZERO;
    case BlendFactor::One:
        return SDL_GPU_BLENDFACTOR_ONE;
    case BlendFactor::SrcAlpha:
        return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    case BlendFactor::OneMinusSrcAlpha:
        return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DstAlpha:
        return SDL_GPU_BLENDFACTOR_DST_ALPHA;
    case BlendFactor::OneMinusDstAlpha:
        return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
    case BlendFactor::SrcColor:
        return SDL_GPU_BLENDFACTOR_SRC_COLOR;
    case BlendFactor::OneMinusSrcColor:
        return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
    case BlendFactor::DstColor:
        return SDL_GPU_BLENDFACTOR_DST_COLOR;
    case BlendFactor::OneMinusDstColor:
        return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
    }
    return SDL_GPU_BLENDFACTOR_ONE;
}

[[nodiscard]] inline SDL_GPUBlendOp toSdl(BlendOp op) noexcept
{
    switch (op) {
    case BlendOp::Add:
        return SDL_GPU_BLENDOP_ADD;
    case BlendOp::Subtract:
        return SDL_GPU_BLENDOP_SUBTRACT;
    case BlendOp::ReverseSubtract:
        return SDL_GPU_BLENDOP_REVERSE_SUBTRACT;
    case BlendOp::Min:
        return SDL_GPU_BLENDOP_MIN;
    case BlendOp::Max:
        return SDL_GPU_BLENDOP_MAX;
    }
    return SDL_GPU_BLENDOP_ADD;
}

[[nodiscard]] inline SDL_GPUFilter toSdl(Filter filter) noexcept
{
    switch (filter) {
    case Filter::Nearest:
        return SDL_GPU_FILTER_NEAREST;
    case Filter::Linear:
        return SDL_GPU_FILTER_LINEAR;
    }
    return SDL_GPU_FILTER_LINEAR;
}

[[nodiscard]] inline SDL_GPUSamplerMipmapMode toSdl(MipmapMode mode) noexcept
{
    switch (mode) {
    case MipmapMode::Nearest:
        return SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    case MipmapMode::Linear:
        return SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    }
    return SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
}

[[nodiscard]] inline SDL_GPUSamplerAddressMode toSdl(AddressMode mode) noexcept
{
    switch (mode) {
    case AddressMode::Repeat:
        return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    case AddressMode::MirroredRepeat:
        return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
    case AddressMode::ClampToEdge:
        return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    }
    return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
}

[[nodiscard]] inline SDL_GPUVertexElementFormat toSdl(VertexFormat format) noexcept
{
    switch (format) {
    case VertexFormat::Float1:
        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
    case VertexFormat::Float2:
        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    case VertexFormat::Float3:
        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    case VertexFormat::Float4:
        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    case VertexFormat::Ubyte4Unorm:
        return SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    }
    return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
}

// Bytes per pixel for the uncompressed formats, which is what a readback needs
// to size its buffer. Block formats return 0: they cannot be read back this way
// and the caller must be told so rather than handed a wrong size.
[[nodiscard]] inline u32 bytesPerPixel(TextureFormat format) noexcept
{
    switch (format) {
    case TextureFormat::R8Unorm:
        return 1;
    case TextureFormat::Rg8Unorm:
        return 2;
    case TextureFormat::D16Unorm:
        return 2;
    case TextureFormat::Rgba8Unorm:
    case TextureFormat::Rgba8UnormSrgb:
    case TextureFormat::Bgra8Unorm:
    case TextureFormat::Bgra8UnormSrgb:
    case TextureFormat::R32Float:
    case TextureFormat::D24UnormS8Uint:
    case TextureFormat::D32Float:
        return 4;
    case TextureFormat::Rgba16Float:
    case TextureFormat::D32FloatS8Uint:
        return 8;
    case TextureFormat::Rgba32Float:
        return 16;
    case TextureFormat::Undefined:
    case TextureFormat::Bc1RgbaUnorm:
    case TextureFormat::Bc3RgbaUnorm:
    case TextureFormat::Bc5RgUnorm:
    case TextureFormat::Bc7RgbaUnorm:
        return 0;
    }
    return 0;
}

} // namespace luaug::rhi::sdlgpu
