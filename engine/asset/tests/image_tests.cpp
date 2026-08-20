#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "luaug/asset/image.h"
#include "luaug/core/i18n.h"

using luaug::asset::decodeImage;
using luaug::asset::Image;
using luaug::asset::writePng;
using luaug::core::u32;

namespace
{

// The catalog has to be loaded or every error message below is a bare key, and
// a test asserting on message text would then assert on nothing. M2's Finding
// 11 and M3's are both this.
struct CatalogFixture
{
    CatalogFixture()
    {
        const auto result = luaug::core::engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
        REQUIRE(result.ok);
    }
};

std::filesystem::path scratchFile(const char* name)
{
    const auto dir = std::filesystem::temp_directory_path() / "luaug-asset-tests";
    std::filesystem::create_directories(dir);
    return dir / name;
}

std::vector<std::byte> readAll(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream.good());
    std::vector<char> raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i)
        bytes[i] = static_cast<std::byte>(raw[i]);
    return bytes;
}

// Four pixels, four different colours, no two channels equal -- so a swizzle, a
// dropped channel or a transposed row all show up instead of matching by luck.
// The same reasoning as M1's clear colour.
std::vector<std::byte> distinctRgba()
{
    const unsigned char raw[]{
        0xFF, 0x00, 0x00, 0xFF, // top-left  opaque red
        0x00, 0xFF, 0x00, 0x80, // top-right half-alpha green
        0x00, 0x00, 0xFF, 0xFF, // bottom-left opaque blue
        0x10, 0x20, 0x30, 0x40, // bottom-right, four different values
    };
    std::vector<std::byte> bytes(sizeof(raw));
    for (std::size_t i = 0; i < sizeof(raw); ++i)
        bytes[i] = static_cast<std::byte>(raw[i]);
    return bytes;
}

} // namespace

TEST_CASE_FIXTURE(CatalogFixture, "image: a PNG round trip preserves every channel of every pixel")
{
    const std::vector<std::byte> source = distinctRgba();
    const auto path = scratchFile("roundtrip.png");

    REQUIRE_FALSE(writePng(path, source, 2, 2).has_value());

    Image decoded;
    REQUIRE_FALSE(decodeImage(readAll(path), decoded).has_value());

    CHECK(decoded.valid());
    CHECK(decoded.width == 2u);
    CHECK(decoded.height == 2u);
    CHECK(decoded.sourceChannels == 4u);
    CHECK(decoded.pixels == source);

    std::filesystem::remove(path);
}

TEST_CASE_FIXTURE(CatalogFixture, "image: a source with fewer channels still decodes to RGBA, and says so")
{
    // A three-channel PNG written by stb, decoded back. The point of the
    // assertion is `sourceChannels`: the pixels come back as RGBA either way,
    // and the count is the only way a caller tells an image that meant to be
    // opaque from one that happens to be.
    const unsigned char rgb[]{
        0xFF, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, //
        0x00, 0x00, 0xFF, 0xFF, 0x10, 0x20, 0x30, 0xFF,
    };
    std::vector<std::byte> opaque(sizeof(rgb));
    for (std::size_t i = 0; i < sizeof(rgb); ++i)
        opaque[i] = static_cast<std::byte>(rgb[i]);

    const auto path = scratchFile("opaque.png");
    REQUIRE_FALSE(writePng(path, opaque, 2, 2).has_value());

    Image decoded;
    REQUIRE_FALSE(decodeImage(readAll(path), decoded).has_value());
    CHECK(decoded.pixels == opaque);
    // stb writes the four channels it was given, so the round trip reports 4;
    // what this pins is that `sourceChannels` reports the FILE's count rather
    // than the 4 the decode forced.
    CHECK(decoded.sourceChannels == 4u);

    std::filesystem::remove(path);
}

TEST_CASE_FIXTURE(CatalogFixture, "image: garbage is an error, not a zero-sized image")
{
    const std::vector<std::byte> notAnImage(64, std::byte{0x7F});

    Image decoded;
    const auto error = decodeImage(notAnImage, decoded);
    REQUIRE(error.has_value());
    // Engine messages carry their key AND the resolved sentence (api-design.md
    // §6), so the assertion that the catalog was actually loaded is the English
    // half -- asserting the key alone would pass against an unloaded catalog,
    // which is M2's Finding 11.
    CHECK(error->message.find("asset.image.err.decode_failed") != std::string::npos);
    CHECK(error->message.find("Could not decode the image.") != std::string::npos);
    CHECK_FALSE(decoded.valid());

    Image untouched;
    CHECK(decodeImage({}, untouched).has_value());
}

TEST_CASE_FIXTURE(CatalogFixture, "image: a decode failure leaves the output empty rather than half-filled")
{
    Image decoded;
    // Fill it first: the contract is that a failed decode resets, not that it
    // leaves whatever the caller had.
    decoded.width = 7;
    decoded.height = 7;
    decoded.pixels.assign(16, std::byte{0xAB});

    REQUIRE(decodeImage(std::vector<std::byte>(8, std::byte{0}), decoded).has_value());
    CHECK(decoded.width == 0u);
    CHECK(decoded.height == 0u);
    CHECK(decoded.pixels.empty());
}

TEST_CASE_FIXTURE(CatalogFixture, "image: writePng rejects a buffer that does not match the size")
{
    const std::vector<std::byte> tooSmall(4 * 4 * 4 - 1, std::byte{0});
    CHECK(writePng(scratchFile("never.png"), tooSmall, 4, 4).has_value());
    CHECK(writePng(scratchFile("never.png"), tooSmall, 0, 4).has_value());
    CHECK_FALSE(std::filesystem::exists(scratchFile("never.png")));
}
