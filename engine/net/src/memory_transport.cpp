#include "luaug/net/memory_transport.h"

#include "luaug/core/i18n.h"
#include "luaug/core/random.h"

#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace luaug::net {

class MemoryEndpoint;

// The listeners by port, and nothing else. An endpoint registers itself when it
// opens a port and removes itself when it closes or dies, so a `connect` can
// only ever reach something alive.
class MemoryNetwork
{
public:
    std::map<u16, MemoryEndpoint*> listeners;
};

namespace {

// One side of a connection: the endpoint at the other end, and the id it knows
// this connection by.
struct Link
{
    MemoryEndpoint* remote = nullptr;
    PeerId remotePeer;
};

} // namespace

class MemoryEndpoint final : public ITransport
{
public:
    explicit MemoryEndpoint(std::shared_ptr<MemoryNetwork> network) : m_network(std::move(network)) {}

    ~MemoryEndpoint() override { close(); }

    MemoryEndpoint(const MemoryEndpoint&) = delete;
    MemoryEndpoint& operator=(const MemoryEndpoint&) = delete;

    std::optional<core::EngineError> open(const TransportConfig& config) override
    {
        if (config.port != 0) {
            if (m_network->listeners.contains(config.port)) {
                const core::I18nArg args[] = {{"port", static_cast<core::i64>(config.port)}};
                return core::makeError(LUAUG_TR("net.err.transport_open_failed"), args);
            }
            m_network->listeners[config.port] = this;
        }
        m_port = config.port;
        m_channels = config.channels;
        m_maxPeers = config.maxPeers;
        m_open = true;
        return std::nullopt;
    }

    void close() override
    {
        if (!m_open)
            return;
        // Everybody still connected hears about it, exactly as a real peer
        // would see its connection time out -- only immediately.
        while (!m_links.empty()) {
            const PeerId peer = m_links.begin()->first;
            disconnect(peer);
        }
        if (m_port != 0) {
            const auto found = m_network->listeners.find(m_port);
            if (found != m_network->listeners.end() && found->second == this)
                m_network->listeners.erase(found);
        }
        m_port = 0;
        m_open = false;
    }

    std::optional<core::EngineError> connect(std::string_view host, u16 port, PeerId& outPeer) override
    {
        if (!m_open)
            return core::makeError(LUAUG_TR("net.err.transport_not_open"));
        const auto found = m_network->listeners.find(port);
        if (found == m_network->listeners.end() || found->second == this) {
            const core::I18nArg args[] = {{"host", host}};
            return core::makeError(LUAUG_TR("net.err.connect_unresolved"), args);
        }
        MemoryEndpoint& remote = *found->second;
        if (m_links.size() >= m_maxPeers || remote.m_links.size() >= remote.m_maxPeers) {
            const core::I18nArg args[] = {{"host", host}, {"port", static_cast<core::i64>(port)}};
            return core::makeError(LUAUG_TR("net.err.transport_no_peer_slot"), args);
        }

        const PeerId local = nextPeer();
        const PeerId far = remote.nextPeer();
        m_links[local] = Link{&remote, far};
        remote.m_links[far] = Link{this, local};
        // Both sides learn at their next poll, which is the contract `connect`
        // states: the peer is not usable until a `Connected` comes out of it.
        m_inbox.push_back(TransportEvent{TransportEvent::Kind::Connected, local, {}, 0});
        remote.m_inbox.push_back(TransportEvent{TransportEvent::Kind::Connected, far, {}, 0});
        outPeer = local;
        return std::nullopt;
    }

    void disconnect(PeerId peer) override
    {
        const auto found = m_links.find(peer);
        if (found == m_links.end())
            return;
        const Link link = found->second;
        m_links.erase(found);
        m_inbox.push_back(TransportEvent{TransportEvent::Kind::Disconnected, peer, {}, 0});
        if (link.remote != nullptr) {
            link.remote->m_links.erase(link.remotePeer);
            link.remote->m_inbox.push_back(TransportEvent{TransportEvent::Kind::Disconnected, link.remotePeer, {}, 0});
        }
    }

    std::optional<core::EngineError> send(PeerId peer, std::span<const u8> payload, Delivery delivery,
                                          u8 channel) override
    {
        (void)delivery;
        if (!m_open)
            return core::makeError(LUAUG_TR("net.err.transport_not_open"));
        const auto found = m_links.find(peer);
        if (found == m_links.end())
            return core::makeError(LUAUG_TR("net.err.transport_unknown_peer"));
        if (channel >= m_channels) {
            const core::I18nArg args[] = {{"channel", static_cast<core::i64>(channel)},
                                          {"channels", static_cast<core::i64>(m_channels)}};
            return core::makeError(LUAUG_TR("net.err.transport_bad_channel"), args);
        }
        const Link& link = found->second;
        link.remote->m_inbox.push_back(TransportEvent{TransportEvent::Kind::Message, link.remotePeer,
                                                      std::vector<u8>(payload.begin(), payload.end()), channel});
        return std::nullopt;
    }

