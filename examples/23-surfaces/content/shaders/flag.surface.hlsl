// A flag in the wind (ADR 0091): vertex displacement on something that is not
// water. The cloth is a flat grid; the wave travels away from the pole, grows
// toward the free edge, and the fragment darkens the folds so the shape reads.

#include "luaug/surface.hlsli"

LUAUG_PARAM(float3, Cloth, float3(0.72, 0.08, 0.06), colour)
LUAUG_PARAM(float, Wind, 1.0, range(0, 3))
LUAUG_TEXTURE(Emblem)

// How far the cloth is pushed out of its plane at a point on it: `u` runs from
// the pole (0) to the free edge (1). Zero at the pole, so the cloth stays on it.
float ripple(float2 uv, float t)
{
    const float reach = uv.x * uv.x;
    return reach * Wind * 0.12f * (sin(uv.x * 9.0f - t * 6.0f + uv.y * 2.0f) + 0.35f * sin(uv.x * 23.0f - t * 11.0f));
}

void surfaceVertex(inout SurfaceVertex vertex, SurfaceInputs inputs)
{
    // The grid lies in x-z; the flag stands it up, so "out of its plane" is y.
    vertex.Position.y += ripple(vertex.Uv0, inputs.Time);
}

void surfaceFragment(SurfaceInputs inputs, inout SurfaceOutput surface)
{
    const float e = 0.01f;
    const float slope = (ripple(inputs.Uv0 + float2(e, 0.0f), inputs.Time) - ripple(inputs.Uv0, inputs.Time)) / e;
    surface.BaseColor = Cloth * LUAUG_SAMPLE(Emblem, inputs.Uv0).rgb * (0.75f + 0.25f * saturate(1.0f - slope));
    surface.Roughness = 0.85f;
}
