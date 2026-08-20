#include "luaug/net/tcp.h"

#include <algorithm>
#include <array>
#include <mutex>
#include <string>

#if defined(_WIN32)
#include <winsock2.h>
// ws2tcpip.h must follow winsock2.h; the order is not stylistic.
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace luaug::net {
namespace {

using core::I18nArg;

// Winsock's address length is `int`; POSIX's is an unsigned `socklen_t`. One
// alias, because casting to the wrong one is a -Wsign-conversion error on
// exactly one of the two tiers -- which is how this was found.
#if defined(_WIN32)
using SocketHandle = SOCKET;
using AddressLength = int;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;

// Winsock refuses every call until this has run. Once per process, and never
// unwound: WSACleanup at exit would race any thread still holding a socket, and
// the process is about to end anyway.
void ensureWinsock()
{
    static std::once_flag once;
    std::call_once(once, [] {
        WSADATA data{};
        WSAStartup(MAKEWORD(2, 2), &data);
    });
}

void closeSocket(SocketHandle handle)
{
    closesocket(handle);
}

[[nodiscard]] int lastSocketError()
{
    return WSAGetLastError();
}

[[nodiscard]] bool wouldBlock(int error)
{
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
}

[[nodiscard]] bool interrupted(int)
{
    return false;
}

[[nodiscard]] bool setNonBlocking(SocketHandle handle, bool enabled)
{
    u_long mode = enabled ? 1u : 0u;
    return ioctlsocket(handle, static_cast<long>(FIONBIO), &mode) == 0;
}

constexpr int kSendFlags = 0;
#else
using SocketHandle = int;
using AddressLength = socklen_t;
constexpr SocketHandle kInvalidSocket = -1;

void ensureWinsock()
{}

void closeSocket(SocketHandle handle)
{
    ::close(handle);
}

[[nodiscard]] int lastSocketError()
{
    return errno;
}

[[nodiscard]] bool wouldBlock(int error)
{
    return error == EWOULDBLOCK || error == EAGAIN || error == EINPROGRESS;
}

[[nodiscard]] bool interrupted(int error)
{
    return error == EINTR;
}

[[nodiscard]] bool setNonBlocking(SocketHandle handle, bool enabled)
{
    const int flags = fcntl(handle, F_GETFL, 0);
    if (flags < 0)
        return false;
    const int updated = enabled ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return fcntl(handle, F_SETFL, updated) == 0;
}

// Without this a write to a peer that has gone away kills the process with
// SIGPIPE instead of returning EPIPE, and the engine dies because a dev server
// was closed.
#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif
#endif

[[nodiscard]] std::string describeSocketError(int error)
{
    return std::to_string(error);
}

// One key per cause rather than one message with the reason interpolated in: a
// whole sentence inside a translated frame is the mixed case R3 exists to
// prevent. The OS error number is data and stays a parameter.
[[nodiscard]] core::EngineError connectError(std::string_view host, u16 port, core::TextKey key)
{
    const std::array<I18nArg, 2> args{I18nArg{"host", host}, I18nArg{"port", static_cast<core::i64>(port)}};
    return core::makeError(key, args);
}

[[nodiscard]] core::EngineError connectErrorCode(std::string_view host, u16 port, int code)
{
    const std::array<I18nArg, 3> args{I18nArg{"host", host}, I18nArg{"port", static_cast<core::i64>(port)},
                                      I18nArg{"code", static_cast<core::i64>(code)}};
    return core::makeError(LUAUG_TR("net.err.connect_failed"), args);
}

// Waits for the socket to become readable or writable. Returns 1 when it is,
// 0 on timeout, -1 on error.
[[nodiscard]] int waitFor(SocketHandle handle, bool forWrite, u32 timeoutMs)
{
    fd_set set;
    FD_ZERO(&set);
    FD_SET(handle, &set);

    timeval timeout{};
    timeout.tv_sec = static_cast<decltype(timeout.tv_sec)>(timeoutMs / 1000u);
    timeout.tv_usec = static_cast<decltype(timeout.tv_usec)>((timeoutMs % 1000u) * 1000u);

#if defined(_WIN32)
    const int nfds = 0;
#else
    const int nfds = handle + 1;
#endif

    for (;;) {
        const int ready = forWrite ? ::select(nfds, nullptr, &set, nullptr, &timeout)
                                   : ::select(nfds, &set, nullptr, nullptr, &timeout);
        if (ready < 0 && interrupted(lastSocketError()))
            continue;
        return ready;
    }
}

} // namespace

struct TcpStream::Impl
{
    SocketHandle handle = kInvalidSocket;
};

TcpStream::TcpStream() : m_impl(std::make_unique<Impl>())
{}

TcpStream::~TcpStream()
{
    close();
}

TcpStream::TcpStream(TcpStream&&) noexcept = default;
TcpStream& TcpStream::operator=(TcpStream&&) noexcept = default;

std::optional<core::EngineError> TcpStream::connect(std::string_view host, u16 port, u32 timeoutMs)
{
    ensureWinsock();
    close();

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    const std::string hostText(host);
    const std::string portText = std::to_string(port);

    addrinfo* resolved = nullptr;
    if (::getaddrinfo(hostText.c_str(), portText.c_str(), &hints, &resolved) != 0 || resolved == nullptr)
        return connectError(host, port, LUAUG_TR("net.err.connect_unresolved"));

    std::optional<core::EngineError> lastError;

    for (const addrinfo* candidate = resolved; candidate != nullptr; candidate = candidate->ai_next) {
        const SocketHandle handle = ::socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
        if (handle == kInvalidSocket) {
            lastError = connectErrorCode(host, port, lastSocketError());
            continue;
        }

        if (!setNonBlocking(handle, true)) {
            closeSocket(handle);
            lastError = connectError(host, port, LUAUG_TR("net.err.connect_socket_setup"));
            continue;
        }

        bool connected = false;
        const int result = ::connect(handle, candidate->ai_addr, static_cast<AddressLength>(candidate->ai_addrlen));
        if (result == 0) {
            connected = true;
        }
        else if (wouldBlock(lastSocketError())) {
            // Writability means the connect resolved, one way or the other;
            // SO_ERROR is what says which.
            if (waitFor(handle, true, timeoutMs) == 1) {
                int soError = 0;
                AddressLength length = static_cast<AddressLength>(sizeof(soError));
#if defined(_WIN32)
                const int probe =
                    ::getsockopt(handle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &length);
#else
                const int probe = ::getsockopt(handle, SOL_SOCKET, SO_ERROR, &soError, &length);
#endif
                connected = probe == 0 && soError == 0;
                if (!connected && soError != 0)
                    lastError = connectErrorCode(host, port, soError);
            }
            else {
                lastError = connectError(host, port, LUAUG_TR("net.err.connect_timeout"));
            }
        }
        else {
            lastError = connectErrorCode(host, port, lastSocketError());
        }

        if (!connected) {
            closeSocket(handle);
            continue;
        }

        if (!setNonBlocking(handle, false)) {
            closeSocket(handle);
            lastError = connectError(host, port, LUAUG_TR("net.err.connect_socket_setup"));
            continue;
        }

        // The control protocol is a stream of small messages whose latency is
        // the whole point; Nagle would hold each one waiting for a companion.
        const int one = 1;
#if defined(_WIN32)
        ::setsockopt(handle, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
#else
        ::setsockopt(handle, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#endif

        m_impl->handle = handle;
        ::freeaddrinfo(resolved);
        return std::nullopt;
    }

    ::freeaddrinfo(resolved);
    return lastError.value_or(connectError(host, port, LUAUG_TR("net.err.connect_exhausted")));
}

std::optional<core::EngineError> TcpStream::send(std::span<const u8> data)
{
    if (!isOpen())
        return core::makeError(LUAUG_TR("net.err.closed"));

    usize sent = 0;
    while (sent < data.size()) {
        const auto chunk = static_cast<int>(std::min<usize>(data.size() - sent, 1u << 20));
#if defined(_WIN32)
        const int written =
            ::send(m_impl->handle, reinterpret_cast<const char*>(data.data() + sent), chunk, kSendFlags);
#else
        const auto written =
            static_cast<int>(::send(m_impl->handle, data.data() + sent, static_cast<usize>(chunk), kSendFlags));
#endif
        if (written > 0) {
            sent += static_cast<usize>(written);
            continue;
        }

        const int error = lastSocketError();
        if (interrupted(error))
            continue;
        if (wouldBlock(error)) {
            if (waitFor(m_impl->handle, true, 5000) == 1)
                continue;
        }

        const std::array<I18nArg, 1> args{I18nArg{"detail", describeSocketError(error)}};
        return core::makeError(LUAUG_TR("net.err.send_failed"), args);
    }

    return std::nullopt;
}

std::optional<core::EngineError> TcpStream::receive(std::span<u8> out, usize& received, u32 timeoutMs)
{
    received = 0;

    if (!isOpen())
        return core::makeError(LUAUG_TR("net.err.closed"));
    if (out.empty())
        return std::nullopt;

    const int ready = waitFor(m_impl->handle, false, timeoutMs);
    if (ready == 0)
        return std::nullopt;
    if (ready < 0) {
        const std::array<I18nArg, 1> args{I18nArg{"detail", describeSocketError(lastSocketError())}};
        return core::makeError(LUAUG_TR("net.err.receive_failed"), args);
    }

#if defined(_WIN32)
    const int got = ::recv(m_impl->handle, reinterpret_cast<char*>(out.data()), static_cast<int>(out.size()), 0);
#else
    const auto got = static_cast<int>(::recv(m_impl->handle, out.data(), out.size(), 0));
#endif

    if (got > 0) {
        received = static_cast<usize>(got);
        return std::nullopt;
    }
    if (got == 0)
        return core::makeError(LUAUG_TR("net.err.closed"));

    const int error = lastSocketError();
    if (interrupted(error) || wouldBlock(error))
        return std::nullopt;

    const std::array<I18nArg, 1> args{I18nArg{"detail", describeSocketError(error)}};
    return core::makeError(LUAUG_TR("net.err.receive_failed"), args);
}

void TcpStream::close()
{
    if (m_impl && m_impl->handle != kInvalidSocket) {
        closeSocket(m_impl->handle);
        m_impl->handle = kInvalidSocket;
    }
}

bool TcpStream::isOpen() const noexcept
{
    return m_impl && m_impl->handle != kInvalidSocket;
}

} // namespace luaug::net
