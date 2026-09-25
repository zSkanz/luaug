// The surface shader contract, version 1 (ADR 0091).
//
// A surface shader is two functions a material names. The engine builds every
// pipeline the surface needs from them -- forward, instanced, shadow, depth and
// blended -- so the sun and its shadows, the lights, image-based lighting, fog
// and the post chain apply to it exactly as they do to the built-in surface.
//
//     #include "luaug/surface.hlsli"
//
//     LUAUG_PARAM(float, WaveHeight, 0.5, range(0, 4))
//     LUAUG_PARAM(float3, Deep, float3(0.0, 0.1, 0.2), colour)
//     LUAUG_TEXTURE(Foam)
//
//     void surfaceVertex(inout SurfaceVertex vertex, SurfaceInputs inputs)
//     {
//         vertex.Position.y += sin(inputs.Time + vertex.Position.x) * WaveHeight;
//     }
//
//     void surfaceFragment(SurfaceInputs inputs, inout SurfaceOutput surface)
//     {
//         surface.BaseColor = Deep + LUAUG_SAMPLE(Foam, inputs.Uv0).rgb;
//     }
//
// `surfaceVertex` may move the vertex, in object space, and change its normal;
// a displaced vertex casts a displaced shadow. `surfaceFragment` decides what
// the surface is -- colour, alpha, metalness, roughness, normal, emission --
// and never how it is lit. Either may be left out; `LUAUG_NO_VERTEX` and
// `LUAUG_NO_FRAGMENT` say so.
//
// **What you may not do**: declare registers, samplers, cbuffers or entry
// points of your own. The layout is the engine's, and a shader that declares
// its own resources fails to compile with a message that says so.
//
// Nothing here is a backend type: the same file compiles to SPIR-V, DXIL and
// MSL, and the same parameters reach it on every backend.

#ifndef LUAUG_SURFACE_HLSLI
#define LUAUG_SURFACE_HLSLI

// Bumped when anything below changes meaning. A shader may test it.
#define LUAUG_SURFACE_CONTRACT 1

// A vertex as the mesh gave it, in the object's space. `Color` is the mesh's
// vertex colour, white when it has none; `Uv1` is its second set, or `Uv0`.
struct SurfaceVertex
{
    float3 Position;
    float3 Normal;
    float4 Tangent;
    float2 Uv0;
    float2 Uv1;
    float4 Color;
};

// Everything a surface may read. In the vertex stage the world, screen and
// scene members describe the vertex BEFORE it moved.
struct SurfaceInputs
{
    // Seconds of simulation, interpolated to the frame being drawn: the clock
    // the world's transforms are drawn at, so a wave on the GPU and the same
    // wave computed in Luau agree on screen.
    float Time;
    float3 CameraPosition;
    // The object's transform: object space to world space.
    float4x4 ObjectToWorld;
    float3 WorldPosition;
    // Unit length, facing out of the surface.
    float3 WorldNormal;
    // `w` is the bitangent's handedness.
    float4 WorldTangent;
    float2 Uv0;
    float2 Uv1;
    float4 VertexColor;
    // [0, 1] across the frame, top-left origin.
    float2 ScreenUv;
    // Pixel coordinates in `xy`; the distance in front of the camera in `w`.
    float4 ScreenPosition;
    // The distance in front of the camera of whatever opaque surface is behind
    // this pixel -- in a blended surface. Anywhere else, a very large number.
    float SceneDepth;
    // What was drawn behind this pixel before any blended surface, linear HDR.
    // Only when the material sets `readsSceneColor`; black otherwise.
    float3 SceneColor;
};

// What the surface is. Starts as the engine's defaults: white, opaque, not
// metallic, roughness 0.7, the interpolated normal, no emission.
struct SurfaceOutput
{
    float3 BaseColor;
    float Alpha;
    float Metallic;
    float Roughness;
    // World space. Leave it for the mesh's own normal.
    float3 Normal;
    float3 Emissive;
};

// A parameter of the material: `LUAUG_PARAM(type, Name, default[, annotation])`
// with `type` one of float, float2, float3, float4, int, bool, and the
// annotation one of `range(min, max)`, `colour` or `toggle`. It is a field of
// the material, set in its `properties` like `Roughness` is, and read here by
// its name. Naming it after a built-in field -- `Color`, `Roughness`,
// `Metalness` -- makes it that field.
#define LUAUG_PARAM(type, name, ...)
// A texture of the material, sampled with `LUAUG_SAMPLE(Name, uv)`:
// `LUAUG_TEXTURE(Name[, white | black | normal])`, the second saying what it
// reads as when the material sets none -- white unless said. At most eight.
#define LUAUG_TEXTURE(name, ...)
#define LUAUG_SAMPLE(name, uv) name.Sample(name##Sampler, (uv))
#define LUAUG_SAMPLE_LEVEL(name, uv, level) name.SampleLevel(name##Sampler, (uv), (level))

// A normal map's texel -- sampled, in [0, 1] -- as a world-space normal on this
// surface, its xy scaled by `strength`. The same frame the built-in surface
// builds: the tangent where the mesh has one, a stable one where it does not.
float3 surfaceNormalFromMap(float3 texel, float strength, SurfaceInputs inputs)
{
    const float3 normal = normalize(inputs.WorldNormal);
    const float tangentLength = length(inputs.WorldTangent.xyz);
    const float3 arbitrary =
        abs(normal.y) < 0.999f ? normalize(cross(float3(0.0f, 1.0f, 0.0f), normal)) : float3(1.0f, 0.0f, 0.0f);
    const float3 t = tangentLength > 1e-5f ? inputs.WorldTangent.xyz / tangentLength : arbitrary;
    const float3 b = cross(normal, t) * inputs.WorldTangent.w;
    float3 tangentNormal = texel * 2.0f - 1.0f;
    tangentNormal.xy *= strength;
    return normalize(mul(normalize(tangentNormal), float3x3(t, b, normal)));
}

#endif // LUAUG_SURFACE_HLSLI
