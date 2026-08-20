// A blocking TCP client stream with deadlines (architecture.md §2 `net_api`).
//
// Client only. Nothing in v1 listens, and the one caller -- the dev-server
// control connection (ADR 0035) -- dials 127.0.0.1. A listener is what would
// put a port on the developer's machine, which is the thing that decision
// exists to avoid.
//
// No socket type appears in this header. `IDevice` earns its opacity the same
// way (R17): a caller that cannot see a `SOCKET` cannot write code that only
// compiles on one platform.
#pragma once

#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "luaug/core/error.h"
#include "luaug/core/types.h"

namespace luaug::net
{

using core::u16;
using core::u32;
using core::u8;
using core::usize;

class TcpStream
{
public:
    TcpStream();
    ~TcpStream();

    TcpStream(const TcpStream&) = delete;
    TcpStream& operator=(const TcpStream&) = delete;
    TcpStream(TcpStream&&) noexcept;
    TcpStream& operator=(TcpStream&&) noexcept;

    // Resolves `host`, connects, and gives up after `timeoutMs`. The timeout is
    // the point: the dev server may not be listening yet, and the OS default
    // for a refused-then-retried connect is tens of seconds of nothing.
    [[nodiscard]] std::optional<core::EngineError> connect(std::string_view host, u16 port, u32 timeoutMs);

    // Writes all of `data` or fails. A partial write is not reported as
    // success: the caller is framing a protocol, and half a frame is worse
    // than a closed connection.
    [[nodiscard]] std::optional<core::EngineError> send(std::span<const u8> data);

    // Reads whatever has arrived, up to `out.size()`.
    //
    // `received == 0` with no error means `timeoutMs` elapsed and nothing came.
    // An orderly close by the peer is `net.err.closed` -- an error, not zero --
    // because a reader that cannot tell "nothing yet" from "never again" spins
    // forever on a dead socket.
    [[nodiscard]] std::optional<core::EngineError> receive(std::span<u8> out, usize& received, u32 timeoutMs);

    void close();
    [[nodiscard]] bool isOpen() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace luaug::net
