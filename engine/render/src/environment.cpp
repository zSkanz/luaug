#include "luaug/render/environment.h"

#include "luaug/jobs/jobs.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace luaug::render {
namespace {

using core::u32;
using core::usize;

constexpr f32 kPi = 3.14159265358979323846f;

[[nodiscard]] f32 saturate(f32 value) noexcept
{
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

// HLSL's, so the two sides of `evaluateSky` agree on the curve as well as on
// the colours.
[[nodiscard]] f32 smoothstep(f32 edge0, f32 edge1, f32 x) noexcept
{
    const f32 t = saturate((x - edge0) / (edge1 - edge0 != 0.0f ? edge1 - edge0 : 1.0f));
    return t * t * (3.0f - 2.0f * t);
}

[[nodiscard]] Color3 scale(Color3 color, f32 factor) noexcept
{
    return Color3{color.r * factor, color.g * factor, color.b * factor};
}

[[nodiscard]] Color3 modulate(Color3 a, Color3 b) noexcept
{
    return Color3{a.r * b.r, a.g * b.g, a.b * b.b};
}

// The low-discrepancy sequence every importance-sampled integral here walks. Van
// der Corput radical inverse in base two, which is one bit reversal rather than
// a loop.
[[nodiscard]] f32 radicalInverse(u32 bits) noexcept
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<f32>(bits) * 2.3283064365386963e-10f;
}

// GGX's own distribution, sampled: `alpha` is roughness squared, and the half
// vector comes out in the tangent frame of `normal`.
[[nodiscard]] Vec3 importanceSampleGgx(f32 u1, f32 u2, f32 alpha, Vec3 normal) noexcept
{
    const f32 phi = 2.0f * kPi * u1;
    const f32 cosTheta = std::sqrt((1.0f - u2) / (1.0f + (alpha * alpha - 1.0f) * u2));
    const f32 sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));

    const Vec3 local{sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta};

    // The usual degenerate-basis guard: a normal along +Z makes the obvious
    // helper vector parallel to it.
    const Vec3 helper = std::fabs(normal.z) < 0.999f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{1.0f, 0.0f, 0.0f};
    const Vec3 tangentX = core::normalize(core::cross(helper, normal));
    const Vec3 tangentY = core::cross(normal, tangentX);
    return tangentX * local.x + tangentY * local.y + normal * local.z;
}

// The bake's own parallelism. Rows rather than texels, so a range is a
// contiguous run of the output buffer and no two workers touch one cache line.
struct LevelJob
{
    const SkyParams* params = nullptr;
    u32 size = 0;
    f32 alpha = 0.0f;
    u32 sampleCount = 0;
    bool mirror = false;
    u16* out = nullptr;
};

void bakeRows(void* user, usize begin, usize end, u32) noexcept
{
    const auto& job = *static_cast<const LevelJob*>(user);
    const f32 inverseSize = 1.0f / static_cast<f32>(job.size);

    for (usize y = begin; y < end; ++y) {
        for (u32 x = 0; x < job.size; ++x) {
            const f32 u = (static_cast<f32>(x) + 0.5f) * inverseSize;
            const f32 v = (static_cast<f32>(y) + 0.5f) * inverseSize;
            const Vec3 normal = octahedralDirection(u, v);

            Vec3 color{};
            if (job.mirror) {
                color = evaluateSky(*job.params, normal);
            }
            else {
                // Karis's assumption: view == normal == reflection. It is what
                // makes the integral independent of the view and therefore
                // precomputable at all.
                f32 totalWeight = 0.0f;
                for (u32 sample = 0; sample < job.sampleCount; ++sample) {
                    const f32 u1 = (static_cast<f32>(sample) + 0.5f) / static_cast<f32>(job.sampleCount);
                    const f32 u2 = radicalInverse(sample);
                    const Vec3 half = importanceSampleGgx(u1, u2, job.alpha, normal);
                    const Vec3 light = half * (2.0f * core::dot(normal, half)) - normal;
                    const f32 nol = core::dot(normal, light);
                    if (nol <= 0.0f)
                        continue;
                    color = color + evaluateSky(*job.params, light) * nol;
                    totalWeight += nol;
                }
                if (totalWeight > 0.0f)
                    color = color * (1.0f / totalWeight);
            }

            const usize index = (y * job.size + x) * 4;
            job.out[index + 0] = floatToHalf(color.x);
            job.out[index + 1] = floatToHalf(color.y);
            job.out[index + 2] = floatToHalf(color.z);
            job.out[index + 3] = floatToHalf(1.0f);
        }
    }
}

