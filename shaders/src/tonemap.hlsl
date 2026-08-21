// The resolve: the `Rgba16Float` HDR target to the swapchain, with exposure.
// The first fullscreen pass in the engine and the first time the swapchain is
// not the only colour target (M4 brief, Decision 11).
//
// **The operator is Khronos PBR Neutral** (KhronosGroup/ToneMapping, the glTF
// viewer's default since 2024), and the reason is what M4 is for. This is the
// milestone where an imported glTF file first appears on screen, and every
// question asked of the result -- does this material look like the file says it
// looks, did the importer read baseColorFactor correctly, is the golden image
// showing a bug or a look -- is a question about hue. ACES's filmic fit answers
// it badly: it is cheap and it is the industry default, but it rotates
// saturated hues towards orange as they brighten, so a red that is wrong and a
// red that is merely bright arrive at the same pixel. PBR Neutral is the same
// order of cost, is monotone in luminance, and desaturates only in the
// highlight roll-off, which leaves a material's colour recognisable right up to
// the point it clips. Reinhard was rejected for the opposite reason: it never
// really reaches white, so every bright surface reads as washed out.
//
// **sRGB encoding happens here and nowhere else.** The swapchain is claimed with
// SDL_GPU's default SDR composition, which is a plain `B8G8R8A8_UNORM`
// (third_party/sdl3/src/gpu/vulkan/SDL_gpu_vulkan.c:245-246): the display wants
// sRGB-encoded bytes and that format applies no transfer function, so this
// shader applies it. Its counterpart, the decode, is the texture format's job --
// base colour and emissive are `Rgba8UnormSrgb` and the sampler linearises them;
// normal and metallic-roughness maps stay `Rgba8Unorm` because their contents
// were never colours. If the swapchain ever becomes `SDR_LINEAR`, the hardware
// starts encoding and `encodeSrgb` below must go, or it happens twice and the
// image turns pale.

#define LUAUG_UNIFORMS_TONEMAP
#include "luaug_pbr.hlsli"
#include "luaug_fullscreen.hlsli"

Texture2D HdrTexture : register(t0, space2);
SamplerState HdrSampler : register(s0, space2);
// The bloom chain's finest level, already the sum of every coarser one.
Texture2D BloomTexture : register(t1, space2);
SamplerState BloomSampler : register(s1, space2);
// One texel: the frame's adapted average luminance (`luminance_adapt.hlsl`).
Texture2D<float> ExposureTexture : register(t2, space2);
SamplerState ExposureSampler : register(s2, space2);

struct Interpolants
{
    float2 Uv : TEXCOORD0;
    float4 Position : SV_Position;
};

Interpolants VertexMain(uint vertexId : SV_VertexID)
{
    Interpolants output;
    // Depth 0: this pass has no depth attachment, and 0 is the near plane in the
    // engine's [0, 1] convention, so it is in front of whatever a future pass
    // might put behind it.
    fullscreenTriangle(vertexId, 0.0f, output.Position, output.Uv);
    return output;
}

// Khronos PBR Neutral, linear Rec.709 in, [0, 1] linear Rec.709 out. Ported
// unchanged from the reference implementation so a difference against the glTF
// viewer is a bug in our lighting rather than in our curve.
float3 tonemapPbrNeutral(float3 color)
{
    const float startCompression = 0.8f - 0.04f;
    const float desaturation = 0.15f;

    // Lifts the darkest channel just enough to keep a near-black from taking on
    // a colour cast when the other two channels are compressed.
    const float x = min(color.r, min(color.g, color.b));
    const float offset = x < 0.08f ? x - 6.25f * x * x : 0.04f;
    color -= offset;

    const float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression)
    {
        return color;
    }

    // A hyperbola on the brightest channel alone, with the other two scaled by
    // the same ratio: that is what keeps the hue where the artist put it.
    const float d = 1.0f - startCompression;
    const float newPeak = 1.0f - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    const float g = 1.0f - 1.0f / (desaturation * (peak - newPeak) + 1.0f);
    return lerp(color, float3(newPeak, newPeak, newPeak), g);
}

float4 FragmentMain(Interpolants input) : SV_Target0
{
    // SampleLevel because the HDR target has one mip and this pass is a 1:1
    // blit; an implicit derivative would only compute a level that is already
    // known to be zero.
    const float3 hdr = HdrTexture.SampleLevel(HdrSampler, input.Uv, 0.0f).rgb;

    // Bloom is added in scene-referred light, BEFORE the curve. Adding it after
    // would put a glow on top of an already-compressed image, which is the look
    // of a screen-space filter rather than of light.
    const float3 bloom = BloomTexture.SampleLevel(BloomSampler, input.Uv, 0.0f).rgb;
    const float3 scene = hdr + bloom * ExposureBloom.y;

    // **Exposure is automatic, with the artist control on top.** The measured
    // value is the frame's adapted geometric-mean luminance; dividing by it maps
    // that mean onto middle grey, which is what an exposure meter does. The
    // compensation is in EV stops, which is the unit a person who has used a
    // camera already knows -- and it is `Lighting.ExposureCompensation`.
    //
    // Middle grey at 0.18 is the photographic convention and it is what makes
    // "correctly exposed" mean the same thing here as it does anywhere else.
    const float measured = max(ExposureTexture.SampleLevel(ExposureSampler, float2(0.5f, 0.5f), 0.0f), 1e-4f);
    const float exposure = (0.18f / measured) * exp2(ExposureBloom.x);
    const float3 exposed = max(scene * exposure, float3(0.0f, 0.0f, 0.0f));

    return float4(encodeSrgb(tonemapPbrNeutral(exposed)), 1.0f);
}
