// The WebSocket client the engine dials the dev server with (ADR 0035).
//
// Client only, and deliberately: only the dev server listens, so the engine
// opens no port in any profile. This is a host seam -- nothing here is bound to
// Luau and no game script can reach it (R17). `@std/net` in the game VM is M7.
//
// The protocol rules live in `websocket_protocol.h`, which has no socket in it;
// this file is the part that cannot be tested against a published vector.
#pragma once

#include "luaug/core/error.h"
#include "luaug/core/types.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace luaug::net {

using core::u16;
using core::u32;
using core::usize;

struct WebSocketClientOptions
{
    std::string host = "127.0.0.1";
    u16 port = 0;
    std::string path = "/";

    u32 connectTimeoutMs = 2000;

    // The ceiling on one reassembled application message. The control protocol
    // carries small JSON objects, so this is generous by two orders of
    // magnitude; what it is really for is that a length field is eight bytes
    // wide and a peer may name any of them.
    usize maxMessageBytes = 1u << 20;
};

class WebSocketClient
{
public:
    WebSocketClient();
    ~WebSocketClient();

    WebSocketClient(const WebSocketClient&) = delete;
    WebSocketClient& operator=(const WebSocketClient&) = delete;
    WebSocketClient(WebSocketClient&&) noexcept;
    WebSocketClient& operator=(WebSocketClient&&) noexcept;

    // Connects and completes the opening handshake. On return without an error
    // the connection carries frames and nothing else.
    [[nodiscard]] std::optional<core::EngineError> connect(const WebSocketClientOptions& options);

    [[nodiscard]] std::optional<core::EngineError> sendText(std::string_view text);

    // One complete application message, fragments reassembled.
    //
    // `received == false` with no error means the deadline passed without a
    // whole message; a partly-arrived one stays buffered for the next call.
    // Ping is answered and Pong is dropped inside this call -- a caller that
    // had to remember to do that would eventually forget, and the connection
    // would die quietly.
    [[nodiscard]] std::optional<core::EngineError> receiveText(std::string& out, bool& received, u32 timeoutMs);

    // Sends a Close frame and shuts the socket. Safe to call twice.
    void close();
    [[nodiscard]] bool isOpen() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace luaug::net
