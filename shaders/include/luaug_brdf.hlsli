// The metallic-roughness BRDF and the light, shadow and fog terms built on it.
//
// Split out of `luaug_pbr.hlsli` so that file stays what `shader_types.h` says
// it is -- the second copy of the uniform bytes -- while the maths that reads
// them lives somewhere a change cannot silently move an offset.
//
// **The shading space is camera-relative with the eye at the origin.** Nothing
// here takes a camera position because the frozen contract has no field for
// one: `GpuLight::PositionRange` is documented camera-relative, so the vertex
// stage's `Model` must be too, and the view vector is therefore `-position`.
// Fog distance is `length(position)` for the same reason.
//
// Everything is linear-light. sRGB decode happens on the way in, through the
// texture format (`Rgba8UnormSrgb` for base colour and emissive), and sRGB
// encode happens once at the very end in `tonemap.hlsl` -- never here.
#ifndef LUAUG_BRDF_HLSLI
#define LUAUG_BRDF_HLSLI

#include "luaug_pbr.hlsli"

static const float LuaugPi = 3.14159265358979323846f;

// Dielectrics reflect about 4% at normal incidence. glTF fixes this rather than
// exposing it, so a material cannot change it and neither can we.
static const float LuaugDielectricF0 = 0.04f;

// Keeps the reciprocals below off the divide-by-zero edge. Small enough that no
// visible term moves, large enough that a half-precision backend survives.
static const float LuaugEpsilon = 1e-4f;

// A perfectly smooth surface is a delta lobe no punctual light can ever hit, so
// roughness is clamped rather than allowed to reach zero. Below roughly 0.045
// GGX highlights start aliasing into single-pixel fireflies.
static const float LuaugMinRoughness = 0.045f;

// The surface, resolved once, so every light reads the same numbers.
struct Surface
{
    // Camera-relative, the space the lights are in.
    float3 Position;
    float3 Normal;
    // From the surface towards the eye.
    float3 View;
    // Base colour with the metallic lobe already removed.
    float3 DiffuseColor;
    // Reflectance at normal incidence.
    float3 SpecularF0;
    // Roughness squared, GGX's own parameter.
    float Alpha;
    float NoV;
};

Surface makeSurface(float3 position, float3 normal, float3 baseColor, float metallic, float roughness)
{
    Surface surface;
    surface.Position = position;
    surface.Normal = normal;
    // The eye is the origin of this space, so the view vector is the position
    // negated. A fragment exactly at the eye would normalize to a NaN, and a
    // fragment exactly at the eye is behind the near plane and never shaded.
    surface.View = normalize(-position);

    const float clampedRoughness = max(roughness, LuaugMinRoughness);
    surface.DiffuseColor = baseColor * (1.0f - metallic);
    surface.SpecularF0 = lerp(float3(LuaugDielectricF0, LuaugDielectricF0, LuaugDielectricF0), baseColor, metallic);
    surface.Alpha = clampedRoughness * clampedRoughness;
    surface.NoV = saturate(dot(normal, surface.View)) + LuaugEpsilon;
    return surface;
}

// Trowbridge-Reitz / GGX. The squared denominator is what gives the long tail
// that separates a GGX highlight from a Blinn-Phong one.
float distributionGgx(float noh, float alpha)
{
    const float a2 = alpha * alpha;
    const float d = noh * noh * (a2 - 1.0f) + 1.0f;
    return a2 / max(LuaugPi * d * d, LuaugEpsilon);
}

// Height-correlated Smith, already divided by the 4*NoL*NoV that the
// Cook-Torrance denominator would otherwise carry -- so specular is `D * V * F`
// with no fourth factor, and the division that most often produces a NaN never
// happens at all.
float visibilitySmithGgxCorrelated(float nov, float nol, float alpha)
{
    const float a2 = alpha * alpha;
    const float lambdaV = nol * sqrt(nov * nov * (1.0f - a2) + a2);
    const float lambdaL = nov * sqrt(nol * nol * (1.0f - a2) + a2);
    return 0.5f / max(lambdaV + lambdaL, LuaugEpsilon);
}

float3 fresnelSchlick(float3 f0, float voh)
{
    const float f = pow(1.0f - voh, 5.0f);
    return f0 + (float3(1.0f, 1.0f, 1.0f) - f0) * f;
}

