// The in-process transport and its lossy decorator (ADR 0069's harness).
//
// **These are what make a replication test a function of the operation
// sequence**, so what they promise is tested as a promise: delivery at the next
// poll, in order, and loss and reordering that come from the seed and from
// nothing else.
#include "luaug/core/i18n.h"
#include "luaug/net/memory_transport.h"

#include <doctest/doctest.h>
#include <vector>

using namespace luaug;
using namespace luaug::net;

namespace {

void seedCatalog()
{
    const auto result = core::engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
}

// A server and a client on one network, connected, with both connect events
// drained. `lossy` wraps the CLIENT'S sends.
struct Pair
{
    std::shared_ptr<MemoryNetwork> network = createMemoryNetwork();
    std::unique_ptr<ITransport> server = createMemoryTransport(network);
    std::unique_ptr<ITransport> client;
    PeerId toServer;
    PeerId toClient;

    explicit Pair(const LossConfig* lossy = nullptr)
    {
        client = lossy != nullptr ? createLossyTransport(createMemoryTransport(network), *lossy)
                                  : createMemoryTransport(network);
        REQUIRE_FALSE(server->open(TransportConfig{.port = 7000, .maxPeers = 4, .channels = 3}).has_value());
        REQUIRE_FALSE(client->open(TransportConfig{.port = 0, .maxPeers = 1, .channels = 3}).has_value());
        REQUIRE_FALSE(client->connect("anything", 7000, toServer).has_value());

        std::vector<TransportEvent> events;
        REQUIRE_FALSE(server->poll(events, 0).has_value());
        REQUIRE(events.size() == 1);
        CHECK(events[0].kind == TransportEvent::Kind::Connected);
        toClient = events[0].peer;
        events.clear();
        REQUIRE_FALSE(client->poll(events, 0).has_value());
        REQUIRE(events.size() == 1);
        CHECK(events[0].kind == TransportEvent::Kind::Connected);
    }

    // Sends `count` one-byte messages numbered from zero and answers the
    // numbers the server received, in the order it received them.
    std::vector<u8> sendNumbered(u8 count, Delivery delivery)
    {
        for (u8 at = 0; at < count; ++at) {
            const u8 payload[] = {at};
            REQUIRE_FALSE(client->send(toServer, payload, delivery, 1).has_value());
        }
        (void)client->poll(scratch, 0);
        std::vector<TransportEvent> events;
        REQUIRE_FALSE(server->poll(events, 0).has_value());
        std::vector<u8> received;
        for (const TransportEvent& event : events) {
            if (event.kind == TransportEvent::Kind::Message && event.payload.size() == 1)
                received.push_back(event.payload[0]);
        }
        return received;
    }

    std::vector<TransportEvent> scratch;
};

} // namespace

TEST_CASE("a memory transport delivers at the next poll, in order")
{
    seedCatalog();
    Pair pair;
    const std::vector<u8> received = pair.sendNumbered(5, Delivery::Reliable);
    CHECK(received == std::vector<u8>{0, 1, 2, 3, 4});
}

TEST_CASE("a second listener on one port is refused, and a connect to nobody fails")
{
    seedCatalog();
    Pair pair;
    std::unique_ptr<ITransport> rival = createMemoryTransport(pair.network);
    CHECK(rival->open(TransportConfig{.port = 7000}).has_value());

    std::unique_ptr<ITransport> lost = createMemoryTransport(pair.network);
    REQUIRE_FALSE(lost->open(TransportConfig{}).has_value());
    PeerId peer;
    CHECK(lost->connect("nowhere", 9999, peer).has_value());
}

TEST_CASE("closing one end disconnects the other")
{
    seedCatalog();
    Pair pair;
    pair.client.reset();
    std::vector<TransportEvent> events;
    REQUIRE_FALSE(pair.server->poll(events, 0).has_value());
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == TransportEvent::Kind::Disconnected);
    CHECK(pair.server->peerCount() == 0);
}

TEST_CASE("loss and reordering come from the seed, never touch reliable traffic, and repeat exactly")
{
    seedCatalog();
    const LossConfig config{.seed = 42, .dropPerMille = 200, .reorderPerMille = 200};

    Pair first(&config);
    const std::vector<u8> reliable = first.sendNumbered(50, Delivery::Reliable);
    CHECK(reliable.size() == 50);

    const std::vector<u8> unreliable = first.sendNumbered(100, Delivery::Unreliable);
    // Some lost and some out of order -- at twenty percent each, over a hundred
    // messages, both are certain for this seed and checked rather than assumed.
    CHECK(unreliable.size() < 100);
    CHECK(unreliable.size() > 50);
    bool reordered = false;
    for (usize at = 1; at < unreliable.size(); ++at)
        reordered = reordered || unreliable[at] < unreliable[at - 1];
    CHECK(reordered);

    // The same seed and the same sends: the same misfortune, message for message.
    Pair second(&config);
    (void)second.sendNumbered(50, Delivery::Reliable);
    CHECK(second.sendNumbered(100, Delivery::Unreliable) == unreliable);
}
