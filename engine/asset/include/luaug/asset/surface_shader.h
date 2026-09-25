#pragma once

// A surface shader's source, read and wrapped (ADR 0091).
//
// **Text in, text out, and nothing else.** This reads what a user wrote against
// `luaug/surface.hlsli` -- its parameters, its textures, which of the two
// functions it defines, and what it must not do -- and writes the HLSL the
// engine compiles for each pass: the user's file, included into an entry point
// the engine owns, with the parameter block and the textures declared where the
// renderer binds them. Compiling that text is somebody else's job (the editor
// and `assetc`, which carry a compiler); a game only ever loads bytecode.
//
// The layout it decides is the renderer's contract as much as the shader's, so
// both sides read it from here:
//
//   * the parameter block is a vertex uniform at slot 1 and a fragment uniform
//     at slot 2, identical bytes in both. Its first 16 bytes are the engine's
//     (the clock and the camera); the parameters follow, packed as an HLSL
//     cbuffer packs them;
//   * a surface texture is fragment sampler `FirstSurfaceSampler + n`, after the
//     engine's thirteen, and vertex sampler `n`.

#include "luaug/core/types.h"

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace luaug::asset {

enum class SurfaceParamType : core::u8
{
    Float,
    Float2,
    Float3,
    Float4,
    Int,
    Bool,
};

enum class SurfaceAnnotation : core::u8
{
    None,
    Range,
    Colour,
    Toggle,
};

// What an unset surface texture reads as.
enum class SurfaceTextureDefault : core::u8
{
    White,
    Black,
    // A flat tangent-space normal: (0.5, 0.5, 1).
    Normal,
};

struct SurfaceParam
{
    std::string name;
    SurfaceParamType type = SurfaceParamType::Float;
    // The default, in as many components as the type has; an int or a bool is
    // its value as a float.
    std::array<core::f32, 4> value{};
    SurfaceAnnotation annotation = SurfaceAnnotation::None;
    core::f32 minimum = 0.0f;
    core::f32 maximum = 1.0f;
    // Byte offset in the parameter block, after the engine's 16.
    core::u32 offset = 0;
    core::u32 line = 0;
};

struct SurfaceTexture
{
    std::string name;
    SurfaceTextureDefault fallback = SurfaceTextureDefault::White;
    core::u32 line = 0;
};

// A problem with the source, by line: an i18n key and what it is about.
struct SurfaceDiagnostic
{
    core::u32 line = 0;
    std::string key;
    std::string subject;
};

struct SurfaceReflection
{
    std::vector<SurfaceParam> params;
    std::vector<SurfaceTexture> textures;
    bool hasVertex = false;
    bool hasFragment = false;
    // The whole block, header included, rounded to 16.
    core::u32 blockBytes = 16;
    std::vector<SurfaceDiagnostic> errors;

    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
    [[nodiscard]] const SurfaceParam* param(std::string_view name) const noexcept;
};

inline constexpr core::u32 SurfaceContractVersion = 1;
inline constexpr core::u32 SurfaceBlockHeaderBytes = 16;
inline constexpr core::u32 MaxSurfaceBlockBytes = 1024;
inline constexpr core::u32 MaxSurfaceTextures = 8;
// The engine's fragment samplers are t0..t12 (`luaug_forward.hlsli`).
inline constexpr core::u32 FirstSurfaceSampler = 13;

// Reads the source. Never throws: every problem is a diagnostic.
[[nodiscard]] SurfaceReflection reflectSurface(std::string_view source);

// The passes a surface is drawn in, each one pipeline.
enum class SurfaceVariant : core::u8
{
    // Opaque and masked parts, one draw each.
    Forward,
    // The same, many at once (per-instance transforms).
    ForwardInstanced,
    // Transparent parts: the forward code, with the scene's depth readable.
    ForwardBlended,
    // Shadow maps and the depth prepass: where the displaced surface is.
    Depth,
    DepthInstanced,
};

enum class SurfaceStage : core::u8
{
    Vertex,
    Fragment,
};

// The HLSL to compile for one stage of one variant: engine headers, the block
// and the textures, `#include "<userInclude>"`, and the entry point
// (`VertexMain` or `FragmentMain`).
[[nodiscard]] std::string surfaceWrapper(const SurfaceReflection& reflection, SurfaceVariant variant,
                                         SurfaceStage stage, std::string_view userInclude);

// How many samplers and uniform buffers each stage of a variant declares --
// the counts shader creation needs, decided by the layout rather than read back
// from a compiler that may have stripped an unused one.
struct SurfaceResourceCounts
{
    core::u32 samplers = 0;
    core::u32 uniformBuffers = 0;
};
[[nodiscard]] SurfaceResourceCounts surfaceResourceCounts(const SurfaceReflection& reflection, SurfaceVariant variant,
                                                          SurfaceStage stage) noexcept;

// Writes one parameter's value into a block laid out by `reflection`.
void writeSurfaceParam(const SurfaceParam& param, std::span<const core::f32> value, std::span<core::u8> block) noexcept;

} // namespace luaug::asset