// One light's contribution, given the direction towards it and the radiance
// arriving from it. Cook-Torrance specular plus Lambert diffuse, with the two
// lobes split by the same Fresnel term so a grazing surface never gains energy.
float3 shadeDirect(Surface surface, float3 lightDirection, float3 radiance)
{
    const float nol = saturate(dot(surface.Normal, lightDirection));
    if (nol <= 0.0f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    const float3 halfVector = normalize(lightDirection + surface.View);
    const float noh = saturate(dot(surface.Normal, halfVector));
    const float voh = saturate(dot(surface.View, halfVector));

    const float d = distributionGgx(noh, surface.Alpha);
    const float v = visibilitySmithGgxCorrelated(surface.NoV, nol, surface.Alpha);
    const float3 f = fresnelSchlick(surface.SpecularF0, voh);

    const float3 specular = d * v * f;
    const float3 diffuse = (float3(1.0f, 1.0f, 1.0f) - f) * surface.DiffuseColor / LuaugPi;
    return (diffuse + specular) * radiance * nol;
}

// A point or spot light, including its distance and cone falloff.
float3 evaluatePunctualLight(Surface surface, GpuLight light)
{
    const float3 toLight = light.PositionRange.xyz - surface.Position;
    const float distanceSquared = max(dot(toLight, toLight), LuaugEpsilon);
    const float3 lightDirection = toLight * rsqrt(distanceSquared);

    // Inverse square, windowed so the light reaches exactly zero at its range
    // rather than trailing off forever. Without the window a light's cost is
    // unbounded and the bounding volume the culler uses is a lie.
    const float range = max(light.PositionRange.w, LuaugEpsilon);
    const float ratio = distanceSquared / (range * range);
    const float window = saturate(1.0f - ratio * ratio);
    const float attenuation = window * window / distanceSquared;

    // `shader_types.h` stores 1.0 in w for a point light and calls it the value
    // that makes the cone test pass everywhere. It is the opposite: cos(half
    // angle) == 1 is the NARROWEST cone there is, and -1 is the one that admits
    // every direction. Remapped with a select rather than a branch, so a point
    // light behaves the way the contract intends and a spot light pays nothing.
    // The day `GpuLight` stores -1 for a point light, this line becomes a no-op.
    const float cosOuter = light.DirectionCosAngle.w >= 1.0f ? -1.0f : light.DirectionCosAngle.w;
    // One angle has to serve as both cone edges, so the inner edge is a fixed
    // fraction of it: a hard cone boundary aliases badly at any resolution.
    const float cosInner = lerp(cosOuter, 1.0f, 0.1f);
    const float cosTheta = dot(light.DirectionCosAngle.xyz, -lightDirection);
    const float cone = saturate((cosTheta - cosOuter) / max(cosInner - cosOuter, LuaugEpsilon));

    return shadeDirect(surface, lightDirection, light.Color.rgb * (attenuation * cone));
}

// How much of the sun reaches this fragment: 1 lit, 0 fully occluded.
//
// The map is a single orthographic cascade fitted around the camera, sampled
// with an ordinary (non-comparison) sampler so the renderer need not create one
// with `compare_enable` set -- the manual test below is the same arithmetic the
// hardware would do, and it keeps the sampler state one thing rather than two.
float sampleSunShadow(
    Texture2D<float> shadowMap, SamplerState shadowSampler, float4x4 sunViewProjection, float3 position, float nol)
{
    const float4 lightClip = mul(sunViewProjection, float4(position, 1.0f));
    // The sun is orthographic so w is 1; the divide is insurance against a
    // future projective cascade rather than a cost worth removing.
    const float3 lightNdc = lightClip.xyz / lightClip.w;

    // Outside the cascade there is no information and the honest answer is
    // "lit". Returning 0 instead is how a scene gains a hard black edge exactly
    // where the fitted box stops.
    if (any(abs(lightNdc.xy) > 1.0f) || lightNdc.z < 0.0f || lightNdc.z > 1.0f)
    {
        return 1.0f;
    }

    // NDC +Y is up and a texture's V runs down. This holds on every backend
    // because SDL_GPU flips the Vulkan viewport to match D3D
    // (third_party/sdl3/src/gpu/vulkan/SDL_gpu_vulkan.c:7503-7505), and depth is
    // [0, 1] (engine/core/include/luaug/core/math.h, conventions block).
    const float2 uv = lightNdc.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);

    // Slope-scaled: a surface nearly edge-on to the sun crosses many depth units
    // inside one texel, so a bias large enough to stop acne there would detach
    // every contact shadow elsewhere.
    const float bias = lerp(0.005f, 0.0005f, saturate(nol));
    const float reference = lightNdc.z - bias;

    // Texel size read from the texture rather than from a uniform: the frozen
    // contract has no field for the map's resolution, and a hardcoded size is
    // how filtering silently stops matching when the map is resized.
    uint width = 0;
    uint height = 0;
    shadowMap.GetDimensions(width, height);
    const float2 texel = float2(1.0f / float(max(width, 1u)), 1.0f / float(max(height, 1u)));

    float lit = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            // SampleLevel, not Sample: a depth map has no mips, and an implicit
            // derivative inside an unrolled loop is a cost with no result.
            const float occluder = shadowMap.SampleLevel(shadowSampler, uv + float2(x, y) * texel, 0.0f);
            lit += reference <= occluder ? 1.0f : 0.0f;
        }
    }
    return lit / 9.0f;
}

// Linear fog towards `fogColor`. `fogRange.z` is 1/(end - start) precomputed on
// the CPU and is zero when fog is off, which zeroes the factor without this
// function ever knowing that fog can be off at all.
float3 applyFog(float3 color, float3 fogColor, float4 fogRange, float viewDistance)
{
    const float factor = saturate((viewDistance - fogRange.x) * fogRange.z);
    return lerp(color, fogColor, factor);
}

#endif // LUAUG_BRDF_HLSLI
