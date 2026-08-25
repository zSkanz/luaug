#include "luaug/asset/image.h"
#include "luaug/asset/texture.h"
#include "luaug/assetc/compiler.h"
#include "luaug/core/i18n.h"

#include <cstddef>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using luaug::core::engineCatalog;
using luaug::core::f32;
using luaug::core::u32;
using luaug::core::usize;

namespace {

void seedRealCatalog()
{
    const auto result = engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
}

// A picture with real structure -- gradients and a hard edge -- because a
// texture codec is trivially lossless on flat colour and this needs to be a
// test of the codec rather than of a constant.
[[nodiscard]] luaug::asset::Image testImage(u32 size, bool withAlpha)
{
    luaug::asset::Image image;
    image.width = size;
    image.height = size;
    image.sourceChannels = withAlpha ? 4u : 3u;
    image.pixels.resize(static_cast<usize>(size) * size * 4u);

    for (u32 y = 0; y < size; ++y) {
        for (u32 x = 0; x < size; ++x) {
            const usize at = (static_cast<usize>(y) * size + x) * 4u;
            const bool leftHalf = x < size / 2;
            image.pixels[at + 0] = static_cast<std::byte>(x * 255u / (size - 1u));
            image.pixels[at + 1] = static_cast<std::byte>(y * 255u / (size - 1u));
            image.pixels[at + 2] = static_cast<std::byte>(leftHalf ? 240u : 16u);
            image.pixels[at + 3] = static_cast<std::byte>(withAlpha ? (leftHalf ? 255u : 64u) : 255u);
        }
    }
    return image;
}

} // namespace

TEST_CASE("a texture survives the encode and comes back as pixels")
{
    seedRealCatalog();

    const luaug::asset::Image source = testImage(64, false);
    std::vector<std::byte> ktx2;
    REQUIRE_FALSE(luaug::assetc::encodeTexture(source, true, ktx2).has_value());
    REQUIRE_FALSE(ktx2.empty());

    luaug::asset::TranscodeOptions options;
    // Uncompressed, so the comparison below is against the encoder's loss and
    // not also against BC7's.
    options.forceUncompressed = true;

    luaug::asset::TextureAsset decoded;
    REQUIRE_FALSE(luaug::asset::transcodeTexture(ktx2, options, decoded).has_value());

    CHECK(decoded.width == 64);
    CHECK(decoded.height == 64);
    CHECK(decoded.format == luaug::asset::TextureFormat::Rgba8);
    CHECK(decoded.srgb);
    REQUIRE(decoded.valid());
    // Mips down to 1x1: 64, 32, 16, 8, 4, 2, 1.
    CHECK(decoded.mips.size() == 7);
    CHECK(decoded.mips[0].width == 64);
    CHECK(decoded.mips.back().width == 1);

    // UASTC is lossy, so this is a similarity check rather than an equality
    // one -- but a LOOSE one would pass on a black image, which is the failure
    // this case exists to catch. Mean absolute error per channel, over the RGB
    // of the base level.
    const luaug::asset::TextureMip& base = decoded.mips[0];
    REQUIRE(base.size == source.pixels.size());
    double totalError = 0.0;
    usize samples = 0;
    for (usize i = 0; i < base.size; i += 4) {
        for (usize channel = 0; channel < 3; ++channel) {
            const auto expected = static_cast<int>(static_cast<unsigned char>(source.pixels[i + channel]));
            const auto actual = static_cast<int>(static_cast<unsigned char>(decoded.pixels[base.offset + i + channel]));
            totalError += std::abs(expected - actual);
            samples += 1;
        }
    }
    const double meanError = totalError / static_cast<double>(samples);
    CHECK(meanError < 4.0);

    // And the picture is still the picture: the hard edge in the blue channel
    // is still where it was put. A codec that returned a flat average would
    // pass the mean-error check on this image and fail here.
    const usize leftAt = base.offset + (static_cast<usize>(32) * 64 + 8) * 4 + 2;
    const usize rightAt = base.offset + (static_cast<usize>(32) * 64 + 56) * 4 + 2;
    CHECK(static_cast<int>(static_cast<unsigned char>(decoded.pixels[leftAt])) > 180);
    CHECK(static_cast<int>(static_cast<unsigned char>(decoded.pixels[rightAt])) < 70);
}

TEST_CASE("alpha survives the round trip and is reported")
{
    seedRealCatalog();

    const luaug::asset::Image source = testImage(32, true);
    std::vector<std::byte> ktx2;
    REQUIRE_FALSE(luaug::assetc::encodeTexture(source, true, ktx2).has_value());

    luaug::asset::TranscodeOptions options;
    options.forceUncompressed = true;
    luaug::asset::TextureAsset decoded;
    REQUIRE_FALSE(luaug::asset::transcodeTexture(ktx2, options, decoded).has_value());

    CHECK(decoded.hasAlpha);
    const luaug::asset::TextureMip& base = decoded.mips[0];
    const usize opaqueAt = base.offset + (static_cast<usize>(16) * 32 + 4) * 4 + 3;
    const usize fadedAt = base.offset + (static_cast<usize>(16) * 32 + 28) * 4 + 3;
    CHECK(static_cast<int>(static_cast<unsigned char>(decoded.pixels[opaqueAt])) > 200);
    CHECK(static_cast<int>(static_cast<unsigned char>(decoded.pixels[fadedAt])) < 120);
}

