// The triangle sample's pipeline: three vertices, one flat colour, nothing
// else. It exists to answer one question on a device the engine has never run
// on -- does SDL3 GPU rasterize here (roadmap M4, ADR 0005) -- so everything
// that could fail for an unrelated reason is absent by construction.
//
// No uniform buffer, and positions are already in clip space. A view-projection
// matrix would put a second suspect in a frame whose whole value is that a
// black screen has exactly one possible cause.
//
// Register spaces are not a style choice: SDL_GPU fixes them per stage, and a
// shader that ignores the contract binds nothing. Nothing here declares a
// resource, which is why no space appears below -- see debug_line.hlsl for the
// uniform-buffer case.
//
// Vertex inputs use TEXCOORDn semantics because that is what SDL_shadercross
// maps to SPIR-V input locations; POSITION/COLOR would compile and then not
// bind. Location order is the order below.

struct VertexInput
{
    float3 ClipPosition : TEXCOORD0;
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
    output.Position = float4(input.ClipPosition, 1.0f);
    output.Color = input.Color;
    return output;
}

float4 FragmentMain(Interpolants input) : SV_Target0
{
    return input.Color;
}
