// The fullscreen triangle every screen-space pass is drawn with, and the sRGB
// transfer function the last of them applies.
//
// A triangle rather than a quad: two triangles meeting on the screen diagonal
// make the GPU shade that diagonal twice, once per triangle, because quads on a
// shared edge cannot be packed. One oversized triangle clipped to the viewport
// has no interior edge and needs no vertex buffer at all -- the positions come
// from `SV_VertexID`, so the pipeline binds nothing and the reflection sidecar
// reports zero vertex inputs.
#ifndef LUAUG_FULLSCREEN_HLSLI
#define LUAUG_FULLSCREEN_HLSLI

// `depth` is what lands in SV_Position.z, in the engine's [0, 1] convention:
// 0 is the near plane, 1 the far one.
void fullscreenTriangle(uint vertexId, float depth, out float4 position, out float2 uv)
{
    // Vertex 0 -> (0,0), 1 -> (2,0), 2 -> (0,2). In UV space, so the sampled
    // texture needs no flip; the clip position below is where the Y flip lives.
    uv = float2(float((vertexId << 1) & 2), float(vertexId & 2));
    // NDC +Y is up while V runs down, on every backend, because SDL_GPU flips
    // the Vulkan viewport to match D3D (SDL_gpu_vulkan.c:7503-7505).
    position = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), depth, 1.0f);
}

// Linear -> sRGB, the piecewise IEC 61966-2-1 curve rather than a plain 1/2.2
// gamma, which is visibly wrong in the darkest stop.
//
// **This is the only place the engine encodes sRGB.** The swapchain is claimed
// with SDL_GPU's default SDR composition, which is a plain `B8G8R8A8_UNORM`
// (third_party/sdl3/src/gpu/vulkan/SDL_gpu_vulkan.c:245-246) -- the display
// wants sRGB-encoded bytes and the format applies no transfer function of its
// own, so the shader has to. The decode at the other end is the texture
// format's job: base-colour and emissive images are created `Rgba8UnormSrgb` so
// the sampler linearises them, while normal and metallic-roughness maps stay
// `Rgba8Unorm` because their contents were never colours. Doing either of these
// twice, or not at all, is how a renderer ends up washed out or muddy.
float3 encodeSrgb(float3 linearColor)
{
    const float3 c = saturate(linearColor);
    const float3 low = c * 12.92f;
    const float3 high = 1.055f * pow(c, 1.0f / 2.4f) - 0.055f;
    return lerp(low, high, step(float3(0.0031308f, 0.0031308f, 0.0031308f), c));
}

#endif // LUAUG_FULLSCREEN_HLSLI