TEST_CASE("one asset transcodes to whatever the device can sample")
{
    seedRealCatalog();

    const luaug::asset::Image source = testImage(32, true);
    std::vector<std::byte> ktx2;
    REQUIRE_FALSE(luaug::assetc::encodeTexture(source, true, ktx2).has_value());

    // The whole point of the container: the pack carries UASTC once and the
    // device decides what it becomes.
    struct Case
    {
        luaug::asset::TranscodeOptions options;
        luaug::asset::TextureFormat expected;
    };
    const Case cases[] = {
        {{}, luaug::asset::TextureFormat::Bc7Rgba},
        {{false, true, true, false, false}, luaug::asset::TextureFormat::Bc3Rgba},
        {{false, false, false, false, false}, luaug::asset::TextureFormat::Rgba8},
        {{true, true, true, true, false}, luaug::asset::TextureFormat::Rgba8},
    };

    for (const Case& entry : cases) {
        luaug::asset::TextureAsset decoded;
        REQUIRE_FALSE(luaug::asset::transcodeTexture(ktx2, entry.options, decoded).has_value());
        CHECK(decoded.format == entry.expected);
        CHECK(decoded.width == 32);
        // Block formats pack sixteen texels into eight or sixteen bytes, so the
        // base level is smaller than the pixels it represents -- which is the
        // whole reason to want one.
        if (luaug::asset::isBlockCompressed(decoded.format)) {
            CHECK(decoded.mips[0].size < static_cast<usize>(32) * 32 * 4);
        }
    }
}

TEST_CASE("baseLevelOnly stops at the level the UI draws")
{
    seedRealCatalog();

    const luaug::asset::Image source = testImage(32, false);
    std::vector<std::byte> ktx2;
    REQUIRE_FALSE(luaug::assetc::encodeTexture(source, true, ktx2).has_value());

    luaug::asset::TranscodeOptions options;
    options.baseLevelOnly = true;
    luaug::asset::TextureAsset decoded;
    REQUIRE_FALSE(luaug::asset::transcodeTexture(ktx2, options, decoded).has_value());

    // A `ScreenGui` image is drawn at one size; mips would be memory nobody
    // samples.
    CHECK(decoded.mips.size() == 1);
    CHECK(decoded.mips[0].width == 32);
}

TEST_CASE("encoding the same image twice produces the same bytes")
{
    seedRealCatalog();

    const luaug::asset::Image source = testImage(64, true);
    std::vector<std::byte> first;
    std::vector<std::byte> second;
    REQUIRE_FALSE(luaug::assetc::encodeTexture(source, true, first).has_value());
    REQUIRE_FALSE(luaug::assetc::encodeTexture(source, true, second).has_value());

    // The property the content hash rests on. The encoder is deliberately
    // single-threaded and built without SSE for exactly this.
    CHECK(first == second);
}

TEST_CASE("the transfer function reaches the bytes")
{
    seedRealCatalog();

    // **The whole point of `srgb` being a parameter.** The same pixels encoded
    // as colour and as data are different bytes, because the encoder bends the
    // values through the transfer curve before compressing them. If these came
    // back equal, the flag would be reaching nothing -- which is exactly the
    // state this compiler was in, with every normal and ORM map marked sRGB.
    const luaug::asset::Image source = testImage(64, true);
    std::vector<std::byte> colour;
    std::vector<std::byte> data;
    REQUIRE_FALSE(luaug::assetc::encodeTexture(source, true, colour).has_value());
    REQUIRE_FALSE(luaug::assetc::encodeTexture(source, false, data).has_value());

    CHECK_FALSE(colour.empty());
    CHECK_FALSE(data.empty());
    CHECK(colour != data);

    // And each is still deterministic on its own, which is the property the
    // content hash rests on.
    std::vector<std::byte> again;
    REQUIRE_FALSE(luaug::assetc::encodeTexture(source, false, again).has_value());
    CHECK(again == data);
}

TEST_CASE("an image with no pixels is refused rather than encoded")
{
    seedRealCatalog();

    luaug::asset::Image empty;
    std::vector<std::byte> out;
    const auto error = luaug::assetc::encodeTexture(empty, true, out);
    REQUIRE(error.has_value());
    CHECK(error->message.find("asset.texture.err.encode_failed") != std::string::npos);
    CHECK(out.empty());
}
