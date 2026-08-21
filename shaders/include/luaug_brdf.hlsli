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

// --- Image-based lighting ----------------------------------------------------
//
// The split-sum approximation (Karis 2013), and the reason M4's ambient looked
// like the early 2010s: a prefiltered environment indexed by roughness, times a
// two-term BRDF table indexed by (N·V, roughness). What it buys is that a metal
// reflects something, and what it costs is Karis's own assumption -- view equals
// normal equals reflection -- which is what makes the environment pre-integrable
// at all and which shows up as reflections that do not stretch with view angle.
//
// The environment is an OCTAHEDRAL 2D texture rather than a cubemap, because the
// frozen RHI (ADR 0037) has no cube texture type. `engine/render/src/
// environment.cpp` bakes it, and the mapping below is that file's
// `octahedralUv` written a second time; the two have to agree exactly or a
// reflection lands in the wrong direction.

// Unit direction to the [0, 1] square, y-up.
float2 octahedralUv(float3 direction)
{
    const float3 d = normalize(direction);
    const float norm = abs(d.x) + abs(d.y) + abs(d.z);
    const float3 n = d / max(norm, 1e-6f);

    float2 f = float2(n.x, n.z);
    if (n.y < 0.0f)
    {
        f = float2((1.0f - abs(n.z)) * (n.x >= 0.0f ? 1.0f : -1.0f),
                   (1.0f - abs(n.x)) * (n.z >= 0.0f ? 1.0f : -1.0f));
    }
    return f * 0.5f + 0.5f;
}

// Irradiance from nine spherical-harmonic coefficients. They arrive
// cosine-convolved and already divided by pi (environment.h), so the result
// multiplies straight by a diffuse albedo -- there is no further 1/pi here and
// adding one is the classic way an SH ambient ends up a third too dark.
float3 evaluateIrradiance(float4 coefficients[9], float3 n)
{
    float3 result = coefficients[0].rgb * 0.282095f;
    result += coefficients[1].rgb * (0.488603f * n.y);
    result += coefficients[2].rgb * (0.488603f * n.z);
    result += coefficients[3].rgb * (0.488603f * n.x);
    result += coefficients[4].rgb * (1.092548f * n.x * n.y);
    result += coefficients[5].rgb * (1.092548f * n.y * n.z);
    result += coefficients[6].rgb * (0.315392f * (3.0f * n.z * n.z - 1.0f));
    result += coefficients[7].rgb * (1.092548f * n.x * n.z);
    result += coefficients[8].rgb * (0.546274f * (n.x * n.x - n.y * n.y));
    // An SH reconstruction can ring below zero where the source has a sharp
    // feature -- and the sun's disc is exactly that. Negative irradiance is not
    // a dim surface, it is a black hole in an otherwise lit wall.
    return max(result, float3(0.0f, 0.0f, 0.0f));
}

// Both lobes of the environment. `occlusion` multiplies this and nothing else:
// a surface's occlusion of the ENVIRONMENT says nothing about whether the sun
// reaches it, and the sun has a shadow map that answers exactly that.
float3 evaluateEnvironment(Surface surface, Texture2D environmentMap, SamplerState environmentSampler,
                           Texture2D brdfLut, SamplerState brdfSampler, float4 irradiance[9], float mipCount,
                           float intensity, float occlusion)
{
    const float3 diffuse = surface.DiffuseColor * evaluateIrradiance(irradiance, surface.Normal);

    // `Alpha` is roughness squared, and the mip chain is indexed by the
    // perceptual roughness the material authored -- taking the square root back
    // out is what keeps the chain's steps even.
    const float roughness = sqrt(surface.Alpha);
    const float3 reflection = reflect(-surface.View, surface.Normal);
    const float3 prefiltered =
        environmentMap.SampleLevel(environmentSampler, octahedralUv(reflection), roughness * (mipCount - 1.0f)).rgb;

    // The table supplies the Fresnel scale in R and the bias in G, which is the
    // second half of the split sum. SampleLevel because the table has one mip
    // and its derivatives are meaningless across a screen.
    const float2 ab = brdfLut.SampleLevel(brdfSampler, float2(surface.NoV, roughness), 0.0f).rg;
    const float3 specular = prefiltered * (surface.SpecularF0 * ab.x + ab.yyy);

    return (diffuse + specular) * (intensity * occlusion);
}

// --- Cascaded shadows --------------------------------------------------------
//
// Four cascades in one 2x2 atlas (shadow.h says why an atlas rather than an
// array), a filter radius constant in WORLD space, a normal-offset bias, and a
// blend band rather than a switch at a plane. The roadmap names the last two as
// the tells of a first cascaded implementation, so they are requirements here
// rather than polish.
//
// **`Gather` is the comparison sampler.** ADR 0038 asks for one "so a tap
// degrades instead of switching", and the property that matters is the
// degradation: `SampleCmp` returns 0.25 rather than flipping 0 to 1 because it
// bilinearly weights four binary comparisons. `Gather` returns those same four
// texels in one texture operation, and the four comparisons and two lerps below
// are six ALU instructions. So the frozen `SamplerDesc` gains no compare state
// and the result is the same arithmetic (ADR 0043).

