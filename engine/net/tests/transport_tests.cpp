// The M7 gate's loopback echo, at the transport seam.
//
// Two hosts in ONE process on 127.0.0.1 rather than two processes, and that is
// the whole reason a loopback echo is a sane gate: it needs no fixture server,
// no port allocator and no cleanup that can fail, and it still exercises the
// real socket path -- ENet's handshake, its channels, its reliability layer and
// its disconnect. What it does not exercise is a network, and a transport bug
// that only appears with loss or reordering is invisible here. That is stated
// rather than implied, because a green loopback test reads like more assurance
// than it is.
#include "luaug/core/i18n.h"
#include "luaug/net/transport.h"

#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace luaug;
using namespace luaug::net;

namespace {

void seedCatalog()
{
    const auto result = core::engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
}

// A port nobody standardised on, high enough to need no privilege. Fixed rather
// than drawn at random: a random port makes a failure unreproducible, and two
// concurrent runs of this suite on one machine is not a case that arises -- CTest
// runs one binary at a time and this is the only test in it that binds.
constexpr u16 EchoPort = 47921;

[[nodiscard]] std::vector<u8> bytesOf(std::string_view text)
{
    const auto* const data = reinterpret_cast<const u8*>(text.data());
    return {data, data + text.size()};
}

[[nodiscard]] std::string textOf(const std::vector<u8>& bytes)
{
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// Pumps both ends until `predicate` holds or the budget is spent.
//
// BOTH ends, every round, and that is the thing a first attempt gets wrong: a
// handshake needs each side to answer the other, so pumping only the side you
// are waiting on deadlocks with both peers holding a half-finished connection.
//
// Bounded by ITERATIONS rather than by a clock. A wall-clock deadline makes a
// test that passes on a fast machine and fails on a loaded CI runner, which is
// the same class of flakiness `--rhi=null` exists to keep out of the soak gate.
template <typename Predicate>
bool pumpUntil(ITransport& a, ITransport& b, std::vector<TransportEvent>& eventsA, std::vector<TransportEvent>& eventsB,
               Predicate predicate, int rounds = 400)
{
    for (int i = 0; i < rounds; ++i) {
        if (predicate()) {
            return true;
        }
        REQUIRE_FALSE(a.poll(eventsA, 5).has_value());
        REQUIRE_FALSE(b.poll(eventsB, 5).has_value());
    }
    return predicate();
}

[[nodiscard]] bool has(const std::vector<TransportEvent>& events, TransportEvent::Kind kind)
{
    for (const TransportEvent& event : events) {
        if (event.kind == kind) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("a message sent into the loopback comes back out of it")
{
    seedCatalog();

    auto server = createEnetTransport();
    auto client = createEnetTransport();
    REQUIRE(server != nullptr);

    REQUIRE_FALSE(server->open({.port = EchoPort, .maxPeers = 4, .channels = 2}).has_value());
    REQUIRE_FALSE(client->open({.port = 0, .maxPeers = 4, .channels = 2}).has_value());

    PeerId toServer;
    REQUIRE_FALSE(client->connect("127.0.0.1", EchoPort, toServer).has_value());
    CHECK(toServer.valid());

    std::vector<TransportEvent> serverEvents;
    std::vector<TransportEvent> clientEvents;

    REQUIRE(pumpUntil(*server, *client, serverEvents, clientEvents, [&] {
        return has(serverEvents, TransportEvent::Kind::Connected) && has(clientEvents, TransportEvent::Kind::Connected);
    }));

    // The server learns the client's id from the event, not from anything it
    // was told -- which is the only way a listening host can learn it.
    PeerId toClient;
    for (const TransportEvent& event : serverEvents) {
        if (event.kind == TransportEvent::Kind::Connected) {
            toClient = event.peer;
        }
    }
    REQUIRE(toClient.valid());

    const std::string sent = "the world streams";
    REQUIRE_FALSE(client->send(toServer, bytesOf(sent), Delivery::Reliable, 0).has_value());

    serverEvents.clear();
    REQUIRE(pumpUntil(*server, *client, serverEvents, clientEvents,
                      [&] { return has(serverEvents, TransportEvent::Kind::Message); }));

    std::string received;
    for (const TransportEvent& event : serverEvents) {
        if (event.kind == TransportEvent::Kind::Message) {
            received = textOf(event.payload);
            CHECK(event.peer == toClient);
            CHECK(event.channel == 0);
        }
    }
    CHECK(received == sent);

    // The ECHO half. A test that only proved one direction would pass with a
    // transport whose server could not answer.
    clientEvents.clear();
    REQUIRE_FALSE(server->send(toClient, bytesOf(received), Delivery::Reliable, 0).has_value());
    REQUIRE(pumpUntil(*server, *client, serverEvents, clientEvents,
                      [&] { return has(clientEvents, TransportEvent::Kind::Message); }));

    for (const TransportEvent& event : clientEvents) {
        if (event.kind == TransportEvent::Kind::Message) {
            CHECK(textOf(event.payload) == sent);
        }
    }

    client->disconnect(toServer);
    serverEvents.clear();
    CHECK(pumpUntil(*server, *client, serverEvents, clientEvents,
                    [&] { return has(serverEvents, TransportEvent::Kind::Disconnected); }));
    CHECK(server->peerCount() == 0);
}

TEST_CASE("every delivery mode arrives over a link that drops nothing")
{
    seedCatalog();

    // Loopback loses nothing, so this cannot distinguish the three modes by
    // their guarantees -- and it does not claim to. What it pins is that all
    // three are WIRED.
    //
    // **It cannot see the mapping either**, which D150 is the proof of: the
    // flags are asserted where they are written, in `enet_transport.cpp`, by
    // `static_assert`. ENet is linked PRIVATE so this file cannot name its
    // constants, and a packet that wrongly became reliable arrives anyway --
    // same bytes, same order -- so there was nothing here to observe.
    auto server = createEnetTransport();
    auto client = createEnetTransport();
    REQUIRE_FALSE(server->open({.port = EchoPort, .maxPeers = 4, .channels = 2}).has_value());
    REQUIRE_FALSE(client->open({.port = 0, .maxPeers = 4, .channels = 2}).has_value());

    PeerId toServer;
    REQUIRE_FALSE(client->connect("127.0.0.1", EchoPort, toServer).has_value());

    std::vector<TransportEvent> serverEvents;
    std::vector<TransportEvent> clientEvents;
    REQUIRE(pumpUntil(*server, *client, serverEvents, clientEvents,
                      [&] { return has(serverEvents, TransportEvent::Kind::Connected); }));

    for (const Delivery delivery : {Delivery::Reliable, Delivery::Unreliable, Delivery::UnreliableSequenced}) {
        REQUIRE_FALSE(client->send(toServer, bytesOf("ping"), delivery, 1).has_value());
    }

    serverEvents.clear();
    int messages = 0;
    (void)pumpUntil(*server, *client, serverEvents, clientEvents, [&] {
        messages = 0;
        for (const TransportEvent& event : serverEvents) {
            if (event.kind == TransportEvent::Kind::Message) {
                messages += 1;
                CHECK(event.channel == 1);
            }
        }
        return messages == 3;
    });
    CHECK(messages == 3);
}

TEST_CASE("the transport refuses what it cannot do instead of pretending")
{
    seedCatalog();

    auto transport = createEnetTransport();

    // Every one of these is a keyed error rather than a crash or a silent
    // no-op, which is what an interface a game script will eventually reach
    // through has to be.
    PeerId unused;
    CHECK(transport->connect("127.0.0.1", EchoPort, unused).has_value());
    CHECK(transport->send(PeerId{1}, bytesOf("x"), Delivery::Reliable, 0).has_value());

    std::vector<TransportEvent> events;
    CHECK(transport->poll(events, 0).has_value());

    REQUIRE_FALSE(transport->open({.port = 0, .maxPeers = 2, .channels = 2}).has_value());
    CHECK(transport->send(PeerId{999}, bytesOf("x"), Delivery::Reliable, 0).has_value());

    PeerId peer;
    REQUIRE_FALSE(transport->connect("127.0.0.1", EchoPort, peer).has_value());
    // Channel 2 on a two-channel host: the last index is one, and an
    // out-of-range channel handed to ENet is undefined rather than an error.
    CHECK(transport->send(peer, bytesOf("x"), Delivery::Reliable, 2).has_value());

    CHECK(transport->connect("this-host-does-not-resolve.invalid", EchoPort, unused).has_value());
}

TEST_CASE("a payload larger than one MTU arrives whole, on every delivery mode")
{
    seedCatalog();

    // **Multi-fragment reassembly was untested at every size that could
    // fragment.** Every other case in this file sends four bytes, and ENet
    // splits at `mtu - sizeof(ENetProtocolHeader) - sizeof(ENetProtocolSendFragment)`
    // -- about 1364 at its default MTU of 1392 -- so the whole fragment path,
    // on all three modes, had never run.
    //
    // That is also where D150 lived: an unreliable payload over that threshold
    // silently became reliable, acknowledged and head-of-line blocking, because
    // `enet_peer_send` checks for fragmentation BEFORE it dispatches on the
    // unsequenced flag. This case cannot see that -- a wrongly-reliable packet
    // arrives just the same -- so the flags are asserted at the mapping instead.
    // What this case CAN see is the half a mapping assertion cannot: that the
    // bytes come back, all of them, in the right order, after a round trip
    // through several fragments.
    auto server = createEnetTransport();
    auto client = createEnetTransport();
    REQUIRE_FALSE(server->open({.port = EchoPort, .maxPeers = 4, .channels = 2}).has_value());
    REQUIRE_FALSE(client->open({.port = 0, .maxPeers = 4, .channels = 2}).has_value());

    PeerId toServer;
    REQUIRE_FALSE(client->connect("127.0.0.1", EchoPort, toServer).has_value());

    std::vector<TransportEvent> serverEvents;
    std::vector<TransportEvent> clientEvents;
    REQUIRE(pumpUntil(*server, *client, serverEvents, clientEvents,
                      [&] { return has(serverEvents, TransportEvent::Kind::Connected); }));

    // Four fragments' worth at the default MTU, and NOT a repeated byte: a
    // reassembly that dropped, duplicated or reordered a fragment would pass
    // against a buffer of one repeated value. The pattern is a cheap
    // position-dependent one so that every offset is distinguishable.
    constexpr core::usize kPayloadBytes = 5000;
    std::vector<u8> payload(kPayloadBytes);
    for (core::usize at = 0; at < payload.size(); ++at)
        payload[at] = static_cast<u8>((at * 31u + (at >> 8u)) & 0xFFu);

    for (const Delivery delivery : {Delivery::Reliable, Delivery::Unreliable, Delivery::UnreliableSequenced}) {
        CAPTURE(static_cast<int>(delivery));

        REQUIRE_FALSE(client->send(toServer, payload, delivery, 1).has_value());

        serverEvents.clear();
        std::vector<u8> received;
        (void)pumpUntil(*server, *client, serverEvents, clientEvents, [&] {
            for (const TransportEvent& event : serverEvents) {
                if (event.kind == TransportEvent::Kind::Message && event.payload.size() == kPayloadBytes) {
                    received = event.payload;
                    return true;
                }
            }
            return false;
        });

        REQUIRE(received.size() == kPayloadBytes);
        CHECK(received == payload);
    }
}
