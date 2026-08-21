#include "luaug/core/content_hash.h"

#include <blake3.h>
#include <cstdint>
#include <cstring>
#include <new>

namespace luaug::core {
namespace {

static_assert(sizeof(blake3_hasher) <= 2048, "ContentHasher::StateBytes no longer fits blake3_hasher");
static_assert(alignof(blake3_hasher) <= 8, "ContentHasher's storage is under-aligned for blake3_hasher");

constexpr usize DigestBytes = 16;

[[nodiscard]] u64 readBigEndian64(const std::byte* bytes) noexcept
{
    u64 value = 0;
    for (usize i = 0; i < 8; ++i) {
        value = (value << 8) | static_cast<u64>(static_cast<unsigned char>(bytes[i]));
    }
    return value;
}

void writeBigEndian64(u64 value, std::byte* bytes) noexcept
{
    for (usize i = 0; i < 8; ++i) {
        bytes[7 - i] = static_cast<std::byte>(value & 0xFFu);
        value >>= 8;
    }
}

[[nodiscard]] int hexDigit(char c) noexcept
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

[[nodiscard]] ContentHash digestOf(const blake3_hasher& hasher) noexcept
{
    std::array<std::byte, DigestBytes> digest{};
    blake3_hasher_finalize(&hasher, reinterpret_cast<std::uint8_t*>(digest.data()), digest.size());
    return fromBytes(std::span<const std::byte, DigestBytes>(digest));
}

} // namespace

std::string ContentHash::toHex() const
{
    static constexpr char digits[] = "0123456789abcdef";
    const std::array<std::byte, DigestBytes> bytes = toBytes(*this);

    std::string out;
    out.resize(DigestBytes * 2);
    for (usize i = 0; i < DigestBytes; ++i) {
        const auto value = static_cast<unsigned char>(bytes[i]);
        out[i * 2] = digits[value >> 4];
        out[i * 2 + 1] = digits[value & 0x0Fu];
    }
    return out;
}

std::array<std::byte, DigestBytes> toBytes(const ContentHash& hash) noexcept
{
    std::array<std::byte, DigestBytes> bytes{};
    writeBigEndian64(hash.high, bytes.data());
    writeBigEndian64(hash.low, bytes.data() + 8);
    return bytes;
}

ContentHash fromBytes(std::span<const std::byte, DigestBytes> bytes) noexcept
{
    ContentHash hash;
    hash.high = readBigEndian64(bytes.data());
    hash.low = readBigEndian64(bytes.data() + 8);
    return hash;
}

bool parseHex(std::string_view text, ContentHash& out)
{
    if (text.size() != DigestBytes * 2) {
        return false;
    }

    std::array<std::byte, DigestBytes> bytes{};
    for (usize i = 0; i < DigestBytes; ++i) {
        const int high = hexDigit(text[i * 2]);
        const int low = hexDigit(text[i * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        bytes[i] = static_cast<std::byte>((high << 4) | low);
    }

    out = fromBytes(std::span<const std::byte, DigestBytes>(bytes));
    return true;
}

ContentHash hashBytes(std::span<const std::byte> bytes) noexcept
{
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    if (!bytes.empty()) {
        blake3_hasher_update(&hasher, bytes.data(), bytes.size());
    }
    return digestOf(hasher);
}

ContentHash hashText(std::string_view text) noexcept
{
    return hashBytes(std::span<const std::byte>(reinterpret_cast<const std::byte*>(text.data()), text.size()));
}

ContentHasher::ContentHasher() noexcept
{
    blake3_hasher_init(::new (m_state.data()) blake3_hasher);
}

void ContentHasher::update(std::span<const std::byte> bytes) noexcept
{
    if (bytes.empty()) {
        return;
    }
    blake3_hasher_update(std::launder(reinterpret_cast<blake3_hasher*>(m_state.data())), bytes.data(), bytes.size());
}

void ContentHasher::update(std::string_view text) noexcept
{
    update(std::span<const std::byte>(reinterpret_cast<const std::byte*>(text.data()), text.size()));
}

ContentHash ContentHasher::finish() const noexcept
{
    // BLAKE3's finalize does not consume the state, which is what lets a caller
    // take the hash of a prefix and keep appending.
    return digestOf(*std::launder(reinterpret_cast<const blake3_hasher*>(m_state.data())));
}

} // namespace luaug::core
