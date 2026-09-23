// What the ground looks like, shared by every shader that draws terrain.
//
// **One look for all of it.** Terrain is one kind of mesh now (ADR 0082); this
// was split out when the ground and the caves were drawn by different shaders
// and had to agree, and it stays the place the look is defined.
//
// Until the terrain has texture sets, the variation is procedural: three
// octaves of value noise, laid triplanar and pinned to the FIELD's own coordinates so nothing swims
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

// The detail noise at one planar projection: three octaves and the grain's
// slope along both of the plane's axes.
struct TerrainOctaves
{
    float Macro;
    float Clump;
    float Grain;
    float2 GrainSlope;
};

TerrainOctaves terrainOctaves(float2 plane)
{
    TerrainOctaves octaves;
    octaves.Macro = terrainNoise(plane * (1.0f / 37.0f));
    octaves.Clump = terrainNoise(plane * (1.0f / 4.3f) + 17.0f);
    octaves.Grain = terrainNoise(plane * (1.0f / 0.55f) + 41.0f);
    octaves.GrainSlope =
        float2(terrainNoise((plane + float2(0.07f, 0.0f)) * (1.0f / 0.55f) + 41.0f) - octaves.Grain,
               terrainNoise((plane + float2(0.0f, 0.07f)) * (1.0f / 0.55f) + 41.0f) - octaves.Grain);
    return octaves;
}

TerrainOctaves terrainWeighted(TerrainOctaves sum, TerrainOctaves octaves, float weight)
{
    sum.Macro += octaves.Macro * weight;
    sum.Clump += octaves.Clump * weight;
    sum.Grain += octaves.Grain * weight;
    sum.GrainSlope += octaves.GrainSlope * weight;
    return sum;
}

// `albedo` is the blended palette colour, `ground` the point's field-space
// position in metres, `normal` its surface normal, `rockAlready` whether the
// material there is one the slope rule leaves alone, and `rock` the rock colour.
//
// **Triplanar.** The noise is laid on the three axis planes and blended by how
// squarely the surface faces each. Laid on the ground plane alone -- which is
// what this did -- every cliff and every cave wall showed it stretched straight
// down into vertical streaks, one per texel of the plane above. Ground that
// faces one plane squarely (almost all of it) pays for that plane only: the
// branches skip a plane whose weight is nothing.
TerrainDetail terrainDetail(float3 albedo, float3 ground, float3 normal, bool rockAlready, float3 rock)
{
    // **Each octave fades out as it shrinks below a pixel.** Noise finer than
    // the pixels drawing it does not look like grain: it crawls as the camera
    // moves, and on a specular surface it sparkles. `fwidth` is how many metres
    // one pixel spans here -- taken before any branch, where derivatives are
    // still defined.
    const float3 span = fwidth(ground);
    const float footprint = max(span.x, max(span.y, span.z));
    const float grainFade = saturate(1.0f - footprint / 0.25f);
    const float clumpFade = saturate(1.0f - footprint / 2.0f);

    float3 weights = abs(normal);
    weights *= weights;
    weights *= weights;
    weights /= max(weights.x + weights.y + weights.z, 1e-5f);

    TerrainOctaves blended = (TerrainOctaves)0;
    [branch] if (weights.y > 0.01f)
        blended = terrainWeighted(blended, terrainOctaves(ground.xz), weights.y);
    [branch] if (weights.x > 0.01f)
        blended = terrainWeighted(blended, terrainOctaves(ground.zy), weights.x);
    [branch] if (weights.z > 0.01f)
        blended = terrainWeighted(blended, terrainOctaves(ground.xy), weights.z);
    // The skipped planes' share, so the octaves still average to a half.
    const float kept = (weights.y > 0.01f ? weights.y : 0.0f) + (weights.x > 0.01f ? weights.x : 0.0f) +
                       (weights.z > 0.01f ? weights.z : 0.0f);
    const float scale = 1.0f / max(kept, 1e-5f);
    const float macro = blended.Macro * scale;
    const float clump = blended.Clump * scale;
    const float grain = blended.Grain * scale;

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

    TerrainDetail detail;
    detail.Albedo = albedo;
    detail.NormalNudge = blended.GrainSlope * scale * (0.8f * grainFade);
    return detail;
}

#endif // LUAUG_TERRAIN_SURFACE_HLSLI