// The octahedron's outer edge: texels that are neighbours in DIRECTION and
// opposite in uv. Averaging each border texel with its partner is what makes a
// bilinear tap across the edge blend two texels that mean nearly the same
// direction, which is the whole of the seam handling this mapping needs.
void mendSeam(u32 size, std::span<u16> texels) noexcept
{
    if (size < 2)
        return;

    const auto average = [&](usize a, usize b) {
        for (u32 channel = 0; channel < 4; ++channel) {
            const f32 mean = 0.5f * (halfToFloat(texels[a * 4 + channel]) + halfToFloat(texels[b * 4 + channel]));
            const u16 encoded = floatToHalf(mean);
            texels[a * 4 + channel] = encoded;
            texels[b * 4 + channel] = encoded;
        }
    };

    const u32 last = size - 1;
    for (u32 i = 0; i < size / 2; ++i) {
        const u32 mirror = last - i;
        average(static_cast<usize>(0) * size + i, static_cast<usize>(0) * size + mirror);
        average(static_cast<usize>(last) * size + i, static_cast<usize>(last) * size + mirror);
        average(static_cast<usize>(i) * size + 0, static_cast<usize>(mirror) * size + 0);
        average(static_cast<usize>(i) * size + last, static_cast<usize>(mirror) * size + last);
    }
}

// The real SH basis through order two, evaluated at a unit direction. The axis
// names are the engine's and not the literature's; only encode and decode have
// to agree, and they do because this is the only copy.
void shBasis(Vec3 d, f32 (&out)[9]) noexcept
{
    out[0] = 0.282095f;
    out[1] = 0.488603f * d.y;
    out[2] = 0.488603f * d.z;
    out[3] = 0.488603f * d.x;
    out[4] = 1.092548f * d.x * d.y;
    out[5] = 1.092548f * d.y * d.z;
    out[6] = 0.315392f * (3.0f * d.z * d.z - 1.0f);
    out[7] = 1.092548f * d.x * d.z;
    out[8] = 0.546274f * (d.x * d.x - d.y * d.y);
}

} // namespace

f32 halfToFloat(u16 value) noexcept
{
    const u32 sign = static_cast<u32>(value & 0x8000u) << 16;
    const u32 exponent = (value >> 10) & 0x1Fu;
    const u32 mantissa = value & 0x3FFu;

    u32 bits = 0;
    if (exponent == 0) {
        // Subnormal or zero. The bake never produces one, but a round trip that
        // silently loses them is a round trip nobody can test with.
        if (mantissa != 0) {
            f32 result = static_cast<f32>(mantissa) * (1.0f / 16777216.0f);
            return (value & 0x8000u) != 0 ? -result : result;
        }
        bits = sign;
    }
    else if (exponent == 31) {
        bits = sign | 0x7F800000u | (mantissa << 13);
    }
    else {
        bits = sign | ((exponent + 112u) << 23) | (mantissa << 13);
    }

    f32 result = 0.0f;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

u16 floatToHalf(f32 value) noexcept
{
    u32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));

    const u32 sign = (bits >> 16) & 0x8000u;
    const auto exponent = static_cast<core::i32>((bits >> 23) & 0xFFu) - 127 + 15;
    const u32 mantissa = bits & 0x7FFFFFu;

    // Saturating rather than producing an infinity: the environment is radiance
    // and an infinity in it propagates through every filter that reads it, so a
    // sky brighter than binary16 can hold clamps to the largest value instead.
    if (exponent >= 31)
        return static_cast<u16>(sign | 0x7BFFu);
    // Flushing to zero rather than encoding a subnormal, which is what every
    // texture unit does with one anyway.
    if (exponent <= 0)
        return static_cast<u16>(sign);

    return static_cast<u16>(sign | (static_cast<u32>(exponent) << 10) | (mantissa >> 13));
}

