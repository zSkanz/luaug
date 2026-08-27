#include "luaug/core/base64.h"

#include <array>

namespace luaug::core {
namespace {

constexpr std::string_view Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// The inverse, built once. 0xFF is "not a base64 character", which is what makes
// the decoder's refusal a table lookup rather than a chain of range tests.
[[nodiscard]] const std::array<u8, 256>& inverse() noexcept
{
    static const std::array<u8, 256> table = [] {
        std::array<u8, 256> built{};
        built.fill(0xFF);
        for (usize at = 0; at < Alphabet.size(); ++at) {
            built[static_cast<u8>(Alphabet[at])] = static_cast<u8>(at);
        }
        return built;
    }();
    return table;
}

} // namespace

std::string base64Encode(std::span<const u8> bytes)
{
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);

    usize at = 0;
    for (; at + 3 <= bytes.size(); at += 3) {
        const u32 group = (static_cast<u32>(bytes[at]) << 16) | (static_cast<u32>(bytes[at + 1]) << 8) |
                          static_cast<u32>(bytes[at + 2]);
        out.push_back(Alphabet[(group >> 18) & 0x3F]);
        out.push_back(Alphabet[(group >> 12) & 0x3F]);
        out.push_back(Alphabet[(group >> 6) & 0x3F]);
        out.push_back(Alphabet[group & 0x3F]);
    }

    if (const usize left = bytes.size() - at; left == 1) {
        const u32 group = static_cast<u32>(bytes[at]) << 16;
        out.push_back(Alphabet[(group >> 18) & 0x3F]);
        out.push_back(Alphabet[(group >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    }
    else if (left == 2) {
        const u32 group = (static_cast<u32>(bytes[at]) << 16) | (static_cast<u32>(bytes[at + 1]) << 8);
        out.push_back(Alphabet[(group >> 18) & 0x3F]);
        out.push_back(Alphabet[(group >> 12) & 0x3F]);
        out.push_back(Alphabet[(group >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

std::optional<std::vector<u8>> base64Decode(std::string_view text)
{
    // **A length that is not a multiple of four is not base64.** Padding is
    // required rather than optional, so this is a single test rather than a
    // family of accepted shapes.
    if (text.size() % 4 != 0) {
        return std::nullopt;
    }

    std::vector<u8> out;
    out.reserve(text.size() / 4 * 3);

    const std::array<u8, 256>& table = inverse();
    for (usize at = 0; at < text.size(); at += 4) {
        const char c0 = text[at];
        const char c1 = text[at + 1];
        const char c2 = text[at + 2];
        const char c3 = text[at + 3];

        const u8 v0 = table[static_cast<u8>(c0)];
        const u8 v1 = table[static_cast<u8>(c1)];
        if (v0 == 0xFF || v1 == 0xFF) {
            return std::nullopt;
        }

        // Padding only in the last group, and only as the last one or two
        // characters -- `A=B=` is not a shorter encoding of anything.
        const bool lastGroup = at + 4 == text.size();
        if (c2 == '=') {
            if (!lastGroup || c3 != '=') {
                return std::nullopt;
            }
            out.push_back(static_cast<u8>((v0 << 2) | (v1 >> 4)));
            return out;
        }

        const u8 v2 = table[static_cast<u8>(c2)];
        if (v2 == 0xFF) {
            return std::nullopt;
        }
        if (c3 == '=') {
            if (!lastGroup) {
                return std::nullopt;
            }
            out.push_back(static_cast<u8>((v0 << 2) | (v1 >> 4)));
            out.push_back(static_cast<u8>((v1 << 4) | (v2 >> 2)));
            return out;
        }

        const u8 v3 = table[static_cast<u8>(c3)];
        if (v3 == 0xFF) {
            return std::nullopt;
        }
        out.push_back(static_cast<u8>((v0 << 2) | (v1 >> 4)));
        out.push_back(static_cast<u8>((v1 << 4) | (v2 >> 2)));
        out.push_back(static_cast<u8>((v2 << 6) | v3));
    }
    return out;
}

} // namespace luaug::core
