// The two ends of a replicated world, over any transport (ADR 0069).
//
// **The model is the one every shipped action game converges on**: the
// authority keeps a short history of what the world looked like at each send,
// each replica acknowledges the newest state it has fully reconstructed, and
// every snapshot is a diff against the state that peer last acknowledged. A
// lost snapshot costs nothing but bandwidth -- the next one is diffed against
// the same baseline and carries everything the lost one did -- and a replica can
// never apply a diff against a state it does not hold, because it only ever
// acknowledges states it does.
//
// **Spawns and despawns are reliable; snapshots are not.** An instance entering
// or leaving is a fact that cannot be superseded, so it travels on the control
// channel. Its fields travel in the snapshot like everybody else's: an instance
// the baseline does not have is sent whole, which is the one decoding path for
// "new" and "changed" rather than two.
//
// **Checked, and honestly.** Each snapshot carries a checksum of the state it
// describes; the replica reconstructs, compares, and only acknowledges a match.
// That catches an apply bug -- a decoder and an encoder disagreeing -- and
// nothing else: it is not a comparison of two worlds, and a replica's world is
// a subset of the authority's by design (ADR 0069, decision 8).
//
// Both classes are driven from outside, one call per tick each way, and hold no
// thread and no clock: what a peer receives is a function of the calls made and
// the transport's delivery, and with the memory transport that is a function of
// the operation sequence alone (R10).
#pragma once

#include "luaug/core/id.h"
#include "luaug/core/name_atom.h"
#include "luaug/net/transport.h"
#include "luaug/replication/extract.h"
#include "luaug/replication/types.h"

#include <deque>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace luaug::scene {
class World;
}

namespace luaug::replication {

// The root of what is replicated -- the `Workspace` in a game, anything in a
// test. Both ends agree on it without saying so, which is what lets a replica
// mount the authority's world under its own.
inline constexpr NetId RootNetId{1};

// How many sent states the authority remembers, and how many received states a
// replica does. A peer whose acknowledgement is older than this is sent the
// whole world again, which is correct and expensive -- and at the default
// thirty snapshots a second it means a peer that has not acknowledged anything
// for two seconds, which is a peer about to time out anyway.
inline constexpr usize StateHistory = 64;

// One instance as the wire sees it: its network id, which of the schema's
// classes describes it, and its fields in schema order. Values are in the
// AUTHORITY'S terms -- its name atoms, its network ids -- on both ends.
struct EntityState
{
    NetId id;
    u8 schema = 0;
    FieldSet fields;
};

// A world at one tick, entities sorted by network id.
struct WorldState
{
    u64 tick = 0;
    std::vector<EntityState> entities;
};

// What a snapshot carries so a replica can prove it reconstructed the state the
// authority meant. FNV-1a over ids, schema indices and field bytes: a checksum,
// not a signature -- it is there to catch our own bugs.
[[nodiscard]] u64 checksumOf(const WorldState& state) noexcept;

class AuthoritySession
{
public:
    explicit AuthoritySession(net::ITransport& transport) noexcept : m_transport(transport) {}

    // Handshakes, acknowledgements and departures. Touches no world.
    void receive();

    // Captures `root`'s subtree as it stands at `tick` and sends every
    // welcomed peer what it lacks: spawns, despawns, then the snapshot.
    void send(const scene::World& world, core::InstanceId root, u64 tick);

    [[nodiscard]] Stats stats() const noexcept { return m_stats; }
    [[nodiscard]] u32 peerCount() const noexcept;
    // The network id an instance travels under, or an invalid id when it has
    // never been captured. For tests and for `Player` mapping.
    [[nodiscard]] NetId netIdOf(core::InstanceId id) const noexcept;

private:
    struct Peer
    {
        net::PeerId id;
        bool welcomed = false;
        // The newest state this peer has proved it holds. Zero: none yet.
        u64 acked = 0;
        // Network ids this peer has been told exist, sorted.
        std::vector<u32> known;
    };

    // The subtree as it stands, and the class name each new id is spawned as.
    void capture(const scene::World& world, core::InstanceId root, u64 tick);
    void sendTo(Peer& peer, const WorldState& current);
    [[nodiscard]] const WorldState* historyAt(u64 tick) const noexcept;
    [[nodiscard]] Peer* peerFor(net::PeerId id) noexcept;

    net::ITransport& m_transport;
    std::vector<Peer> m_peers;
    // Keyed by the instance's packed id, ordered so a capture's retirement pass
    // walks in id order (R10). A network id is never reused: an instance that
    // leaves and a new one in its slot are two ids.
    std::map<u64, u32> m_netIds;
    u32 m_nextNetId = RootNetId.value + 1;
    // What each id is spawned as, by the authority's class name atom.
    std::map<u32, core::NameAtom> m_classNames;
    std::deque<std::shared_ptr<const WorldState>> m_history;
    // The world's atom table, for the strings a message carries. Captured by
    // `send`, which is the only caller that can need it.
    const scene::World* m_world = nullptr;
    u64 m_tick = 0;
    Stats m_stats;
};

class ReplicaSession
{
public:
    // `authority` is the peer `connect` returned. The handshake starts when the
    // transport reports the connection, not before.
    ReplicaSession(net::ITransport& transport, net::PeerId authority) noexcept
        : m_transport(transport), m_authority(authority)
    {}

    // Applies everything that arrived, in arrival order, under `root`, and
    // acknowledges what it reconstructed.
    void receive(scene::World& world, core::InstanceId root);

    [[nodiscard]] bool welcomed() const noexcept { return m_welcomed; }
    [[nodiscard]] bool connected() const noexcept { return m_connected; }
    // The newest state applied to the world. Zero before the first.
    [[nodiscard]] u64 appliedTick() const noexcept { return m_applied; }
    // This replica's player number, as the authority's welcome named it.
    [[nodiscard]] u32 playerId() const noexcept { return m_playerId; }
    [[nodiscard]] core::InstanceId localOf(NetId id) const noexcept;
    [[nodiscard]] Stats stats() const noexcept { return m_stats; }
    // Snapshots refused because the reconstruction did not match what the
    // authority described. Zero in a correct build; a test asserts it.
    [[nodiscard]] u64 checksumFailures() const noexcept { return m_checksumFailures; }

private:
    void onSnapshot(scene::World& world, core::InstanceId root, std::span<const u8> bytes);
    void onSpawn(scene::World& world, std::span<const u8> bytes);
    void onDespawn(scene::World& world, std::span<const u8> bytes);
    void applyToWorld(scene::World& world, core::InstanceId root, const WorldState& state);
    [[nodiscard]] const WorldState* stateAt(u64 tick) const noexcept;

    net::ITransport& m_transport;
    net::PeerId m_authority;
    bool m_connected = false;
    bool m_welcomed = false;
    u32 m_playerId = 0;
    u64 m_applied = 0;
    // The authority's name atoms, as this world's.
    std::map<u32, core::NameAtom> m_names;
    // Network id to local instance, for everything spawned and not despawned.
    std::map<u32, core::InstanceId> m_locals;
    // What the world was last given, per id, in the authority's terms -- so an
    // apply writes only what changed.
    std::map<u32, FieldSet> m_written;
    // Ids that have left. Never reused, so this only grows, by one id per
    // despawn: a filter a reconstructed state must pass.
    std::set<u32> m_departed;
    std::deque<std::shared_ptr<const WorldState>> m_states;
    u64 m_checksumFailures = 0;
    Stats m_stats;
};

} // namespace luaug::replication
