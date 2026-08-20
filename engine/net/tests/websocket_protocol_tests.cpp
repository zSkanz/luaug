// RFC 6455 framing and handshake, against the examples the RFC itself
// publishes: §1.3 for the accept value, §5.7 for the frames. Our own output is
// never the reference.

#include <doctest/doctest.h>

#include <array>
#include <string>
#include <vector>

#include "luaug/core/i18n.h"
#include "luaug/net/websocket_protocol.h"

using namespace luaug::net::ws;
using luaug::core::u8;

namespace
{

// An error is identified by the `[key]` prefix `makeError` writes, and that
// prefix is the CATALOG's name for the key. Without this every raise reports
// `[i18n:missing:xxxxxxxx]` and every assertion on the message silently matches
// nothing -- M2 Finding 11, which cost fourteen tests at once.
void seedRealCatalog()
{
    const auto result = luaug::core::engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
}

// Every §5.7 example masks with this key, so it is the one value that lets our
// encoder be compared byte for byte against the document.
constexpr luaug::core::u32 kRfcMaskKey = 0x37fa213du;
constexpr luaug::core::usize kMaxPayload = 1u << 20;

std::vector<u8> bytesOf(std::string_view text)
{
    return std::vector<u8>(text.begin(), text.end());
}

std::string textOf(const std::vector<u8>& payload)
{
    return std::string(payload.begin(), payload.end());
}

} // namespace