SkyParams skyParamsFor(Vec3 sunDirection, Color3 fogColor) noexcept
{
    SkyParams params;
    params.sunDirection = core::normalize(sunDirection);

    const f32 elevation = params.sunDirection.y;

    // Civil twilight at one end and a sun clear of the haze at the other:
    // sin(-6 degrees) and sin(8 degrees). Below the first the sun contributes
    // nothing and the scene is lit by its ambient and its lamps.
    params.dayFactor = smoothstep(-0.1045f, 0.1392f, elevation);

    // How far the sun is from the horizon, in the range that matters for
    // colour. sin(17.5 degrees) is where the reddening is spent.
    const f32 height = smoothstep(0.0f, 0.30f, elevation);

    // Rayleigh's consequence rather than Rayleigh: a low sun's light has crossed
    // enough atmosphere to have lost most of its blue.
    const Color3 lowSun{1.0f, 0.42f, 0.16f};
    const Color3 highSun{1.0f, 0.96f, 0.90f};
    params.sunColor = core::lerp(lowSun, highSun, height);

    const Color3 duskZenith{0.18f, 0.16f, 0.34f};
    const Color3 dayZenith{0.10f, 0.26f, 0.62f};
    const f32 nightScale = 0.04f + 0.96f * params.dayFactor;
    params.zenithColor = scale(core::lerp(duskZenith, dayZenith, height), nightScale);

    // `Lighting.FogColor` keeps its meaning exactly at midday -- the tint is
    // white there -- and warms as the sun drops, which is what a horizon does.
    const Color3 tint = core::lerp(params.sunColor, Color3{1.0f, 1.0f, 1.0f}, height);
    params.horizonColor = scale(modulate(fogColor, tint), 0.05f + 0.95f * params.dayFactor);

    // Two cosines of a constant, hoisted out of the quarter-million calls to
    // `evaluateSky` that a full prefilter makes.
    params.discCosOuter = std::cos(params.sunAngularRadius);
    params.discCosInner = std::cos(params.sunAngularRadius * 0.9f);

    return params;
}

Vec3 evaluateSky(const SkyParams& params, Vec3 direction) noexcept
{
    const Vec3 d = core::normalize(direction);

    // Biased towards the horizon: a linear blend in y spends most of the visible
    // sky on the zenith colour and leaves the horizon as a thin band, which
    // reads as a hard line rather than as sky.
    // `sqrt` rather than the `pow(height, 0.45)` this started as, and the change
    // is worth naming because it moved the milestone's largest single cost. A
    // `pow` is tens of nanoseconds; a full prefilter is a quarter of a million
    // sky evaluations, and two `pow` calls in each of them came to two and a
    // half milliseconds a frame in a scene whose sun moves. The exponent goes
    // 0.45 to 0.5, which shifts the gradient by less than the eight-bit output
    // can represent. `sky.hlsl` matches.
    const f32 height = saturate(d.y);
    const Color3 gradient = core::lerp(params.horizonColor, params.zenithColor, std::sqrt(height));

    const f32 below = saturate(-d.y);
    const Color3 sky = core::lerp(gradient, scale(params.horizonColor, 0.35f), std::sqrt(below));

    const f32 cosAngle = core::dot(d, params.sunDirection);
    const f32 disc = smoothstep(params.discCosOuter, params.discCosInner, cosAngle);
    // The sixty-fourth power as six squarings. The second `pow` this function
    // used to call, and the same reasoning as the first.
    const f32 forward = saturate(cosAngle);
    const f32 forward2 = forward * forward;
    const f32 forward4 = forward2 * forward2;
    const f32 forward8 = forward4 * forward4;
    const f32 forward16 = forward8 * forward8;
    const f32 forward32 = forward16 * forward16;
    const f32 glow = forward32 * forward32 * 0.25f;

    // The disc is far brighter than the sky, and that is not decoration: it is
    // what puts a highlight in a mirror. A disc at radiance 1 reflects as a pale
    // smudge, which is the M4 look this milestone exists to replace.
    const f32 discIntensity = kSunDiscIntensity * params.dayFactor;
    const Color3 sun = scale(params.sunColor, disc * discIntensity + glow);

    return Vec3{sky.r + sun.r, sky.g + sun.g, sky.b + sun.b};
}

