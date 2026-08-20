// SHA-1 against the vectors in RFC 3174 §7.3 / FIPS 180-1, and base64 against
// RFC 4648 §10. Published vectors on purpose: a digest tested against its own
// output proves only that it is deterministic.

#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "luaug/net/detail/digest.h"

using luaug::core::u8;
using luaug::net::detail::base64Encode;
using luaug::net::detail::sha1;

namespace
{

std::string hex(const luaug::net::detail::Sha1Digest& digest)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(40);
    for (const u8 byte : digest)
    {
        out.push_back(kDigits[(byte >> 4) & 0x0Fu]);
        out.push_back(kDigits[byte & 0x0Fu]);
    }
    return out;
}

std::string base64Of(std::string_view text)
{
    return base64Encode(std::span<const u8>(reinterpret_cast<const u8*>(text.data()), text.size()));
}

} // namespace

TEST_CASE("sha1 matches the published vectors")
{
    CHECK(hex(sha1(std::string_view{""})) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    CHECK(hex(sha1(std::string_view{"abc"})) == "a9993e364706816aba3e25717850c26c9cd0d89d");

    // 56 bytes: the padding does not fit in the same block as the message, so
    // this is the case that catches a wrong two-block tail.
    CHECK(hex(sha1(std::string_view{"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"}))
        == "84983e441c3bd26ebaae4aa1f95129e5e54670f1");

    // 55 bytes: the largest message whose 0x80 terminator and 8-byte length
    // still fit in the same block, which is the tighter half of the boundary
    // the 56-byte vector above covers from the other side. FIPS publishes no
    // vector at this length; the expected value is cross-checked against
    // Windows CNG (`[System.Security.Cryptography.SHA1]`), an implementation we
    // did not write. A digest checked against our own output would assert
    // nothing.
    CHECK(hex(sha1(std::string_view{"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnop"}))
        == "47b172810795699fe739197d1a1f5960700242f1");

    const std::string million(1000000, 'a');
    CHECK(hex(sha1(million)) == "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
}

TEST_CASE("base64 matches RFC 4648's vectors, padding included")
{
    CHECK(base64Of("") == "");
    CHECK(base64Of("f") == "Zg==");
    CHECK(base64Of("fo") == "Zm8=");
    CHECK(base64Of("foo") == "Zm9v");
    CHECK(base64Of("foob") == "Zm9vYg==");
    CHECK(base64Of("fooba") == "Zm9vYmE=");
    CHECK(base64Of("foobar") == "Zm9vYmFy");
}

TEST_CASE("base64 encodes the high bytes that a random nonce will contain")
{
    // The handshake nonce is 16 random bytes, so the alphabet's top half and
    // the two non-alphanumeric characters are reachable in ordinary use -- and
    // an implementation that got the sign of a byte wrong passes every
    // ASCII-only vector above.
    const std::vector<u8> bytes{0xFBu, 0xFFu, 0xBEu, 0xFFu, 0x00u, 0xFFu};
    CHECK(base64Encode(bytes) == "+/++/wD/");
}
