// The replication seam (ADR 0069).
//
// **Null is a real state and not an error**, which is the pattern the animation
// host and the physics mirror both state about themselves. A solo game has no
// replication object at all: `app` holds a `unique_ptr` that is null, calls
// nothing, and pays one branch a tick. That is what makes
// `LUAUG_ENABLE_REPLICATION=OFF` a build with no socket code in it rather than a
// build with socket code nobody reaches (ADR 0070, clause 3).
#pragma once

#include "luaug/replication/types.h"

#include <memory>
#include <optional>

namespace luaug::core {
struct EngineError;
}

namespace luaug::scene {
class World;
}

namespace luaug::replication {

// What the authority and the replica both are.
//
// **Two calls a tick and they are not symmetric.** `receive` runs before the
// simulation, because what arrived is input to the tick that follows it, and
// `send` runs after, because what is sent is the tick's result. Reversing them
// costs a tick of latency in each direction and looks like nothing at all in a
// loopback test.
class IReplication
{
public:
    virtual ~IReplication() = default;

    // Applies everything that arrived since the last call.
    //
    // On an authority that is intent; on a replica it is spawns, despawns and
    // snapshots. **The world is mutated here and nowhere else in this class**,
    // which is what lets `send` take a const world.
    virtual void receive(scene::World& world) = 0;

    // Extracts, diffs and sends this tick's state.
    //
    // `tick` is the simulation's own count and never a clock: what a peer is
    // told is a function of the operation sequence (R10), and a send rate
    // measured in milliseconds would make the bytes on the wire depend on how
    // fast the machine was.
    virtual void send(const scene::World& world, u64 tick) = 0;

    [[nodiscard]] virtual Status status() const = 0;
    [[nodiscard]] virtual Stats stats() const = 0;

    // Closes every connection and stops listening. Idempotent.
    virtual void shutdown() = 0;
};

// Builds one, or says why not.
//
// **This is the single caller of anything that binds a socket** (ADR 0070,
// clause 2), and `app` reaches it from argument parsing. There is no other path:
// no service method, no property, no script. The rule is checkable rather than
// promised precisely because this function is the only door.
//
// Returns null with no error for `Topology::Solo` -- a solo game is not a
// failure to network, it is a game that is not networked.
[[nodiscard]] std::unique_ptr<IReplication> createReplication(const Config& config,
                                                              std::optional<core::EngineError>& error);

} // namespace luaug::replication
