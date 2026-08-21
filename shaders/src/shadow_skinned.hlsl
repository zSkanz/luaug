// The sun's shadow pass for a SKINNED mesh: `shadow_depth.hlsl` with the same
// linear blend the forward skinned pass uses.
//
// It exists because the alternative is a character whose shadow stands still
// while it walks. A shadow pass that drew the bind pose would be a bug nobody
// could photograph -- the image is correct everywhere except on the ground.
//
// Position and the skin stream, and nothing else: the pass reads no normal, no
// tangent and no UV, and a vertex input a shader does not consume is one more
// location the pipeline's vertex layout has to describe for no result. The
// buffers bound are still the full 48-byte `asset::Vertex` at slot 0 and the
// 24-byte `asset::SkinVertex` at slot 1, which is why the offsets below are the
// ones they are.
//
// Register spaces are fixed per stage by SDL_GPU (SDL_gpu.h:2699-2730) and
// vertex inputs use TEXCOORDn because that is what SDL_shadercross maps to
// SPIR-V input locations.

#define LUAUG_UNIFORMS_SHADOW
#define LUAUG_UNIFORMS_SKIN
#include "luaug_pbr.hlsli"

struct VertexInput
{
    float3 Position : TEXCOORD0;
    // `float4` and not `uint4`: see `pbr_skinned.hlsl` and D042. The stream
    // holds floats, and declaring an integer input reinterprets them.
    float4 Joints : TEXCOORD1;
    float4 Weights : TEXCOORD2;
};

struct Interpolants
{
    float4 Position : SV_Position;
};

Interpolants VertexMain(VertexInput input)
{
    Interpolants output;
    const float4 posed = mul(skinMatrix(uint4(input.Joints), input.Weights), float4(input.Position, 1.0f));
    output.Position = mul(LightViewProjection, mul(ShadowModel, posed));
    return output;
}

void FragmentMain()
{
}
