// One direction of `BlurEffect`'s separable Gaussian (ADR 0096).
//
// Run twice -- across, then down -- at whatever level of the downsample chain
// puts the Gaussian's width between two and four texels there, which is what
// keeps the cost flat however large the blur: a blur of 60 pixels at 1080p is
// the same eleven taps as one of 6, at a sixteenth of the pixels.
//
// **Two texels per tap.** The weights are paired and each pair is read with one
// bilinear sample placed between them at the offset their weights make it, so a
// kernel nine texels either side costs five samples a side rather than nine.

#include "luaug_fullscreen.hlsli"

// `render::GpuLookBlurUniforms`, 32 bytes.
cbuffer GpuLookBlurUniforms : register(b0, space3)
{
    // xy one source texel along the direction of this pass, z the Gaussian's
    // standard deviation in texels, w how many texels either side it reaches.
    float4 BlurStepSigma;
    // Unused; the block is kept at two vectors for the next field.
    float4 BlurReserved;
};

Texture2D SourceTexture : register(t0, space2);
SamplerState SourceSampler : register(s0, space2);

struct Interpolants
{
    float2 Uv : TEXCOORD0;
    float4 Position : SV_Position;
};

Interpolants VertexMain(uint vertexId : SV_VertexID)
{
    Interpolants output;
    fullscreenTriangle(vertexId, 0.0f, output.Position, output.Uv);
    return output;
}

float weightAt(float texels, float sigma)
{
    return exp(-0.5f * texels * texels / (sigma * sigma));
}

float4 FragmentMain(Interpolants input) : SV_Target0
{
    const float2 stepUv = BlurStepSigma.xy;
    const float sigma = max(BlurStepSigma.z, 1e-3f);
    const int reach = (int)BlurStepSigma.w;

    float3 sum = SourceTexture.SampleLevel(SourceSampler, input.Uv, 0.0f).rgb;
    float total = 1.0f;
    [loop]
    for (int texel = 1; texel <= reach; texel += 2)
    {
        const float near = weightAt((float)texel, sigma);
        const float far = texel + 1 <= reach ? weightAt((float)(texel + 1), sigma) : 0.0f;
        const float pair = near + far;
        // Where between the two texels one bilinear read returns exactly
        // near * a + far * b, divided by their sum.
        const float offset = (texel * near + (texel + 1) * far) / pair;
        sum += SourceTexture.SampleLevel(SourceSampler, input.Uv + stepUv * offset, 0.0f).rgb * pair;
        sum += SourceTexture.SampleLevel(SourceSampler, input.Uv - stepUv * offset, 0.0f).rgb * pair;
        total += 2.0f * pair;
    }
    return float4(sum / total, 1.0f);
}
