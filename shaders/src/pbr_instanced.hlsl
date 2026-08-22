// The forward pass for a run of objects that share a mesh and a material: one
// draw call, with each object's transform read from a second vertex stream that
// advances per INSTANCE rather than per vertex (ADR 0043).
//
// **Why this file exists is a measurement, not a preference.** Two thousand
// enemies sharing one mesh cost 11.1 ms a frame, of which 6.9 ms is submitting
// 2,092 forward draws -- and the same scene costs the same at 320x180 and at 4K,
// which is what says the GPU is idle and the frame is CPU-side submission
// (`docs/perf-baselines.md`, M2). Twelve thousand triangles, in total.
//
// The fragment stage is `luaug_forward.hlsli`'s, shared with `pbr.hlsl` and
// `pbr_skinned.hlsl`. What differs, again, is only the vertex input layout and
// the vertex stage -- which is exactly the shape M6 established for skinning.
//
// **The normal matrix is derived here rather than carried.** A cofactor matrix
// is four more `float4` attributes, and a vertex layout is capped at sixteen
// attributes on the weakest conforming device; three cross products per vertex
// is the cheaper half of that trade. It is the same construction the CPU path
// uses (`normalMatrixOf` in `renderer_default.cpp`) and for the same reason: it
// is `det(M) * M^-T`, so nothing divides by a determinant a degenerate transform
// makes zero.

#define LUAUG_UNIFORMS_OBJECT
#define LUAUG_UNIFORMS_FRAME
#define LUAUG_UNIFORMS_MATERIAL
#include "luaug_forward.hlsli"

struct VertexInput
{
    float3 Position : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float4 Tangent : TEXCOORD2;
    float2 Uv : TEXCOORD3;
    // Slot 1, per instance: the model matrix as four columns, then the
    // instance's own alpha. `render::GpuInstance`, 80 bytes.
    float4 ModelColumn0 : TEXCOORD4;
    float4 ModelColumn1 : TEXCOORD5;
    float4 ModelColumn2 : TEXCOORD6;
    float4 ModelColumn3 : TEXCOORD7;
    float4 InstanceAlphaUnused : TEXCOORD8;
};

// `core::Mat4` stores `m[column][row]`, so the four attributes ARE the matrix's
// four columns -- and **HLSL's `float4x4(a, b, c, d)` constructor fills ROWS**.
// Handing it four columns therefore builds the TRANSPOSE, which is what this
// shader did until somebody looked at the horde instead of counting its draw
// calls (D043). `mul(M^T, p)` puts a dot product of the translation into `w`, so
// every instanced vertex went somewhere off-screen and the pass drew nothing.
//
// `column_major` on the uniform blocks is not the same statement: it describes
// how a matrix is LAID OUT IN MEMORY, and a vertex attribute has no layout to
// declare -- it arrives as four separate vectors and how they are assembled is
// this function's business.
float4x4 instanceModel(VertexInput input)
{
    return transpose(float4x4(input.ModelColumn0, input.ModelColumn1, input.ModelColumn2, input.ModelColumn3));
}

Interpolants VertexMain(VertexInput input)
{
    Interpolants output;

    const float4x4 model = instanceModel(input);
    const float4 shadingPosition = mul(model, float4(input.Position, 1.0f));
    output.ShadingPosition = shadingPosition.xyz;
    output.Position = mul(ViewProjection, shadingPosition);
    output.ViewDepth = output.Position.w;

    // The cofactor matrix of the rotation-scale block, so a non-uniform scale
    // tilts the normal instead of shearing it. Read straight off the attributes
    // rather than out of `model`: these are the transformed basis vectors, which
    // is what the matrix's COLUMNS are, and indexing a `float4x4` gives rows.
    const float3 a = input.ModelColumn0.xyz;
    const float3 b = input.ModelColumn1.xyz;
    const float3 c = input.ModelColumn2.xyz;
    const float3x3 normalMatrix = float3x3(cross(b, c), cross(c, a), cross(a, b));

    output.Normal = mul(normalMatrix, input.Normal);
    // A tangent is a direction along the surface, not a covector, so it rides on
    // the model matrix rather than on the cofactor one.
    output.Tangent = float4(mul((float3x3)model, input.Tangent.xyz), input.Tangent.w);
    output.Uv = input.Uv;
    output.InstanceAlpha = input.InstanceAlphaUnused.x;

    return output;
}

float4 FragmentMain(Interpolants input) : SV_Target0
{
    return shadeForward(input);
}
