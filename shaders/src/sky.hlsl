// The sky: a horizon-to-zenith gradient with a sun disc, drawn as one
// fullscreen triangle before any geometry.
//
// It writes linear HDR into the same `Rgba16Float` target the forward pass uses,
// so `tonemap.hlsl` resolves sky and geometry together and the sun disc can be
// brighter than 1.0 without clipping on the way out.
//
// Drawn first, with depth write off, which is why the depth it emits does not
// matter much -- it emits 1.0, the far plane in the engine's [0, 1] convention,
// so that a pass which ever does enable the depth test still puts the sky behind
// everything rather than in front of it.
//
// The vertex stage reads no uniforms and no vertex buffer; `GpuSkyUniforms` is a
// fragment-stage block because turning a screen position back into a direction
// is a per-pixel job.

#define LUAUG_UNIFORMS_SKY
#include "luaug_pbr.hlsli"
#include "luaug_fullscreen.hlsli"

struct Interpolants
{
    // Normalised device coordinates, carried rather than recomputed from
    // SV_Position, which would need the viewport size this shader is not given.
    float2 Ndc : TEXCOORD0;
    float4 Position : SV_Position;
};

Interpolants VertexMain(uint vertexId : SV_VertexID)
{
    Interpolants output;
    float2 uv = float2(0.0f, 0.0f);
    fullscreenTriangle(vertexId, 1.0f, output.Position, uv);
    output.Ndc = output.Position.xy;
    return output;
}

float4 FragmentMain(Interpolants input) : SV_Target0
{
    // Unprojected at both ends of the depth range and differenced, rather than
    // unprojecting the far plane alone and normalising it. The difference of two
    // points is a direction whatever the shading space's origin is, which keeps
    // this correct if the camera ever stops being at the origin of it.
    const float4 nearPoint = mul(InverseViewProjection, float4(input.Ndc, 0.0f, 1.0f));
    const float4 farPoint = mul(InverseViewProjection, float4(input.Ndc, 1.0f, 1.0f));
    const float3 direction = normalize(farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w);

    // Gradient by height, biased towards the horizon: a linear blend in `y`
    // spends most of the visible sky on the zenith colour and leaves the horizon
    // as a thin band, which reads as a hard line rather than as sky.
    const float height = saturate(direction.y);
    const float3 gradient = lerp(HorizonColor.rgb, ZenithColor.rgb, pow(height, 0.45f));

    // Below the horizon the gradient is mirrored and darkened rather than left
    // undefined: there is no ground plane in this pass, and a camera tilted down
    // must see something that is not the horizon colour smeared flat.
    const float below = saturate(-direction.y);
    const float3 sky = lerp(gradient, HorizonColor.rgb * 0.35f, pow(below, 0.5f));

    const float3 sunDirection = normalize(SunDirectionSize.xyz);
    const float cosAngle = dot(direction, sunDirection);
    // `w` is the disc's angular radius in radians. The edge is softened over the
    // outer tenth of it, because a hard disc edge aliases into a stair at every
    // resolution and no amount of MSAA touches it -- this is a fragment value,
    // not a geometric one.
    const float cosOuter = cos(SunDirectionSize.w);
    const float cosInner = cos(SunDirectionSize.w * 0.9f);
    const float disc = smoothstep(cosOuter, cosInner, cosAngle);

    // A wide, cheap forward-scattering glow around the disc. Not a scattering
    // model: the sky is a gradient from four colours, and an atmosphere solver
    // behind it would be answering a question nothing in this milestone asks.
    const float glow = pow(saturate(cosAngle), 64.0f) * 0.25f;

    // `SunColor.w` is how much brighter the disc is than the sky around it, and
    // at M7.5 it stopped being decoration: the same sky is prefiltered into the
    // environment every surface reflects, and a disc at radiance 1 reflects as a
    // pale smudge rather than as a highlight. The glow keeps the unscaled
    // colour, because a glow that bright would be a second sun.
    return float4(sky + SunColor.rgb * (disc * SunColor.w + glow), 1.0f);
}
