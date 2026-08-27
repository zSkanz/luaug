// The ENet implementation of `ITransport` (ADR 0012).
//
// One translation unit, and every ENet type stays inside it. `transport.h`
// exposes a factory returning the interface for exactly that reason (R17): the
// day GameNetworkingSockets or QUIC arrives beside this, nothing above has to
// change, because nothing above ever knew what was underneath.
#include "luaug/core/i18n.h"
#include "luaug/core/log.h"
#include "luaug/net/transport.h"

#include <algorithm>
#include <cstdint>
#include <enet/enet.h>
#include <unordered_map>

namespace luaug::net {
namespace {

using core::I18nArg;
using core::LogLevel;

// ENet is initialised once per process and deinitialised never.
//
// Never, deliberately. `enet_deinitialize` calls `WSACleanup` on Windows, and
// this process has other things holding sockets -- the dev-server control
// connection (ADR 0035) among them. A transport going away must not take the
// platform's networking down with it, and the cost is one refcount.
[[nodiscard]] bool ensureEnet()
{
    static const bool initialized = [] {
        if (enet_initialize() != 0) {
            core::log(LogLevel::Warn, LUAUG_TR("net.err.transport_init_failed"), {});
            return false;
        }
        return true;
    }();
    return initialized;
}

// The ENet flags one `Delivery` asks for.
//
// **`UNRELIABLE_FRAGMENT` is on both unreliable modes, and leaving it off was a
// defect that only appeared above one MTU** (D150). `enet_peer_send` tests for
// fragmentation FIRST, before it dispatches on unsequenced-versus-reliable
// (`third_party/enet/peer.c:121`), and its fragment branch reads
// `(flags & (RELIABLE | UNRELIABLE_FRAGMENT)) == UNRELIABLE_FRAGMENT`. With
// neither bit set that test fails, so the `else` sends
// `SEND_FRAGMENT | COMMAND_FLAG_ACKNOWLEDGE` -- **reliable, acknowledged and
// head-of-line blocking**, which is the exact thing `UnreliableSequenced` is
// chosen to avoid. The threshold is `mtu - sizeof(ENetProtocolHeader) -
// sizeof(ENetProtocolSendFragment)`, about 1364 bytes at ENet's default MTU of
// 1392, and a state delta is precisely the payload that crosses it.
[[nodiscard]] constexpr enet_uint32 flagsFor(Delivery delivery) noexcept
{
    switch (delivery) {
    case Delivery::Reliable:
        return ENET_PACKET_FLAG_RELIABLE;
    case Delivery::Unreliable:
        // ENet's UNSEQUENCED is the one that may arrive out of order. Its plain
        // unreliable packet is in fact sequenced, which is a naming trap this
        // switch exists to pay for once rather than at every call site.
        //
        // **A fragmented one is sequenced anyway**, and that is ENet's
        // limitation rather than a choice made here: there is no unsequenced
        // fragment command in the protocol, so a payload over one MTU gets a
        // sequence number. Sequenced-and-droppable is still much nearer this
        // mode's contract -- "may be dropped and may arrive out of order" --
        // than the reliable, ordered delivery it used to silently become.
        return ENET_PACKET_FLAG_UNSEQUENCED | ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT;
    case Delivery::UnreliableSequenced:
        return ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT;
    }
    return ENET_PACKET_FLAG_RELIABLE;
}

// **Asserted here rather than in a test, because no test can see it.** ENet is
// linked PRIVATE precisely so that nothing above this file knows its constants
// (the CMakeLists calls that "the R17 rule holding in the build graph"), and a
// loopback round-trip cannot tell the two apart anyway: a packet that wrongly
// became reliable still arrives, with the same bytes, in the same order. What
// went wrong was invisible to every observation available to a test.
//
// A compile-time check costs nothing, runs on every build on every platform,
// and reads as the contract it is guarding.
static_assert((flagsFor(Delivery::Unreliable) & ENET_PACKET_FLAG_RELIABLE) == 0,
              "an unreliable delivery must not be sent reliably");
static_assert((flagsFor(Delivery::UnreliableSequenced) & ENET_PACKET_FLAG_RELIABLE) == 0,
              "an unreliable-sequenced delivery must not be sent reliably");
static_assert((flagsFor(Delivery::Unreliable) & ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT) != 0,
              "an unreliable delivery must stay unreliable above one MTU (D150)");
static_assert((flagsFor(Delivery::UnreliableSequenced) & ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT) != 0,
              "an unreliable-sequenced delivery must stay unreliable above one MTU (D150)");
static_assert((flagsFor(Delivery::Reliable) & ENET_PACKET_FLAG_RELIABLE) != 0,
              "a reliable delivery must be sent reliably");

class EnetTransport final : public ITransport
{
public:
    ~EnetTransport() override { close(); }