TEST_CASE("computeAccept reproduces the worked example in RFC 6455 section 1.3")
{
    CHECK(computeAccept("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST_CASE("the client handshake carries the required headers and its own expected accept")
{
    // "the sample nonce" -- the 16 bytes whose base64 is the RFC's example key.
    const std::string_view sample = "the sample nonce";
    REQUIRE(sample.size() == 16);

    std::array<u8, 16> nonce{};
    for (std::size_t i = 0; i < nonce.size(); ++i)
        nonce[i] = static_cast<u8>(sample[i]);

    const ClientHandshake handshake = buildClientHandshake("127.0.0.1", 4560, "/control", nonce);

    CHECK(handshake.request.starts_with("GET /control HTTP/1.1\r\n"));
    CHECK(handshake.request.find("Host: 127.0.0.1:4560\r\n") != std::string::npos);
    CHECK(handshake.request.find("Upgrade: websocket\r\n") != std::string::npos);
    CHECK(handshake.request.find("Connection: Upgrade\r\n") != std::string::npos);
    CHECK(handshake.request.find("Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n") != std::string::npos);
    CHECK(handshake.request.find("Sec-WebSocket-Version: 13\r\n") != std::string::npos);
    CHECK(handshake.request.ends_with("\r\n\r\n"));

    CHECK(handshake.expectedAccept == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST_CASE("a masked text frame matches the section 5.7 example byte for byte")
{
    std::vector<u8> out;
    encodeFrame(out, Opcode::Text, true, bytesOf("Hello"), kRfcMaskKey);

    const std::vector<u8> expected{
        0x81u, 0x85u, 0x37u, 0xfau, 0x21u, 0x3du, 0x7fu, 0x9fu, 0x4du, 0x51u, 0x58u};
    CHECK(out == expected);
}

TEST_CASE("a masked pong matches the section 5.7 example byte for byte")
{
    std::vector<u8> out;
    encodeFrame(out, Opcode::Pong, true, bytesOf("Hello"), kRfcMaskKey);

    const std::vector<u8> expected{
        0x8au, 0x85u, 0x37u, 0xfau, 0x21u, 0x3du, 0x7fu, 0x9fu, 0x4du, 0x51u, 0x58u};
    CHECK(out == expected);
}

TEST_CASE("the two extended length forms use the headers section 5.7 shows")
{
    // 256 bytes: `0x82 0x7E 0x0100` in the RFC, then our four mask bytes.
    std::vector<u8> medium;
    encodeFrame(medium, Opcode::Binary, true, std::vector<u8>(256, 0x00u), kRfcMaskKey);
    CHECK(medium[0] == 0x82u);
    CHECK(medium[1] == 0xFEu); // 0x7E with the mask bit the RFC's unmasked example lacks
    CHECK(medium[2] == 0x01u);
    CHECK(medium[3] == 0x00u);
    CHECK(medium.size() == 4 + 4 + 256);

    // 65536 bytes: `0x82 0x7F 0x0000000000010000`.
    std::vector<u8> large;
    encodeFrame(large, Opcode::Binary, true, std::vector<u8>(65536, 0x00u), kRfcMaskKey);
    CHECK(large[0] == 0x82u);
    CHECK(large[1] == 0xFFu);
    const std::vector<u8> lengthField(large.begin() + 2, large.begin() + 10);
    CHECK(lengthField == std::vector<u8>{0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u, 0x00u, 0x00u});
    CHECK(large.size() == 10 + 4 + 65536);
}

TEST_CASE("the unmasked frames a server sends decode to their payloads")
{
    const std::vector<u8> hello{0x81u, 0x05u, 0x48u, 0x65u, 0x6cu, 0x6cu, 0x6fu};
    const DecodeResult result = decodeFrame(hello, kMaxPayload);

    REQUIRE(result.status == DecodeStatus::Ok);
    CHECK(result.consumed == hello.size());
    CHECK(result.frame.opcode == Opcode::Text);
    CHECK(result.frame.fin);
    CHECK_FALSE(result.frame.masked);
    CHECK(textOf(result.frame.payload) == "Hello");

    const std::vector<u8> ping{0x89u, 0x05u, 0x48u, 0x65u, 0x6cu, 0x6cu, 0x6fu};
    const DecodeResult pinged = decodeFrame(ping, kMaxPayload);
    REQUIRE(pinged.status == DecodeStatus::Ok);
    CHECK(pinged.frame.opcode == Opcode::Ping);
    CHECK(textOf(pinged.frame.payload) == "Hello");
}

TEST_CASE("the section 5.7 fragmented message decodes as two frames")
{
    const std::vector<u8> first{0x01u, 0x03u, 0x48u, 0x65u, 0x6cu};
    const std::vector<u8> second{0x80u, 0x02u, 0x6cu, 0x6fu};

    const DecodeResult head = decodeFrame(first, kMaxPayload);
    REQUIRE(head.status == DecodeStatus::Ok);
    CHECK(head.frame.opcode == Opcode::Text);
    CHECK_FALSE(head.frame.fin);
    CHECK(textOf(head.frame.payload) == "Hel");

    const DecodeResult tail = decodeFrame(second, kMaxPayload);
    REQUIRE(tail.status == DecodeStatus::Ok);
    CHECK(tail.frame.opcode == Opcode::Continuation);
    CHECK(tail.frame.fin);
    CHECK(textOf(tail.frame.payload) == "lo");
}

TEST_CASE("a masked frame decodes, and reports that it was masked")
{
    // A client rejects this rather than accepting it silently -- section 5.1
    // forbids a server to mask -- so the decoder's job is to report the fact,
    // not to hide it by unmasking.
    const std::vector<u8> masked{
        0x81u, 0x85u, 0x37u, 0xfau, 0x21u, 0x3du, 0x7fu, 0x9fu, 0x4du, 0x51u, 0x58u};
    const DecodeResult result = decodeFrame(masked, kMaxPayload);

    REQUIRE(result.status == DecodeStatus::Ok);
    CHECK(result.frame.masked);
    CHECK(textOf(result.frame.payload) == "Hello");
}

TEST_CASE("a frame that is only partly here asks for more, and never guesses")
{
    const std::vector<u8> whole{0x81u, 0x05u, 0x48u, 0x65u, 0x6cu, 0x6cu, 0x6fu};

    for (std::size_t prefix = 0; prefix < whole.size(); ++prefix)
    {
        const std::vector<u8> partial(whole.begin(), whole.begin() + static_cast<std::ptrdiff_t>(prefix));
        const DecodeResult result = decodeFrame(partial, kMaxPayload);
        CHECK(result.status == DecodeStatus::NeedMore);
        CHECK(result.consumed == 0);
    }

    // And a whole frame followed by the start of the next consumes only its own.
    std::vector<u8> two = whole;
    two.push_back(0x81u);
    const DecodeResult first = decodeFrame(two, kMaxPayload);
    REQUIRE(first.status == DecodeStatus::Ok);
    CHECK(first.consumed == whole.size());
}

TEST_CASE("the malformed frames a stream cannot resynchronise from are rejected")
{
    SUBCASE("a reserved bit is set")
    {
        const std::vector<u8> frame{0xC1u, 0x00u};
        CHECK(decodeFrame(frame, kMaxPayload).status == DecodeStatus::Malformed);
    }
    SUBCASE("the opcode is not one this version defines")
    {
        const std::vector<u8> frame{0x83u, 0x00u};
        CHECK(decodeFrame(frame, kMaxPayload).status == DecodeStatus::Malformed);
    }
    SUBCASE("a control frame is fragmented")
    {
        const std::vector<u8> frame{0x09u, 0x00u};
        CHECK(decodeFrame(frame, kMaxPayload).status == DecodeStatus::Malformed);
    }
    SUBCASE("a control frame carries more than 125 bytes")
    {
        const std::vector<u8> frame{0x89u, 0x7Eu, 0x01u, 0x00u};
        CHECK(decodeFrame(frame, kMaxPayload).status == DecodeStatus::Malformed);
    }
    SUBCASE("a 16-bit length names what the 7-bit field could have held")
    {
        const std::vector<u8> frame{0x81u, 0x7Eu, 0x00u, 0x05u};
        CHECK(decodeFrame(frame, kMaxPayload).status == DecodeStatus::Malformed);
    }
    SUBCASE("a 64-bit length names what the 16-bit field could have held")
    {
        const std::vector<u8> frame{0x81u, 0x7Fu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u, 0x00u};
        CHECK(decodeFrame(frame, kMaxPayload).status == DecodeStatus::Malformed);
    }
    SUBCASE("the high bit of a 64-bit length is set")
    {
        const std::vector<u8> frame{0x81u, 0x7Fu, 0x80u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
        CHECK(decodeFrame(frame, kMaxPayload).status == DecodeStatus::Malformed);
    }
}

TEST_CASE("a length larger than we will hold is refused rather than waited for")
{
    // Waiting would be a hang: the bytes are never coming, because we would
    // refuse to keep them if they did. 2 MiB declared against a 1 MiB ceiling.
    const std::vector<u8> frame{0x82u, 0x7Fu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x20u, 0x00u, 0x00u};
    CHECK(decodeFrame(frame, kMaxPayload).status == DecodeStatus::Malformed);
}

TEST_CASE("everything we encode, we can decode")
{
    for (const std::size_t size : {std::size_t{0}, std::size_t{1}, std::size_t{125}, std::size_t{126},
             std::size_t{65535}, std::size_t{65536}})
    {
        std::vector<u8> payload(size);
        for (std::size_t i = 0; i < size; ++i)
            payload[i] = static_cast<u8>(i * 31u + 7u);

        std::vector<u8> wire;
        encodeFrame(wire, Opcode::Binary, true, payload, 0x1234ABCDu);

        const DecodeResult result = decodeFrame(wire, kMaxPayload);
        REQUIRE(result.status == DecodeStatus::Ok);
        CHECK(result.consumed == wire.size());
        CHECK(result.frame.masked);
        CHECK(result.frame.payload == payload);
    }
}

TEST_CASE("the response headers end where the payload begins")
{
    CHECK_FALSE(findHeaderEnd("HTTP/1.1 101 Switching Protocols\r\n").has_value());

    const std::string_view complete = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n\r\n\x81\x05";
    const auto end = findHeaderEnd(complete);
    REQUIRE(end.has_value());
    CHECK(complete.substr(*end) == "\x81\x05");
}

TEST_CASE("a well-formed server handshake is accepted")
{
    const std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
        "\r\n";
    CHECK_FALSE(validateServerHandshake(response, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=").has_value());
}

TEST_CASE("header names and the two token values are matched case-insensitively")
{
    // Nothing in HTTP promises the casing a server chooses, and rejecting a
    // valid peer over it would be a bug that only shows up against one server.
    const std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "upgrade: WebSocket\r\n"
        "connection: keep-alive, Upgrade\r\n"
        "sec-websocket-accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
        "\r\n";
    CHECK_FALSE(validateServerHandshake(response, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=").has_value());
}

TEST_CASE("every way a handshake can fail reports the same key and says which one it was")
{
    seedRealCatalog();

    const auto failsWith = [](std::string_view response, std::string_view expected)
    {
        const auto error = validateServerHandshake(response, expected);
        REQUIRE(error.has_value());
        CHECK(error->key.hash == LUAUG_TR("net.err.handshake_failed").hash);
        return error->message;
    };

    SUBCASE("the status is not 101")
    {
        const std::string response =
            "HTTP/1.1 400 Bad Request\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
            "Sec-WebSocket-Accept: x\r\n\r\n";
        CHECK(failsWith(response, "x").find("400") != std::string::npos);
    }
    SUBCASE("the upgrade header is absent")
    {
        const std::string response =
            "HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: x\r\n\r\n";
        CHECK_FALSE(failsWith(response, "x").empty());
    }
    SUBCASE("the accept value is for a different key")
    {
        const std::string response =
            "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
            "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n";
        CHECK_FALSE(failsWith(response, "AAAAAAAAAAAAAAAAAAAAAAAAAAA=").empty());
    }
    SUBCASE("there is no accept header at all")
    {
        const std::string response =
            "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n";
        CHECK_FALSE(failsWith(response, "x").empty());
    }
}
