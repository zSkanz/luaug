// The forward pass for a SKINNED mesh: `pbr.hlsl`'s shading, reached through a
// linear blend of up to four joints per vertex (roadmap M6, "minimal skeletal
// animation").
//
// **A copy of `pbr.hlsl`'s vertex stage rather than a `#define` inside it.** The
// two differ in their vertex INPUT layout, which is part of the pipeline
// description and not of the code -- one shader with a branch would still have
// to declare the joint and weight attributes, and every static mesh in every
// world would then carry two more vertex attributes it never reads (M6 brief,
// Decision 11). The fragment stage is shared and is `pbr.hlsl`'s, compiled
// again: it reads only interpolants, so there is nothing in it to skin.
//
// The extra vertex buffer is `asset::SkinVertex` (engine/asset/include/luaug/
// asset/model.h) at slot 1: four `uint16` joint indices then four `float`
// weights, 24 bytes, parallel to the position stream by construction.
//
// Register spaces are fixed per stage by SDL_GPU and a shader that ignores them
// binds nothing (SDL_gpu.h:2699-2730).

#define LUAUG_UNIFORMS_OBJECT
#define LUAUG_UNIFORMS_FRAME
#define LUAUG_UNIFORMS_MATERIAL
#define LUAUG_UNIFORMS_SKIN
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
    // Slot 1, the skin stream. `uint4` rather than `uint16_t4`: the vertex
    // format widens the two 16-bit pairs on the way in, which every backend
    // does for free, and 16-bit shader types need a capability not every one of
    // them has.
    uint4 Joints : TEXCOORD4;
    float4 Weights : TEXCOORD5;
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

    // Model space to posed model space, then the object's own transform. Two
    // multiplies rather than one premultiplied palette: the palette is per
    // SKELETON and `Model` is per draw, so folding them would mean re-uploading
    // the whole palette for every draw that shares a rig.
    const float4x4 skin = skinMatrix(input.Joints, input.Weights);
    const float4 posed = mul(skin, float4(input.Position, 1.0f));
    const float4 shadingPosition = mul(Model, posed);
    output.ShadingPosition = shadingPosition.xyz;
    output.Position = mul(ViewProjection, shadingPosition);

    // The cofactor matrix, so a non-uniform scale tilts the normal correctly
    // instead of shearing it. Renormalised in the fragment stage, after
    // interpolation has shortened it.
    // The skin's rotation-scale block first, then the object's cofactor matrix.
    // Not the skin's own cofactor: a joint matrix is rigid plus whatever scale
    // the exporter baked into the inverse bind, and computing a 3x3 cofactor per
    // vertex to catch that is a cost the case does not justify. A rig with a
    // non-uniform joint scale shades slightly wrong, and that is written down
    // here rather than discovered.
    output.Normal = mul((float3x3)NormalMatrix, mul((float3x3)skin, input.Normal));
    // A tangent is a direction along the surface, not a covector, so it rides on
    // the model matrix rather than on the cofactor one. `w` is a sign and is
    // carried through untouched.
    output.Tangent = float4(mul((float3x3)Model, mul((float3x3)skin, input.Tangent.xyz)), input.Tangent.w);
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