    std::optional<core::EngineError> open(const TransportConfig& config) override
    {
        if (!ensureEnet()) {
            return core::makeError(LUAUG_TR("net.err.transport_init_failed"));
        }
        close();

        ENetAddress address{};
        ENetAddress* bindTo = nullptr;
        if (config.port != 0) {
            address.host = ENET_HOST_ANY;
            address.port = config.port;
            bindTo = &address;
        }

        m_channels = std::max<u8>(1, config.channels);
        m_host = enet_host_create(bindTo, config.maxPeers, m_channels, 0, 0);
        if (m_host == nullptr) {
            const I18nArg args[] = {{"port", static_cast<core::i64>(config.port)}};
            return core::makeError(LUAUG_TR("net.err.transport_open_failed"), args);
        }
        return std::nullopt;
    }

    void close() override
    {
        if (m_host == nullptr) {
            return;
        }
        // Peers are RESET rather than gracefully disconnected. A graceful close
        // needs a round trip and this runs from a destructor: a caller that
        // wants the peer told calls `disconnect` and pumps `poll`.
        for (const auto& entry : m_peers) {
            entry.second->data = nullptr;
            enet_peer_reset(entry.second);
        }
        m_peers.clear();
        enet_host_destroy(m_host);
        m_host = nullptr;
    }

    std::optional<core::EngineError> connect(std::string_view host, u16 port, PeerId& outPeer) override
    {
        outPeer = PeerId{};
        if (m_host == nullptr) {
            return core::makeError(LUAUG_TR("net.err.transport_not_open"));
        }

        ENetAddress address{};
        address.port = port;
        const std::string hostText(host);
        if (enet_address_set_host(&address, hostText.c_str()) != 0) {
            const I18nArg args[] = {{"host", hostText}};
            return core::makeError(LUAUG_TR("net.err.connect_unresolved"), args);
        }

        ENetPeer* const peer = enet_host_connect(m_host, &address, m_channels, 0);
        if (peer == nullptr) {
            const I18nArg args[] = {{"host", hostText}, {"port", static_cast<core::i64>(port)}};
            return core::makeError(LUAUG_TR("net.err.transport_no_peer_slot"), args);
        }

        outPeer = track(peer);
        return std::nullopt;
    }

    void disconnect(PeerId peer) override
    {
        const auto at = m_peers.find(peer.value);
        if (at == m_peers.end()) {
            return;
        }
        enet_peer_disconnect(at->second, 0);
    }

    std::optional<core::EngineError> send(PeerId peer, std::span<const u8> payload, Delivery delivery,
                                          u8 channel) override
    {
        if (m_host == nullptr) {
            return core::makeError(LUAUG_TR("net.err.transport_not_open"));
        }
        const auto at = m_peers.find(peer.value);
        if (at == m_peers.end()) {
            return core::makeError(LUAUG_TR("net.err.transport_unknown_peer"));
        }
        if (channel >= m_channels) {
            const I18nArg args[] = {{"channel", static_cast<core::i64>(channel)},
                                    {"channels", static_cast<core::i64>(m_channels)}};
            return core::makeError(LUAUG_TR("net.err.transport_bad_channel"), args);
        }

        ENetPacket* const packet = enet_packet_create(payload.data(), payload.size(), flagsFor(delivery));
        if (packet == nullptr) {
            return core::makeError(LUAUG_TR("net.err.transport_send_failed"));
        }
        if (enet_peer_send(at->second, channel, packet) != 0) {
            // ENet takes ownership on SUCCESS only, so a failed send is ours to
            // free. Getting that backwards is a leak per dropped packet, which
            // is the shape of leak nobody finds.
            enet_packet_destroy(packet);
            return core::makeError(LUAUG_TR("net.err.transport_send_failed"));
        }
        return std::nullopt;
    }

