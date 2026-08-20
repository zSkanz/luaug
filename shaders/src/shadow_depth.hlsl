// The sun's shadow pass: depth only, from one orthographic view fitted around
// the camera (M4 brief, Decision 10 -- one cascade, and the sun is the only
// shadow caster in v1).
//
// There is no colour target, so the fragment stage writes nothing at all. It
// exists because `SDL_CreateGPUGraphicsPipeline` requires both a vertex and a
// fragment shader; a `void` entry point with no `SV_Target` is the smallest
// thing that satisfies it, and it is also the correct one -- a pipeline with
// zero colour targets whose fragment shader declares `SV_Target0` is a
// mismatch the D3D12 debug layer objects to.
//
// **Only position is declared.** The pass reads nothing else, and a vertex
// input a shader does not consume is an attribute the pipeline still has to
// describe; declaring the unused three would be three more locations for the
// pipeline's vertex layout to agree with for no result. The buffer bound is
// still the full 48-byte `asset::Vertex` -- position is at offset 0, so one
// attribute at location 0 reads it correctly.
//
// Register spaces are fixed per stage by SDL_GPU (SDL_gpu.h:2699-2730) and
// vertex inputs use TEXCOORDn because that is what SDL_shadercross maps to
// SPIR-V input locations.

#define LUAUG_UNIFORMS_SHADOW
#include "luaug_pbr.hlsli"

struct VertexInput
{
    float3 Position : TEXCOORD0;
};

struct Interpolants
{
    float4 Position : SV_Position;
};

Interpolants VertexMain(VertexInput input)
{
    Interpolants output;
    // Two matrices rather than one premultiplied on the CPU: the shadow pass
    // and the forward pass draw the same objects with the same per-object
    // model matrix, and combining here costs one multiply per vertex against a
    // second per-object upload per pass.
    output.Position = mul(LightViewProjection, mul(ShadowModel, float4(input.Position, 1.0f)));
    return output;
}

void FragmentMain()
{
}
