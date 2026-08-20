#include "luaug/imgcmp/compare.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>

using luaug::imgcmp::compare;
using luaug::imgcmp::CompareOptions;
using luaug::imgcmp::CompareStatus;
using luaug::imgcmp::Image;
using luaug::imgcmp::makeImage;
using luaug::imgcmp::renderDiff;

namespace {

// Every image here is generated: the comparator has to be testable on a machine
// with no GPU and with no fixtures checked into the repo (roadmap M1).
using Rgba = std::array<std::uint8_t, 4>;

std::size_t offsetOf(const Image& image, int x, int y)
{
    const auto row = static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width);
    return (row + static_cast<std::size_t>(x)) * 4u;
}

void setPixel(Image& image, int x, int y, Rgba value)
{
    const std::size_t base = offsetOf(image, x, y);
    for (std::size_t channel = 0; channel < 4u; ++channel)
        image.rgba[base + channel] = value[channel];
}

Rgba pixelAt(const Image& image, int x, int y)
{
    const std::size_t base = offsetOf(image, x, y);
    return Rgba{image.rgba[base], image.rgba[base + 1], image.rgba[base + 2], image.rgba[base + 3]};
}

// A flat mid-gray canvas: uniform so that any reported difference can only come
// from the pixels a test deliberately perturbs.
Image grayCanvas(int width = 4, int height = 4, std::uint8_t level = 100)
{
    Image image = makeImage(width, height, level);
    for (std::size_t base = 3u; base < image.rgba.size(); base += 4u)
        image.rgba[base] = 255;
    return image;
}

} // namespace

TEST_CASE("identical images match")
{
    const Image expected = grayCanvas();
    const Image actual = expected;

    const luaug::imgcmp::CompareResult result = compare(actual, expected, CompareOptions{});

    CHECK(result.status == CompareStatus::Match);
    CHECK(result.passed());
    CHECK(result.differentPixels == 0);
    CHECK(result.maxChannelDelta == 0);
}

TEST_CASE("tolerance is the largest per-channel delta still counted as equal")
{
    const Image expected = grayCanvas();
    const CompareOptions options{2, 0};

    SUBCASE("a delta exactly at the tolerance is not a difference")
    {
        Image actual = expected;
        setPixel(actual, 1, 1, Rgba{102, 100, 100, 255});

        const luaug::imgcmp::CompareResult result = compare(actual, expected, options);

        CHECK(result.status == CompareStatus::Match);
        CHECK(result.differentPixels == 0);
        // Reported even though it passed -- this is the headroom reading.
        CHECK(result.maxChannelDelta == 2);
    }

    SUBCASE("one step past the tolerance is a difference")
    {
        Image actual = expected;
        setPixel(actual, 1, 1, Rgba{103, 100, 100, 255});

        const luaug::imgcmp::CompareResult result = compare(actual, expected, options);

        CHECK(result.status == CompareStatus::PixelsDiffer);
        CHECK_FALSE(result.passed());
        CHECK(result.differentPixels == 1);
        CHECK(result.maxChannelDelta == 3);
    }

    SUBCASE("a zero tolerance rejects a single-step drift")
    {
        Image actual = expected;
        setPixel(actual, 0, 0, Rgba{101, 100, 100, 255});

        const luaug::imgcmp::CompareResult result = compare(actual, expected, CompareOptions{0, 0});

        CHECK(result.status == CompareStatus::PixelsDiffer);
        CHECK(result.differentPixels == 1);
    }

    SUBCASE("any channel can trip the comparison, alpha included")
    {
        Image actual = expected;
        setPixel(actual, 2, 3, Rgba{100, 100, 100, 200});

        const luaug::imgcmp::CompareResult result = compare(actual, expected, options);

        CHECK(result.status == CompareStatus::PixelsDiffer);
        CHECK(result.differentPixels == 1);
        CHECK(result.maxChannelDelta == 55);
    }
}

TEST_CASE("max-different-pixels bounds how many pixels may differ")
{
    const Image expected = grayCanvas();
    Image actual = expected;
    setPixel(actual, 0, 0, Rgba{200, 100, 100, 255});
    setPixel(actual, 1, 0, Rgba{100, 200, 100, 255});
    setPixel(actual, 2, 0, Rgba{100, 100, 200, 255});

    SUBCASE("one short of the count still fails")
    {
        const luaug::imgcmp::CompareResult result = compare(actual, expected, CompareOptions{2, 2});

        CHECK(result.status == CompareStatus::PixelsDiffer);
        CHECK(result.differentPixels == 3);
    }

    SUBCASE("exactly the count passes")
    {
        const luaug::imgcmp::CompareResult result = compare(actual, expected, CompareOptions{2, 3});

        CHECK(result.status == CompareStatus::Match);
        // The count and the max delta are reported whatever the verdict.
        CHECK(result.differentPixels == 3);
        CHECK(result.maxChannelDelta == 100);
    }
}

TEST_CASE("images that cannot be compared pixel-for-pixel are never a match")
{
    const Image expected = grayCanvas(4, 4);

    SUBCASE("differing dimensions")
    {
        const Image actual = grayCanvas(4, 5);

        const luaug::imgcmp::CompareResult result = compare(actual, expected, CompareOptions{});

        CHECK(result.status == CompareStatus::SizeMismatch);
        CHECK_FALSE(result.passed());
        CHECK(result.differentPixels == 0);
        CHECK(result.maxChannelDelta == 0);
    }

    SUBCASE("a buffer that disagrees with its declared size")
    {
        Image actual = expected;
        actual.rgba.pop_back();

        CHECK(compare(actual, expected, CompareOptions{}).status == CompareStatus::SizeMismatch);
    }

    SUBCASE("an empty image has nothing to verify")
    {
        const Image empty;

        CHECK(compare(empty, empty, CompareOptions{}).status == CompareStatus::SizeMismatch);
    }
}

TEST_CASE("the diff image marks exactly the differing pixels")
{
    const Image expected = grayCanvas();
    Image actual = expected;
    setPixel(actual, 3, 2, Rgba{100, 100, 220, 255});
    // Within tolerance: must be treated as background, not as a hit.
    setPixel(actual, 0, 1, Rgba{102, 100, 100, 255});

    const Image diff = renderDiff(actual, expected, CompareOptions{2, 0});

    REQUIRE(diff.wellFormed());
    CHECK(diff.width == expected.width);
    CHECK(diff.height == expected.height);

    CHECK(pixelAt(diff, 3, 2) == Rgba{255, 0, 255, 255});

    SUBCASE("untouched pixels stay in a dark opaque band")
    {
        for (const Rgba background : {pixelAt(diff, 0, 0), pixelAt(diff, 0, 1)}) {
            CHECK(background[0] == background[1]);
            CHECK(background[1] == background[2]);
            CHECK(background[0] < 128);
            CHECK(background[3] == 255);
        }
    }

    SUBCASE("a diff needs a common pixel grid")
    {
        CHECK(renderDiff(actual, grayCanvas(2, 2), CompareOptions{}).empty());
    }
}
