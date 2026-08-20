// RFC 6455, the pure half: framing and the opening handshake, with no socket
// anywhere. Separated from the client (`websocket.h`) so that every rule the
// protocol states can be tested against the published vectors in RFC 6455 §1.3
// and §5.7 rather than against our own output -- which is the only way a
// framing test proves anything.
//
// The engine is a WebSocket CLIENT and never a server (ADR 0035), so this
// encodes masked frames and rejects masked ones: RFC 6455 §5.1 requires a
// client to mask and forbids a server to.
#pragma once

#include "luaug/core/error.h"
#include "luaug/core/types.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace luaug::net::ws {

using core::u16;
using core::u32;
using core::u8;
using core::usize;

enum class Opcode : u8
{
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA,
};

struct Frame
{
    Opcode opcode = Opcode::Text;
    bool fin = true;
    // Whether the frame arrived masked. A client rejects this: a server that
    // masks is violating §5.1, and silently unmasking it would hide the fault.
    bool masked = false;
    std::vector<u8> payload;
};

// Appends one frame to `out`. `maskKey` is the 32-bit masking key §5.3 requires
// of every client frame; it is a parameter rather than drawn here so that the
// encoder is deterministic under test and the randomness lives at the one call
// site that should own it.
void encodeFrame(std::vector<u8>& out, Opcode opcode, bool fin, std::span<const u8> payload, u32 maskKey);

enum class DecodeStatus
{
    Ok,
    // The buffer holds a prefix of a valid frame. Read more and call again.
    NeedMore,
    // The bytes cannot begin a valid frame. The connection must be closed:
    // resynchronising a stream protocol after a bad frame is not possible.
    Malformed,
};

struct DecodeResult
{
    DecodeStatus status = DecodeStatus::NeedMore;
    Frame frame;
    // Bytes consumed from `input` when the status is Ok; zero otherwise.
    usize consumed = 0;
};

// `maxPayload` bounds what a declared length may ask us to buffer. A frame
// header is eight bytes of length field, so without this a peer -- hostile or
// merely broken -- names 2^63 and we try to allocate it. Exceeding it is
// Malformed rather than NeedMore, because waiting for bytes we refuse to hold
// is a hang.
[[nodiscard]] DecodeResult decodeFrame(std::span<const u8> input, usize maxPayload);

struct ClientHandshake
{
    // The complete HTTP/1.1 upgrade request, ready to write to the socket.
    std::string request;
    // What `Sec-WebSocket-Accept` must equal in the response.
    std::string expectedAccept;
};

// `nonce` is the 16 random bytes §4.1 requires. Passed in for the same reason
// `maskKey` is.
[[nodiscard]] ClientHandshake buildClientHandshake(std::string_view host, u16 port, std::string_view path,
                                                   std::span<const u8, 16> nonce);

// §4.1: base64(SHA-1(key + the protocol's fixed GUID)).
[[nodiscard]] std::string computeAccept(std::string_view clientKeyBase64);

// Index just past the `\r\n\r\n` that ends the response headers, or nothing if
// the buffer does not contain it yet.
[[nodiscard]] std::optional<usize> findHeaderEnd(std::string_view buffer);

// Checks the status line, the two upgrade headers and the accept value.
// Returns the first failure; nothing means the handshake completed.
[[nodiscard]] std::optional<core::EngineError> validateServerHandshake(std::string_view response,
                                                                       std::string_view expectedAccept);

} // namespace luaug::net::ws
