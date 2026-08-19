// RHI vocabulary: handles, formats and the small value types the command
// interface takes (ADR 0005).
//
// The shape is deliberately close to SDL_GPU so the default backend adapter
// stays thin, but no SDL type appears here and none may. Neutrality is not an
// aspiration: `rhi_capture` and `rhi_null` compile against these headers and
// know nothing about SDL, and a second real backend (bgfx, mobile phase) is
// the reason the seam exists at all.
#pragma once

#include "luaug/core/types.h"

namespace luaug::rhi
{

using core::f32;
using core::f64;
using core::i32;
using core::i64;
using core::u16;
using core::u32;
using core::u64;
using core::u8;
using core::usize;

// Which backend a device is. Runtime selection exists only among backends the
// build compiled in (ADR 0023); `app` owns the one factory that maps this to a
// constructor.
enum class BackendId : u8
{
    SdlGpu,
    Capture,
    Null,
};

// Resources are named by opaque, typed ids rather than pointers. Distinct
// structs so a TextureHandle cannot be passed where a BufferHandle belongs, and
// plain integers so `rhi_capture` can record a command stream that is
// byte-comparable across runs -- a pointer would make every capture unique.
//
// Zero is always the null handle: a default-constructed handle is invalid, and
// a backend never hands out 0.
#define LUAUG_RHI_HANDLE(Name)                                                                                         \
    struct Name                                                                                                        \
    {                                                                                                                  \
        u32 id = 0;                                                                                                    \
        [[nodiscard]] constexpr bool valid() const noexcept { return id != 0; }                                         \
        [[nodiscard]] constexpr bool operator==(const Name&) const noexcept = default;                                  \
    }

LUAUG_RHI_HANDLE(BufferHandle);
LUAUG_RHI_HANDLE(TextureHandle);
LUAUG_RHI_HANDLE(SamplerHandle);
LUAUG_RHI_HANDLE(ShaderHandle);
LUAUG_RHI_HANDLE(PipelineHandle);

#undef LUAUG_RHI_HANDLE

// A curated set, not every format a GPU knows. It covers what a forward
// renderer, a debug-draw pass and a screenshot readback need, plus the block
// formats the asset pipeline will transcode to (ADR 0010). It grows when
// something actually uses a format -- an enumerator no backend has ever been
// asked for is an untested branch pretending to be a feature.
enum class TextureFormat : u8
{
    Undefined = 0,

    R8Unorm,
    Rg8Unorm,
    Rgba8Unorm,
    Rgba8UnormSrgb,
    Bgra8Unorm,
    Bgra8UnormSrgb,
    R32Float,
    Rgba16Float,
    Rgba32Float,

    D16Unorm,
    D24UnormS8Uint,
    D32Float,
    D32FloatS8Uint,

    Bc1RgbaUnorm,
    Bc3RgbaUnorm,
    Bc5RgUnorm,
    Bc7RgbaUnorm,
};

[[nodiscard]] constexpr bool isDepthFormat(TextureFormat format) noexcept
{
    return format == TextureFormat::D16Unorm || format == TextureFormat::D24UnormS8Uint
        || format == TextureFormat::D32Float || format == TextureFormat::D32FloatS8Uint;
}

enum class TextureUsage : u32
{
    None = 0,
    Sampled = 1u << 0,
    ColorTarget = 1u << 1,
    DepthStencilTarget = 1u << 2,
};

[[nodiscard]] constexpr TextureUsage operator|(TextureUsage a, TextureUsage b) noexcept
{
    return static_cast<TextureUsage>(static_cast<u32>(a) | static_cast<u32>(b));
}

[[nodiscard]] constexpr bool hasUsage(TextureUsage value, TextureUsage flag) noexcept
{
    return (static_cast<u32>(value) & static_cast<u32>(flag)) != 0;
}

enum class BufferUsage : u32
{
    None = 0,
    Vertex = 1u << 0,
    Index = 1u << 1,
    Indirect = 1u << 2,
};

[[nodiscard]] constexpr BufferUsage operator|(BufferUsage a, BufferUsage b) noexcept
{
    return static_cast<BufferUsage>(static_cast<u32>(a) | static_cast<u32>(b));
}

[[nodiscard]] constexpr bool hasUsage(BufferUsage value, BufferUsage flag) noexcept
{
    return (static_cast<u32>(value) & static_cast<u32>(flag)) != 0;
}

enum class ShaderStage : u8
{
    Vertex,
    Fragment,
};

// The blob format a shader was compiled to. The build produces all three from
// one HLSL source (ADR 0006); a device accepts exactly the one its backend
// speaks, which `Capabilities::shaderFormat` reports.
enum class ShaderFormat : u8
{
    Unknown = 0,
    SpirV,
    Dxil,
    Msl,
};

enum class PrimitiveType : u8
{
    TriangleList,
    TriangleStrip,
    LineList,
    LineStrip,
    PointList,
};

enum class IndexType : u8
{
    U16,
    U32,
};

enum class FillMode : u8
{
    Solid,
    Wireframe,
};

enum class CullMode : u8
{
    None,
    Front,
    Back,
};

enum class FrontFace : u8
{
    CounterClockwise,
    Clockwise,
};

enum class CompareOp : u8
{
    Never,
    Less,
    Equal,
    LessOrEqual,
    Greater,
    NotEqual,
    GreaterOrEqual,
    Always,
};

enum class BlendFactor : u8
{
    Zero,
    One,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha,
    SrcColor,
    OneMinusSrcColor,
    DstColor,
    OneMinusDstColor,
};

enum class BlendOp : u8
{
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max,
};

enum class Filter : u8
{
    Nearest,
    Linear,
};

enum class MipmapMode : u8
{
    Nearest,
    Linear,
};

enum class AddressMode : u8
{
    Repeat,
    MirroredRepeat,
    ClampToEdge,
};

enum class LoadOp : u8
{
    Load,
    Clear,
    DontCare,
};

enum class StoreOp : u8
{
    Store,
    DontCare,
};

// Vertex attribute types, named by what the shader sees.
enum class VertexFormat : u8
{
    Float1,
    Float2,
    Float3,
    Float4,
    Ubyte4Unorm,
};

struct Viewport
{
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 width = 0.0f;
    f32 height = 0.0f;
    f32 minDepth = 0.0f;
    f32 maxDepth = 1.0f;
};

struct Rect
{
    i32 x = 0;
    i32 y = 0;
    i32 width = 0;
    i32 height = 0;
};

// Linear, not sRGB-encoded: the swapchain format decides encoding, and having
// the clear value mean different things per target is a classic source of
// "why is it darker in the screenshot".
struct ColorRgba
{
    f32 r = 0.0f;
    f32 g = 0.0f;
    f32 b = 0.0f;
    f32 a = 1.0f;
};

} // namespace luaug::rhi