Vec3 octahedralDirection(f32 u, f32 v) noexcept
{
    const f32 fx = u * 2.0f - 1.0f;
    const f32 fy = v * 2.0f - 1.0f;

    Vec3 d{fx, 1.0f - std::fabs(fx) - std::fabs(fy), fy};
    const f32 fold = saturate(-d.y);
    d.x += d.x >= 0.0f ? -fold : fold;
    d.z += d.z >= 0.0f ? -fold : fold;
    return core::normalize(d);
}

void octahedralUv(Vec3 direction, f32& u, f32& v) noexcept
{
    const Vec3 d = core::normalize(direction);
    const f32 norm = std::fabs(d.x) + std::fabs(d.y) + std::fabs(d.z);
    const f32 nx = d.x / (norm != 0.0f ? norm : 1.0f);
    const f32 ny = d.y / (norm != 0.0f ? norm : 1.0f);
    const f32 nz = d.z / (norm != 0.0f ? norm : 1.0f);

    f32 fx = nx;
    f32 fy = nz;
    if (ny < 0.0f) {
        fx = (1.0f - std::fabs(nz)) * (nx >= 0.0f ? 1.0f : -1.0f);
        fy = (1.0f - std::fabs(nx)) * (nz >= 0.0f ? 1.0f : -1.0f);
    }

    u = fx * 0.5f + 0.5f;
    v = fy * 0.5f + 0.5f;
}

u32 environmentSampleCount(u32 level) noexcept
{
    // Cheap at the mirror end because there is nothing to integrate, and
    // bounded at the rough end because the levels there are sixteen texels.
    //
    // Halved from {1, 32, 64, 96, 128, 128} once the prefilter was measured
    // rather than assumed: it was the largest single cost in a frame whose sun
    // moves. What fewer samples buy is noise, and noise in a GGX prefilter of a
    // sky shows at the SHARP end -- the sun's disc -- which is level zero and
    // takes one sample either way.
    static constexpr u32 kCounts[kEnvironmentMipCount]{1, 24, 32, 48, 64, 64};
    return kCounts[level < kEnvironmentMipCount ? level : kEnvironmentMipCount - 1];
}

void bakeEnvironmentLevel(const SkyParams& params, u32 size, f32 roughness, u32 sampleCount, std::span<u16> out)
{
    if (size == 0 || out.size() < static_cast<usize>(size) * size * 4)
        return;

    LevelJob job{
        .params = &params,
        .size = size,
        .alpha = std::max(roughness * roughness, 1e-3f),
        .sampleCount = std::max(sampleCount, 1u),
        .mirror = roughness <= 0.0f,
        .out = out.data(),
    };

    // A grain of eight rows: enough that a range is real work, small enough that
    // a 128-row level still splits across every worker. The partition is a
    // function of the data (jobs.h), so the output does not depend on how many
    // workers this machine has -- which is what keeps a golden a golden.
    jobs::parallelFor("environment.prefilter", jobs::Domain::Render, 0, size, 8, &bakeRows, &job);

    mendSeam(size, out);
}

