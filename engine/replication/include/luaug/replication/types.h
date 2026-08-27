// The replication seam's vocabulary (ADR 0069, ADR 0070).
//
// **POD only, and no transport type reaches this header.** `IReplication` is
// what `app` holds and what `scene` learns one fact from, and neither may see an
// ENet handle for the reason R17 gives about Jolt: a backend type in a public
// header is a backend everything above it is compiled against.
#pragma once

#include "luaug/core/id.h"
#include "luaug/core/types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace luaug::replication {

using core::u16;
using core::u32;
using core::u64;
using core::u8;
using core::usize;

// **The four postures, decided at process start from arguments and never
// changed** (ADR 0070). A script reads this and branches on it as a gameplay
// question -- "do I decide this" -- rather than as a configuration one.
//
// `Solo` is the default and its answers are the solo truth: authority true, one
// player, no peers. That is what makes `if NetworkService.Authority then` a
// branch that is present AND TAKEN in a game nobody networked.
enum class Topology : u8
{
    Solo = 0,
    // Client and server in one process. Authority true.
    Host = 1,
    // Headless, no client of its own. Authority true.
    Dedicated = 2,
    // A client of somebody else's authority. Authority false.
    Replica = 3,
};

[[nodiscard]] constexpr bool hasAuthority(Topology topology) noexcept
{
    return topology != Topology::Replica;
}

// A peer's identity, which is not an `InstanceId` and must not be confused with
// one.
//
// **Ids are never reused within a session.** A reconnecting player is a new
// peer with a new id; whether the GAME treats them as the same person is the
// game's question and it needs an account, which is the backend's business
// rather than the engine's.
struct PeerId
{
    u32 value = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const PeerId& other) const noexcept { return value == other.value; }
};

// **A network id, and never a pointer and never a path.**
//
// A path is a string that goes stale the moment anything is renamed and is
// O(depth) to resolve on a replica that may not hold the ancestors -- which it
// often will not, because interest management gives it a subset by design.
struct NetId
{
    u32 value = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const NetId& other) const noexcept { return value == other.value; }
};

// What `app` hands the module at startup, from arguments and from nowhere else
// (ADR 0070).
struct Config
{
    Topology topology = Topology::Solo;
    // Where to listen, for `Host` and `Dedicated`. Ignored otherwise.
    u16 port = 0;
    // Where to dial, for `Replica`. Ignored otherwise.
    std::string address;
    // How many peers an authority accepts.
    //
    // **Validated against the transport's own cap rather than trusted.** ENet
    // refuses more than 4,095 and returns null from `enet_host_create`, which is
    // a startup failure that reads as "networking is broken" rather than as "you
    // asked for too many".
    u32 maxPeers = 32;
    // Snapshots per second. A COUNT of ticks between sends rather than a
    // millisecond interval, so it is a function of the simulation rather than of
    // the wall clock (R10).
    u32 ticksPerSnapshot = 2;
};

// What a script can see, and every field is a `HostFact`: it describes the
// machine and the build rather than the world, so it is out of the world hash.
struct Status
{
    Topology topology = Topology::Solo;
    bool authority = true;
    // The authority's tick, which on a replica is the last one it applied and on
    // an authority is its own.
    u64 serverTick = 0;
    // Connected peers, not counting this process.
    u32 peerCount = 0;
    // Round trip in milliseconds, on a replica. Zero elsewhere.
    u32 pingMs = 0;
};

// One tick's worth of what the module did, for the overlay and for a test that
// wants to assert a send happened rather than infer it.
struct Stats
{
    u64 snapshotsSent = 0;
    u64 snapshotsReceived = 0;
    u64 bytesSent = 0;
    u64 bytesReceived = 0;
    // Instances that entered and left interest this tick, summed over peers.
    u32 spawned = 0;
    u32 despawned = 0;
};

} // namespace luaug::replication
