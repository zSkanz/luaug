#include "luaug/imgcmp/compare.h"

#include <cstdint>

namespace luaug::imgcmp {
namespace {

constexpr std::size_t kChannels = 4;

// Rec.709 luma in fixed point, so the backdrop is deterministic across
// platforms: a diff image that changes with the floating-point unit would be
// useless as evidence attached to a gate record.
constexpr int kLumaR = 54;
constexpr int kLumaG = 183;
constexpr int kLumaB = 19;

// Magenta reads as "not from the scene" against both bright and dark content,
// which red does not.
constexpr std::uint8_t kMarkR = 255;
constexpr std::uint8_t kMarkG = 0;
constexpr std::uint8_t kMarkB = 255;

[[nodiscard]] bool comparable(const Image& actual, const Image& expected) noexcept
{
    return actual.wellFormed() && expected.wellFormed() && actual.width == expected.width &&
           actual.height == expected.height;
}

// Largest per-channel delta of one pixel, both images indexed at `base`.
[[nodiscard]] int pixelDelta(const Image& actual, const Image& expected, std::size_t base) noexcept
{
    int worst = 0;
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        const int a = actual.rgba[base + channel];
        const int e = expected.rgba[base + channel];
        const int delta = a > e ? a - e : e - a;
        if (delta > worst)
            worst = delta;
    }
    return worst;
}

} // namespace

CompareResult compare(const Image& actual, const Image& expected, const CompareOptions& options)
{
    CompareResult result;
    if (!comparable(actual, expected))
        return result;

    result.status = CompareStatus::Match;

    const std::size_t byteCount = actual.rgba.size();
    for (std::size_t base = 0; base < byteCount; base += kChannels) {
        const int delta = pixelDelta(actual, expected, base);
        if (delta > result.maxChannelDelta)
            result.maxChannelDelta = delta;
        if (delta > options.tolerance)
            ++result.differentPixels;
    }

    if (result.differentPixels > options.maxDifferentPixels)
        result.status = CompareStatus::PixelsDiffer;

    return result;
}

Image renderDiff(const Image& actual, const Image& expected, const CompareOptions& options)
{
    if (!comparable(actual, expected))
        return Image{};

    Image diff = makeImage(actual.width, actual.height);

    const std::size_t byteCount = actual.rgba.size();
    for (std::size_t base = 0; base < byteCount; base += kChannels) {
        if (pixelDelta(actual, expected, base) > options.tolerance) {
            diff.rgba[base + 0] = kMarkR;
            diff.rgba[base + 1] = kMarkG;
            diff.rgba[base + 2] = kMarkB;
        }
        else {
            const int luma =
                (kLumaR * actual.rgba[base + 0] + kLumaG * actual.rgba[base + 1] + kLumaB * actual.rgba[base + 2]) >> 8;
            // Compressed into a dark band so that any highlight stays the
            // brightest thing in the image, whatever the scene looked like.
            const auto shade = static_cast<std::uint8_t>(24 + (luma >> 2));
            diff.rgba[base + 0] = shade;
            diff.rgba[base + 1] = shade;
            diff.rgba[base + 2] = shade;
        }
        diff.rgba[base + 3] = 255;
    }

    return diff;
}

} // namespace luaug::imgcmp
