// The 2D pass: screen-space coloured quads, drawn over the world.
//
// Everything the UI draws is one of these. A `Frame`'s background is a quad, a
// `TextLabel`'s glyphs are quads -- the built-in vector face emits axis-aligned
// rectangles rather than sampled bitmaps (engine/ui/src/text.cpp) -- and a
// nine-slice patch will be nine of them. There is deliberately no texture and no
// UV: nothing in v1's UI samples one, and a sampler bound for a future caller is
// a sampler the capture gate would record every frame for nothing.
//
// Register spaces are SDL_GPU's, not a style choice: a vertex shader's uniform
// buffers live at b[n] in space1 (SDL_gpu.h, SDL_CreateGPUShader), which is why
// the projection is read here rather than in the fragment stage. Vertex inputs
// use TEXCOORDn because that is what SDL_shadercross maps to SPIR-V input
// locations; POSITION would compile and then bind nothing.

cbuffer Ui2dProjection : register(b0, space1)
{
    // Pixels to clip space, as a scale and a bias rather than a matrix: the
    // transform is diagonal, and four floats say so where sixteen would hide it.
    //
    // x, y are 2/width and -2/height -- the sign is what flips the UI's y-down
    // convention onto the API's y-up clip space, and it is the ONE place that
    // conversion happens.
    float4 ScreenToClip;
};

struct VertexInput
{
    float2 Position : TEXCOORD0;
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
    output.Position = float4(input.Position.x * ScreenToClip.x + ScreenToClip.z,
                             input.Position.y * ScreenToClip.y + ScreenToClip.w, 0.0f, 1.0f);
    output.Color = input.Color;
    return output;
}

float4 FragmentMain(Interpolants input) : SV_Target0
{
    return input.Color;
}
