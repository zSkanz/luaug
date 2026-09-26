// The sea, as a surface shader (ADR 0091) -- user code, written with nothing
// but `luaug/surface.hlsli`, which is the point of it.
//
// **The same three waves as `src/scripts/init.luau`**, term for term: the GPU
// moves every vertex of the water and Luau moves the boat and floats the cargo,
// and because both evaluate one function at one clock (`inputs.Time` is
// `RunService.SimTime`, interpolated to the frame), nothing on screen can
// disagree with anything else on screen.
//
// Directional sine waves on deep water, `omega = sqrt(g * k)`, rather than
// Gerstner waves: a Gerstner crest also moves sideways, and then the height
// under a given (x, z) has no closed form -- the Luau side would be
// approximating the surface the boat sits on.

#include "luaug/surface.hlsli"

LUAUG_PARAM(float3, Deep, float3(0.012, 0.055, 0.105), colour)
LUAUG_PARAM(float3, Shallow, float3(0.05, 0.28, 0.32), colour)
LUAUG_PARAM(float3, Foam, float3(0.85, 0.9, 0.92), colour)
// How deep the water must be before it is its deep colour, in metres.
LUAUG_PARAM(float, Clarity, 6.0, range(0.5, 30))
// How wide the foam line is where the water meets something, in metres.
LUAUG_PARAM(float, FoamWidth, 0.35, range(0, 4))
// How far what is under the water bends, as a fraction of the screen.
LUAUG_PARAM(float, Refraction, 0.03, range(0, 0.2))

static const float Gravity = 9.81f;
static const float Pi = 3.14159265f;
static const int WaveCount = 3;
// Wavelength, amplitude, direction x, direction z, phase -- as in init.luau.
static const float Waves[WaveCount][5] = {
    {139.0f, 1.9f, 1.0f, 0.15f, 0.0f},
    {67.0f, 1.15f, 0.75f, -0.66f, 1.7f},
    {41.0f, 0.62f, -0.35f, 0.94f, 3.9f},
};

// The height of the sea at a world column, and its two slopes.
float3 sea(float2 xz, float t)
{
    float height = 0.0f;
    float2 slope = float2(0.0f, 0.0f);
    [unroll] for (int index = 0; index < WaveCount; ++index)
    {
        const float k = 2.0f * Pi / Waves[index][0];
        const float2 direction = normalize(float2(Waves[index][2], Waves[index][3]));
        const float omega = sqrt(Gravity * k);
        const float phase = dot(direction * k, xz) - omega * t + Waves[index][4];
        height += Waves[index][1] * sin(phase);
        slope += direction * k * Waves[index][1] * cos(phase);
    }
    return float3(height, slope);
}

void surfaceVertex(inout SurfaceVertex vertex, SurfaceInputs inputs)
{
    // The grid is flat and a metre across; its part is scaled, so the wave is
    // taken at the vertex's WORLD column and moved back into the part's space.
    const float3 wave = sea(inputs.WorldPosition.xz, inputs.Time);
    const float3 up = mul((float3x3)inputs.ObjectToWorld, float3(0.0f, 1.0f, 0.0f));
    vertex.Position.y += wave.x / max(length(up), 1e-4f);
}

void surfaceFragment(SurfaceInputs inputs, inout SurfaceOutput surface)
{
    const float3 wave = sea(inputs.WorldPosition.xz, inputs.Time);
    surface.Normal = normalize(float3(-wave.y, 1.0f, -wave.z));
    surface.Metallic = 0.0f;

    // **How much water is behind this pixel**: the distance to what is under
    // it, less the distance to the surface itself.
    const float2 bend = surface.Normal.xz * Refraction;
    const float behind = sceneDepthAt(inputs.ScreenUv + bend);
    // Refract only into what is behind the water, never into what is in front.
    const float2 uv = behind > inputs.ScreenPosition.w ? inputs.ScreenUv + bend : inputs.ScreenUv;
    const float thickness = max(sceneDepthAt(uv) - inputs.ScreenPosition.w, 0.0f);
    const float murk = saturate(thickness / Clarity);

    // Foam where the water is thin: against the hull, a crate, the shore.
    const float edge = FoamWidth > 0.0f ? 1.0f - saturate(thickness / FoamWidth) : 0.0f;
    const float foam = edge * edge;

    // What is under the water, tinted by how much water it is seen through, is
    // light the surface lets through -- emitted, so it is not lit twice. Foam
    // is opaque, so it lets through none.
    const float3 under = sceneColorAt(uv);
    surface.Emissive = lerp(under * Shallow * 2.2f, Deep * 0.5f, murk) * (1.0f - foam);
    surface.BaseColor = lerp(lerp(Shallow, Deep, murk) * 0.45f, Foam, foam);
    surface.Roughness = lerp(0.14f, 0.85f, foam);
    surface.Alpha = 1.0f;
}
