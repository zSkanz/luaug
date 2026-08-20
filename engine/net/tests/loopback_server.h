// Test scaffolding: a one-connection TCP server on 127.0.0.1, so the WebSocket
// client can be driven against bytes a test chooses -- including the malformed
// ones no real server would send.
//
// Deliberately NOT part of the module. Nothing in v1 listens (ADR 0035), and a
// `TcpListener` whose only caller is a test would be public surface bought for
// nothing. The real interop proof is the M3 gate's end-to-end run against
// Lute's WebSocket server; this is for the cases Lute would never produce.
#pragma once

#include <atomic>
#include <cstring>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "luaug/core/types.h"

namespace luaug::net::testing {

using core::u16;
using core::u8;
using core::usize;

#if defined(_WIN32)
using RawSocket = SOCKET;
inline constexpr RawSocket kInvalid = INVALID_SOCKET;
inline void closeRaw(RawSocket s)
{
    if (s != kInvalid)
        closesocket(s);
}
inline void startupSockets()
{
    static bool done = [] {
        WSADATA data{};
        WSAStartup(MAKEWORD(2, 2), &data);
        return true;
    }();
    (void)done;
}
#else
using RawSocket = int;
inline constexpr RawSocket kInvalid = -1;
inline void closeRaw(RawSocket s)
{
    if (s != kInvalid)
        ::close(s);
}
inline void startupSockets()
{}
#endif

// The accepted connection, as seen by the fake server.
class Connection
{
public:
    explicit Connection(RawSocket socket) : m_socket(socket)
    {
        // A read deadline, because the failure mode of this helper is a test
        // that waits forever for bytes the client was never going to send --
        // and a suite that hangs tells you less than one that fails, while
        // costing a CI runner its whole timeout to say it.
#if defined(_WIN32)
        const DWORD timeoutMs = 10000;
        ::setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
#else
        timeval timeout{};
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
        ::setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
    }

    ~Connection() { closeRaw(m_socket); }

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Reads until `marker` has been seen, and returns everything up to and
    // including it. Throws if the peer closes first, which fails the test with
    // a message rather than hanging it.
    std::string readUntil(std::string_view marker)
    {
        while (m_buffer.find(marker) == std::string::npos)
            pump();
        const usize end = m_buffer.find(marker) + marker.size();
        std::string taken = m_buffer.substr(0, end);
        m_buffer.erase(0, end);
        return taken;
    }

    std::vector<u8> read(usize count)
    {
        while (m_buffer.size() < count)
            pump();
        std::vector<u8> taken(m_buffer.begin(), m_buffer.begin() + static_cast<std::ptrdiff_t>(count));
        m_buffer.erase(0, count);
        return taken;
    }

    void write(std::span<const u8> data)
    {
        usize sent = 0;
        while (sent < data.size()) {
#if defined(_WIN32)
            const int written = ::send(m_socket, reinterpret_cast<const char*>(data.data() + sent),
                                       static_cast<int>(data.size() - sent), 0);
#else
            const auto written = static_cast<int>(::send(m_socket, data.data() + sent, data.size() - sent, 0));
#endif
            if (written <= 0)
                throw std::runtime_error("loopback server: the client went away mid-write");
            sent += static_cast<usize>(written);
        }
    }

    void write(std::string_view text)
    {
        write(std::span<const u8>(reinterpret_cast<const u8*>(text.data()), text.size()));
    }

    void close() { closeRaw(std::exchange(m_socket, kInvalid)); }

private:
    void pump()
    {
        char chunk[2048];
#if defined(_WIN32)
        const int got = ::recv(m_socket, chunk, static_cast<int>(sizeof(chunk)), 0);
#else
        const auto got = static_cast<int>(::recv(m_socket, chunk, sizeof(chunk), 0));
#endif
        if (got <= 0)
            throw std::runtime_error("loopback server: timed out or closed while waiting for the client");
        m_buffer.append(chunk, static_cast<usize>(got));
    }

    RawSocket m_socket = kInvalid;
    std::string m_buffer;
};

class LoopbackServer
{
public:
    LoopbackServer()
    {
        startupSockets();

        m_listener = ::socket(AF_INET, SOCK_STREAM, 0);
        if (m_listener == kInvalid)
            throw std::runtime_error("loopback server: no socket");

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        // Port zero: the OS picks a free one, so two tests never collide and a
        // developer's occupied port never fails a build.
        address.sin_port = 0;

        if (::bind(m_listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
            throw std::runtime_error("loopback server: bind failed");
        if (::listen(m_listener, 1) != 0)
            throw std::runtime_error("loopback server: listen failed");

        sockaddr_in bound{};
        socklen_t length = sizeof(bound);
        if (::getsockname(m_listener, reinterpret_cast<sockaddr*>(&bound), &length) != 0)
            throw std::runtime_error("loopback server: getsockname failed");
        m_port = ntohs(bound.sin_port);
    }

    ~LoopbackServer()
    {
        join();
        closeRaw(m_listener);
    }

    LoopbackServer(const LoopbackServer&) = delete;
    LoopbackServer& operator=(const LoopbackServer&) = delete;

    [[nodiscard]] u16 port() const noexcept { return m_port; }

    // Accepts one client on a background thread and hands it to `handler`. An
    // exception inside the handler is recorded rather than thrown across the
    // thread boundary, where it would terminate the process; the test reads it
    // back from `failure()` after `join`.
    void serve(std::function<void(Connection&)> handler)
    {
        m_thread = std::thread([this, handler = std::move(handler)] {
            const RawSocket accepted = ::accept(m_listener, nullptr, nullptr);
            if (accepted == kInvalid) {
                m_failure = "loopback server: accept failed";
                return;
            }
            Connection connection(accepted);
            try {
                handler(connection);
            } catch (const std::exception& error) {
                m_failure = error.what();
            }
        });
    }

    void join()
    {
        if (m_thread.joinable())
            m_thread.join();
    }

    // Empty when the server side saw nothing wrong.
    [[nodiscard]] const std::string& failure() const noexcept { return m_failure; }

private:
    RawSocket m_listener = kInvalid;
    u16 m_port = 0;
    std::thread m_thread;
    std::string m_failure;
};

} // namespace luaug::net::testing
