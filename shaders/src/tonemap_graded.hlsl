// The resolve with a colour grade in it: `tonemap.hlsl`, plus every enabled
// `ColorCorrectionEffect` of the frame composed into one affine map (ADR 0096,
// `render::resolveLook`).
//
// **A second pipeline rather than a branch in the first**, and the reason is
// every golden in the repository. A world with no colour correction must draw
// through exactly the shader it always drew through -- the same bytes, the same
// uniform block -- so this file includes that one whole, renames its entry
// point out of the way, and adds its own. The renderer makes this pipeline the
// first frame a grade exists, as it does the decals', so a world without one
// never builds it.
//
// The grade sits where the class's documentation says: on the exposed light,
// before the curve, so a highlight it brightens still rolls off rather than
// clipping.

#define FragmentMain tonemapUngradedFragment
// Through the include directory, because the compiler reads this file from
// memory and has no directory of its own to resolve a sibling against.
#include "../src/tonemap.hlsl"
#undef FragmentMain

// `render::GpuGradeUniforms`, 48 bytes, at the fragment stage's SECOND slot:
// the first is `GpuTonemapUniforms`, unchanged.
cbuffer GpuGradeUniforms : register(b1, space3)
{
    // Three rows of an affine map of linear colour: out = row . (r, g, b, 1).
    float4 GradeRed;
    float4 GradeGreen;
    float4 GradeBlue;
};

float4 FragmentMain(Interpolants input) : SV_Target0
{
    const float3 hdr = HdrTexture.SampleLevel(HdrSampler, input.Uv, 0.0f).rgb;
    const float3 bloom = BloomTexture.SampleLevel(BloomSampler, input.Uv, 0.0f).rgb;
    const float3 scene = hdr + bloom * ExposureBloom.y;

    const float measured = max(ExposureTexture.SampleLevel(ExposureSampler, float2(0.5f, 0.5f), 0.0f), 1e-4f);
    const float exposure = (LuaugExposureKey / measured) * exp2(ExposureBloom.x);
    const float4 exposed = float4(max(scene * exposure, float3(0.0f, 0.0f, 0.0f)), 1.0f);

    // Clamped at black after the grade too: a brightness below zero darkens,
    // and it must not take a pixel into negative light the curve has no answer
    // for.
    const float3 graded = max(float3(dot(GradeRed, exposed), dot(GradeGreen, exposed), dot(GradeBlue, exposed)),
                              float3(0.0f, 0.0f, 0.0f));

    return float4(encodeSrgb(tonemapPbrNeutral(graded)), 1.0f);
}
