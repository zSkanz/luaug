// The forward pass's fragment stage, shared by `pbr.hlsl` and `pbr_skinned.hlsl`.
//
// **It was a copy until M7.5.** M6's note in `pbr_skinned.hlsl` said the right
// thing -- the fragment stage "is shared and is `pbr.hlsl`'s, compiled again"
// -- and then said it in a second file with the whole function pasted into it.
// That held while the function was one screen. This milestone gives it image-
// based lighting, cascades, a cluster lookup and an occlusion term, and two
// copies of that is two shaders that will disagree on the day one of them is
// edited alone.
//
// So the texture slots, the interpolants and the shading live here, and each
// entry file supplies only what actually differs: its vertex INPUT layout and
// its vertex stage. That difference is real and is why there are still two
// pipelines (M6 brief, Decision 11) -- one shader with a branch would make every
// static mesh in every world carry joint and weight attributes it never reads.
//
// **Every one of the ten fragment texture slots must always have something
// bound.** The material's `TextureFlags` are multipliers rather than branches
// (`shader_types.h` says why), so the sample happens whether the material has
// that texture or not, and a slot the renderer leaves empty is an unbound
// descriptor read. A 1x1 default is the whole fix.
#ifndef LUAUG_FORWARD_HLSLI
#define LUAUG_FORWARD_HLSLI

#include "luaug_brdf.hlsli"

// Slot order is `MaterialDef`'s own field order -- base colour, normal,
// metallic-roughness, emissive -- which is also `TextureFlags`' xyzw order, with
// the shadow map and then the two image-based-lighting tables appended.
Texture2D BaseColorTexture : register(t0, space2);
SamplerState BaseColorSampler : register(s0, space2);
Texture2D NormalTexture : register(t1, space2);
SamplerState NormalSampler : register(s1, space2);
Texture2D MetallicRoughnessTexture : register(t2, space2);
SamplerState MetallicRoughnessSampler : register(s2, space2);
Texture2D EmissiveTexture : register(t3, space2);
SamplerState EmissiveSampler : register(s3, space2);
// The four cascades, as a 2x2 atlas (shadow.h says why). Sampled with `Gather`
// and an explicit bilinear comparison rather than through a comparison sampler,
// so the sampler here is a plain point one.
Texture2D<float> ShadowMap : register(t4, space2);
SamplerState ShadowSampler : register(s4, space2);
// The prefiltered environment, octahedral, one mip per roughness step. See
// `luaug_brdf.hlsli`'s image-based-lighting section and
// `engine/render/src/environment.cpp`.
Texture2D EnvironmentMap : register(t5, space2);
SamplerState EnvironmentSampler : register(s5, space2);
// The split-sum BRDF table: N·V on u, roughness on v, Fresnel scale in R and
// bias in G. Independent of the environment, so it is baked once.
Texture2D BrdfLut : register(t6, space2);
SamplerState BrdfSampler : register(s6, space2);
// The clustered light tables (clusters.h). Three textures because the frozen
// RHI has no storage buffer and a fragment shader's only bulk-data route is a
// sampled texture; point-sampled at texel centres, which is an exact fetch.
//
// The samplers are declared and used rather than skipped: SDL_GPU's binding
// model is texture-and-sampler PAIRS, and a shader that declared a texture
// alone would report a resource count the device then rejects.
Texture2D<float> ClusterGridTexture : register(t7, space2);
SamplerState ClusterGridSampler : register(s7, space2);
Texture2D<float> LightIndexTexture : register(t8, space2);
SamplerState LightIndexSampler : register(s8, space2);
Texture2D LightDataTexture : register(t9, space2);
SamplerState LightDataSampler : register(s9, space2);

// An exact texel fetch through a point sampler.
float clusterFetch1(Texture2D<float> texture, SamplerState pointSampler, uint x, uint y, uint width, uint height)
{
    const float2 uv = (float2(float(x), float(y)) + 0.5f) / float2(float(width), float(height));
    return texture.SampleLevel(pointSampler, uv, 0.0f).r;
}

float4 clusterFetch4(Texture2D texture, SamplerState pointSampler, uint x, uint y, uint width, uint height)
{
    const float2 uv = (float2(float(x), float(y)) + 0.5f) / float2(float(width), float(height));
    return texture.SampleLevel(pointSampler, uv, 0.0f);
}

