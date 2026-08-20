// The forward pass: metallic-roughness PBR, one shadowed directional sun, up to
// `kMaxForwardLights` punctual lights, then distance fog. It is the default
// shader every material names unless it names another (`MaterialDef::shader`).
//
// **One shader, two pipelines.** The opaque and the blended passes compile the
// same code and differ only in their pipeline state -- depth-write and blending
// -- because a fragment's colour does not depend on which pass drew it. What
// differs is the order, and `extract` owns that (M4's third design constraint).
//
// It writes **linear HDR** into an `Rgba16Float` target. Nothing here tonemaps
// and nothing here encodes sRGB -- `tonemap.hlsl` does both, once, on the way to
// the swapchain.
//
// Register spaces are fixed per stage by SDL_GPU and a shader that ignores them
// binds nothing (SDL_gpu.h:2699-2730). Vertex inputs use TEXCOORDn semantics
// because that is what SDL_shadercross maps to SPIR-V input locations, and the
// declaration order below is the location order.
//
// The vertex layout is `asset::Vertex` (engine/asset/include/luaug/asset/
// model.h): 48 bytes interleaved, position float3 / normal float3 / tangent
// float4 / uv float2, with `tangent.w` the bitangent handedness sign that glTF
// defines.
//
// **Every one of the five fragment texture slots must always have something
// bound.** The material's `TextureFlags` are multipliers rather than branches
// (`shader_types.h` says why), so the sample happens whether the material has
// that texture or not, and a slot the renderer leaves empty is an unbound
// descriptor read. A 1x1 default is the whole fix.
//
// Slot order is `MaterialDef`'s own field order -- base colour, normal,
// metallic-roughness, emissive -- which is also `TextureFlags`' xyzw order, with
// the sun's shadow map appended.

#define LUAUG_UNIFORMS_OBJECT
#define LUAUG_UNIFORMS_FRAME
#define LUAUG_UNIFORMS_MATERIAL
#include "luaug_brdf.hlsli"

Texture2D BaseColorTexture : register(t0, space2);
SamplerState BaseColorSampler : register(s0, space2);
Texture2D NormalTexture : register(t1, space2);
SamplerState NormalSampler : register(s1, space2);
Texture2D MetallicRoughnessTexture : register(t2, space2);
SamplerState MetallicRoughnessSampler : register(s2, space2);
Texture2D EmissiveTexture : register(t3, space2);
SamplerState EmissiveSampler : register(s3, space2);
Texture2D<float> ShadowMap : register(t4, space2);
SamplerState ShadowSampler : register(s4, space2);

struct VertexInput
{
    float3 Position : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float4 Tangent : TEXCOORD2;
    float2 Uv : TEXCOORD3;
};

struct Interpolants
{
    // Camera-relative: `Model` carries the camera-relative translation, which is
    // what makes the eye the origin of this space and the lights' documented
    // camera-relative positions directly comparable.
    float3 ShadingPosition : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float4 Tangent : TEXCOORD2;
    float2 Uv : TEXCOORD3;
    // `1 - BasePart.Transparency`, carried down because `GpuObjectUniforms` is
    // a vertex-stage block (b0 space1) and the fragment stage cannot see it.
    // Constant across a triangle, so the interpolation is a formality.
    float InstanceAlpha : TEXCOORD4;
    float4 Position : SV_Position;
};

Interpolants VertexMain(VertexInput input)
{
    Interpolants output;

    const float4 shadingPosition = mul(Model, float4(input.Position, 1.0f));
    output.ShadingPosition = shadingPosition.xyz;
    output.Position = mul(ViewProjection, shadingPosition);

    // The cofactor matrix, so a non-uniform scale tilts the normal correctly
    // instead of shearing it. Renormalised in the fragment stage, after
    // interpolation has shortened it.
    output.Normal = mul((float3x3)NormalMatrix, input.Normal);
    // A tangent is a direction along the surface, not a covector, so it rides on
    // the model matrix rather than on the cofactor one. `w` is a sign and is
    // carried through untouched.
    output.Tangent = float4(mul((float3x3)Model, input.Tangent.xyz), input.Tangent.w);
    output.Uv = input.Uv;
    output.InstanceAlpha = InstanceAlphaUnused.x;

    return output;
}

// The tangent frame, guarding the case glTF permits and the importer leaves
// zeroed: a mesh with no tangents at all. `normalize` of a zero vector is a NaN
// that spreads across the whole pixel, and with a flat normal map any frame
// gives the same answer, so an arbitrary perpendicular is the correct fallback.
float3x3 tangentFrame(float3 normal, float4 tangent)
{
    const float tangentLength = length(tangent.xyz);
    const float3 arbitrary =
        abs(normal.y) < 0.999f ? normalize(cross(float3(0.0f, 1.0f, 0.0f), normal)) : float3(1.0f, 0.0f, 0.0f);
    const float3 t = tangentLength > 1e-5f ? tangent.xyz / tangentLength : arbitrary;
    // glTF's own definition: the bitangent is cross(normal, tangent) times the
    // stored handedness sign.
    const float3 b = cross(normal, t) * tangent.w;
    return float3x3(t, b, normal);
}

