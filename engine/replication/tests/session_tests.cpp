// Two worlds, one authority (ADR 0069): the sessions over the memory transport.
//
// **Every case here is a function of the operation sequence.** The memory
// transport delivers at the next poll and the lossy one misbehaves from a seed,
// so a failure reproduces exactly -- which is the property a replication bug
// most needs and a socket least provides.
#include "luaug/core/i18n.h"
#include "luaug/net/memory_transport.h"
#include "luaug/replication/replication.h"
#include "luaug/replication/session.h"
#include "luaug/scene/components.h"
#include "luaug/scene/world.h"

#include <doctest/doctest.h>
#include <string>

#include "../../scene/tests/scene_fixture.h"

using namespace luaug;
using namespace luaug::replication;

namespace {

constexpr core::u16 Port = 7100;

void seedCatalog()
{
    const auto result = core::engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
}

// One side: its own registry, its own atoms, its own world, and a folder that
// is the replicated root.
struct Side
{
    scene::testing::Fixture fixture;
    core::InstanceId root;

    Side() { root = fixture.folder("Root"); }

    [[nodiscard]] scene::World& world() { return fixture.world; }

    core::InstanceId part(std::string_view name, core::DVec3 at, core::InstanceId parent)
    {
        const core::InstanceId id = fixture.part(name);
        fixture.world.rigidBodies().add(id, scene::RigidBodyComponent{});
        fixture.world.parts().find(id)->cframe.position = at;
        REQUIRE_FALSE(fixture.world.setParent(id, parent).has_value());
        return id;
    }

    // The child of `parent` called `name`, or nothing.
    [[nodiscard]] core::InstanceId child(core::InstanceId parent, std::string_view name)
    {
        const core::NameAtom atom = fixture.atom(name);
        for (core::InstanceId at = fixture.world.firstChild(parent); at.valid(); at = fixture.world.nextSibling(at)) {
            if (fixture.world.name(at) == atom)
                return at;
        }
        return {};
    }
};

// An authority and one replica on a memory network. `loss` wraps the
// authority's transport, which is the direction snapshots travel.
struct Match
{
    std::shared_ptr<net::MemoryNetwork> network = net::createMemoryNetwork();
    std::unique_ptr<net::ITransport> serverTransport;
    std::unique_ptr<net::ITransport> clientTransport = net::createMemoryTransport(network);
    std::optional<AuthoritySession> authority;
    std::optional<ReplicaSession> replica;
    Side server;
    Side client;
    core::u64 tick = 0;

    explicit Match(const net::LossConfig* loss = nullptr)
    {
        seedCatalog();
        serverTransport = loss != nullptr ? net::createLossyTransport(net::createMemoryTransport(network), *loss)
                                          : net::createMemoryTransport(network);
        REQUIRE_FALSE(
            serverTransport->open(net::TransportConfig{.port = Port, .maxPeers = 4, .channels = 4}).has_value());
        REQUIRE_FALSE(clientTransport->open(net::TransportConfig{.port = 0, .maxPeers = 1, .channels = 4}).has_value());
        net::PeerId toServer;
        REQUIRE_FALSE(clientTransport->connect("memory", Port, toServer).has_value());
        authority.emplace(*serverTransport);
        replica.emplace(*clientTransport, toServer);
    }

    // One tick, in the order the frame runs it: what arrived, the tick, what
    // to send -- on both ends.
    void step()
    {
        tick += 1;
        authority->receive();
        authority->send(server.world(), server.root, tick);
        replica->receive(client.world(), client.root);
    }

    void run(int ticks)
    {
        for (int at = 0; at < ticks; ++at)
            step();
    }
};

} // namespace

