#include "luaug/net/detail/digest.h"

#include <cstring>
#include <vector>

namespace luaug::net::detail {
namespace {

using core::u32;
using core::u64;
using core::u8;
using core::usize;

constexpr u32 rotateLeft(u32 value, u32 bits)
{
    return static_cast<u32>((value << bits) | (value >> (32u - bits)));
}

void processBlock(const u8* block, std::array<u32, 5>& state)
{
    std::array<u32, 80> w{};

    for (usize i = 0; i < 16; ++i) {
        w[i] = static_cast<u32>(block[i * 4]) << 24 | static_cast<u32>(block[i * 4 + 1]) << 16 |
               static_cast<u32>(block[i * 4 + 2]) << 8 | static_cast<u32>(block[i * 4 + 3]);
    }
    for (usize i = 16; i < 80; ++i)
        w[i] = rotateLeft(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    u32 a = state[0];
    u32 b = state[1];
    u32 c = state[2];
    u32 d = state[3];
    u32 e = state[4];

    for (usize i = 0; i < 80; ++i) {
        u32 f = 0;
        u32 k = 0;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999u;
        }
        else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        }
        else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        }
        else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }

        const u32 temp = rotateLeft(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rotateLeft(b, 30);
        b = a;
        a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

} // namespace

Sha1Digest sha1(std::span<const u8> data)
{
    std::array<u32, 5> state{0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};

    const usize wholeBlocks = data.size() / 64;
    for (usize i = 0; i < wholeBlocks; ++i)
        processBlock(data.data() + i * 64, state);

    // The tail plus padding is at most two blocks: the 0x80 terminator and the
    // 8-byte length may not share a block with more than 55 remaining bytes.
    std::array<u8, 128> tail{};
    const usize remaining = data.size() - wholeBlocks * 64;
    if (remaining > 0)
        std::memcpy(tail.data(), data.data() + wholeBlocks * 64, remaining);
    tail[remaining] = 0x80u;

    const usize tailBlocks = (remaining + 1 + 8 > 64) ? 2u : 1u;
    const u64 bitLength = static_cast<u64>(data.size()) * 8u;
    for (usize i = 0; i < 8; ++i)
        tail[tailBlocks * 64 - 1 - i] = static_cast<u8>((bitLength >> (i * 8)) & 0xFFu);

    for (usize i = 0; i < tailBlocks; ++i)
        processBlock(tail.data() + i * 64, state);

    Sha1Digest digest{};
    for (usize i = 0; i < 5; ++i) {
        digest[i * 4] = static_cast<u8>((state[i] >> 24) & 0xFFu);
        digest[i * 4 + 1] = static_cast<u8>((state[i] >> 16) & 0xFFu);
        digest[i * 4 + 2] = static_cast<u8>((state[i] >> 8) & 0xFFu);
        digest[i * 4 + 3] = static_cast<u8>(state[i] & 0xFFu);
    }
    return digest;
}

Sha1Digest sha1(std::string_view text)
{
    return sha1(std::span<const u8>(reinterpret_cast<const u8*>(text.data()), text.size()));
}

std::string base64Encode(std::span<const u8> data)
{
    static constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);

    usize i = 0;
    for (; i + 3 <= data.size(); i += 3) {
        const u32 triple =
            static_cast<u32>(data[i]) << 16 | static_cast<u32>(data[i + 1]) << 8 | static_cast<u32>(data[i + 2]);
        out.push_back(kAlphabet[(triple >> 18) & 0x3Fu]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3Fu]);
        out.push_back(kAlphabet[(triple >> 6) & 0x3Fu]);
        out.push_back(kAlphabet[triple & 0x3Fu]);
    }

    const usize remaining = data.size() - i;
    if (remaining == 1) {
        const u32 triple = static_cast<u32>(data[i]) << 16;
        out.push_back(kAlphabet[(triple >> 18) & 0x3Fu]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3Fu]);
        out.push_back('=');
        out.push_back('=');
    }
    else if (remaining == 2) {
        const u32 triple = static_cast<u32>(data[i]) << 16 | static_cast<u32>(data[i + 1]) << 8;
        out.push_back(kAlphabet[(triple >> 18) & 0x3Fu]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3Fu]);
        out.push_back(kAlphabet[(triple >> 6) & 0x3Fu]);
        out.push_back('=');
    }

    return out;
}

} // namespace luaug::net::detail