float4 FragmentMain(Interpolants input) : SV_Target0
{
    const float metallicFactor = MetallicRoughnessNormalCutoff.x;
    const float roughnessFactor = MetallicRoughnessNormalCutoff.y;
    const float normalScale = MetallicRoughnessNormalCutoff.z;
    const float alphaCutoff = MetallicRoughnessNormalCutoff.w;

    float4 baseColor = BaseColorFactor;
    const float4 sampledBase = BaseColorTexture.Sample(BaseColorSampler, input.Uv);
    baseColor *= lerp(float4(1.0f, 1.0f, 1.0f, 1.0f), sampledBase, TextureFlags.x);
    // The two sources of transparency multiply: a glTF material can be
    // see-through on its own, and a script can make an otherwise opaque mesh
    // see-through with `BasePart.Transparency`. Honouring only one leaves a case
    // that renders wrong, and the alpha is what `extract` sorted the draw by, so
    // the two have to agree on this product.
    baseColor.a *= input.InstanceAlpha;

    // The cutoff doubles as the alpha mode: the renderer writes 0 for Opaque and
    // Blend materials and the real cutoff for Mask ones, because the frozen
    // contract has no field for `AlphaMode`. A cutoff of 0 can never discard,
    // since alpha is never negative, so one `clip` serves all three modes.
    clip(baseColor.a - alphaCutoff);

    // glTF packs occlusion, roughness and metalness into R, G and B of one
    // image. Occlusion is deliberately not read: the spec leaves R undefined
    // unless the same texture is also referenced as the occlusion map, and
    // `MaterialDef` has no occlusion strength to gate it with, so reading it
    // would darken correct materials at random.
    const float3 sampledMetallicRoughness = MetallicRoughnessTexture.Sample(MetallicRoughnessSampler, input.Uv).rgb;
    const float roughness = roughnessFactor * lerp(1.0f, sampledMetallicRoughness.g, TextureFlags.z);
    const float metallic = metallicFactor * lerp(1.0f, sampledMetallicRoughness.b, TextureFlags.z);

    const float3 geometricNormal = normalize(input.Normal);
    const float3x3 frame = tangentFrame(geometricNormal, input.Tangent);
    float3 tangentNormal = NormalTexture.Sample(NormalSampler, input.Uv).xyz * 2.0f - 1.0f;
    tangentNormal.xy *= normalScale;
    const float3 mappedNormal = normalize(mul(normalize(tangentNormal), frame));
    const float3 normal = normalize(lerp(geometricNormal, mappedNormal, TextureFlags.y));

    const Surface surface = makeSurface(input.ShadingPosition, normal, baseColor.rgb, metallic, roughness);

    // The sun. Its direction already points from the world towards it, so the
    // dot against a normal needs no negation.
    const float3 sunDirection = normalize(SunDirectionBrightness.xyz);
    const float sunNol = saturate(dot(normal, sunDirection));
    const float shadow = sampleSunShadow(ShadowMap, ShadowSampler, SunViewProjection, input.ShadingPosition, sunNol);
    // The sun has a brightness but no colour: `GpuFrameUniforms` carries only
    // `SunDirectionBrightness`, so the light it casts is white even when the
    // sky's disc is tinted by `GpuSkyUniforms::SunColor`.
    const float sunBrightness = SunDirectionBrightness.w * shadow;
    float3 color = shadeDirect(surface, sunDirection, float3(sunBrightness, sunBrightness, sunBrightness));

    // Bounded by the live count rather than by the array size, so an unlit scene
    // costs nothing. `min` because a count larger than the array would read past
    // the block.
    const uint lightCount = min((uint)LightCountUnused.x, (uint)LUAUG_MAX_FORWARD_LIGHTS);
    for (uint i = 0; i < lightCount; ++i)
    {
        color += evaluatePunctualLight(surface, Lights[i]);
    }

    // Flat irradiance on BOTH lobes, and the specular half is not decoration.
    //
    // The first version of this shader put ambient on the diffuse lobe alone,
    // on the argument that an ambient specular term without a reflection probe
    // is a constant highlight pretending to be a reflection. The counter-example
    // settles it: a mirror in a uniformly lit white room is white, not black.
    // `Lighting.Ambient` is documented as light reaching every surface from
    // every direction, and a uniform environment does reach a specular lobe --
    // so diffuse-only is the physically WRONG simplification, not the
    // conservative one.
    //
    // It also has a visible consequence rather than a theoretical one: a metal
    // has no diffuse lobe at all, so diffuse-only ambient renders every metal in
    // every scene pure black wherever a punctual highlight does not land. That
    // is correct-looking enough to ship and wrong enough to be reported as a
    // renderer bug forever.
    //
    // This is still not IBL (M4 brief, NOT-in-scope 6): there is no probe, no
    // prefiltered mip chain and no parallax. It is the degenerate case of the
    // split-sum approximation where the environment is one colour, which is
    // exactly what `Lighting.Ambient` says it is. `1 - roughness` stands in for
    // the horizon-occlusion term a real environment BRDF would supply, so a
    // rough metal dims towards its diffuse-less black rather than glowing flat.
    const float ambientSpecular = 1.0f - sqrt(surface.Alpha);
    color += Ambient.rgb * (surface.DiffuseColor + surface.SpecularF0 * ambientSpecular);

    float3 emissive = EmissiveFactor.rgb;
    emissive *= lerp(float3(1.0f, 1.0f, 1.0f), EmissiveTexture.Sample(EmissiveSampler, input.Uv).rgb, TextureFlags.w);
    color += emissive;

    // The eye is this space's origin, so the distance to it is the length of the
    // shading position. Fog is applied to emissive too: something glowing behind
    // fog is still behind fog.
    color = applyFog(color, FogColor.rgb, FogRange, length(input.ShadingPosition));

    return float4(color, baseColor.a);
}
