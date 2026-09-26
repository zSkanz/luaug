// The scene's depth as a distance, for blended surface shaders (ADR 0091).
//
// A blended surface reads how far away the opaque surface behind it is -- the
// line where water meets a rock is where the two distances meet -- and it
// cannot read the depth buffer itself, which is bound to the very pass it draws
// in. So after the opaque surfaces, and only in a frame with a blended surface
// shader, this turns the depth buffer into distances in front of the camera
// (`SurfaceInputs.SceneDepth`) in a texture of its own.

#include "luaug_fullscreen.hlsli"

Texture2D<float> SceneDepth : register(t0, space2);
SamplerState SceneDepthSampler : register(s0, space2);

cbuffer SceneDepthUniforms : register(b0, space3)
{
    column_major float4x4 InverseProjection;
};

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

float FragmentMain(Interpolants input) : SV_Target0
{
    const float depth = SceneDepth.SampleLevel(SceneDepthSampler, input.Uv, 0.0f);
    const float2 ndc = float2(input.Uv.x * 2.0f - 1.0f, 1.0f - input.Uv.y * 2.0f);
    const float4 view = mul(InverseProjection, float4(ndc, depth, 1.0f));
    // The camera looks down -z; a distance in front of it is positive.
    return -view.z / view.w;
}
