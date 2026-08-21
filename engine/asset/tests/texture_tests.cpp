#include "luaug/asset/texture.h"
#include "luaug/core/i18n.h"

#include <cstddef>
#include <cstring>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace luaug::asset;
using luaug::core::engineCatalog;
using luaug::core::usize;

namespace {

void seedRealCatalog()
{
    const auto result = engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
}

// The KTX2 identifier, so the bytes below are the shape of a real file rather
// than obvious noise -- a reader that only checked the first byte would pass a
// test built from zeroes.
constexpr unsigned char Ktx2Identifier[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};

} // namespace

// The round trip -- encode a real image, transcode it back -- lives in
// `tools/assetc`, because encoding needs the basis ENCODER and the engine is
// deliberately linked against only the transcoder. What belongs here is the
// half a shipped game actually runs: what happens to bad bytes.

TEST_CASE("an empty texture blob is refused")
{
    seedRealCatalog();

    TextureAsset texture;
    const auto error = transcodeTexture({}, {}, texture);
    REQUIRE(error.has_value());
    CHECK(error->message.find("asset.texture.err.malformed") != std::string::npos);
    CHECK_FALSE(texture.valid());
}

TEST_CASE("a file that is not KTX2 is refused rather than parsed")
{
    seedRealCatalog();

    std::vector<std::byte> noise(4096);
    for (usize i = 0; i < noise.size(); ++i) {
        noise[i] = static_cast<std::byte>((i * 37u) & 0xFFu);
    }

    TextureAsset texture;
    const auto error = transcodeTexture(noise, {}, texture);
    REQUIRE(error.has_value());
    CHECK(error->message.find("asset.texture.err.malformed") != std::string::npos);
}

TEST_CASE("a truncated KTX2 header is an error at every length")
{
    seedRealCatalog();

    // A plausible-looking header, cut short at every point. The identifier is
    // real, so this exercises the parser rather than the first `memcmp`.
    std::vector<std::byte> header(160);
    std::memcpy(header.data(), Ktx2Identifier, sizeof(Ktx2Identifier));
    for (usize i = sizeof(Ktx2Identifier); i < header.size(); ++i) {
        header[i] = static_cast<std::byte>((i * 13u) & 0xFFu);
    }

    for (usize length = 0; length <= header.size(); ++length) {
        std::vector<std::byte> truncated(header.begin(), header.begin() + static_cast<std::ptrdiff_t>(length));
        TextureAsset texture;
        // The requirement is an answer and not a crash. A malformed file that
        // happened to parse into an empty texture would still be refused by
        // `valid()`; what must never happen is a read past the end.
        const auto error = transcodeTexture(truncated, {}, texture);
        CHECK((error.has_value() || !texture.valid()));
    }
}

TEST_CASE("format names and block sizes agree with each other")
{
    CHECK(std::string(textureFormatName(TextureFormat::Rgba8)) == "rgba8");
    CHECK(std::string(textureFormatName(TextureFormat::Bc7Rgba)) == "bc7");

    CHECK_FALSE(isBlockCompressed(TextureFormat::Rgba8));
    CHECK_FALSE(isBlockCompressed(TextureFormat::Unknown));
    CHECK(isBlockCompressed(TextureFormat::Bc1Rgb));
    CHECK(isBlockCompressed(TextureFormat::Bc3Rgba));
    CHECK(isBlockCompressed(TextureFormat::Bc5Rg));
    CHECK(isBlockCompressed(TextureFormat::Bc7Rgba));
}
