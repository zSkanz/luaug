#include "luaug/core/base64.h"

#include <doctest/doctest.h>
#include <string>

using namespace luaug;

namespace {

[[nodiscard]] std::vector<core::u8> bytesOf(std::string_view text)
{
    return std::vector<core::u8>(text.begin(), text.end());
}

} // namespace

TEST_CASE("base64 matches the standard alphabet, padding included")
{
    // RFC 4648's own vectors, which is the point of using them: an encoder that
    // agrees with them agrees with every other decoder in the world.
    CHECK(core::base64Encode(bytesOf("")) == "");
    CHECK(core::base64Encode(bytesOf("f")) == "Zg==");
    CHECK(core::base64Encode(bytesOf("fo")) == "Zm8=");
    CHECK(core::base64Encode(bytesOf("foo")) == "Zm9v");
    CHECK(core::base64Encode(bytesOf("foob")) == "Zm9vYg==");
    CHECK(core::base64Encode(bytesOf("fooba")) == "Zm9vYmE=");
    CHECK(core::base64Encode(bytesOf("foobar")) == "Zm9vYmFy");
}

TEST_CASE("every byte survives a round trip")
{
    std::vector<core::u8> all;
    all.reserve(256);
    for (int value = 0; value < 256; ++value) {
        all.push_back(static_cast<core::u8>(value));
    }

    const std::optional<std::vector<core::u8>> back = core::base64Decode(core::base64Encode(all));
    REQUIRE(back.has_value());
    CHECK(*back == all);

    // And every length, so the two padded cases are covered at every offset.
    for (core::usize length = 0; length <= 32; ++length) {
        const std::vector<core::u8> slice(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(length));
        const std::optional<std::vector<core::u8>> round = core::base64Decode(core::base64Encode(slice));
        CAPTURE(length);
        REQUIRE(round.has_value());
        CHECK(*round == slice);
    }
}

TEST_CASE("text that is not base64 is refused rather than repaired")
{
    // **Refusing is the feature.** A decoder that skipped a stray character
    // would turn a corrupt scene into a plausible one, and a world that loads
    // wrong is worse than one that says it cannot load.
    CHECK_FALSE(core::base64Decode("Zm9vYmF").has_value());   // not a multiple of four
    CHECK_FALSE(core::base64Decode("Zm9v Ymfy").has_value()); // a space
    CHECK_FALSE(core::base64Decode("Zm9vYm!y").has_value());  // outside the alphabet
    CHECK_FALSE(core::base64Decode("Zg==Zg==").has_value());  // padding before the last group
    CHECK_FALSE(core::base64Decode("Z=g=").has_value());      // padding in the wrong place
    CHECK_FALSE(core::base64Decode("====").has_value());
}