    std::optional<core::EngineError> poll(std::vector<TransportEvent>& out, u32 timeoutMs) override
    {
        (void)timeoutMs;
        for (TransportEvent& event : m_inbox)
            out.push_back(std::move(event));
        m_inbox.clear();
        return std::nullopt;
    }

    [[nodiscard]] usize peerCount() const noexcept override { return m_links.size(); }

private:
    [[nodiscard]] PeerId nextPeer() noexcept { return PeerId{++m_lastPeer}; }

    std::shared_ptr<MemoryNetwork> m_network;
    // Ordered, so closing disconnects in id order and the events it queues are
    // a function of the connections rather than of a hash (R10).
    struct PeerLess
    {
        [[nodiscard]] bool operator()(PeerId a, PeerId b) const noexcept { return a.value < b.value; }
    };
    std::map<PeerId, Link, PeerLess> m_links;
    std::vector<TransportEvent> m_inbox;
    u32 m_lastPeer = 0;
    u16 m_port = 0;
    u8 m_channels = 2;
    usize m_maxPeers = 32;
    bool m_open = false;
};

namespace {

class LossyTransport final : public ITransport
{
public:
    LossyTransport(std::unique_ptr<ITransport> inner, const LossConfig& config)
        : m_inner(std::move(inner)), m_config(config), m_random(config.seed, 0x6C6F7373ull)
    {}

    std::optional<core::EngineError> open(const TransportConfig& config) override { return m_inner->open(config); }
    void close() override
    {
        m_held.reset();
        m_inner->close();
    }
    std::optional<core::EngineError> connect(std::string_view host, u16 port, PeerId& outPeer) override
    {
        return m_inner->connect(host, port, outPeer);
    }
    void disconnect(PeerId peer) override { m_inner->disconnect(peer); }

    std::optional<core::EngineError> send(PeerId peer, std::span<const u8> payload, Delivery delivery,
                                          u8 channel) override
    {
        if (delivery == Delivery::Reliable)
            return m_inner->send(peer, payload, delivery, channel);

        // Two draws every time, whichever way the first falls, so whether one
        // message is dropped never shifts which later ones are.
        const bool drop = m_random.nextU32() % 1000u < m_config.dropPerMille;
        const bool reorder = m_random.nextU32() % 1000u < m_config.reorderPerMille;
        if (drop)
            return std::nullopt;
        if (reorder && !m_held.has_value()) {
            m_held = Held{peer, std::vector<u8>(payload.begin(), payload.end()), delivery, channel};
            return std::nullopt;
        }
        std::optional<core::EngineError> error = m_inner->send(peer, payload, delivery, channel);
        releaseHeld();
        return error;
    }

    std::optional<core::EngineError> poll(std::vector<TransportEvent>& out, u32 timeoutMs) override
    {
        // A message held back and never followed would otherwise be held for
        // ever, which is a drop the configuration did not ask for.
        releaseHeld();
        return m_inner->poll(out, timeoutMs);
    }

    [[nodiscard]] usize peerCount() const noexcept override { return m_inner->peerCount(); }

private:
    struct Held
    {
        PeerId peer;
        std::vector<u8> payload;
        Delivery delivery = Delivery::Unreliable;
        u8 channel = 0;
    };

    void releaseHeld()
    {
        if (!m_held.has_value())
            return;
        const Held held = std::move(*m_held);
        m_held.reset();
        (void)m_inner->send(held.peer, held.payload, held.delivery, held.channel);
    }

    std::unique_ptr<ITransport> m_inner;
    LossConfig m_config;
    core::Pcg32 m_random;
    std::optional<Held> m_held;
};

} // namespace

std::shared_ptr<MemoryNetwork> createMemoryNetwork()
{
    return std::make_shared<MemoryNetwork>();
}

std::unique_ptr<ITransport> createMemoryTransport(std::shared_ptr<MemoryNetwork> network)
{
    return std::make_unique<MemoryEndpoint>(std::move(network));
}

std::unique_ptr<ITransport> createLossyTransport(std::unique_ptr<ITransport> inner, const LossConfig& config)
{
    return std::make_unique<LossyTransport>(std::move(inner), config);
}

} // namespace luaug::net
