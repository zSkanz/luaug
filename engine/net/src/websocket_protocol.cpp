#include "luaug/net/websocket_protocol.h"

#include <array>
#include <string>

#include "luaug/net/detail/digest.h"

namespace luaug::net::ws
{
namespace
{

using core::I18nArg;
using core::u64;

// RFC 6455 §1.3. A protocol constant, not a secret and not configurable.
constexpr std::string_view kGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

constexpr bool isControlOpcode(u8 opcode)
{
    return (opcode & 0x08u) != 0u;
}

constexpr bool isKnownOpcode(u8 opcode)
{
    return opcode == 0x0u || opcode == 0x1u || opcode == 0x2u || opcode == 0x8u || opcode == 0x9u || opcode == 0xAu;
}

[[nodiscard]] constexpr char lowerAscii(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] bool equalsIgnoreCase(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (usize i = 0; i < a.size(); ++i)
    {
        if (lowerAscii(a[i]) != lowerAscii(b[i]))
            return false;
    }
    return true;
}

[[nodiscard]] bool containsIgnoreCase(std::string_view haystack, std::string_view needle)
{
    if (needle.size() > haystack.size())
        return false;
    for (usize i = 0; i + needle.size() <= haystack.size(); ++i)
    {
        if (equalsIgnoreCase(haystack.substr(i, needle.size()), needle))
            return true;
    }
    return false;
}

[[nodiscard]] std::string_view trim(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
        text.remove_suffix(1);
    return text;
}

// The value of the first header with this name, or nothing.
[[nodiscard]] std::optional<std::string_view> headerValue(std::string_view response, std::string_view name)
{
    usize lineStart = response.find("\r\n");
    if (lineStart == std::string_view::npos)
        return std::nullopt;
    lineStart += 2;

    while (lineStart < response.size())
    {
        usize lineEnd = response.find("\r\n", lineStart);
        if (lineEnd == std::string_view::npos)
            lineEnd = response.size();

        const std::string_view line = response.substr(lineStart, lineEnd - lineStart);
        if (line.empty())
            break;

        const usize colon = line.find(':');
        if (colon != std::string_view::npos && equalsIgnoreCase(trim(line.substr(0, colon)), name))
            return trim(line.substr(colon + 1));

        lineStart = lineEnd + 2;
    }
    return std::nullopt;
}

[[nodiscard]] core::EngineError handshakeError(std::string_view detail)
{
    const std::array<I18nArg, 1> args{I18nArg{"detail", detail}};
    return core::makeError(LUAUG_TR("net.err.handshake_failed"), args);
}

} // namespace

void encodeFrame(std::vector<u8>& out, Opcode opcode, bool fin, std::span<const u8> payload, u32 maskKey)
{
    out.push_back(static_cast<u8>((fin ? 0x80u : 0x00u) | static_cast<u8>(opcode)));

    const usize length = payload.size();
    if (length <= 125)
    {
        out.push_back(static_cast<u8>(0x80u | length));
    }
    else if (length <= 0xFFFFu)
    {
        out.push_back(static_cast<u8>(0x80u | 126u));
        out.push_back(static_cast<u8>((length >> 8) & 0xFFu));
        out.push_back(static_cast<u8>(length & 0xFFu));
    }
    else
    {
        out.push_back(static_cast<u8>(0x80u | 127u));
        const u64 wide = static_cast<u64>(length);
        for (usize i = 0; i < 8; ++i)
            out.push_back(static_cast<u8>((wide >> ((7 - i) * 8)) & 0xFFu));
    }

    const std::array<u8, 4> mask{
        static_cast<u8>((maskKey >> 24) & 0xFFu),
        static_cast<u8>((maskKey >> 16) & 0xFFu),
        static_cast<u8>((maskKey >> 8) & 0xFFu),
        static_cast<u8>(maskKey & 0xFFu)};
    for (const u8 byte : mask)
        out.push_back(byte);

    for (usize i = 0; i < length; ++i)
        out.push_back(static_cast<u8>(payload[i] ^ mask[i % 4]));
}

DecodeResult decodeFrame(std::span<const u8> input, usize maxPayload)
{
    DecodeResult result;

    if (input.size() < 2)
        return result;

    const u8 byte0 = input[0];
    const u8 byte1 = input[1];

    // RSV1..3 are reserved and must be zero: no extension is negotiated, so a
    // peer setting one is describing a frame we cannot interpret.
    if ((byte0 & 0x70u) != 0u)
    {
        result.status = DecodeStatus::Malformed;
        return result;
    }

    const u8 opcode = static_cast<u8>(byte0 & 0x0Fu);
    if (!isKnownOpcode(opcode))
    {
        result.status = DecodeStatus::Malformed;
        return result;
    }

    const bool fin = (byte0 & 0x80u) != 0u;
    const bool masked = (byte1 & 0x80u) != 0u;
    const u8 shortLength = static_cast<u8>(byte1 & 0x7Fu);

    // §5.5: a control frame carries at most 125 bytes and is never fragmented.
    if (isControlOpcode(opcode) && (shortLength > 125u || !fin))
    {
        result.status = DecodeStatus::Malformed;
        return result;
    }

    usize cursor = 2;
    u64 payloadLength = shortLength;

    if (shortLength == 126u)
    {
        if (input.size() < cursor + 2)
            return result;
        payloadLength = static_cast<u64>(input[cursor]) << 8 | static_cast<u64>(input[cursor + 1]);
        cursor += 2;

        // §5.2 requires the minimal encoding, so a 16-bit field naming a length
        // the 7-bit field could hold is not the frame it claims to be.
        if (payloadLength < 126u)
        {
            result.status = DecodeStatus::Malformed;
            return result;
        }
    }
    else if (shortLength == 127u)
    {
        if (input.size() < cursor + 8)
            return result;

        payloadLength = 0;
        for (usize i = 0; i < 8; ++i)
            payloadLength = (payloadLength << 8) | static_cast<u64>(input[cursor + i]);
        cursor += 8;

        // §5.2: the high bit must be zero, and the minimal-encoding rule again.
        if ((payloadLength >> 63) != 0u || payloadLength <= 0xFFFFu)
        {
            result.status = DecodeStatus::Malformed;
            return result;
        }
    }

    if (payloadLength > static_cast<u64>(maxPayload))
    {
        result.status = DecodeStatus::Malformed;
        return result;
    }

    if (masked)
    {
        if (input.size() < cursor + 4)
            return result;
        cursor += 4;
    }

    const usize payloadSize = static_cast<usize>(payloadLength);
    if (input.size() < cursor + payloadSize)
        return result;

    result.frame.opcode = static_cast<Opcode>(opcode);
    result.frame.fin = fin;
    result.frame.masked = masked;
    result.frame.payload.assign(
        input.begin() + static_cast<std::ptrdiff_t>(cursor),
        input.begin() + static_cast<std::ptrdiff_t>(cursor + payloadSize));

    if (masked)
    {
        const usize maskAt = cursor - 4;
        for (usize i = 0; i < payloadSize; ++i)
            result.frame.payload[i] = static_cast<u8>(result.frame.payload[i] ^ input[maskAt + (i % 4)]);
    }

    result.status = DecodeStatus::Ok;
    result.consumed = cursor + payloadSize;
    return result;
}

std::string computeAccept(std::string_view clientKeyBase64)
{
    std::string combined;
    combined.reserve(clientKeyBase64.size() + kGuid.size());
    combined.append(clientKeyBase64);
    combined.append(kGuid);

    const detail::Sha1Digest digest = detail::sha1(combined);
    return detail::base64Encode(std::span<const u8>(digest.data(), digest.size()));
}

ClientHandshake buildClientHandshake(
    std::string_view host, u16 port, std::string_view path, std::span<const u8, 16> nonce)
{
    ClientHandshake handshake;

    const std::string key = detail::base64Encode(std::span<const u8>(nonce.data(), nonce.size()));
    handshake.expectedAccept = computeAccept(key);

    handshake.request.append("GET ").append(path).append(" HTTP/1.1\r\n");
    handshake.request.append("Host: ").append(host).append(":").append(std::to_string(port)).append("\r\n");
    handshake.request.append("Upgrade: websocket\r\n");
    handshake.request.append("Connection: Upgrade\r\n");
    handshake.request.append("Sec-WebSocket-Key: ").append(key).append("\r\n");
    handshake.request.append("Sec-WebSocket-Version: 13\r\n");
    handshake.request.append("\r\n");

    return handshake;
}

std::optional<usize> findHeaderEnd(std::string_view buffer)
{
    const usize at = buffer.find("\r\n\r\n");
    if (at == std::string_view::npos)
        return std::nullopt;
    return at + 4;
}

std::optional<core::EngineError> validateServerHandshake(std::string_view response, std::string_view expectedAccept)
{
    const usize statusEnd = response.find("\r\n");
    if (statusEnd == std::string_view::npos)
        return handshakeError("the response has no status line");

    const std::string_view status = response.substr(0, statusEnd);
    if (!status.starts_with("HTTP/1.1 101") && !status.starts_with("HTTP/1.0 101"))
        return handshakeError(std::string("expected a 101 status, got: ").append(status));

    const std::optional<std::string_view> upgrade = headerValue(response, "Upgrade");
    if (!upgrade.has_value() || !equalsIgnoreCase(*upgrade, "websocket"))
        return handshakeError("the Upgrade header is missing or is not websocket");

    const std::optional<std::string_view> connection = headerValue(response, "Connection");
    if (!connection.has_value() || !containsIgnoreCase(*connection, "upgrade"))
        return handshakeError("the Connection header does not name an upgrade");

    const std::optional<std::string_view> accept = headerValue(response, "Sec-WebSocket-Accept");
    if (!accept.has_value())
        return handshakeError("the response carries no Sec-WebSocket-Accept");
    if (*accept != expectedAccept)
        return handshakeError("Sec-WebSocket-Accept does not match the key we sent");

    return std::nullopt;
}

} // namespace luaug::net::ws