// Every light that reaches this fragment's cluster, and no others. That bound --
// not the frame's total -- is what a fragment pays for, and it is the whole
// point of clustering: M4 iterated eight unculled lights on every pixel of every
// draw whether they reached it or not.
float3 evaluateClusteredLights(Surface surface, float2 pixel, float viewDepth)
{
    if (LightCountUnused.x <= 0.0f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    const uint tileX = min((uint)(pixel.x * ViewportParams.z * float(LUAUG_CLUSTER_TILES_X)),
                           (uint)(LUAUG_CLUSTER_TILES_X - 1));
    const uint tileY = min((uint)(pixel.y * ViewportParams.w * float(LUAUG_CLUSTER_TILES_Y)),
                           (uint)(LUAUG_CLUSTER_TILES_Y - 1));
    // Olsson and Assarsson's exponential slicing, and the logarithm is the whole
    // reason one grid serves a near plane at 0.1 and a far one in the hundreds.
    const float rawSlice = log2(max(viewDepth, 1e-6f)) * ClusterParams.x + ClusterParams.y;
    const uint slice = (uint)clamp(rawSlice, 0.0f, float(LUAUG_CLUSTER_SLICES - 1));

    const float packed = clusterFetch1(ClusterGridTexture, ClusterGridSampler, slice * LUAUG_CLUSTER_TILES_X + tileX,
                                       tileY, LUAUG_CLUSTER_GRID_WIDTH, LUAUG_CLUSTER_GRID_HEIGHT);
    const uint offset = (uint)(packed / LUAUG_CLUSTER_OFFSET_SHIFT);
    const uint count = min((uint)(packed - float(offset) * LUAUG_CLUSTER_OFFSET_SHIFT),
                           (uint)LUAUG_MAX_LIGHTS_PER_CLUSTER);

    float3 color = float3(0.0f, 0.0f, 0.0f);
    for (uint i = 0u; i < count; ++i)
    {
        const uint entry = offset + i;
        const float encoded = clusterFetch1(LightIndexTexture, LightIndexSampler, entry % LUAUG_LIGHT_INDEX_WIDTH,
                                            entry / LUAUG_LIGHT_INDEX_WIDTH, LUAUG_LIGHT_INDEX_WIDTH,
                                            LUAUG_LIGHT_INDEX_HEIGHT);
        const uint lightIndex = (uint)(encoded + 0.5f);

        // `GpuLight`'s own three rows, rebuilt from three texels. The struct is
        // unchanged; only where its bytes come from is.
        GpuLight light;
        light.PositionRange =
            clusterFetch4(LightDataTexture, LightDataSampler, 0u, lightIndex, 3u, LUAUG_MAX_CLUSTERED_LIGHTS);
        light.Color =
            clusterFetch4(LightDataTexture, LightDataSampler, 1u, lightIndex, 3u, LUAUG_MAX_CLUSTERED_LIGHTS);
        light.DirectionCosAngle =
            clusterFetch4(LightDataTexture, LightDataSampler, 2u, lightIndex, 3u, LUAUG_MAX_CLUSTERED_LIGHTS);

        color += evaluatePunctualLight(surface, light);
    }
    return color;
}

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
    // Distance along the camera's forward axis, in metres, which is what the
    // cascade splits are stated in. Carried rather than derived from
    // `SV_Position`, which would need the projection this stage is not given --
    // and for a perspective matrix it is exactly the clip w.
    float ViewDepth : TEXCOORD5;
    float4 Position : SV_Position;
};

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

// Linear HDR into an `Rgba16Float` target. Nothing here tonemaps and nothing
// here encodes sRGB -- `tonemap.hlsl` does both, once, on the way out.
float4 shadeForward(Interpolants input)
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
    // dot against a normal needs no negation. Its COLOUR is new at M7.5: the
    // light it casts warms as it approaches the horizon, derived from its
    // elevation rather than authored (environment.h).
    const float3 sunDirection = normalize(SunDirectionBrightness.xyz);
    const float sunNol = saturate(dot(normal, sunDirection));
    const float shadow =
        sampleSunShadow(ShadowMap, ShadowSampler, input.ShadingPosition, normal, sunNol, input.ViewDepth);
    const float3 sunRadiance = SunColorUnused.rgb * (SunDirectionBrightness.w * shadow);
    float3 color = shadeDirect(surface, sunDirection, sunRadiance);

    color += evaluateClusteredLights(surface, input.Position.xy, input.ViewDepth);

    // The environment, on both lobes, and this is what M7.5 exists for: until
    // now `Lighting.Ambient` was applied flat to both, which `pbr.hlsl`'s own
    // comment called "the degenerate case of the split-sum approximation where
    // the environment is one colour". It is no longer one colour -- it is the
    // sky, prefiltered by roughness, so a metal reflects the hour the script
    // set instead of reflecting nothing.
    //
    // `Ambient` is ADDED to the irradiance rather than replaced by it, so the
    // property keeps meaning what it documents: light reaching every surface
    // from every direction, a stand-in for bounced light there is still none of.
    color += evaluateEnvironment(surface, EnvironmentMap, EnvironmentSampler, BrdfLut, BrdfSampler, IrradianceSh,
                                 EnvironmentParams.x, EnvironmentParams.y, 1.0f);
    // And `Ambient` on the DIFFUSE lobe only, which is a change of side rather
    // than a change of mind. M4's comment argued for putting it on both, and the
    // argument was right at the time: "a mirror in a uniformly lit white room is
    // white, not black", so a specular lobe with no environment behind it had to
    // get something or every metal rendered black. There is an environment
    // behind it now, and it answers that case properly -- adding a flat term on
    // top of a prefiltered one is counting the same light twice, and it shows up
    // as metal that cannot be made dark.
    color += Ambient.rgb * surface.DiffuseColor;

    float3 emissive = EmissiveFactor.rgb;
    emissive *= lerp(float3(1.0f, 1.0f, 1.0f), EmissiveTexture.Sample(EmissiveSampler, input.Uv).rgb, TextureFlags.w);
    color += emissive;

    // The eye is this space's origin, so the distance to it is the length of the
    // shading position. Fog is applied to emissive too: something glowing behind
    // fog is still behind fog.
    color = applyFog(color, FogColor.rgb, FogRange, length(input.ShadingPosition));

    return float4(color, baseColor.a);
}

#endif // LUAUG_FORWARD_HLSLI