// One tap: hardware-PCF's own answer, computed rather than sampled.
float shadowTapPcf(Texture2D<float> atlas, SamplerState pointSampler, float2 uv, float reference, float2 atlasSize)
{
    // Gather returns the 2x2 neighbourhood in counter-clockwise order starting
    // at the lower left: (-,+), (+,+), (+,-), (-,-) relative to the sample.
    const float4 depths = atlas.Gather(pointSampler, uv);
    const float4 lit = step(reference, depths);

    // The same bilinear weights the texture unit would have used, from where the
    // sample falls inside its texel.
    const float2 texel = uv * atlasSize - 0.5f;
    const float2 fraction = frac(texel);

    const float bottom = lerp(lit.w, lit.z, fraction.x);
    const float top = lerp(lit.x, lit.y, fraction.x);
    return lerp(bottom, top, 1.0f - fraction.y);
}

// One cascade, 3x3 taps of the above. Returns 1 where the sun reaches.
float sampleCascade(Texture2D<float> atlas, SamplerState pointSampler, uint cascade, float3 position, float3 normal,
                    float nol, float2 atlasSize)
{
    const float texelWorld = CascadeTexelWorld[cascade];
    const float depthRange = max(CascadeDepthRange[cascade], 1e-3f);

    // Normal-offset bias, replacing a depth-only one: displacing the sample
    // along the surface normal is what survives a grazing receiver, and it
    // scales with the SINE of the light angle so it grows exactly where it is
    // needed and vanishes where it is not.
    const float slope = sqrt(saturate(1.0f - nol * nol));
    const float3 offset = normal * (texelWorld * ShadowParams.y * slope);

    const float4 lightClip = mul(CascadeViewProjection[cascade], float4(position + offset, 1.0f));
    const float3 ndc = lightClip.xyz / lightClip.w;
    if (any(abs(ndc.xy) > 1.0f) || ndc.z < 0.0f || ndc.z > 1.0f)
    {
        // Outside the cascade there is no information and the honest answer is
        // "lit". Returning 0 is how a scene gains a hard black edge exactly
        // where a fitted box stops.
        return 1.0f;
    }

    // NDC +Y is up and a texture's V runs down. This holds on every backend
    // because SDL_GPU flips the Vulkan viewport to match D3D
    // (SDL_gpu_vulkan.c:7503-7505), and depth is [0, 1].
    const float2 local = ndc.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);

    // The residual depth bias is stated in METRES and converted here, because a
    // constant in depth units means a different distance in every cascade.
    const float reference = ndc.z - ShadowParams.w / depthRange;

    // The filter radius, constant in world metres and clamped to a texel range.
    // The clamp is the honest part: a 3x3 kernel spread over twenty texels is
    // four samples of a large region rather than a filter, and it bands.
    const float radiusTexels = clamp(ShadowParams.x / max(texelWorld, 1e-6f), 0.8f, 3.0f);
    // A cascade's tile spans half the atlas, and its own extent is
    // `texelWorld * tile` metres across -- so a step of one texel is this much
    // local uv, and half that much atlas uv.
    const float2 step2 = (radiusTexels / (0.5f * atlasSize)) * 0.5f;

    // The tile: cascade 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right.
    const float2 tile = float2(float(cascade & 1u), float(cascade >> 1u)) * 0.5f;

    // The atlas's whole tax, and it is four lines: a tap that walks off a tile
    // reads the NEIGHBOURING cascade's depth, which is a bright seam along the
    // split. Clamping to the tile inset by the kernel is what stops it.
    const float2 inset = step2 + 0.5f / atlasSize;
    const float2 lowest = tile + inset;
    const float2 highest = tile + float2(0.5f, 0.5f) - inset;

    float lit = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const float2 uv = clamp(tile + local * 0.5f + float2(x, y) * step2, lowest, highest);
            lit += shadowTapPcf(atlas, pointSampler, uv, reference, atlasSize);
        }
    }
    return lit / 9.0f;
}

// How much of the sun reaches this fragment: 1 lit, 0 fully occluded.
//
// `viewDepth` is the fragment's distance along the camera's forward axis, which
// is what the splits are stated in. It arrives as an interpolant rather than
// being derived from `SV_Position`, because deriving it would need the
// projection this stage is not given.
float sampleSunShadow(Texture2D<float> atlas, SamplerState pointSampler, float3 position, float3 normal, float nol,
                      float viewDepth)
{
    uint width = 0;
    uint height = 0;
    atlas.GetDimensions(width, height);
    const float2 atlasSize = float2(float(max(width, 1u)), float(max(height, 1u)));

    // The first cascade that reaches this fragment. A loop rather than a chain
    // of selects so the count is a constant one place.
    uint cascade = 3u;
    [unroll]
    for (uint i = 0u; i < 4u; ++i)
    {
        if (viewDepth <= CascadeFar[i])
        {
            cascade = i;
            break;
        }
    }

    const float lit = sampleCascade(atlas, pointSampler, cascade, position, normal, nol, atlasSize);

    // Blend over a BAND rather than switching at a plane, which the roadmap
    // names as the second tell of a first cascaded implementation. A hard
    // handover is visible for the same reason a filter that changes width is:
    // two adjacent patches of one surface shaded by two different maps.
    const float near = cascade == 0u ? 0.0f : CascadeFar[cascade - 1u];
    const float far = CascadeFar[cascade];
    const float band = max((far - near) * ShadowParams.z, 1e-4f);
    const float blend = saturate((viewDepth - (far - band)) / band);
    if (blend <= 0.0f || cascade >= 3u)
    {
        return lit;
    }

    const float next = sampleCascade(atlas, pointSampler, cascade + 1u, position, normal, nol, atlasSize);
    return lerp(lit, next, blend);
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