void bakeIrradianceSh(const SkyParams& params, Vec3 (&out)[9]) noexcept
{
    for (Vec3& coefficient : out)
        coefficient = Vec3{};

    // A uniform grid in (theta, phi) with the sine weight, rather than a walk
    // over the octahedral texels: the octahedron's solid angle per texel is not
    // constant and the Jacobian that corrects it is more code than this loop.
    constexpr u32 kThetaSteps = 64;
    constexpr u32 kPhiSteps = 128;
    const f32 dTheta = kPi / static_cast<f32>(kThetaSteps);
    const f32 dPhi = 2.0f * kPi / static_cast<f32>(kPhiSteps);

    for (u32 t = 0; t < kThetaSteps; ++t) {
        const f32 theta = (static_cast<f32>(t) + 0.5f) * dTheta;
        const f32 sinTheta = std::sin(theta);
        const f32 cosTheta = std::cos(theta);
        for (u32 p = 0; p < kPhiSteps; ++p) {
            const f32 phi = (static_cast<f32>(p) + 0.5f) * dPhi;
            const Vec3 d{sinTheta * std::cos(phi), cosTheta, sinTheta * std::sin(phi)};
            const Vec3 radiance = evaluateSky(params, d);
            const f32 weight = sinTheta * dTheta * dPhi;

            f32 basis[9]{};
            shBasis(d, basis);
            for (u32 i = 0; i < 9; ++i)
                out[i] = out[i] + radiance * (basis[i] * weight);
        }
    }

    // Ramamoorthi and Hanrahan's cosine-lobe convolution, already divided by pi
    // so a shader multiplies the result straight by the diffuse albedo. A0 = pi,
    // A1 = 2pi/3, A2 = pi/4.
    static constexpr f32 kConvolution[9]{1.0f,  2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f, 0.25f,
                                         0.25f, 0.25f,       0.25f,       0.25f};
    for (u32 i = 0; i < 9; ++i)
        out[i] = out[i] * kConvolution[i];
}

void bakeBrdfLut(u32 size, std::span<u16> out)
{
    if (size == 0 || out.size() < static_cast<usize>(size) * size * 4)
        return;

    constexpr u32 kSamples = 512;
    const Vec3 normal{0.0f, 0.0f, 1.0f};

    for (u32 y = 0; y < size; ++y) {
        const f32 roughness = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(size);
        const f32 alpha = std::max(roughness * roughness, 1e-3f);
        // Smith's geometry term for IBL uses k = alpha / 2, which is the
        // remapping Karis publishes and is NOT the direct-lighting one.
        const f32 k = alpha * 0.5f;

        for (u32 x = 0; x < size; ++x) {
            const f32 nov = std::max((static_cast<f32>(x) + 0.5f) / static_cast<f32>(size), 1e-3f);
            const Vec3 view{std::sqrt(1.0f - nov * nov), 0.0f, nov};

            f32 scaleTerm = 0.0f;
            f32 biasTerm = 0.0f;
            for (u32 sample = 0; sample < kSamples; ++sample) {
                const f32 u1 = (static_cast<f32>(sample) + 0.5f) / static_cast<f32>(kSamples);
                const f32 u2 = radicalInverse(sample);
                const Vec3 half = importanceSampleGgx(u1, u2, alpha, normal);
                const Vec3 light = half * (2.0f * core::dot(view, half)) - view;

                const f32 nol = saturate(light.z);
                const f32 noh = saturate(half.z);
                const f32 voh = saturate(core::dot(view, half));
                if (nol <= 0.0f)
                    continue;

                const f32 g1v = nov / (nov * (1.0f - k) + k);
                const f32 g1l = nol / (nol * (1.0f - k) + k);
                const f32 visibility = g1v * g1l * voh / std::max(noh * nov, 1e-4f);
                const f32 fc = std::pow(1.0f - voh, 5.0f);

                scaleTerm += (1.0f - fc) * visibility;
                biasTerm += fc * visibility;
            }

            const usize index = (static_cast<usize>(y) * size + x) * 4;
            out[index + 0] = floatToHalf(scaleTerm / static_cast<f32>(kSamples));
            out[index + 1] = floatToHalf(biasTerm / static_cast<f32>(kSamples));
            out[index + 2] = floatToHalf(0.0f);
            out[index + 3] = floatToHalf(1.0f);
        }
    }
}

} // namespace luaug::render
