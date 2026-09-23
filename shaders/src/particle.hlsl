// Particles (F2): one camera-facing square per instance, shaped in the shader.
//
// **No vertex buffer at all.** Each particle is an instance -- position and
// size, colour, and how it blends -- and its six corners come from the vertex
// index, so ten thousand particles are one instanced draw of six vertices.
//
// **One pipeline for both kinds of particle**, blended premultiplied: the
// colour is multiplied by its opacity here, and the alpha written is the
// opacity times what is NOT emission. At emission 0 that is an ordinary blend;
// at 1 the alpha is zero and the colour simply adds -- fire and sparks -- and
// between is both, with no second pipeline and no sort between the two kinds.
//
// **Soft, and tested against the scene by hand.** The pass has no depth
// attachment: it reads the depth the opaque surfaces wrote as a texture, which
// a pass with it attached cannot (decals are drawn the same way). A fragment
// behind the scene is dropped, which is the depth test; one in front of it
// fades out over the last stretch before the surface it would cross, which is
// what stops a puff of smoke drawing a hard line where it meets the ground.
// The stretch is the particle's own half-size, up to a metre: a spark stays
// crisp and a cloud fades over the cloud's depth.

cbuffer GpuParticleUniforms : register(b0, space1)
{
    float4x4 ViewProjection;
    // The camera's own right and up, in the camera-relative world space every
    // position here is in: a particle faces the camera by being built on them.
    float4 CameraRight;
    float4 CameraUp;
};

cbuffer GpuParticleLighting : register(b0, space3)
{
    // What lights a particle that is not emitting: the ambient, and the sun
    // arriving from any direction -- a particle has no normal worth the name.
    float4 Ambient;
    float4 SunLight;
    float4 ParticleFogColor;
    // x: fog start, z: 1 / (end - start), zero when fog is off.
    float4 ParticleFogRange;
    // x near plane, y far plane, zw one over the target's size in pixels.
    float4 ParticleDepth;
};

// The opaque scene's depth, as the prepass and the forward pass left it.
Texture2D SceneDepth : register(t0, space2);
SamplerState SceneDepthSampler : register(s0, space2);

struct VertexInput
{
    // xyz camera-relative position, w width in metres.
    float4 PositionSize : TEXCOORD0;
    // Linear colour times brightness, and opacity.
    float4 Color : TEXCOORD1;
    // x emission, y shape.
    float4 Params : TEXCOORD2;
    uint Vertex : SV_VertexID;
};

struct Interpolants
{
    float4 Position : SV_Position;
    float4 Color : TEXCOORD0;
    float2 Corner : TEXCOORD1;
    float2 Params : TEXCOORD2;
    float Distance : TEXCOORD3;
    // Its distance along the view axis and its half-size, for the fade.
    float ViewDepth : TEXCOORD4;
    float HalfSize : TEXCOORD5;
};

Interpolants VertexMain(VertexInput input)
{
    // Two triangles, in the order a square is cut: 0 1 2, 0 2 3.
    const float2 corners[6] = {float2(-1.0f, -1.0f), float2(1.0f, -1.0f), float2(1.0f, 1.0f),
                               float2(-1.0f, -1.0f), float2(1.0f, 1.0f), float2(-1.0f, 1.0f)};
    const float2 corner = corners[input.Vertex % 6u];
    const float halfSize = input.PositionSize.w * 0.5f;
    const float3 position =
        input.PositionSize.xyz + (CameraRight.xyz * corner.x + CameraUp.xyz * corner.y) * halfSize;

    Interpolants output;
    output.Position = mul(ViewProjection, float4(position, 1.0f));
    output.Color = input.Color;
    output.Corner = corner;
    output.Params = input.Params.xy;
    output.Distance = length(input.PositionSize.xyz);
    output.ViewDepth = output.Position.w;
    output.HalfSize = halfSize;
    return output;
}

float4 FragmentMain(Interpolants input) : SV_Target0
{
    const float radius = length(input.Corner);
    const uint shape = uint(input.Params.y + 0.5f);
    float coverage = 1.0f;
    if (shape == 0u) {
        // Soft: a falloff to nothing at the edge, squared so the centre is
        // dense and the rim is air.
        const float fall = saturate(1.0f - radius);
        coverage = fall * fall;
    }
    else if (shape == 1u) {
        // Disc: a crisp edge, anti-aliased across one pixel's width.
        const float edge = max(fwidth(radius), 1e-4f);
        coverage = 1.0f - smoothstep(1.0f - edge, 1.0f, radius);
    }

    // The scene's depth at this pixel, linear, against the particle's own.
    const float near = ParticleDepth.x;
    const float far = ParticleDepth.y;
    const float device = SceneDepth.SampleLevel(SceneDepthSampler, input.Position.xy * ParticleDepth.zw, 0.0f).r;
    const float scene = (near * far) / max(far - device * (far - near), 1e-6f);
    const float soft = saturate((scene - input.ViewDepth) / clamp(input.HalfSize, 1e-3f, 1.0f));

    const float alpha = input.Color.a * coverage * soft;
    if (alpha <= 0.002f)
        discard;

    const float emission = saturate(input.Params.x);
    const float fog = saturate((input.Distance - ParticleFogRange.x) * ParticleFogRange.z);
    // Blended particles are lit and fogged like a surface; emitting ones carry
    // their own light and simply fade into the fog.
    const float3 lit = input.Color.rgb * (Ambient.rgb + SunLight.rgb);
    const float3 blended = lerp(lit, ParticleFogColor.rgb, fog);
    const float3 added = input.Color.rgb * (1.0f - fog);
    const float3 color = lerp(blended, added, emission);

    return float4(color * alpha, alpha * (1.0f - emission));
}
