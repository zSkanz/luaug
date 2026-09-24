// `createReplication`: the one door to a socket (ADR 0070, clause 2).
#include "luaug/replication/replication.h"

#include "luaug/core/error.h"
#include "luaug/core/i18n.h"
#include "luaug/core/log.h"
#include "luaug/replication/session.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "wire_schema.gen.h"

namespace luaug::replication {
namespace {

// The schema's four channels -- Control, State, Intent and the reserved one --
// opened whether or not all are used, so a peer that uses the reserved one
// later is not a peer this build refuses at the transport.
constexpr u8 ChannelCount = static_cast<u8>(std::size(generated::Channels));

class Replication final : public IReplication
{
public:
    Replication(std::unique_ptr<net::ITransport> transport, const Config& config)
        : m_transport(std::move(transport)), m_config(config)
    {}

    [[nodiscard]] std::optional<core::EngineError> start()
    {
        net::TransportConfig transport;
        transport.channels = ChannelCount;
        if (m_config.topology == Topology::Replica) {
            transport.port = 0;
            transport.maxPeers = 1;
            if (auto error = m_transport->open(transport); error.has_value())
                return error;
            net::PeerId authority;
            if (auto error = m_transport->connect(m_config.address, m_config.port, authority); error.has_value())
                return error;
            m_replica.emplace(*m_transport, authority);
            m_replica->setInterpolationDelay(m_config.interpolationDelayTicks);
            if (m_probe)
                m_replica->setReferenceProbe(m_probe);
            m_replica->setCharacterReplay(m_characterReplay);
            return std::nullopt;
        }
        transport.port = m_config.port;
        transport.maxPeers = m_config.maxPeers;
        if (auto error = m_transport->open(transport); error.has_value())
            return error;
        m_authority.emplace(*m_transport);
        return std::nullopt;
    }

    void receive(scene::World& world, core::InstanceId root) override
    {
        if (m_authority.has_value()) {
            m_authority->receive(world, root);
        }
        else if (m_replica.has_value()) {
            m_replica->receive(world, root);
            redial();
        }
    }

    void send(const scene::World& world, core::InstanceId root, u64 tick) override
    {
        m_tick = tick;
        // **A count of ticks, never milliseconds** (R10): what a peer is told
        // is a function of the simulation, not of how fast this machine ran it.
        if (m_authority.has_value() && tick % std::max<u32>(1, m_config.ticksPerSnapshot) == 0)
            m_authority->send(world, root, tick);
        // Intent every tick: it is small, and a snapshot rate is a choice about
        // the world while an input rate is a choice about how a game feels.
        if (m_replica.has_value())
            m_replica->sendIntent(world, tick);
    }

    void sendMessages(scene::World& world) override
    {
        if (m_authority.has_value())
            m_authority->sendMessages(world);
        else if (m_replica.has_value())
            m_replica->sendMessages(world);
    }

    [[nodiscard]] Status status() const override
    {
        Status status;
        status.topology = m_config.topology;
        status.authority = hasAuthority(m_config.topology);
        if (m_authority.has_value()) {
            status.serverTick = m_tick;
            status.peerCount = m_authority->peerCount();
        }
        else if (m_replica.has_value()) {
            status.serverTick = m_replica->appliedTick();
            status.peerCount = m_replica->welcomed() ? 1 : 0;
        }
        return status;
    }

    void setReferenceProbe(std::function<bool(core::InstanceId)> probe) override
    {
        m_probe = std::move(probe);
        if (m_replica.has_value())
            m_replica->setReferenceProbe(m_probe);
    }

    void setCharacterReplay(scene::ICharacterReplay* replay) override
    {
        m_characterReplay = replay;
        if (m_replica.has_value())
            m_replica->setCharacterReplay(replay);
    }

    [[nodiscard]] std::vector<core::InstanceId> drainStreamedOut() override
    {
        return m_replica.has_value() ? m_replica->drainStreamedOut() : std::vector<core::InstanceId>{};
    }

    [[nodiscard]] Stats stats() const override
    {
        if (m_authority.has_value())
            return m_authority->stats();
        if (m_replica.has_value())
            return m_replica->stats();
        return {};
    }

    void shutdown() override
    {
        m_authority.reset();
        m_replica.reset();
        if (m_transport != nullptr)
            m_transport->close();
    }

    ~Replication() override { shutdown(); }

    Replication(const Replication&) = delete;
    Replication& operator=(const Replication&) = delete;

private:
    // **A replica whose connection went dials again, and keeps dialling**
    // (ADR 0085): after one second, then two, four and so on to thirty, counted
    // in ticks so a slow machine waits as long in game time as a fast one. The
    // authority welcomes the same player back when it answers; until then the
    // world the replica holds stands still rather than vanishing.
    void redial()
    {
        m_receives += 1;
        if (!m_replica->lost()) {
            if (m_attempts > 0)
                core::log(core::LogLevel::Info, LUAUG_TR("net.info.reconnected"));
            m_attempts = 0;
            m_redialAt = 0;
            return;
        }
        if (m_redialAt == 0) {
            core::log(core::LogLevel::Warn, LUAUG_TR("net.warn.connection_lost"));
            m_redialAt = m_receives + RedialFirstTicks;
            return;
        }
        if (m_receives < m_redialAt)
            return;
        net::PeerId authority;
        if (!m_transport->connect(m_config.address, m_config.port, authority).has_value())
            m_replica->rebind(authority);
        m_attempts = std::min<u32>(m_attempts + 1, RedialDoublings);
        m_redialAt = m_receives + (RedialFirstTicks << m_attempts);
    }

    // One second at the fixed rate, doubled up to five times: thirty-two.
    static constexpr u64 RedialFirstTicks = 60;
    static constexpr u32 RedialDoublings = 5;

    std::unique_ptr<net::ITransport> m_transport;
    Config m_config;
    u64 m_receives = 0;
    u64 m_redialAt = 0;
    u32 m_attempts = 0;
    std::optional<AuthoritySession> m_authority;
    std::optional<ReplicaSession> m_replica;
    std::function<bool(core::InstanceId)> m_probe;
    scene::ICharacterReplay* m_characterReplay = nullptr;
    u64 m_tick = 0;
};

} // namespace

std::unique_ptr<IReplication> createReplication(const Config& config, std::optional<core::EngineError>& error)
{
    return createReplicationOver(net::createEnetTransport(), config, error);
}

std::unique_ptr<IReplication> createReplicationOver(std::unique_ptr<net::ITransport> transport, const Config& config,
                                                    std::optional<core::EngineError>& error)
{
    error.reset();
    if (config.topology == Topology::Solo)
        return nullptr;
    // Refused here as well as by the transport, in the players' terms rather
    // than the transport's, and before a socket is opened.
    if (config.topology != Topology::Replica && (config.maxPeers == 0 || config.maxPeers > net::EnetPeerCap)) {
        const core::I18nArg args[] = {{"count", static_cast<core::i64>(config.maxPeers)},
                                      {"cap", static_cast<core::i64>(net::EnetPeerCap)}};
        error = core::makeError(LUAUG_TR("net.err.replication_peer_cap"), args);
        return nullptr;
    }
    if (transport == nullptr) {
        error = core::makeError(LUAUG_TR("net.err.transport_init_failed"));
        return nullptr;
    }
    auto replication = std::make_unique<Replication>(std::move(transport), config);
    if (std::optional<core::EngineError> failed = replication->start(); failed.has_value()) {
        error = std::move(failed);
        return nullptr;
    }
    return replication;
}

} // namespace luaug::replication
