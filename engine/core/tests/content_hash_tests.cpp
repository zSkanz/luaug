#include "luaug/core/content_hash.h"

#include <cstddef>
#include <doctest/doctest.h>
#include <string>
#include <unordered_map>
#include <vector>

using namespace luaug::core;

namespace {

// The input every official BLAKE3 test vector uses: a repeating sequence of
// 251 bytes, 0, 1, 2, ..., 250, 0, 1, ... The generator is written out here
// rather than the data, because the data for the largest case is 64 KiB.
[[nodiscard]] std::vector<std::byte> vectorInput(usize length)
{
    std::vector<std::byte> input(length);
    for (usize i = 0; i < length; ++i) {
        input[i] = static_cast<std::byte>(i % 251);
    }
    return input;
}

} // namespace

TEST_CASE("hashes match the official BLAKE3 test vectors")
{
    // The first 32 hex digits of each published digest, from
    // third_party/blake3/test_vectors/test_vectors.json -- which is upstream's
    // own file at the pinned commit rather than a number recalled from
    // anywhere. Truncating to 128 bits is BLAKE3's documented shape, so the
    // prefix of the published hash IS the expected value.
    struct Vector
    {
        usize inputLength;
        const char* hex;
    };
    static constexpr Vector vectors[] = {
        {0, "af1349b9f5f9a1a6a0404dea36dcc949"},    {1, "2d3adedff11b61f14c886e35afa03673"},
        {2, "7b7015bb92cf0b318037702a6cdd81de"},    {3, "e1be4d7a8ab5560aa4199eea339849ba"},
        {1024, "42214739f095a406f3fc83deb889744a"}, {2048, "e776b6028c7cd22a4d0ba182a8bf6220"},
    };

    for (const Vector& entry : vectors) {
        const std::vector<std::byte> input = vectorInput(entry.inputLength);
        const ContentHash hash = hashBytes(input);
        CHECK(hash.toHex() == std::string(entry.hex));
    }
}

TEST_CASE("the incremental hasher agrees with the one-shot one")
{
    const std::vector<std::byte> input = vectorInput(3000);

    ContentHasher hasher;
    // Deliberately uneven, and deliberately crossing BLAKE3's 1024-byte chunk
    // boundary in the middle of an update: a hasher that only agreed when its
    // updates lined up with chunks would pass a tidier test.
    usize offset = 0;
    for (const usize step : {1u, 7u, 900u, 1u, 1200u}) {
        const usize count = step < input.size() - offset ? step : input.size() - offset;
        hasher.update(std::span<const std::byte>(input.data() + offset, count));
        offset += count;
    }
    hasher.update(std::span<const std::byte>(input.data() + offset, input.size() - offset));

    CHECK(hasher.finish() == hashBytes(input));
}

TEST_CASE("finishing does not consume the hasher")
{
    ContentHasher hasher;
    hasher.update("abc");
    const ContentHash afterPrefix = hasher.finish();
    CHECK(afterPrefix == hashText("abc"));

    // The same object keeps going, which is what a caller hashing a stream in
    // pieces and reporting progress needs.
    hasher.update("def");
    CHECK(hasher.finish() == hashText("abcdef"));
    CHECK(hasher.finish() == hashText("abcdef"));
    CHECK(afterPrefix != hashText("abcdef"));
}

TEST_CASE("an empty input has a hash and it is not zero")
{
    const ContentHash empty = hashText("");
    CHECK_FALSE(empty.isZero());
    CHECK(empty == hashBytes({}));
    // A default-constructed hash is the "no hash" value, and it must not
    // collide with the hash of nothing -- otherwise "unset" and "empty file"
    // are the same content address.
    CHECK(ContentHash{}.isZero());
    CHECK(ContentHash{} != empty);
}

TEST_CASE("hex is the canonical text form and it round-trips")
{
    const ContentHash hash = hashText("the pack's table of contents");
    const std::string hex = hash.toHex();
    CHECK(hex.size() == 32);

    ContentHash parsed;
    REQUIRE(parseHex(hex, parsed));
    CHECK(parsed == hash);

    // Case-insensitive on input, lowercase on output: a hash that round-trips
    // through text has to compare equal AS TEXT too, or a manifest written by
    // one tool and read by another disagrees with itself.
    std::string upper = hex;
    for (char& c : upper) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    ContentHash fromUpper;
    REQUIRE(parseHex(upper, fromUpper));
    CHECK(fromUpper == hash);
    CHECK(fromUpper.toHex() == hex);
}

TEST_CASE("a malformed hex string is refused rather than half-parsed")
{
    ContentHash out = hashText("sentinel");
    const ContentHash before = out;

    CHECK_FALSE(parseHex("", out));
    CHECK_FALSE(parseHex("af1349b9f5f9a1a6a0404dea36dcc94", out));   // one short
    CHECK_FALSE(parseHex("af1349b9f5f9a1a6a0404dea36dcc9499", out)); // one long
    CHECK_FALSE(parseHex("af1349b9f5f9a1a6a0404dea36dcc94z", out));  // not hex
    // Untouched on every refusal: a parse that half-wrote its output would
    // hand the caller a content address to something that does not exist.
    CHECK(out == before);
}

TEST_CASE("bytes are big-endian and round-trip")
{
    const ContentHash hash = hashText("byte order is an agreement");
    const std::array<std::byte, 16> bytes = toBytes(hash);
    CHECK(fromBytes(std::span<const std::byte, 16>(bytes)) == hash);

    // The first byte of the digest is the top byte of `high`, which is what
    // makes the hex form and the binary form the same sequence.
    const std::string hex = hash.toHex();
    const auto firstByte = static_cast<unsigned char>(bytes[0]);
    CHECK(static_cast<u64>(firstByte) == (hash.high >> 56));
    CHECK(hex.substr(0, 2) ==
          std::string(1, "0123456789abcdef"[firstByte >> 4]) + std::string(1, "0123456789abcdef"[firstByte & 0x0F]));
}

TEST_CASE("hashes order and key containers")
{
    const ContentHash a = hashText("a");
    const ContentHash b = hashText("b");
    CHECK((a < b || b < a));
    CHECK_FALSE(a < a);

    std::unordered_map<ContentHash, int> byHash;
    byHash[a] = 1;
    byHash[b] = 2;
    byHash[hashText("a")] = 3;
    CHECK(byHash.size() == 2);
    CHECK(byHash[a] == 3);
}

TEST_CASE("different content hashes differently")
{
    CHECK(hashText("asset://models/tree.glb") != hashText("asset://models/tree.gltf"));
    // One flipped bit deep inside a megabyte, which is the case a checksum
    // over a length would miss.
    std::vector<std::byte> first = vectorInput(1u << 20);
    std::vector<std::byte> second = first;
    second[500000] ^= std::byte{0x01};
    CHECK(hashBytes(first) != hashBytes(second));
}