TEST_CASE("a replica is welcomed, and holds the authority's instances, named, parented and placed")
{
    Match match;
    const core::InstanceId car = match.server.fixture.model("Car");
    REQUIRE_FALSE(match.server.world().setParent(car, match.server.root).has_value());
    (void)match.server.part("Wheel", {1.0, 2.0, 3.0}, car);
    (void)match.server.part("Body", {0.0, 5.0, 0.0}, car);

    match.run(4);
    CHECK(match.replica->welcomed());
    CHECK(match.authority->peerCount() == 1);
    CHECK(match.replica->checksumFailures() == 0);

    // **By name, in the replica's own atoms.** The two fixtures intern in
    // different orders, so a name that travelled as the authority's atom
    // number would come out as some other word.
    const core::InstanceId replicaCar = match.client.child(match.client.root, "Car");
    REQUIRE(replicaCar.valid());
    CHECK(match.client.world().classOf(replicaCar) == match.client.fixture.schema.modelClass);
    const core::InstanceId wheel = match.client.child(replicaCar, "Wheel");
    REQUIRE(wheel.valid());
    const scene::PartComponent* part = match.client.world().parts().find(wheel);
    REQUIRE(part != nullptr);
    CHECK(part->cframe.position.x == doctest::Approx(1.0));
    CHECK(part->cframe.position.z == doctest::Approx(3.0));
    CHECK(match.client.child(replicaCar, "Body").valid());
}

TEST_CASE("a moved part moves on the replica, and a quiet world sends almost nothing")
{
    Match match;
    const core::InstanceId crate = match.server.part("Crate", {0.0, 0.0, 0.0}, match.server.root);
    match.run(4);

    // Nothing changes: a snapshot is a header and no records.
    const core::u64 before = match.authority->stats().bytesSent;
    match.run(1);
    const core::u64 quiet = match.authority->stats().bytesSent - before;
    CHECK(quiet < 64);

    match.server.world().parts().find(crate)->cframe.position = core::DVec3{4.0, 0.0, -2.0};
    const core::u64 moving = match.authority->stats().bytesSent;
    match.run(2);
    // One field of one instance: a record header and a transform, not a world.
    CHECK(match.authority->stats().bytesSent - moving < 2 * 64 + 128);

    const core::InstanceId replicaCrate = match.client.child(match.client.root, "Crate");
    REQUIRE(replicaCrate.valid());
    CHECK(match.client.world().parts().find(replicaCrate)->cframe.position.x == doctest::Approx(4.0));
    CHECK(match.replica->checksumFailures() == 0);
}

TEST_CASE("a renamed, reparented and destroyed instance is all three on the replica")
{
    Match match;
    const core::InstanceId shelf = match.server.fixture.folder("Shelf");
    REQUIRE_FALSE(match.server.world().setParent(shelf, match.server.root).has_value());
    const core::InstanceId box = match.server.part("Box", {0.0, 0.0, 0.0}, match.server.root);
    const core::InstanceId doomed = match.server.part("Doomed", {0.0, 0.0, 0.0}, match.server.root);
    match.run(4);
    REQUIRE(match.client.child(match.client.root, "Doomed").valid());

    match.server.world().setName(box, match.server.fixture.atom("Parcel"));
    REQUIRE_FALSE(match.server.world().setParent(box, shelf).has_value());
    REQUIRE(match.server.world().destroy(doomed));
    match.server.world().retireDestroyed();
    match.run(3);
    match.client.world().retireDestroyed();

    CHECK_FALSE(match.client.child(match.client.root, "Doomed").valid());
    CHECK_FALSE(match.client.child(match.client.root, "Box").valid());
    const core::InstanceId replicaShelf = match.client.child(match.client.root, "Shelf");
    REQUIRE(replicaShelf.valid());
    CHECK(match.client.child(replicaShelf, "Parcel").valid());
    CHECK(match.replica->checksumFailures() == 0);
}