    std::optional<core::EngineError> poll(std::vector<TransportEvent>& out, u32 timeoutMs) override
    {
        if (m_host == nullptr) {
            return core::makeError(LUAUG_TR("net.err.transport_not_open"));
        }

        ENetEvent event{};
        // The FIRST service call carries the timeout and the rest do not: the
        // caller asked to wait up to `timeoutMs` for something, not for each
        // thing, and a loop that re-waited would block for the whole timeout
        // once per event on a busy connection.
        enet_uint32 wait = timeoutMs;
        while (enet_host_service(m_host, &event, wait) > 0) {
            wait = 0;
            switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:
                // Every field named, empty ones included. Clang counts a skipped
                // designator as a missing initializer under `-Werror`, and the
                // Tier-2 build is where that is discovered.
                out.push_back(TransportEvent{
                    .kind = TransportEvent::Kind::Connected, .peer = track(event.peer), .payload = {}, .channel = 0});
                break;
            case ENET_EVENT_TYPE_DISCONNECT: {
                const PeerId id = idOf(event.peer);
                forget(event.peer);
                out.push_back(TransportEvent{
                    .kind = TransportEvent::Kind::Disconnected, .peer = id, .payload = {}, .channel = 0});
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE: {
                TransportEvent message{
                    .kind = TransportEvent::Kind::Message, .peer = idOf(event.peer), .payload = {}, .channel = 0};
                message.channel = event.channelID;
                const auto* const bytes = reinterpret_cast<const u8*>(event.packet->data);
                message.payload.assign(bytes, bytes + event.packet->dataLength);
                enet_packet_destroy(event.packet);
                out.push_back(std::move(message));
                break;
            }
            case ENET_EVENT_TYPE_NONE:
                break;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] usize peerCount() const noexcept override { return m_peers.size(); }

private:
    // The id lives in ENet's own per-peer `data` pointer rather than in a map
    // keyed by peer address. That is what keeps `idOf` correct after a peer
    // slot is REUSED: ENet clears the field on reset, so a recycled slot cannot
    // answer with the previous connection's id.
    [[nodiscard]] PeerId track(ENetPeer* peer)
    {
        if (peer->data != nullptr) {
            return PeerId{static_cast<u32>(reinterpret_cast<std::uintptr_t>(peer->data))};
        }
        const u32 id = m_nextId++;
        peer->data = reinterpret_cast<void*>(static_cast<std::uintptr_t>(id));
        m_peers.emplace(id, peer);
        return PeerId{id};
    }

    [[nodiscard]] static PeerId idOf(ENetPeer* peer) noexcept
    {
        if (peer == nullptr || peer->data == nullptr) {
            return PeerId{};
        }
        return PeerId{static_cast<u32>(reinterpret_cast<std::uintptr_t>(peer->data))};
    }

    void forget(ENetPeer* peer)
    {
        const PeerId id = idOf(peer);
        peer->data = nullptr;
        m_peers.erase(id.value);
    }

    ENetHost* m_host = nullptr;
    u8 m_channels = 2;
    // Ids start at one, so a default-constructed `PeerId` is never a peer.
    u32 m_nextId = 1;
    std::unordered_map<u32, ENetPeer*> m_peers;
};

} // namespace

std::unique_ptr<ITransport> createEnetTransport()
{
    return std::make_unique<EnetTransport>();
}

} // namespace luaug::net
