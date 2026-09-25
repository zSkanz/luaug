// The sky, when a `Sky` governs it (ADR 0096): six pictures or the engine's
// own gradient beneath, and the sun, the moon and the stars on top.
//
// **A second pipeline beside `sky.hlsl`, and that one is untouched**: a world
// with no `Sky` draws through exactly the shader it always drew through. This
// one takes the plain sky's block at slot 0 -- the same colours, derived the
// same way -- and adds its own at slot 1.
//
// **The sun is where the clock puts it**, whatever the pictures show: its
// direction is the plain sky's, and so is the direction every shadow is cast
// from. A picture with a sun painted in would disagree with it, which is why
// `CelestialBodiesShown` exists.

#define LUAUG_UNIFORMS_SKY
#include "luaug_pbr.hlsli"
#define LUAUG_UNIFORMS_LOOK_SKY
#include "luaug_look.hlsli"
#include "luaug_fullscreen.hlsli"

// The six pictures, resampled into one octahedral image; a white pixel when
// there are none, which the flags then ignore.
Texture2D SkyboxTexture : register(t0, space2);
SamplerState SkyboxSampler : register(s0, space2);
Texture2D SunTexture : register(t1, space2);
SamplerState SunSampler : register(s1, space2);
Texture2D MoonTexture : register(t2, space2);
SamplerState MoonSampler : register(s2, space2);

struct Interpolants
{
    float2 Ndc : TEXCOORD0;
    float4 Position : SV_Position;
};

Interpolants VertexMain(uint vertexId : SV_VertexID)
{
    Interpolants output;
    float2 uv = float2(0.0f, 0.0f);
    fullscreenTriangle(vertexId, 1.0f, output.Position, uv);
    output.Ndc = output.Position.xy;
    return output;
}

// A disc about `centre` of angular radius `radius`, as texture coordinates on
// it -- (0.5, 0.5) at its middle, the unit square its bounds -- and whether the
// direction is inside it at all.
float2 discUv(float3 direction, float3 centre, float radius, out float inside)
{
    const float3 up = abs(centre.y) < 0.99f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    const float3 across = normalize(cross(up, centre));
    const float3 upward = cross(centre, across);
    const float reach = max(tan(radius), 1e-4f);
    const float2 local = float2(dot(direction, across), -dot(direction, upward)) / reach;
    inside = dot(direction, centre) > 0.0f && max(abs(local.x), abs(local.y)) <= 1.0f ? 1.0f : 0.0f;
    return local * 0.5f + 0.5f;
}

// One pseudo-random number in [0, 1) per integer cell, the same on every
// machine: stars are where they are every night.
float4 cellHash(float3 cell)
{
    float4 p = frac(float4(cell.xyzx) * float4(0.1031f, 0.1030f, 0.0973f, 0.1099f));
    p += dot(p, p.wzxy + 33.33f);
    return frac((p.xxyz + p.yzzw) * p.zywx);
}

// The stars: at most one in each cell of a grid laid over the six faces of a
// cube around the camera, placed and sized by the cell's own hash.
float starsAt(float3 direction)
{
    const float cells = LookStars.x;
    if (cells <= 0.0f)
        return 0.0f;
    const float3 a = abs(direction);
    float2 face;
    float faceIndex;
    if (a.x >= a.y && a.x >= a.z)
    {
        face = direction.zy / a.x;
        faceIndex = direction.x > 0.0f ? 0.0f : 1.0f;
    }
    else if (a.y >= a.z)
    {
        face = direction.xz / a.y;
        faceIndex = direction.y > 0.0f ? 2.0f : 3.0f;
    }
    else
    {
        face = direction.xy / a.z;
        faceIndex = direction.z > 0.0f ? 4.0f : 5.0f;
    }
    const float2 grid = (face * 0.5f + 0.5f) * cells;
    const float2 cell = floor(grid);
    const float4 hash = cellHash(float3(cell, faceIndex));
    if (hash.x > LookStars.y)
        return 0.0f;
    const float2 centre = cell + 0.2f + 0.6f * hash.yz;
    // A star is a point: about a pixel and a half across whatever the lens,
    // from how far one pixel turns the view -- measured on the DIRECTION, which
    // is continuous, and not on the grid, which jumps at every cube face and
    // drew a dashed line of star fragments down each seam.
    const float pixel = max(length(fwidth(direction)) * cells * 0.5f, 1e-4f);
    const float spread = length(grid - centre) / pixel;
    const float brightness = 0.25f + 2.5f * hash.w * hash.w * hash.w;
    return brightness * saturate(1.5f - spread);
}

