// `Atmosphere` (ADR 0096): the air between the camera and everything it sees,
// laid over the frame once the opaque world and the sky are in it.
//
// **One pass for the ground and the sky, so they meet.** Every pixel is a ray
// from the camera: to the surface the depth buffer holds, or -- where it holds
// the far plane -- out into the sky. The same integral of the same air along
// each gives how much of what is behind survives and how much of the air's own
// light replaces it, so a far hillside and the horizon above it fade into the
// same colour by the same rule, rather than by two rules tuned to agree.
//
// The air thins with height: density falls off exponentially above `Offset`
// and thickens below it, and that has a closed-form integral along a straight
// ray, so the whole pass is one depth read and a few exponentials a pixel.
// `Haze` adds thickness that grows towards the horizon.
//
// It writes by blending, `dst = air * (1 - T) + dst * T`, with the air's light
// in the colour and the surviving fraction T in alpha -- so it never reads the
// image it is changing, and the forward pass it interrupts resumes into the
// same target afterwards.

#define LUAUG_UNIFORMS_AIR
#include "luaug_look.hlsli"
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

float3 unproject(float2 ndc, float depth)
{
    const float4 homogeneous = mul(AirInverseViewProjection, float4(ndc, depth, 1.0f));
    return homogeneous.xyz / homogeneous.w;
}

float4 FragmentMain(Interpolants input) : SV_Target0
{
    const float2 ndc = float2(input.Uv.x * 2.0f - 1.0f, 1.0f - input.Uv.y * 2.0f);
    const float depth = DepthTexture.SampleLevel(DepthSampler, input.Uv, 0.0f);

    // The camera is the origin of the space these matrices work in, so a
    // point's position is also the ray to it.
    const float3 direction = normalize(unproject(ndc, 1.0f) - unproject(ndc, 0.0f));
    const float reach = depth < 1.0f ? length(unproject(ndc, depth)) : AirLight.w;

    // **Over the open sky, a share of the integral** (`AirGlare.w`): the sky's
    // gradient is already the look of the air above, and adding the whole of it
    // again turned a clear afternoon's blue to grey thirty degrees up. Towards
    // the horizon the integral is large enough that the share still buries it,
    // so the sky and the far ground still meet in the air's colour.
    const float share = depth < 1.0f ? 1.0f : AirGlare.w;
    const float transmitted = exp(-airOpticalDepth(direction.y, reach) * share);

    // The air's own light, and the lobe of it towards the sun.
    const float lobe = pow(saturate(dot(direction, AirSun.xyz)), AirSun.w);
    const float3 air = AirLight.rgb + AirGlare.rgb * lobe;
    return float4(air * (1.0f - transmitted), transmitted);
}
