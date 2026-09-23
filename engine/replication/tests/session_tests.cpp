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
#include "luaug/scene/class_registry.h"
#include "luaug/scene/components.h"
#include "luaug/scene/enum_registry.h"
#include "luaug/scene/players.h"
#include "luaug/scene/world.h"

#include <doctest/doctest.h>
#include <string>

#include "../../scene/tests/scene_fixture.h"
#include "class_descriptors.gen.h"

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
        // The transport's own semantics: each snapshot applied as it arrives.
        replica->setInterpolationDelay(0);
    }

    // One tick, in the order the frame runs it: what arrived, the tick, what
    // to send -- on both ends.
    void step()
    {
        tick += 1;
        authority->receive(server.world(), server.root);
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
    Side server;
    authority.receive(server.world(), server.root);
    // A hello claiming protocol 999.
    const core::u8 hello[] = {1, 0xE7, 0x03, 0x00, 0x00};
    REQUIRE_FALSE(rogue->send(toServer, hello, net::Delivery::Reliable, 0).has_value());
    authority.receive(server.world(), server.root);
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

namespace {

// A world with the engine's real classes: a data model, its `NetworkService`
// and a `Workspace`, which is the shape players need and the hand-built
// fixture does not have.
struct RealSide
{
    core::AtomTable atoms;
    scene::ClassRegistry classes;
    scene::EnumRegistry enums;
    scene::World world{classes, enums, atoms, 99u};
    core::InstanceId dataModel;
    core::InstanceId network;
    core::InstanceId workspace;
    core::InstanceId lighting;

    RealSide()
    {
        scene::generated::registerClasses(classes, atoms);
        scene::generated::registerEnums(enums, atoms);
        // `Decal` is the renderer's class, and this module sits beside the
        // renderer rather than above it -- so the one class the wire carries
        // from there is declared here by hand, storing what the real one does.
        (void)classes.registerClass({
            .name = atoms.intern("Decal"),
            .super = classes.findId(atoms.intern("Instance")),
            .defaultName = atoms.intern("Decal"),
            .attachComponents = [](scene::World& world,
                                   core::InstanceId id) { world.decals().add(id, scene::DecalComponent{}); },
            .detachComponents = [](scene::World& world, core::InstanceId id) { world.decals().remove(id); },
        });
        // And `Lighting`, the service whose properties travel, for the same
        // reason.
        (void)classes.registerClass({
            .name = atoms.intern("Lighting"),
            .super = classes.findId(atoms.intern("Instance")),
            .defaultName = atoms.intern("Lighting"),
            .attachComponents = [](scene::World& world,
                                   core::InstanceId id) { world.lighting().add(id, scene::LightingComponent{}); },
            .detachComponents = [](scene::World& world, core::InstanceId id) { world.lighting().remove(id); },
        });
        const auto make = [this](std::string_view name) {
            const core::InstanceId id = world.create(classes.findId(atoms.intern(name)));
            REQUIRE(id.valid());
            world.setName(id, atoms.intern(name));
            return id;
        };
        dataModel = make("DataModel");
        network = make("NetworkService");
        workspace = make("Workspace");
        lighting = make("Lighting");
        REQUIRE_FALSE(world.setParent(lighting, dataModel).has_value());
        REQUIRE_FALSE(world.setParent(network, dataModel).has_value());
        REQUIRE_FALSE(world.setParent(workspace, dataModel).has_value());
    }
};

} // namespace

TEST_CASE("a joined replica is a player on the authority, and what it does arrives as intent")
{
    seedCatalog();
    auto network = net::createMemoryNetwork();
    auto serverTransport = net::createMemoryTransport(network);
    auto clientTransport = net::createMemoryTransport(network);
    REQUIRE_FALSE(serverTransport->open(net::TransportConfig{.port = Port, .maxPeers = 4, .channels = 4}).has_value());
    REQUIRE_FALSE(clientTransport->open(net::TransportConfig{.port = 0, .maxPeers = 1, .channels = 4}).has_value());
    net::PeerId toServer;
    REQUIRE_FALSE(clientTransport->connect("memory", Port, toServer).has_value());

    RealSide server;
    RealSide client;
    // What the host does at boot for everybody but a dedicated server.
    const core::InstanceId host = scene::createPlayer(server.world, server.network, 1, true);
    const core::InstanceId me = scene::createPlayer(client.world, client.network, 0, true);
    REQUIRE(host.valid());
    REQUIRE(me.valid());

    AuthoritySession authority(*serverTransport);
    ReplicaSession replica(*clientTransport, toServer);
    const auto step = [&](core::u64 tick) {
        authority.receive(server.world, server.workspace);
        authority.send(server.world, server.workspace, tick);
        replica.receive(client.world, client.workspace);
        replica.sendIntent(client.world, tick);
    };
    for (core::u64 tick = 1; tick <= 3; ++tick)
        step(tick);

    // The authority has two players now, and the replica knows which it is.
    const core::InstanceId remote = scene::playerByUserId(server.world, 2);
    REQUIRE(remote.valid());
    CHECK(server.world.parentOf(remote) == server.network);
    CHECK(client.world.players().find(me)->userId == 2);
    CHECK(scene::localPlayerOf(server.world) == host);

    // **And the replica knows who else is playing**: the host's player is on
    // it too, as a player nobody at that machine drives.
    const core::InstanceId hostSeen = scene::playerByUserId(client.world, 1);
    REQUIRE(hostSeen.valid());
    CHECK_FALSE(client.world.players().find(hostSeen)->local);
    CHECK(client.world.parentOf(hostSeen) == client.network);
    CHECK(scene::localPlayerOf(client.world) == me);

    // The replica's player jumps and walks; the authority's copy of that player
    // reads it, by action NAME, in its own atoms.
    client.world.players().find(me)->intents = {
        scene::PlayerIntent{client.atoms.intern("Jump"), 0, core::Vec3{}, true},
        scene::PlayerIntent{client.atoms.intern("Move"), 2, core::Vec3{0.5f, -1.0f, 0.0f}, false},
    };
    step(4);
    step(5);
    const scene::PlayerComponent* seen = server.world.players().find(remote);
    REQUIRE(seen != nullptr);
    REQUIRE(seen->intents.size() == 2);
    CHECK(server.atoms.text(seen->intents[0].action) == "Jump");
    CHECK(seen->intents[0].pressed);
    CHECK(server.atoms.text(seen->intents[1].action) == "Move");
    CHECK(static_cast<double>(seen->intents[1].axis.y) == doctest::Approx(-1.0));

    // Leaving is a player removed, not a player left behind.
    clientTransport.reset();
    authority.receive(server.world, server.workspace);
    server.world.retireDestroyed();
    CHECK_FALSE(scene::playerByUserId(server.world, 2).valid());
    CHECK(scene::localPlayerOf(server.world) == host);
}

TEST_CASE("a character's transform replicates, because a CharacterBody carries BasePart's fields too")
{
    // **The defect this pins**: the schema said `CharacterBody` inherited the
    // part's six fields in its doc and not in its data, so a character on a
    // replica held its spawn position for ever.
    seedCatalog();
    auto network = net::createMemoryNetwork();
    auto serverTransport = net::createMemoryTransport(network);
    auto clientTransport = net::createMemoryTransport(network);
    REQUIRE_FALSE(serverTransport->open(net::TransportConfig{.port = Port, .maxPeers = 4, .channels = 4}).has_value());
    REQUIRE_FALSE(clientTransport->open(net::TransportConfig{.port = 0, .maxPeers = 1, .channels = 4}).has_value());
    net::PeerId toServer;
    REQUIRE_FALSE(clientTransport->connect("memory", Port, toServer).has_value());

    RealSide server;
    RealSide client;
    const core::InstanceId body = server.world.create(server.classes.findId(server.atoms.intern("CharacterBody")));
    REQUIRE(body.valid());
    server.world.setName(body, server.atoms.intern("Hero"));
    REQUIRE_FALSE(server.world.setParent(body, server.workspace).has_value());

    AuthoritySession authority(*serverTransport);
    ReplicaSession replica(*clientTransport, toServer);
    // The transport's own semantics: each snapshot applied as it arrives.
    replica.setInterpolationDelay(0);
    for (core::u64 tick = 1; tick <= 6; ++tick) {
        server.world.parts().find(body)->cframe.position = core::DVec3{static_cast<double>(tick), 2.0, -3.0};
        authority.receive(server.world, server.workspace);
        authority.send(server.world, server.workspace, tick);
        replica.receive(client.world, client.workspace);
    }

    const core::InstanceId hero = client.world.findFirstChild(client.workspace, client.atoms.intern("Hero"));
    REQUIRE(hero.valid());
    CHECK(client.world.characterBodies().find(hero) != nullptr);
    CHECK(client.world.parts().find(hero)->cframe.position.x == doctest::Approx(6.0));
    CHECK(client.world.parts().find(hero)->cframe.position.z == doctest::Approx(-3.0));
    CHECK(replica.checksumFailures() == 0);
}

// --- Characters, interest and prediction (N1, ADR 0076) -------------------

namespace {

// Two real worlds over the memory transport, each with the players a host and
// a replica have at boot, stepped in the frame's order on both ends.
struct PlayedMatch
{
    std::shared_ptr<net::MemoryNetwork> network = net::createMemoryNetwork();
    std::unique_ptr<net::ITransport> serverTransport;
    std::unique_ptr<net::ITransport> clientTransport = net::createMemoryTransport(network);
    RealSide server;
    RealSide client;
    core::InstanceId host;
    core::InstanceId me;
    std::optional<AuthoritySession> authority;
    std::optional<ReplicaSession> replica;
    core::u64 tick = 0;

    explicit PlayedMatch(const net::LossConfig* loss = nullptr)
    {
        seedCatalog();
        serverTransport = loss != nullptr ? net::createLossyTransport(net::createMemoryTransport(network), *loss)
                                          : net::createMemoryTransport(network);
        REQUIRE_FALSE(
            serverTransport->open(net::TransportConfig{.port = Port, .maxPeers = 4, .channels = 4}).has_value());
        REQUIRE_FALSE(clientTransport->open(net::TransportConfig{.port = 0, .maxPeers = 1, .channels = 4}).has_value());
        net::PeerId toServer;
        REQUIRE_FALSE(clientTransport->connect("memory", Port, toServer).has_value());
        host = scene::createPlayer(server.world, server.network, 1, true);
        me = scene::createPlayer(client.world, client.network, 0, true);
        authority.emplace(*serverTransport);
        replica.emplace(*clientTransport, toServer);
        run(3);
    }

    // A part under the authority's workspace, at `at`.
    core::InstanceId part(std::string_view name, core::DVec3 at)
    {
        const core::InstanceId id = server.world.create(server.classes.findId(server.atoms.intern("Part")));
        REQUIRE(id.valid());
        server.world.setName(id, server.atoms.intern(name));
        server.world.parts().find(id)->cframe.position = at;
        REQUIRE_FALSE(server.world.setParent(id, server.workspace).has_value());
        return id;
    }

    // The authority's player for this replica.
    [[nodiscard]] core::InstanceId remote() { return scene::playerByUserId(server.world, 2); }

    // The replica's copy of an authority instance, or nothing.
    [[nodiscard]] core::InstanceId copyOf(core::InstanceId id) { return replica->localOf(authority->netIdOf(id)); }

    void step()
    {
        tick += 1;
        authority->receive(server.world, server.workspace);
        authority->send(server.world, server.workspace, tick);
        replica->receive(client.world, client.workspace);
        replica->sendIntent(client.world, tick);
    }

    void run(int ticks)
    {
        for (int at = 0; at < ticks; ++at)
            step();
    }
};

} // namespace

TEST_CASE("each machine's Player.Character is its own copy of the part the authority named")
{
    PlayedMatch match;
    const core::InstanceId racer = match.part("Racer", core::DVec3{0.0, 1.0, 0.0});
    const core::InstanceId hostRacer = match.part("HostRacer", core::DVec3{5.0, 1.0, 0.0});
    REQUIRE(match.remote().valid());
    match.server.world.players().find(match.remote())->character = racer;
    match.server.world.players().find(match.host)->character = hostRacer;
    match.run(3);

    const core::InstanceId mine = match.copyOf(racer);
    REQUIRE(mine.valid());
    CHECK(match.client.world.players().find(match.me)->character == mine);
    const core::InstanceId hostSeen = scene::playerByUserId(match.client.world, 1);
    REQUIRE(hostSeen.valid());
    CHECK(match.client.world.players().find(hostSeen)->character == match.copyOf(hostRacer));

    // And a character taken away is taken away everywhere.
    match.server.world.players().find(match.remote())->character = {};
    match.run(2);
    CHECK_FALSE(match.client.world.players().find(match.me)->character.valid());
}

TEST_CASE("a replica is sent what is near its character, and nothing far from it")
{
    PlayedMatch match;
    match.server.world.engineState().streamingLoadRadius = 100.0;
    const core::InstanceId racer = match.part("Racer", core::DVec3{0.0, 1.0, 0.0});
    const core::InstanceId near = match.part("Near", core::DVec3{40.0, 1.0, 0.0});
    const core::InstanceId far = match.part("Far", core::DVec3{500.0, 1.0, 0.0});
    // A model straddling the boundary: its near part comes, its far part does
    // not, and the model comes because something in it did.
    const core::InstanceId model =
        match.server.world.create(match.server.classes.findId(match.server.atoms.intern("Model")));
    REQUIRE_FALSE(match.server.world.setParent(model, match.server.workspace).has_value());
    const core::InstanceId inside = match.part("Inside", core::DVec3{10.0, 1.0, 10.0});
    const core::InstanceId outside = match.part("Outside", core::DVec3{900.0, 1.0, 10.0});
    REQUIRE_FALSE(match.server.world.setParent(inside, model).has_value());
    REQUIRE_FALSE(match.server.world.setParent(outside, model).has_value());
    // And whatever is attached to a near part comes with it, wherever it is.
    const core::InstanceId attached = match.part("Attached", core::DVec3{700.0, 1.0, 0.0});
    REQUIRE_FALSE(match.server.world.setParent(attached, near).has_value());

    match.server.world.players().find(match.remote())->character = racer;
    match.run(4);

    CHECK(match.copyOf(racer).valid());
    CHECK(match.copyOf(near).valid());
    CHECK(match.copyOf(model).valid());
    CHECK(match.copyOf(inside).valid());
    CHECK(match.copyOf(attached).valid());
    CHECK_FALSE(match.copyOf(far).valid());
    CHECK_FALSE(match.copyOf(outside).valid());

    // The far part walks in and arrives; walks just past the radius and stays,
    // inside the hysteresis; and leaves past it.
    match.server.world.parts().find(far)->cframe.position = core::DVec3{90.0, 1.0, 0.0};
    match.run(3);
    CHECK(match.copyOf(far).valid());
    match.server.world.parts().find(far)->cframe.position = core::DVec3{115.0, 1.0, 0.0};
    match.run(3);
    CHECK(match.copyOf(far).valid());
    match.server.world.parts().find(far)->cframe.position = core::DVec3{200.0, 1.0, 0.0};
    match.run(3);
    CHECK_FALSE(match.copyOf(far).valid());
    // And back again: a second spawn of the same id, whole.
    match.server.world.parts().find(far)->cframe.position = core::DVec3{20.0, 1.0, 0.0};
    match.run(3);
    REQUIRE(match.copyOf(far).valid());
    match.run(8);
    CHECK(match.client.world.parts().find(match.copyOf(far))->cframe.position.x == doctest::Approx(20.0));
    CHECK(match.replica->checksumFailures() == 0);
}

TEST_CASE("a replica moves its own character at once, and the snapshots only correct it")
{
    PlayedMatch match;
    const core::InstanceId racer = match.part("Racer", core::DVec3{0.0, 1.0, 0.0});
    match.server.world.players().find(match.remote())->character = racer;
    match.run(3);
    const core::InstanceId mine = match.copyOf(racer);
    REQUIRE(mine.valid());

    // The game, on both ends: while "Move" is held, the character goes half a
    // metre along x a tick. The authority runs it from the intent it received;
    // the replica runs it from its own input, at once -- which is prediction.
    const core::NameAtom move = match.client.atoms.intern("Move");
    match.client.world.players().find(match.me)->intents = {scene::PlayerIntent{move, 0, core::Vec3{}, true}};
    const auto serverGame = [&] {
        const scene::PlayerComponent* player = match.server.world.players().find(match.remote());
        for (const scene::PlayerIntent& intent : player->intents) {
            if (match.server.atoms.text(intent.action) == "Move" && intent.pressed)
                match.server.world.parts().find(racer)->cframe.position.x += 0.5;
        }
    };
    const auto clientGame = [&] { match.client.world.parts().find(mine)->cframe.position.x += 0.5; };

    // A round trip of several ticks: the replica reads the network every
    // fourth tick, so an answer is always a few ticks stale when it lands.
    for (int frame = 1; frame <= 60; ++frame) {
        match.tick += 1;
        match.authority->receive(match.server.world, match.server.workspace);
        serverGame();
        match.authority->send(match.server.world, match.server.workspace, match.tick);
        if (frame % 4 == 0)
            match.replica->receive(match.client.world, match.client.workspace);
        clientGame();
        match.replica->sendIntent(match.client.world, match.tick);
    }

    const double server = match.server.world.parts().find(racer)->cframe.position.x;
    const double client = match.client.world.parts().find(mine)->cframe.position.x;
    // **Ahead of the authority, not behind it**: the replica shows the moves it
    // has made and the authority has not answered yet.
    CHECK(client > server);
    CHECK(client - server < 5.0);
    // And the two agree about every move both have seen, so nothing was
    // corrected.
    CHECK(match.replica->stats().corrections == 0);

    // The authority disagrees -- a teleport the replica could not predict --
    // and the replica is corrected by exactly that much.
    match.server.world.parts().find(racer)->cframe.position.z = 30.0;
    for (int frame = 1; frame <= 12; ++frame) {
        match.tick += 1;
        match.authority->receive(match.server.world, match.server.workspace);
        match.authority->send(match.server.world, match.server.workspace, match.tick);
        match.replica->receive(match.client.world, match.client.workspace);
        match.replica->sendIntent(match.client.world, match.tick);
    }
    CHECK(match.client.world.parts().find(mine)->cframe.position.z == doctest::Approx(30.0));
    CHECK(match.replica->stats().corrections >= 1);
}

TEST_CASE("another player's part is drawn between snapshots rather than stepping at their rate")
{
    PlayedMatch match;
    const core::InstanceId racer = match.part("Racer", core::DVec3{0.0, 1.0, 0.0});
    const core::InstanceId other = match.part("Other", core::DVec3{0.0, 1.0, 5.0});
    match.server.world.players().find(match.remote())->character = racer;
    match.run(3);
    const core::InstanceId seen = match.copyOf(other);
    REQUIRE(seen.valid());

    // It moves a metre a tick, and the authority snapshots every other tick,
    // as the default rate does.
    std::vector<double> positions;
    for (int frame = 1; frame <= 40; ++frame) {
        match.tick += 1;
        match.authority->receive(match.server.world, match.server.workspace);
        match.server.world.parts().find(other)->cframe.position.x += 1.0;
        if (match.tick % 2 == 0)
            match.authority->send(match.server.world, match.server.workspace, match.tick);
        match.replica->receive(match.client.world, match.client.workspace);
        positions.push_back(match.client.world.parts().find(seen)->cframe.position.x);
    }
    // Once the buffer is full, every tick moves it a metre: no stall on the
    // tick with no snapshot, and no two-metre jump on the tick with one.
    for (std::size_t at = 20; at < positions.size(); ++at)
        CHECK(positions[at] - positions[at - 1] == doctest::Approx(1.0));
    // And it is drawn a few ticks behind the authority, not ahead of it.
    const double server = match.server.world.parts().find(other)->cframe.position.x;
    CHECK(positions.back() < server);
    CHECK(server - positions.back() <= 6.0);
}

TEST_CASE("a decal placed on the authority is seen on a replica, image and all")
{
    PlayedMatch match;
    const core::InstanceId crate = match.part("Crate", core::DVec3{0.0, 1.0, 0.0});
    const core::InstanceId decal =
        match.server.world.create(match.server.classes.findId(match.server.atoms.intern("Decal")));
    REQUIRE(decal.valid());
    scene::DecalComponent* mark = match.server.world.decals().find(decal);
    REQUIRE(mark != nullptr);
    mark->texture = match.server.atoms.intern("asset://textures/scorch.png");
    mark->size = core::Vec3{3.0f, 3.0f, 1.0f};
    mark->transparency = 0.25f;
    REQUIRE_FALSE(match.server.world.setParent(decal, crate).has_value());
    match.run(4);

    const core::InstanceId seen = match.copyOf(decal);
    REQUIRE(seen.valid());
    const scene::DecalComponent* copy = match.client.world.decals().find(seen);
    REQUIRE(copy != nullptr);
    // **The URN, in the replica's own atoms**: the authority's atom number
    // would name some other string here, or none.
    CHECK(match.client.atoms.text(copy->texture) == "asset://textures/scorch.png");
    CHECK(static_cast<double>(copy->size.x) == doctest::Approx(3.0));
    CHECK(static_cast<double>(copy->transparency) == doctest::Approx(0.25));
    CHECK(match.client.world.parentOf(seen) == match.copyOf(crate));

    // A new image is a new string, and it travels the same way.
    match.server.world.decals().find(decal)->texture = match.server.atoms.intern("asset://textures/footprint.png");
    match.run(3);
    CHECK(match.client.atoms.text(match.client.world.decals().find(seen)->texture) == "asset://textures/footprint.png");
    CHECK(match.replica->checksumFailures() == 0);
}

TEST_CASE("night on the authority is night on the replica: Lighting's properties travel")
{
    PlayedMatch match;
    scene::LightingComponent* sky = match.server.world.lighting().find(match.server.lighting);
    REQUIRE(sky != nullptr);
    sky->clockTime = 19.5f;
    sky->fogEnd = 300.0f;
    sky->fogColor = core::Color3{0.2f, 0.1f, 0.3f};
    match.run(4);

    const scene::LightingComponent* seen = match.client.world.lighting().find(match.client.lighting);
    REQUIRE(seen != nullptr);
    CHECK(static_cast<double>(seen->clockTime) == doctest::Approx(19.5));
    CHECK(static_cast<double>(seen->fogEnd) == doctest::Approx(300.0));
    CHECK(static_cast<double>(seen->fogColor.b) == doctest::Approx(0.3));
    // A service stays where every world keeps it, and is never a copy.
    CHECK(match.client.world.parentOf(match.client.lighting) == match.client.dataModel);
    CHECK(match.client.world.name(match.client.lighting) == match.client.atoms.intern("Lighting"));

    // And the clock keeps up as the authority's day turns.
    sky->clockTime = 6.0f;
    match.run(3);
    CHECK(static_cast<double>(match.client.world.lighting().find(match.client.lighting)->clockTime) ==
          doctest::Approx(6.0));
    CHECK(match.replica->checksumFailures() == 0);
}
