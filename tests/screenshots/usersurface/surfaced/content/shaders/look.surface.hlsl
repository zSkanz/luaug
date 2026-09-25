// The built-in surface, written as a surface shader (ADR 0091).
//
// **This file is a test of the contract, not the engine's surface.** The
// built-in path stays in `luaug_forward.hlsli`; this is the same surface
// written only with what `luaug/surface.hlsli` gives a user, and the render
// captures forced onto it must match the built-in's goldens. If they cannot,
// the contract is missing something a user would need, and the contract is
// what gets fixed.
//
// Its parameters are named after the material's built-in fields, which is
// what makes them those fields: a part's `Color`, its material's `Roughness`,
// its `ColorMap`, reach it as they reach the built-in surface.

#include "luaug/surface.hlsli"

LUAUG_PARAM(float3, Color, float3(1.0, 1.0, 1.0), colour)
LUAUG_PARAM(float, Metalness, 0.0, range(0, 1))
LUAUG_PARAM(float, Roughness, 0.7, range(0, 1))
LUAUG_PARAM(float, NormalScale, 1.0, range(0, 4))
LUAUG_PARAM(float3, Emissive, float3(0.0, 0.0, 0.0), colour)
LUAUG_TEXTURE(ColorMap)
LUAUG_TEXTURE(NormalMap, normal)
LUAUG_TEXTURE(MetallicRoughnessMap)
LUAUG_TEXTURE(EmissiveMap)

void surfaceFragment(SurfaceInputs inputs, inout SurfaceOutput surface)
{
    const float4 base = LUAUG_SAMPLE(ColorMap, inputs.Uv0);
    surface.BaseColor = Color * base.rgb;
    surface.Alpha = base.a;
    const float3 metallicRoughness = LUAUG_SAMPLE(MetallicRoughnessMap, inputs.Uv0).rgb;
    surface.Roughness = Roughness * metallicRoughness.g;
    surface.Metallic = Metalness * metallicRoughness.b;
    // No map is the mesh's own normal, exactly -- not a flat texel's, which is
    // 128/255 and tilts it by a hair (what the proof caught on shadow edges).
    if (LUAUG_TEXTURE_SET(NormalMap))
        surface.Normal = surfaceNormalFromMap(LUAUG_SAMPLE(NormalMap, inputs.Uv0).rgb, NormalScale, inputs);
    surface.Emissive = Emissive * LUAUG_SAMPLE(EmissiveMap, inputs.Uv0).rgb;
}
