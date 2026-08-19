// The debug-draw line pipeline: world-space line segments with a per-vertex
// colour, drawn straight from a transient vertex buffer with no lighting, no
// depth bias and no material of any kind. It is the pass every other visual
// feature gets debugged with, so its interface is deliberately the smallest
// thing that can express "a coloured segment in world space".
//
// Register spaces are not a style choice: SDL_GPU fixes them per stage, and a
// shader that ignores the contract binds nothing. A vertex shader's uniform
// buffers live at b[n] in space1 (third_party/sdl3/include/SDL3/SDL_gpu.h,
// SDL_CreateGPUShader); a fragment shader's would live in space3, which is why
// the transform is read here rather than there.
//
// Vertex inputs use TEXCOORDn semantics because that is what SDL_shadercross
// maps to SPIR-V input locations; POSITION/COLOR would compile and then not
// bind. Location order is the order below.

cbuffer DebugLineTransform : register(b0, space1)
{
    // Already combined on the CPU: one matrix multiply per frame beats one per
    // vertex, and the debug pass draws a great many vertices.
    float4x4 ViewProjection;
};

struct VertexInput
{
    float3 WorldPosition : TEXCOORD0;
    float4 Color : TEXCOORD1;
};

struct Interpolants
{
    float4 Color : TEXCOORD0;
    float4 Position : SV_Position;
};

Interpolants VertexMain(VertexInput input)
{
    Interpolants output;
    output.Position = mul(ViewProjection, float4(input.WorldPosition, 1.0f));
    output.Color = input.Color;
    return output;
}

float4 FragmentMain(Interpolants input) : SV_Target0
{
    return input.Color;
}