TEST_CASE("under loss and reordering the replica converges and never fails its checksum")
{
    // **The case `transport_tests.cpp` says is invisible without a decorator.**
    // A third of the snapshots lost and a fifth reordered: the replica must end
    // exactly where the authority is, having acknowledged only states it held.
    const net::LossConfig loss{.seed = 7, .dropPerMille = 330, .reorderPerMille = 200};
    Match match(&loss);
    const core::InstanceId mover = match.server.part("Mover", {0.0, 0.0, 0.0}, match.server.root);
    for (int at = 0; at < 8; ++at)
        (void)match.server.part("Still" + std::to_string(at), {static_cast<double>(at), 1.0, 0.0}, match.server.root);

    for (int at = 0; at < 200; ++at) {
        match.server.world().parts().find(mover)->cframe.position = core::DVec3{static_cast<double>(at), 0.0, 0.0};
        match.step();
    }
    // Held still long enough for a snapshot to get through.
    match.run(30);

    CHECK(match.replica->checksumFailures() == 0);
    const core::InstanceId replicaMover = match.client.child(match.client.root, "Mover");
    REQUIRE(replicaMover.valid());
    CHECK(match.client.world().parts().find(replicaMover)->cframe.position.x == doctest::Approx(199.0));
    CHECK(match.client.child(match.client.root, "Still7").valid());
    // Loss cost snapshots, not correctness: fewer arrived than were sent.
    CHECK(match.replica->stats().snapshotsReceived < match.authority->stats().snapshotsSent);
}

TEST_CASE("a peer speaking another protocol is refused before anything is parsed")
{
    seedCatalog();
    auto network = net::createMemoryNetwork();
    auto serverTransport = net::createMemoryTransport(network);
    auto rogue = net::createMemoryTransport(network);
    REQUIRE_FALSE(serverTransport->open(net::TransportConfig{.port = Port, .maxPeers = 4, .channels = 4}).has_value());
    REQUIRE_FALSE(rogue->open(net::TransportConfig{.port = 0, .maxPeers = 1, .channels = 4}).has_value());
    net::PeerId toServer;
    REQUIRE_FALSE(rogue->connect("memory", Port, toServer).has_value());

    AuthoritySession authority(*serverTransport);
    authority.receive();
    // A hello claiming protocol 999.
    const core::u8 hello[] = {1, 0xE7, 0x03, 0x00, 0x00};
    REQUIRE_FALSE(rogue->send(toServer, hello, net::Delivery::Reliable, 0).has_value());
    authority.receive();
    CHECK(authority.peerCount() == 0);
    CHECK(serverTransport->peerCount() == 0);
}

TEST_CASE("solo is no replication at all, and an impossible peer count is refused by name")
{
    seedCatalog();
    std::optional<core::EngineError> error;
    CHECK(createReplicationOver(net::createMemoryTransport(net::createMemoryNetwork()), Config{}, error) == nullptr);
    CHECK_FALSE(error.has_value());

    Config tooMany;
    tooMany.topology = Topology::Dedicated;
    tooMany.maxPeers = 100000;
    CHECK(createReplicationOver(net::createMemoryTransport(net::createMemoryNetwork()), tooMany, error) == nullptr);
    REQUIRE(error.has_value());
    CHECK(error->message.find("net.err.replication_peer_cap") != std::string::npos);
}

TEST_CASE("createReplicationOver drives both postures through the seam")
{
    seedCatalog();
    auto network = net::createMemoryNetwork();
    std::optional<core::EngineError> error;
    Config hostConfig;
    hostConfig.topology = Topology::Host;
    hostConfig.port = Port;
    auto host = createReplicationOver(net::createMemoryTransport(network), hostConfig, error);
    REQUIRE(host != nullptr);
    Config joinConfig;
    joinConfig.topology = Topology::Replica;
    joinConfig.port = Port;
    joinConfig.address = "memory";
    auto join = createReplicationOver(net::createMemoryTransport(network), joinConfig, error);
    REQUIRE(join != nullptr);

    Side server;
    Side client;
    (void)server.part("Beacon", {9.0, 0.0, 0.0}, server.root);
    for (core::u64 tick = 1; tick <= 8; ++tick) {
        host->receive(server.world(), server.root);
        host->send(server.world(), server.root, tick);
        join->receive(client.world(), client.root);
        join->send(client.world(), client.root, tick);
    }
    CHECK(host->status().authority);
    CHECK(host->status().peerCount == 1);
    CHECK_FALSE(join->status().authority);
    CHECK(join->status().serverTick > 0);
    CHECK(client.child(client.root, "Beacon").valid());
}
