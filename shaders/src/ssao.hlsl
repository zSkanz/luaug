// Screen-space ambient occlusion, from the depth the PREPASS wrote.
//
// **Depth is readable here because a prepass wrote it and nothing has it
// attached** -- that is the roadmap's design constraint for M7.5, answered while
// the pass list was open rather than afterwards (brief, Decision 8). What it
// does NOT answer is a blended pass sampling the depth it is attached to, which
// is what water foam needs; that is one RHI field or one copy call and it is
// named rather than implied.
//
// **The normal is reconstructed from depth derivatives**, not read from a
// G-buffer. A forward renderer that grows a normal target has grown half a
// deferred one, and the cost of the reconstruction is a wrong normal along a
// silhouette -- which the depth-aware blur after this pass mostly eats.
//
// The occlusion term produced here multiplies the IMAGE-BASED and ambient terms
// and nothing else. A surface's occlusion of the environment says nothing about
// whether the sun reaches it, and the sun has a shadow map that answers exactly
// that (brief, Decision 13).

#define LUAUG_UNIFORMS_SSAO
#include "luaug_pbr.hlsli"
#include "luaug_fullscreen.hlsli"

Texture2D<float> DepthTexture : register(t0, space2);
SamplerState DepthSampler : register(s0, space2);

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

static const int LuaugSsaoSamples = 16;

// The depth buffer holds `f * (d - n) / ((f - n) * d)`, which is what
// `core::perspective` produces for a [0, 1] range. This is that, inverted.
float linearDepthOf(float deviceDepth)
{
    const float near = SsaoProjection.z;
    const float far = SsaoProjection.w;
    return (near * far) / max(far - deviceDepth * (far - near), 1e-6f);
}

// Screen position and depth back to a view-space point. The V flip is here
// because a texture's V runs down while normalised device Y runs up.
float3 viewPositionOf(float2 uv, float linearDepth)
{
    const float x = (uv.x * 2.0f - 1.0f) * SsaoProjection.x * linearDepth;
    const float y = (1.0f - uv.y * 2.0f) * SsaoProjection.y * linearDepth;
    return float3(x, y, -linearDepth);
}

// Interleaved gradient noise: a per-pixel rotation that is a pure function of
// the pixel, so it is stable frame to frame and identical on every machine --
// which a golden image needs and a random one would not give.
float interleavedGradientNoise(float2 pixel)
{
    return frac(52.9829189f * frac(dot(pixel, float2(0.06711056f, 0.00583715f))));
}

float4 FragmentMain(Interpolants input) : SV_Target0
{
    const float deviceDepth = DepthTexture.SampleLevel(DepthSampler, input.Uv, 0.0f);
    // The far plane: sky, and nothing occludes sky.
    if (deviceDepth >= 1.0f)
    {
        return float4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    const float linearDepth = linearDepthOf(deviceDepth);
    const float3 position = viewPositionOf(input.Uv, linearDepth);

    // Derivatives of the reconstructed position give the plane the surface lies
    // in. The facing test is what makes the winding irrelevant: whichever way
    // the cross product came out, the normal must point back at the eye.
    float3 normal = normalize(cross(ddx(position), ddy(position)));
    if (dot(normal, -position) < 0.0f)
    {
        normal = -normal;
    }

    const float3 tangent =
        normalize(abs(normal.z) < 0.99f ? cross(float3(0.0f, 0.0f, 1.0f), normal) : float3(1.0f, 0.0f, 0.0f));
    const float3 bitangent = cross(normal, tangent);

    const float rotation = interleavedGradientNoise(input.Position.xy) * 6.28318530718f;
    const float radius = SsaoParams.x;
    const float bias = SsaoParams.y;

    float occlusion = 0.0f;
    [unroll]
    for (int i = 0; i < LuaugSsaoSamples; ++i)
    {
        // A spiral rather than a stored kernel: the golden angle spreads
        // successive samples as far apart as an irrational turn allows, and the
        // square root keeps them uniform in area rather than clumped at the
        // centre.
        const float fraction = (float(i) + 0.5f) / float(LuaugSsaoSamples);
        const float angle = float(i) * 2.39996322973f + rotation;
        const float distance = radius * sqrt(fraction);

        // The disc lies in the surface's own plane and is then lifted along the
        // normal, which is what makes it a hemisphere rather than a sphere -- a
        // sphere kernel occludes every flat surface by half.
        const float3 offset =
            (tangent * cos(angle) + bitangent * sin(angle)) * distance + normal * (distance * 0.5f);
        const float3 samplePoint = position + offset;

        // Projected back to the screen by hand: this pass has the two tangents
        // of the field of view and a depth, which is everything a projection
        // matrix would have given it.
        const float sampleDepth = -samplePoint.z;
        if (sampleDepth <= 0.0f)
        {
            continue;
        }
        float2 sampleUv;
        sampleUv.x = (samplePoint.x / (SsaoProjection.x * sampleDepth)) * 0.5f + 0.5f;
        sampleUv.y = 0.5f - (samplePoint.y / (SsaoProjection.y * sampleDepth)) * 0.5f;
        if (any(sampleUv < 0.0f) || any(sampleUv > 1.0f))
        {
            continue;
        }

        const float occluderDepth = linearDepthOf(DepthTexture.SampleLevel(DepthSampler, sampleUv, 0.0f));

        // The range check is what stops a distant wall from occluding a near
        // floor: an occluder further away than the radius is a different
        // surface, not a fold in this one.
        const float difference = sampleDepth - occluderDepth;
        const float rangeCheck = smoothstep(0.0f, 1.0f, radius / max(abs(linearDepth - occluderDepth), 1e-4f));
        occlusion += (difference > bias ? 1.0f : 0.0f) * rangeCheck;
    }

    const float visibility = saturate(1.0f - (occlusion / float(LuaugSsaoSamples)) * SsaoParams.z);
    return float4(visibility, visibility, visibility, 1.0f);
}