float4 FragmentMain(Interpolants input) : SV_Target0
{
    const float4 nearPoint = mul(InverseViewProjection, float4(input.Ndc, 0.0f, 1.0f));
    const float4 farPoint = mul(InverseViewProjection, float4(input.Ndc, 1.0f, 1.0f));
    const float3 direction = normalize(farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w);

    float3 sky;
    if (LookSkyFlags.x > 0.5f)
    {
        // The pictures, at their own brightness at every hour.
        sky = SkyboxTexture.SampleLevel(SkyboxSampler, lookOctahedralUv(direction), 0.0f).rgb;
    }
    else
    {
        // The engine's own gradient, as `sky.hlsl` draws it.
        const float height = saturate(direction.y);
        const float3 gradient = lerp(HorizonColor.rgb, ZenithColor.rgb, sqrt(height));
        const float below = saturate(-direction.y);
        sky = lerp(gradient, HorizonColor.rgb * 0.35f, pow(below, 0.5f));
    }

    if (LookSkyFlags.y > 0.5f)
    {
        // The stars first, so the moon and the sun cover them.
        const float aboveHorizon = saturate(direction.y * 12.0f);
        sky += starsAt(direction) * LookStars.z * aboveHorizon * float3(0.9f, 0.95f, 1.0f);

        // The moon, opposite the sun.
        float inMoon;
        const float2 moonUv = discUv(direction, LookMoon.xyz, LookMoon.w, inMoon);
        if (inMoon > 0.0f)
        {
            float4 face;
            if (LookSkyFlags.w > 0.5f)
            {
                face = MoonTexture.SampleLevel(MoonSampler, moonUv, 0.0f);
            }
            else
            {
                const float edge = length(moonUv - 0.5f) * 2.0f;
                face = float4(1.0f, 1.0f, 1.0f, saturate((1.0f - edge) * 12.0f));
            }
            sky = lerp(sky, face.rgb * LookMoonColor.rgb, face.a * LookMoonColor.w);
        }

        // And the sun: a picture of it, or the plain sky's soft-edged disc.
        const float3 sunDirection = normalize(SunDirectionSize.xyz);
        const float cosAngle = dot(direction, sunDirection);
        const float glow = pow(saturate(cosAngle), 64.0f) * 0.25f;
        if (LookSkyFlags.z > 0.5f)
        {
            float inSun;
            const float2 sunUv = discUv(direction, sunDirection, SunDirectionSize.w, inSun);
            const float4 face = inSun > 0.0f ? SunTexture.SampleLevel(SunSampler, sunUv, 0.0f) : float4(0.0f, 0.0f, 0.0f, 0.0f);
            sky += SunColor.rgb * (face.rgb * face.a * SunColor.w + glow);
        }
        else
        {
            const float disc = smoothstep(cos(SunDirectionSize.w), cos(SunDirectionSize.w * 0.9f), cosAngle);
            sky += SunColor.rgb * (disc * SunColor.w + glow);
        }
    }

    // **The clouds, last**: over the stars, the moon and the sun, which a thin
    // cloud lets through a little and a thick one hides. Lit from above by the
    // sun and by the sky around them, darker underneath where they are thick.
    const float2 cloud = lookClouds(direction);
    if (cloud.x > 0.0f)
    {
        const float thick = LookClouds.y;
        const float alpha = cloud.x * lerp(0.35f, 0.95f, thick);
        const float shade = lerp(1.0f, 0.5f, thick * saturate(cloud.y * 2.0f - 0.6f));
        const float3 lit = LookCloudColor.rgb *
                           (SunColor.rgb * LookCloudColor.w + ZenithColor.rgb * 0.5f + HorizonColor.rgb * 0.5f);
        sky = lerp(sky, lit * shade, alpha);
    }

    return float4(sky, 1.0f);
}
