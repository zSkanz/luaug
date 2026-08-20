#include "luaug/imgcmp/image.h"

#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <string_view>
#include <vector>

using luaug::imgcmp::decodePng;
using luaug::imgcmp::encodePng;
using luaug::imgcmp::Image;
using luaug::imgcmp::loadPngFile;
using luaug::imgcmp::LoadResult;
using luaug::imgcmp::makeImage;
using luaug::imgcmp::writePngFile;
using luaug::imgcmp::WriteResult;

namespace {

// Deterministic, dependency-free content: a gradient over all four channels so
// a lost or reordered channel cannot survive the round trip unnoticed.
Image gradient(int width, int height)
{
    Image image = makeImage(width, height);
    for (std::size_t pixel = 0; pixel < image.pixelCount(); ++pixel) {
        const std::size_t base = pixel * 4u;
        image.rgba[base + 0] = static_cast<std::uint8_t>(pixel * 7u);
        image.rgba[base + 1] = static_cast<std::uint8_t>(pixel * 13u);
        image.rgba[base + 2] = static_cast<std::uint8_t>(255u - pixel);
        image.rgba[base + 3] = static_cast<std::uint8_t>(200u + pixel);
    }
    return image;
}

} // namespace

TEST_CASE("makeImage produces a buffer that matches its dimensions")
{
    const Image image = makeImage(3, 2, std::uint8_t{7});

    CHECK(image.wellFormed());
    CHECK(image.pixelCount() == 6);
    CHECK(image.rgba.size() == 24);
    CHECK(image.rgba.front() == 7);

    SUBCASE("degenerate dimensions yield an empty image rather than a lie")
    {
        CHECK(makeImage(0, 4).empty());
        CHECK(makeImage(4, -1).empty());
    }
}

TEST_CASE("a PNG round trip preserves every channel")
{
    const Image original = gradient(5, 3);

    std::vector<std::uint8_t> encoded;
    const WriteResult written = encodePng(original, encoded);
    REQUIRE_MESSAGE(written.ok, written.error);
    REQUIRE_FALSE(encoded.empty());

    const LoadResult decoded = decodePng(encoded);
    REQUIRE_MESSAGE(decoded.ok, decoded.error);

    CHECK(decoded.image.width == original.width);
    CHECK(decoded.image.height == original.height);
    CHECK(decoded.image.rgba == original.rgba);
}

TEST_CASE("decoding reports a reason instead of returning half an image")
{
    SUBCASE("empty input")
    {
        const LoadResult result = decodePng({});

        CHECK_FALSE(result.ok);
        CHECK_FALSE(result.error.empty());
        CHECK(result.image.empty());
    }

    SUBCASE("bytes that are not a PNG")
    {
        constexpr std::string_view text = "this is not a PNG file";
        std::vector<std::uint8_t> bytes;
        bytes.reserve(text.size());
        for (const char character : text)
            bytes.push_back(static_cast<std::uint8_t>(character));

        const LoadResult result = decodePng(bytes);

        CHECK_FALSE(result.ok);
        CHECK_FALSE(result.error.empty());
    }
}

TEST_CASE("encoding refuses an image whose buffer disagrees with its dimensions")
{
    Image malformed = makeImage(4, 4);
    malformed.rgba.pop_back();

    std::vector<std::uint8_t> encoded;
    const WriteResult written = encodePng(malformed, encoded);

    CHECK_FALSE(written.ok);
    CHECK_FALSE(written.error.empty());
    CHECK(encoded.empty());
}

// The two failure paths a screenshot check actually hits: the render never
// produced a file, or the diff cannot be placed where it was asked for. Neither
// case touches the filesystem, so the tests stay side-effect free.
TEST_CASE("file I/O failures are reported, not swallowed")
{
    SUBCASE("a missing input file")
    {
        const LoadResult result = loadPngFile("imgcmp_no_such_input_file.png");

        CHECK_FALSE(result.ok);
        CHECK_FALSE(result.error.empty());
    }

    SUBCASE("an unwritable output path")
    {
        const WriteResult written = writePngFile("imgcmp_no_such_directory/out.png", makeImage(2, 2));

        CHECK_FALSE(written.ok);
        CHECK_FALSE(written.error.empty());
    }
}
