// What the ground looks like, shared by every shader that draws terrain.
//
// **One look, whichever encoding drew it.** The ground is drawn from its height
// atlas (`terrain.hlsl`) and a cave from a CPU mesh (`terrain_cave.hlsl`), and
// the two meet at the edge of every cave opening. When they were shaded by
// different code the opening showed as a square of flat colour in varied grass;
// both now take their colour variation, their rock and their grain from here.
//
// Until the terrain has texture sets, the variation is procedural: three
// octaves of value noise, pinned to the FIELD's own coordinates so nothing swims
// when the camera moves -- patches of about 37 m, clumps of about 4 m and grain
// of about half a metre -- and a slope rule that turns steep ground to rock the
// way the reference terrains' "autoshader" does.

#ifndef LUAUG_TERRAIN_SURFACE_HLSLI
#define LUAUG_TERRAIN_SURFACE_HLSLI

// The palette ids the slope rule treats as already rock.
#define LUAUG_TERRAIN_ROCK 3u
#define LUAUG_TERRAIN_BASALT 7u

float terrainHash(float2 cell)
{
    float3 p = frac(float3(cell.xyx) * 0.1031f);
    p += dot(p, p.yzx + 33.33f);
    return frac((p.x + p.y) * p.z);
}

float terrainNoise(float2 position)
{
    const float2 cell = floor(position);
    const float2 f = position - cell;
    const float2 u = f * f * (3.0f - 2.0f * f);
    const float a = terrainHash(cell);
    const float b = terrainHash(cell + float2(1.0f, 0.0f));
    const float c = terrainHash(cell + float2(0.0f, 1.0f));
    const float d = terrainHash(cell + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

struct TerrainDetail
{
    float3 Albedo;
    // A tangent-space nudge for the normal: the grain, raked by a low sun.
    float2 NormalNudge;
};

// `albedo` is the blended palette colour, `ground` the point's field-space
// x and z in metres, `normal` its surface normal, `rockAlready` whether the
// material there is one the slope rule leaves alone, and `rock` the rock colour.
TerrainDetail terrainDetail(float3 albedo, float2 ground, float3 normal, bool rockAlready, float3 rock)
{
    const float macro = terrainNoise(ground * (1.0f / 37.0f));
    const float clump = terrainNoise(ground * (1.0f / 4.3f) + 17.0f);
    const float grain = terrainNoise(ground * (1.0f / 0.55f) + 41.0f);

    // **Each octave fades out as it shrinks below a pixel.** Noise finer than
    // the pixels drawing it does not look like grain: it crawls as the camera
    // moves, and on a specular surface it sparkles. `fwidth` is how many metres
    // one pixel spans here.
    const float footprint = max(fwidth(ground.x), fwidth(ground.y));
    const float grainFade = saturate(1.0f - footprint / 0.25f);
    const float clumpFade = saturate(1.0f - footprint / 2.0f);

    // Steep ground is rock, whatever it was painted: grass does not hold to a
    // cliff. The threshold wanders with the clump noise so the boundary is a
    // ragged edge rather than a contour line.
    const float slope = 1.0f - saturate(normal.y);
    const float rockiness = smoothstep(0.24f, 0.36f, slope + (clump - 0.5f) * 0.12f);
    if (!rockAlready)
        albedo = lerp(albedo, rock, rockiness);

    // Brightness at three scales and a slight hue drift at the largest: a field
    // is not one green.
    const float shade = 1.0f + (macro - 0.5f) * 0.28f + (clump - 0.5f) * 0.16f * clumpFade +
                        (grain - 0.5f) * 0.10f * grainFade;
    albedo *= shade;
    albedo *= lerp(float3(1.04f, 0.97f, 0.94f), float3(0.95f, 1.03f, 1.02f), macro);

    const float grainX = terrainNoise((ground + float2(0.07f, 0.0f)) * (1.0f / 0.55f) + 41.0f) - grain;
    const float grainZ = terrainNoise((ground + float2(0.0f, 0.07f)) * (1.0f / 0.55f) + 41.0f) - grain;

    TerrainDetail detail;
    detail.Albedo = albedo;
    detail.NormalNudge = float2(grainX, grainZ) * (0.8f * grainFade);
    return detail;
}

#endif // LUAUG_TERRAIN_SURFACE_HLSLI
