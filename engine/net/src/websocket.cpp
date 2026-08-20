#include "luaug/net/websocket.h"

#include "luaug/net/tcp.h"
#include "luaug/net/websocket_protocol.h"

#include <array>
#include <chrono>
#include <random>
#include <vector>

namespace luaug::net {
namespace {

using core::I18nArg;
using core::u64;

constexpr usize kReadChunk = 4096;

// The masking key must be unpredictable (RFC 6455 §5.3), so it is not drawn
// from the world's seeded stream. R10 governs simulation state, and none of
// this reaches it: a mask never enters the world hash and never decides
// anything a tick can observe.
[[nodiscard]] u32 randomWord(std::random_device& device)
{
    return static_cast<u32>(device());
}

// Milliseconds remaining before `deadline`, clamped at zero. steady_clock for
// the same reason every other deadline in the host uses it: a wall clock that
// steps backwards would make a timeout last an hour.
[[nodiscard]] u32 remainingMs(std::chrono::steady_clock::time_point deadline)
{
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline)
        return 0;
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
    return static_cast<u32>(left);
}

} // namespace

struct WebSocketClient::Impl
{
    TcpStream stream;
    std::vector<u8> inbound;
    // The message being reassembled across continuation frames, and the opcode
    // that started it -- a continuation carries no opcode of its own.
    std::vector<u8> pending;
    bool assembling = false;
    bool pendingIsText = false;
    usize maxMessageBytes = 1u << 20;
    std::random_device random;
};

WebSocketClient::WebSocketClient() : m_impl(std::make_unique<Impl>())
{}

WebSocketClient::~WebSocketClient()
{
    close();
}

WebSocketClient::WebSocketClient(WebSocketClient&&) noexcept = default;
WebSocketClient& WebSocketClient::operator=(WebSocketClient&&) noexcept = default;

std::optional<core::EngineError> WebSocketClient::connect(const WebSocketClientOptions& options)
{
    m_impl->inbound.clear();
    m_impl->pending.clear();
    m_impl->assembling = false;
    m_impl->maxMessageBytes = options.maxMessageBytes;

    if (auto error = m_impl->stream.connect(options.host, options.port, options.connectTimeoutMs))
        return error;

    std::array<u8, 16> nonce{};
    for (usize i = 0; i < nonce.size(); i += 4) {
        const u32 word = randomWord(m_impl->random);
        nonce[i] = static_cast<u8>((word >> 24) & 0xFFu);
        nonce[i + 1] = static_cast<u8>((word >> 16) & 0xFFu);
        nonce[i + 2] = static_cast<u8>((word >> 8) & 0xFFu);
        nonce[i + 3] = static_cast<u8>(word & 0xFFu);
    }

    const ws::ClientHandshake handshake = ws::buildClientHandshake(options.host, options.port, options.path, nonce);

    const std::span<const u8> request(reinterpret_cast<const u8*>(handshake.request.data()), handshake.request.size());
    if (auto error = m_impl->stream.send(request)) {
        m_impl->stream.close();
        return error;
    }

    // Read until the header terminator. Anything past it is already frame
    // bytes: a server may legally send its first frame in the same packet as
    // the response, and discarding the remainder would lose it.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(options.connectTimeoutMs);
    std::string response;

    for (;;) {
        if (const auto end = ws::findHeaderEnd(response)) {
            if (auto error = ws::validateServerHandshake(response, handshake.expectedAccept)) {
                m_impl->stream.close();
                return error;
            }

            const std::string_view leftover = std::string_view(response).substr(*end);
            m_impl->inbound.assign(leftover.begin(), leftover.end());
            return std::nullopt;
        }

        const u32 left = remainingMs(deadline);
        if (left == 0) {
            m_impl->stream.close();
            return core::makeError(LUAUG_TR("net.err.handshake_timeout"));
        }

        std::array<u8, kReadChunk> chunk{};
        usize got = 0;
        if (auto error = m_impl->stream.receive(chunk, got, left)) {
            m_impl->stream.close();
            return error;
        }
        response.append(reinterpret_cast<const char*>(chunk.data()), got);

        // A response that never terminates is a peer feeding us a header
        // forever. The ceiling on a message is the ceiling on this too.
        if (response.size() > m_impl->maxMessageBytes) {
            m_impl->stream.close();
            return core::makeError(LUAUG_TR("net.err.handshake_headers_unbounded"));
        }
    }
}

