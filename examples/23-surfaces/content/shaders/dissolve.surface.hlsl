// A dissolve (ADR 0091): a `Mask` surface that eats itself away. Each part
// wears its own clone of the material and a script moves `Threshold` from 0 to
// 1; where the noise is below it, the surface is cut, and a glowing rim runs
// just ahead of the cut.

#include "luaug/surface.hlsli"

LUAUG_PARAM(float3, Base, float3(0.55, 0.58, 0.62), colour)
LUAUG_PARAM(float3, Rim, float3(6.0, 2.2, 0.4), colour)
LUAUG_PARAM(float, Threshold, 0.0, range(0, 1))
LUAUG_PARAM(float, RimWidth, 0.06, range(0, 0.3))

float hash(float3 p)
{
    p = frac(p * 0.3183099f + 0.1f);
    p *= 17.0f;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

// Value noise on a lattice, smoothed: blobs about a third of a metre across.
float noise(float3 p)
{
    const float3 cell = floor(p);
    const float3 f = frac(p);
    const float3 u = f * f * (3.0f - 2.0f * f);
    return lerp(lerp(lerp(hash(cell), hash(cell + float3(1, 0, 0)), u.x),
                     lerp(hash(cell + float3(0, 1, 0)), hash(cell + float3(1, 1, 0)), u.x), u.y),
                lerp(lerp(hash(cell + float3(0, 0, 1)), hash(cell + float3(1, 0, 1)), u.x),
                     lerp(hash(cell + float3(0, 1, 1)), hash(cell + float3(1, 1, 1)), u.x), u.y),
                u.z);
}

void surfaceFragment(SurfaceInputs inputs, inout SurfaceOutput surface)
{
    const float n = 0.65f * noise(inputs.WorldPosition * 3.0f) + 0.35f * noise(inputs.WorldPosition * 7.0f);
    // The material's `AlphaMode` is Mask: an alpha under its cutoff is cut.
    surface.Alpha = n >= Threshold ? 1.0f : 0.0f;
    const float rim = Threshold > 0.0f ? 1.0f - saturate((n - Threshold) / RimWidth) : 0.0f;
    surface.BaseColor = Base;
    surface.Emissive = Rim * rim;
    surface.Roughness = 0.5f;
    surface.Metallic = 0.6f;
}
