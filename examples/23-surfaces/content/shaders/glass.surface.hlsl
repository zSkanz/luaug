// Glass (ADR 0091): refraction through the scene's colour. A blended surface
// reads what was drawn behind it; this one bends that by its normal, tints it,
// and lets the lighting add the reflection on top.

#include "luaug/surface.hlsli"

LUAUG_PARAM(float3, Tint, float3(0.82, 0.93, 0.96), colour)
LUAUG_PARAM(float, Bend, 0.04, range(0, 0.2))

void surfaceFragment(SurfaceInputs inputs, inout SurfaceOutput surface)
{
    // A slow ripple in the pane, so the bend is visible on a flat sheet.
    const float3 wobble = float3(sin(inputs.WorldPosition.y * 5.0f + inputs.WorldPosition.x * 2.0f), 0.0f,
                                 cos(inputs.WorldPosition.x * 4.0f)) * 0.25f;
    surface.Normal = normalize(inputs.WorldNormal + wobble);
    const float2 uv = inputs.ScreenUv + (surface.Normal.xz - inputs.WorldNormal.xz) * Bend;
    // Light through the glass is emitted, so it is not lit a second time.
    surface.Emissive = sceneColorAt(uv) * Tint;
    surface.BaseColor = float3(0.02f, 0.02f, 0.02f);
    surface.Roughness = 0.05f;
    surface.Metallic = 0.0f;
    surface.Alpha = 1.0f;
}