std::optional<core::EngineError> WebSocketClient::sendText(std::string_view text)
{
    if (!isOpen())
        return core::makeError(LUAUG_TR("net.err.closed"));

    std::vector<u8> wire;
    wire.reserve(text.size() + 14);
    ws::encodeFrame(wire, ws::Opcode::Text, true,
                    std::span<const u8>(reinterpret_cast<const u8*>(text.data()), text.size()),
                    randomWord(m_impl->random));

    return m_impl->stream.send(wire);
}

std::optional<core::EngineError> WebSocketClient::receiveText(std::string& out, bool& received, u32 timeoutMs)
{
    received = false;
    out.clear();

    if (!isOpen())
        return core::makeError(LUAUG_TR("net.err.closed"));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    for (;;) {
        const ws::DecodeResult decoded = ws::decodeFrame(m_impl->inbound, m_impl->maxMessageBytes);

        if (decoded.status == ws::DecodeStatus::Malformed) {
            close();
            return core::makeError(LUAUG_TR("net.err.frame_malformed"));
        }

        if (decoded.status == ws::DecodeStatus::Ok) {
            m_impl->inbound.erase(m_impl->inbound.begin(),
                                  m_impl->inbound.begin() + static_cast<std::ptrdiff_t>(decoded.consumed));

            // §5.1: a server must not mask. Unmasking it anyway would hide a
            // peer that is not speaking this protocol.
            if (decoded.frame.masked) {
                close();
                return core::makeError(LUAUG_TR("net.err.server_masked"));
            }

            const ws::Frame& frame = decoded.frame;
            switch (frame.opcode) {
            case ws::Opcode::Ping: {
                std::vector<u8> pong;
                ws::encodeFrame(pong, ws::Opcode::Pong, true, frame.payload, randomWord(m_impl->random));
                if (auto error = m_impl->stream.send(pong))
                    return error;
                continue;
            }
            case ws::Opcode::Pong:
                continue;
            case ws::Opcode::Close: {
                std::vector<u8> reply;
                ws::encodeFrame(reply, ws::Opcode::Close, true, {}, randomWord(m_impl->random));
                (void)m_impl->stream.send(reply);
                m_impl->stream.close();
                return core::makeError(LUAUG_TR("net.err.closed"));
            }
            case ws::Opcode::Binary:
            case ws::Opcode::Text: {
                if (m_impl->assembling) {
                    close();
                    return core::makeError(LUAUG_TR("net.err.message_interleaved"));
                }
                m_impl->pendingIsText = frame.opcode == ws::Opcode::Text;
                m_impl->pending = frame.payload;
                m_impl->assembling = true;
                break;
            }
            case ws::Opcode::Continuation: {
                if (!m_impl->assembling) {
                    close();
                    return core::makeError(LUAUG_TR("net.err.continuation_orphan"));
                }
                if (m_impl->pending.size() + frame.payload.size() > m_impl->maxMessageBytes) {
                    close();
                    return core::makeError(LUAUG_TR("net.err.message_too_large"));
                }
                m_impl->pending.insert(m_impl->pending.end(), frame.payload.begin(), frame.payload.end());
                break;
            }
            }

            if (!frame.fin)
                continue;

            m_impl->assembling = false;
            if (!m_impl->pendingIsText) {
                m_impl->pending.clear();
                // The control channel is JSON text (ADR 0035). A binary message
                // means the peer is talking about something else, and guessing
                // which is worse than saying so.
                close();
                return core::makeError(LUAUG_TR("net.err.binary_on_text_channel"));
            }

            out.assign(m_impl->pending.begin(), m_impl->pending.end());
            m_impl->pending.clear();
            received = true;
            return std::nullopt;
        }

        const u32 left = remainingMs(deadline);
        if (left == 0)
            return std::nullopt;

        std::array<u8, kReadChunk> chunk{};
        usize got = 0;
        if (auto error = m_impl->stream.receive(chunk, got, left))
            return error;
        if (got == 0)
            continue;

        m_impl->inbound.insert(m_impl->inbound.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(got));
    }
}

void WebSocketClient::close()
{
    if (!m_impl || !m_impl->stream.isOpen())
        return;

    std::vector<u8> frame;
    ws::encodeFrame(frame, ws::Opcode::Close, true, {}, randomWord(m_impl->random));
    (void)m_impl->stream.send(frame);
    m_impl->stream.close();
}

bool WebSocketClient::isOpen() const noexcept
{
    return m_impl && m_impl->stream.isOpen();
}

} // namespace luaug::net
