// Two transports that are not a network (ADR 0069's test harness).
//
// **Why this exists beside ENet rather than instead of it.** ENet's loopback is
// a real socket, so what it delivers and when is decided by the operating
// system's scheduler -- which is exactly the thing a deterministic gate cannot
// depend on. Replication has to be tested as a function of the operation
// sequence (R10): the same sends, the same polls, the same bytes out. These
// deliver what was sent at the next `poll`, in order, every time, and the lossy
// decorator below adds the misfortune a network adds -- from a seed, never from
// a clock.
//
// **Single-threaded by contract.** Every endpoint on one `MemoryNetwork` is
// driven from one thread, which is how a test and the two-worlds gate both use
// it. A transport that needed locks to be correct would be testing the locks.
#pragma once

#include "luaug/core/types.h"
#include "luaug/net/transport.h"

#include <memory>

namespace luaug::net {

// The switchboard two or more memory transports share. A port opened on one
// endpoint is what a `connect` on another reaches. Held by `shared_ptr` so an
// endpoint outliving the test that made the network cannot dangle.
class MemoryNetwork;

[[nodiscard]] std::shared_ptr<MemoryNetwork> createMemoryNetwork();

// One endpoint. `open` with a port listens on the network; `connect` names the
// host as anything (it is ignored -- there is one network) and the port as the
// listener's.
[[nodiscard]] std::unique_ptr<ITransport> createMemoryTransport(std::shared_ptr<MemoryNetwork> network);

// What the lossy decorator does to messages that did not ask for reliability.
//
// **Per mille rather than a float**, so a configuration is an exact value that
// prints the same on every machine, and a test that says "ten percent" means
// one hundred out of a thousand rather than whatever 0.1 rounds to.
struct LossConfig
{
    core::u64 seed = 1;
    // Chance an unreliable message is dropped.
    core::u32 dropPerMille = 0;
    // Chance an unreliable message is held back and delivered after the next
    // one -- which is what reordering looks like from the receiving end.
    core::u32 reorderPerMille = 0;
};

// Wraps a transport so its unreliable traffic is lost and reordered, from a
// seed. **Reliable messages are never touched**: a reliable channel that lost
// messages would be testing a transport that does not exist.
[[nodiscard]] std::unique_ptr<ITransport> createLossyTransport(std::unique_ptr<ITransport> inner,
                                                               const LossConfig& config);

} // namespace luaug::net
