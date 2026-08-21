// The transport seam (ADR 0012, architecture.md §2 `net_api`).
//
// **This interface is the deliverable; the implementation behind it is not.**
// v1 ships primitives and a loopback echo -- replication is post-v1 (R15) --
// so what M7 owes is a shape future replication can be built on without the
// engine being rebuilt around it. ADR 0012 names GameNetworkingSockets as the
// eventual default with ENet as the LAN option; the manifest row for GNS says
// in full why only ENet is vendored, and the short version is that GNS brings
// protobuf and OpenSSL for a seam with no v1 caller.
//
// **No ENet type appears here** (R17). A caller that can see an `ENetPeer`
// writes code that only works with one transport, which defeats the seam on the
// first line of the first use.
//
// What is deliberately NOT here, so that a later reader does not mistake it for
// an oversight: no replication, no entity ownership, no serialisation, no
// prediction, no clock synchronisation. This moves BYTES between two endpoints,
// with a reliability flag per message. Everything above that is a design nobody
// has approved yet, and a half-built version of it here would be worse than
// none.
#pragma once

#include "luaug/core/error.h"
#include "luaug/core/types.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace luaug::net {

using core::u16;
using core::u32;
using core::u8;
using core::usize;

// Which guarantee a message wants, chosen per message rather than per channel.
//
// Per MESSAGE because the mix is the normal case: a position update wants to be
// dropped if it is late and a chat line does not, and they belong to the same
// conversation. A per-connection setting forces two connections for what is one
// relationship.
enum class Delivery : u8
{
    // Arrives, in order, or the connection fails. The expensive one.
    Reliable,

    // May be dropped and may arrive out of order. What a stream of positions
    // wants: a late position is worse than a missing one.
    Unreliable,

    // May be dropped; never delivered out of order. A dropped message is simply
    // superseded by the next. This is what most state replication actually
    // wants, and it is the option people reach for last because it has no name
    // in the sockets API everyone learned first.
    UnreliableSequenced,
};

// A stable handle to a connected endpoint. An integer rather than a pointer
// because a peer can vanish between two lines of caller code, and a stale
// integer is a lookup that fails while a stale pointer is undefined behaviour.
struct PeerId
{
    u32 value = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }
    friend constexpr bool operator==(PeerId, PeerId) noexcept = default;
};

struct TransportEvent
{
    enum class Kind : u8
    {
        None,
        Connected,
        Disconnected,
        Message,
    };

    Kind kind = Kind::None;
    PeerId peer;

    // Owned by the event and only valid until the next `poll`. Copied by a
    // caller that wants it for longer, which is stated rather than left to be
    // discovered: the alternative is an allocation per datagram.
    std::vector<u8> payload;

    // The channel the message arrived on. Zero for connect and disconnect.
    u8 channel = 0;
};

struct TransportConfig
{
    // Zero binds nothing and makes this a client. A non-zero port listens.
    //
    // One type for both roles, deliberately: a peer-to-peer session is two
    // hosts that both listen, and a client/server split baked into the type
    // would make that unrepresentable in a seam whose whole job is to outlive
    // the topology chosen above it.
    u16 port = 0;

    // How many peers this host will hold at once. A hard limit rather than a
    // hint: the memory is allocated up front, which is the property that makes
    // a transport's footprint knowable.
    usize maxPeers = 32;

    // Independent ordered streams. Two channels means a reliable chat message
    // cannot block a reliable state message behind it -- head-of-line blocking
    // is per channel, and having exactly one is how a transport ends up with a
    // stall nobody can explain.
    u8 channels = 2;
};

// **Poll-driven, never callback-driven**, and this is the R10 decision in the
// interface rather than a note about it. A callback fires whenever a packet
// arrives, which is a wall-clock-driven entry into game code and therefore a
// source of divergence between two runs of the same simulation. `poll` is
// called at a point the frame loop chooses, and everything it returns is
// applied at that point.
class ITransport
{
public:
    virtual ~ITransport() = default;

    // Binds if `config.port` is non-zero. An unbound host can still `connect`.
    [[nodiscard]] virtual std::optional<core::EngineError> open(const TransportConfig& config) = 0;
    virtual void close() = 0;

    // Begins a connection. The peer is NOT usable until a `Connected` event for
    // it comes out of `poll` -- returning a `PeerId` that is not yet connected
    // is deliberate, so a caller can key its own bookkeeping before the
    // handshake finishes rather than after.
    [[nodiscard]] virtual std::optional<core::EngineError> connect(std::string_view host, u16 port,
                                                                   PeerId& outPeer) = 0;
    virtual void disconnect(PeerId peer) = 0;

    [[nodiscard]] virtual std::optional<core::EngineError> send(PeerId peer, std::span<const u8> payload,
                                                                Delivery delivery, u8 channel) = 0;

    // Drains up to `timeoutMs` of waiting events into `out`, appending. Returns
    // with whatever it has when the time is spent; zero means "whatever is
    // ready right now".
    [[nodiscard]] virtual std::optional<core::EngineError> poll(std::vector<TransportEvent>& out, u32 timeoutMs) = 0;

    [[nodiscard]] virtual usize peerCount() const noexcept = 0;
};

// The one implementation (ADR 0012). Named as a factory rather than a class so
// that no header outside `net_enet` ever sees an ENet type.
//
// Its properties are worth stating where a caller will read them: UDP, optional
// reliability and sequencing, **no encryption and no authentication**. An ENet
// channel is a LAN or an otherwise trusted link. Anything else waits for the
// GameNetworkingSockets row in the manifest to be filled in.
[[nodiscard]] std::unique_ptr<ITransport> createEnetTransport();

} // namespace luaug::net
