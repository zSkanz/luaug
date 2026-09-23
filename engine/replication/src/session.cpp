#include "luaug/replication/session.h"

#include "luaug/scene/class_registry.h"
#include "luaug/scene/components.h"
#include "luaug/scene/players.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>

#include "wire_schema.gen.h"

namespace luaug::replication {
namespace {

using core::f64;
using core::i32;
using core::InstanceId;
using generated::MessageType;

// Channels by the schema's numbering. Named here once so a send cannot pick one
// by a bare digit.
constexpr u8 ControlChannel = 0;
constexpr u8 StateChannel = 1;
constexpr u8 IntentChannel = 2;

// A snapshot record whose fields are the whole set rather than a diff.
constexpr u8 FullRecord = 1;

// **The value a written-field record holds when the write could not happen yet**
// -- a parent the replica has not been told about. No real field is all ones,
// so the next apply sees a difference and tries again.
[[nodiscard]] FieldValue pendingValue() noexcept
{
    FieldValue value;
    value.raw.fill(0xFF);
    return value;
}

[[nodiscard]] u64 packed(InstanceId id) noexcept
{
    return (static_cast<u64>(id.generation) << 32) | id.index;
}

// --- Little-endian bytes ----------------------------------------------------

class Writer
{
public:
    std::vector<u8> bytes;

    void u8v(u8 value) { bytes.push_back(value); }
    void u16v(u16 value)
    {
        for (int at = 0; at < 2; ++at)
            bytes.push_back(static_cast<u8>(value >> (8 * at)));
    }
    void u32v(u32 value)
    {
        for (int at = 0; at < 4; ++at)
            bytes.push_back(static_cast<u8>(value >> (8 * at)));
    }
    void u64v(u64 value)
    {
        for (int at = 0; at < 8; ++at)
            bytes.push_back(static_cast<u8>(value >> (8 * at)));
    }
    void text(std::string_view value)
    {
        u16v(static_cast<u16>(std::min<usize>(value.size(), 0xFFFF)));
        bytes.insert(bytes.end(), value.begin(), value.begin() + std::min<usize>(value.size(), 0xFFFF));
    }
};

// Every read is bounds-checked and a failed one poisons the reader, so a
// decoder can read a whole message and ask once at the end whether it was all
// there -- rather than checking after every field and forgetting one.
class Reader
{
public:
    explicit Reader(std::span<const u8> bytes) noexcept : m_bytes(bytes) {}

    [[nodiscard]] bool ok() const noexcept { return m_ok; }
    [[nodiscard]] bool done() const noexcept { return m_at == m_bytes.size(); }
    [[nodiscard]] std::span<const u8> bytes() const noexcept { return m_bytes; }
    [[nodiscard]] usize& at() noexcept { return m_at; }
    void fail() noexcept { m_ok = false; }

    [[nodiscard]] u64 read(usize width) noexcept
    {
        if (!m_ok || m_bytes.size() - m_at < width) {
            m_ok = false;
            return 0;
        }
        u64 value = 0;
        for (usize at = 0; at < width; ++at)
            value |= static_cast<u64>(m_bytes[m_at + at]) << (8 * at);
        m_at += width;
        return value;
    }
    [[nodiscard]] u8 u8v() noexcept { return static_cast<u8>(read(1)); }
    [[nodiscard]] u16 u16v() noexcept { return static_cast<u16>(read(2)); }
    [[nodiscard]] u32 u32v() noexcept { return static_cast<u32>(read(4)); }
    [[nodiscard]] u64 u64v() noexcept { return read(8); }
    [[nodiscard]] std::string_view text() noexcept
    {
        const u16 length = u16v();
        if (!m_ok || m_bytes.size() - m_at < length) {
            m_ok = false;
            return {};
        }
        const std::string_view value{reinterpret_cast<const char*>(m_bytes.data() + m_at), length};
        m_at += length;
        return value;
    }

private:
    std::span<const u8> m_bytes;
    usize m_at = 0;
    bool m_ok = true;
};

[[nodiscard]] usize schemaCount() noexcept
{
    return std::size(generated::Classes);
}

[[nodiscard]] u8 schemaIndexOf(const generated::ClassDesc* desc) noexcept
{
    for (usize at = 0; at < schemaCount(); ++at) {
        if (&generated::Classes[at] == desc)
            return static_cast<u8>(at);
    }
    return 0;
}

// The flat index of a common field by name, which is fixed by the schema.
[[nodiscard]] usize commonIndex(std::string_view name) noexcept
{
    for (usize at = 0; at < std::size(generated::CommonFields); ++at) {
        if (generated::CommonFields[at].name == name)
            return at;
    }
    return static_cast<usize>(-1);
}

// The flat index a wire id names in a class, or past the end when it names none.
[[nodiscard]] usize indexOfWireId(const generated::ClassDesc& desc, u16 wireId) noexcept
{
    const usize count = fieldCount(desc);
    for (usize at = 0; at < count; ++at) {
        if (wireIdAt(desc, at) == wireId)
            return at;
    }
    return count;
}

[[nodiscard]] auto findEntity(std::vector<EntityState>& entities, u32 id)
{
    return std::lower_bound(entities.begin(), entities.end(), id,
                            [](const EntityState& entity, u32 probe) { return entity.id.value < probe; });
}

[[nodiscard]] const EntityState* findEntity(const WorldState& state, u32 id) noexcept
{
    const auto at = std::lower_bound(state.entities.begin(), state.entities.end(), id,
                                     [](const EntityState& entity, u32 probe) { return entity.id.value < probe; });
    return at != state.entities.end() && at->id.value == id ? &*at : nullptr;
}

[[nodiscard]] u32 bitsOf(float value) noexcept
{
    u32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

[[nodiscard]] float floatOf(u32 bits) noexcept
{
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void sendBytes(net::ITransport& transport, net::PeerId peer, const std::vector<u8>& bytes, net::Delivery delivery,
               u8 channel, Stats& stats)
{
    if (!transport.send(peer, bytes, delivery, channel).has_value())
        stats.bytesSent += bytes.size();
}

} // namespace

u64 checksumOf(const WorldState& state) noexcept
{
    u64 hash = 0xCBF29CE484222325ull;
    const auto mix = [&hash](const void* data, usize size) {
        const auto* bytes = static_cast<const u8*>(data);
        for (usize at = 0; at < size; ++at) {
            hash ^= bytes[at];
            hash *= 0x100000001B3ull;
        }
    };
    for (const EntityState& entity : state.entities) {
        mix(&entity.id.value, sizeof(entity.id.value));
        mix(&entity.schema, sizeof(entity.schema));
        for (const FieldValue& field : entity.fields)
            mix(field.raw.data(), field.raw.size());
    }
    return hash;
}

// --- Authority ----------------------------------------------------------------

AuthoritySession::Peer* AuthoritySession::peerFor(net::PeerId id) noexcept
{
    for (Peer& peer : m_peers) {
        if (peer.id == id)
            return &peer;
    }
    return nullptr;
}

u32 AuthoritySession::peerCount() const noexcept
{
    return static_cast<u32>(
        std::count_if(m_peers.begin(), m_peers.end(), [](const Peer& peer) { return peer.welcomed; }));
}

NetId AuthoritySession::netIdOf(InstanceId id) const noexcept
{
    const auto found = m_netIds.find(packed(id));
    return found != m_netIds.end() ? NetId{found->second} : NetId{};
}

void AuthoritySession::receive(scene::World& world, InstanceId root)
{
    // Where players live. A world with no `NetworkService` -- a test's bare
    // tree -- still replicates; it just has nobody to name.
    const InstanceId network = scene::networkServiceOf(world, world.parentOf(root));
    std::vector<net::TransportEvent> events;
    (void)m_transport.poll(events, 0);
    for (const net::TransportEvent& event : events) {
        switch (event.kind) {
        case net::TransportEvent::Kind::Connected:
            if (peerFor(event.peer) == nullptr) {
                Peer peer;
                peer.id = event.peer;
                m_peers.push_back(std::move(peer));
                std::sort(m_peers.begin(), m_peers.end(),
                          [](const Peer& a, const Peer& b) { return a.id.value < b.id.value; });
            }
            break;
        case net::TransportEvent::Kind::Disconnected:
            if (Peer* gone = peerFor(event.peer); gone != nullptr && gone->player.valid())
                scene::removePlayer(world, network, gone->player);
            std::erase_if(m_peers, [&](const Peer& peer) { return peer.id == event.peer; });
            break;
        case net::TransportEvent::Kind::Message: {
            Peer* peer = peerFor(event.peer);
            if (peer == nullptr)
                break;
            m_stats.bytesReceived += event.payload.size();
            Reader reader(event.payload);
            const auto type = static_cast<MessageType>(reader.u8v());
            if (event.channel == IntentChannel && type == MessageType::Intent && peer->welcomed) {
                const u64 tick = reader.u64v();
                scene::PlayerComponent* player = peer->player.valid() ? world.players().find(peer->player) : nullptr;
                if (!reader.ok() || tick <= peer->intentTick || player == nullptr)
                    break;
                std::vector<scene::PlayerIntent> intents;
                const u16 count = reader.u16v();
                for (u16 at = 0; at < count && reader.ok(); ++at) {
                    const std::string_view name = reader.text();
                    scene::PlayerIntent intent;
                    intent.type = static_cast<core::i32>(reader.u8v());
                    intent.axis.x = floatOf(reader.u32v());
                    intent.axis.y = floatOf(reader.u32v());
                    intent.axis.z = floatOf(reader.u32v());
                    intent.pressed = reader.u8v() != 0;
                    if (!reader.ok())
                        break;
                    intent.action = world.atoms().intern(name);
                    intents.push_back(intent);
                }
                // Whole or not at all: half an intent is a player whose second
                // key was released by a truncated packet.
                if (reader.ok() && reader.done()) {
                    player->intents = std::move(intents);
                    peer->intentTick = tick;
                }
                break;
            }
            if (event.channel != ControlChannel)
                break;
            if (type == MessageType::Hello) {
                const u32 version = reader.u32v();
                // **Version first, and a mismatch is a refusal rather than a
                // guess** -- a decoder reading field ids it does not have is a
                // replica confidently wrong about the world.
                if (!reader.ok() || version != generated::ProtocolVersion) {
                    m_transport.disconnect(peer->id);
                    break;
                }
                peer->welcomed = true;
                peer->userId = m_nextUserId++;
                if (network.valid())
                    peer->player = scene::createPlayer(world, network, peer->userId, false);
                Writer welcome;
                welcome.u8v(static_cast<u8>(MessageType::Welcome));
                welcome.u32v(generated::ProtocolVersion);
                welcome.u32v(peer->userId);
                welcome.u64v(m_tick);
                sendBytes(m_transport, peer->id, welcome.bytes, net::Delivery::Reliable, ControlChannel, m_stats);
            }
            else if (type == MessageType::Ack) {
                const u64 tick = reader.u64v();
                if (reader.ok())
                    peer->acked = std::max(peer->acked, tick);
            }
            break;
        }
        case net::TransportEvent::Kind::None:
            break;
        }
    }
}

void AuthoritySession::capture(const scene::World& world, InstanceId root, u64 tick)
{
    auto state = std::make_shared<WorldState>();
    state->tick = tick;
    const usize parentField = commonIndex("Parent");

    std::map<u64, u32> seen;
    // The walk itself, for interest: which instance each entity is and where
    // its parent is in this list, in pre-order.
    m_order.clear();
    std::map<u64, i32> orderOf;
    // Pre-order, children in sibling order: a parent is always captured before
    // its children, so a new child's parent already has an id.
    std::vector<InstanceId> stack;
    for (InstanceId child = world.firstChild(root); child.valid(); child = world.nextSibling(child))
        stack.push_back(child);
    std::reverse(stack.begin(), stack.end());

    while (!stack.empty()) {
        const InstanceId id = stack.back();
        stack.pop_back();
        const generated::ClassDesc* desc = schemaFor(world, id);
        FieldSet fields;
        // **An instance the schema does not describe takes its subtree with
        // it.** A replica could not parent the children to anything.
        if (desc == nullptr || !extractFields(world, id, *desc, fields))
            continue;

        const u64 key = packed(id);
        u32 netId = 0;
        if (const auto found = m_netIds.find(key); found != m_netIds.end()) {
            netId = found->second;
        }
        else {
            netId = m_nextNetId++;
            m_classNames[netId] = world.classes().find(world.classOf(id))->name;
        }
        seen[key] = netId;

        const InstanceId parent = world.parentOf(id);
        const auto parentId = parent == root ? RootNetId.value : seen.at(packed(parent));
        setNetId(fields[parentField], NetId{parentId});

        state->entities.push_back(EntityState{NetId{netId}, schemaIndexOf(desc), std::move(fields)});
        const auto parentOrder = orderOf.find(packed(parent));
        orderOf[key] = static_cast<i32>(m_order.size());
        m_order.push_back(Captured{netId, id, parentOrder != orderOf.end() ? parentOrder->second : -1});

        std::vector<InstanceId> children;
        for (InstanceId child = world.firstChild(id); child.valid(); child = world.nextSibling(child))
            children.push_back(child);
        stack.insert(stack.end(), children.rbegin(), children.rend());
    }

    // **The services whose properties travel** (`Service = true` in the wire
    // schema), each under an id of its own that never changes: every world has
    // one from boot, so there is nothing to spawn and nothing to number.
    const InstanceId dataModel = world.parentOf(root);
    for (usize index = 0; index < std::size(generated::Classes) && dataModel.valid(); ++index) {
        const generated::ClassDesc& desc = generated::Classes[index];
        if (!desc.service)
            continue;
        for (InstanceId child = world.firstChild(dataModel); child.valid(); child = world.nextSibling(child)) {
            if (world.atoms().text(world.classes().find(world.classOf(child))->name) != desc.name)
                continue;
            FieldSet fields;
            if (!extractFields(world, child, desc, fields))
                break;
            setNetId(fields[parentField], RootNetId);
            const u32 netId = ServiceNetIdBase + static_cast<u32>(index);
            state->entities.push_back(EntityState{NetId{netId}, static_cast<u8>(index), std::move(fields)});
            m_order.push_back(Captured{netId, child, -1});
            break;
        }
    }

    // Instances gone since the last capture give their ids up for good.
    for (auto at = m_netIds.begin(); at != m_netIds.end();) {
        if (!seen.contains(at->first)) {
            m_classNames.erase(at->second);
            at = m_netIds.erase(at);
        }
        else {
            ++at;
        }
    }
    m_netIds = std::move(seen);

    std::sort(state->entities.begin(), state->entities.end(),
              [](const EntityState& a, const EntityState& b) { return a.id.value < b.id.value; });
    m_history.push_back(std::move(state));
    while (m_history.size() > StateHistory)
        m_history.pop_front();
}

const WorldState* AuthoritySession::historyAt(u64 tick) const noexcept
{
    for (auto at = m_history.rbegin(); at != m_history.rend(); ++at) {
        if ((*at)->tick == tick)
            return at->get();
    }
    return nullptr;
}

void AuthoritySession::send(const scene::World& world, InstanceId root, u64 tick)
{
    m_world = &world;
    m_tick = tick;
    capture(world, root, tick);
    const WorldState& current = *m_history.back();

    // Everybody taking part, in join order -- the children of `NetworkService`,
    // which is where every path that makes a player puts it -- each as its user
    // id and its character's NetId, zero for none. The pair is what lets a
    // replica know which part is its own (`Player.Character`).
    std::vector<u32> roster;
    const InstanceId network = scene::networkServiceOf(world, world.parentOf(root));
    for (InstanceId child = network.valid() ? world.firstChild(network) : InstanceId{}; child.valid();
         child = world.nextSibling(child)) {
        const scene::PlayerComponent* player = world.players().find(child);
        if (player == nullptr || world.destroyed(child))
            continue;
        roster.push_back(player->userId);
        roster.push_back(player->character.valid() ? netIdOf(player->character).value : 0u);
    }

    for (Peer& peer : m_peers) {
        if (!peer.welcomed)
            continue;
        const std::vector<u32> relevant = interestOf(world, peer);
        sendTo(peer, current, roster, relevant);
    }
}

std::vector<u32> AuthoritySession::interestOf(const scene::World& world, const Peer& peer) const
{
    std::vector<u32> relevant;
    relevant.reserve(m_order.size());

    // **Measured from the peer's character** (`Player.Character`). A player
    // with none has nothing to measure from and is sent everything, which is
    // what every session did before interest existed.
    const scene::PlayerComponent* player = peer.player.valid() ? world.players().find(peer.player) : nullptr;
    const scene::PartComponent* body = player != nullptr && player->character.valid() && world.alive(player->character)
                                           ? world.parts().find(player->character)
                                           : nullptr;
    if (body == nullptr) {
        for (const Captured& entry : m_order)
            relevant.push_back(entry.netId);
        std::sort(relevant.begin(), relevant.end());
        return relevant;
    }

    // The streaming radius, with the streaming manager's hysteresis: in at the
    // load radius, out only past a quarter more, so a part on the boundary does
    // not spawn and despawn on alternate ticks.
    const f64 radius = world.engineState().streamingLoadRadius;
    const f64 keep = radius * 1.25;
    const core::DVec3 focus = body->cframe.position;

    const usize count = m_order.size();
    std::vector<u8> inRange(count, 0);
    std::vector<u8> positioned(count, 0);
    std::vector<u8> marked(count, 0);
    for (usize at = 0; at < count; ++at) {
        const scene::PartComponent* part = world.parts().find(m_order[at].id);
        if (part == nullptr)
            continue;
        positioned[at] = 1;
        const core::DVec3 delta = part->cframe.position - focus;
        const f64 distance = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
        const f64 reach = std::binary_search(peer.known.begin(), peer.known.end(), m_order[at].netId) ? keep : radius;
        inRange[at] = distance <= reach * reach ? 1 : 0;
    }

    // Down: a part in range brings everything under it -- what is attached to
    // a part goes with it, whatever its own position says. Pre-order, so a
    // parent is decided before its children.
    std::vector<u8> positionedAbove(count, 0);
    for (usize at = 0; at < count; ++at) {
        const i32 parent = m_order[at].parent;
        if (parent >= 0) {
            const auto up = static_cast<usize>(parent);
            positionedAbove[at] = positioned[up] || positionedAbove[up] ? 1 : 0;
            marked[at] = marked[up] && (positioned[up] || positionedAbove[up]) ? 1 : 0;
        }
        if (inRange[at])
            marked[at] = 1;
    }

    // Up: the ancestors of anything sent, so it has somewhere to be parented;
    // and a container with no part anywhere in or above it, which costs nothing
    // and has no position to be far from. Reverse pre-order visits children
    // before their parent.
    std::vector<u8> positionedBelow(count, 0);
    for (usize at = count; at-- > 0;) {
        const i32 parent = m_order[at].parent;
        if (!marked[at] && !positioned[at] && !positionedBelow[at] && !positionedAbove[at])
            marked[at] = 1;
        if (parent >= 0) {
            const auto up = static_cast<usize>(parent);
            if (marked[at] && (positioned[at] || positionedBelow[at] || positionedAbove[at] || inRange[at]))
                marked[up] = 1;
            if (positioned[at] || positionedBelow[at])
                positionedBelow[up] = 1;
        }
    }

    for (usize at = 0; at < count; ++at) {
        if (marked[at])
            relevant.push_back(m_order[at].netId);
    }
    std::sort(relevant.begin(), relevant.end());
    return relevant;
}

void AuthoritySession::sendTo(Peer& peer, const WorldState& everything, const std::vector<u32>& roster,
                              const std::vector<u32>& relevant)
{
    // **This peer's world is what is in its interest.** Everything below --
    // spawns, despawns, the diff and the checksum -- is over this, so a replica
    // reconstructs and verifies exactly the subset it was sent (ADR 0069
    // decision 8: its hash is a subset by design).
    WorldState filtered;
    filtered.tick = everything.tick;
    filtered.entities.reserve(relevant.size());
    for (const EntityState& entity : everything.entities) {
        if (std::binary_search(relevant.begin(), relevant.end(), entity.id.value))
            filtered.entities.push_back(entity);
    }
    const WorldState& current = filtered;

    // --- Who is playing, whole, when it changed for this peer.
    if (!peer.rosterSent || peer.roster != roster) {
        Writer players;
        players.u8v(static_cast<u8>(MessageType::Players));
        players.u32v(static_cast<u32>(roster.size() / 2));
        for (const u32 value : roster)
            players.u32v(value);
        sendBytes(m_transport, peer.id, players.bytes, net::Delivery::Reliable, ControlChannel, m_stats);
        peer.roster = roster;
        peer.rosterSent = true;
    }

    // --- Spawns and despawns, reliable, before the snapshot that needs them.
    std::vector<u32> now;
    now.reserve(current.entities.size());
    for (const EntityState& entity : current.entities)
        now.push_back(entity.id.value);

    std::vector<u32> entering;
    std::set_difference(now.begin(), now.end(), peer.known.begin(), peer.known.end(), std::back_inserter(entering));
    std::vector<u32> leaving;
    std::set_difference(peer.known.begin(), peer.known.end(), now.begin(), now.end(), std::back_inserter(leaving));
    // A service exists on both ends from boot: its first record arrives whole
    // because no baseline has it, and it is never spawned or despawned.
    std::erase_if(entering, [](u32 id) { return id >= ServiceNetIdBase; });
    std::erase_if(leaving, [](u32 id) { return id >= ServiceNetIdBase; });

    if (!leaving.empty()) {
        Writer despawn;
        despawn.u8v(static_cast<u8>(MessageType::Despawn));
        despawn.u32v(static_cast<u32>(leaving.size()));
        for (const u32 id : leaving)
            despawn.u32v(id);
        sendBytes(m_transport, peer.id, despawn.bytes, net::Delivery::Reliable, ControlChannel, m_stats);
        m_stats.despawned += static_cast<u32>(leaving.size());
    }
    if (!entering.empty()) {
        Writer spawn;
        spawn.u8v(static_cast<u8>(MessageType::Spawn));
        spawn.u32v(static_cast<u32>(entering.size()));
        for (const u32 id : entering) {
            spawn.u32v(id);
            spawn.text(m_world->atoms().text(m_classNames.at(id)));
        }
        sendBytes(m_transport, peer.id, spawn.bytes, net::Delivery::Reliable, ControlChannel, m_stats);
        m_stats.spawned += static_cast<u32>(entering.size());
    }
    peer.known = std::move(now);
    // What this peer holds at this tick, for the baseline a later snapshot is
    // diffed against: the global state then, cut to what this peer had then.
    peer.interest.push_back(PeerInterest{current.tick, peer.known});
    while (peer.interest.size() > StateHistory)
        peer.interest.pop_front();

    // --- The snapshot, against what this peer last proved it holds.
    const WorldState* baseline = peer.acked != 0 ? historyAt(peer.acked) : nullptr;
    const std::vector<u32>* heldThen = nullptr;
    for (const PeerInterest& held : peer.interest) {
        if (held.tick == peer.acked)
            heldThen = &held.ids;
    }
    if (heldThen == nullptr)
        baseline = nullptr;

    struct Record
    {
        const EntityState* entity = nullptr;
        bool full = false;
        std::vector<usize> fields;
    };
    std::vector<Record> records;
    std::set<u32> atoms;
    const usize nameField = commonIndex("Name");
    for (const EntityState& entity : current.entities) {
        // Diffed only against what this peer HAD at the baseline -- and whole
        // when it is entering now, whatever the baseline says: the replica
        // scrubbed it from every stored state when it left.
        const bool held =
            baseline != nullptr && std::binary_search(heldThen->begin(), heldThen->end(), entity.id.value);
        const bool entered = std::binary_search(entering.begin(), entering.end(), entity.id.value);
        const EntityState* before = held && !entered ? findEntity(*baseline, entity.id.value) : nullptr;
        Record record{&entity, before == nullptr || before->schema != entity.schema, {}};
        for (usize at = 0; at < entity.fields.size(); ++at) {
            if (record.full || !(before->fields[at] == entity.fields[at]))
                record.fields.push_back(at);
        }
        if (record.fields.empty())
            continue;
        // **Every name-shaped field, not `Name` alone**: a decal's image is a
        // content URN, and an atom number means nothing on another machine.
        const generated::ClassDesc& described = generated::Classes[entity.schema];
        for (const usize at : record.fields) {
            const generated::FieldDesc* field = fieldAt(described, at);
            if (at == nameField || (field != nullptr && field->encoding == generated::Encoding::NameAtom))
                atoms.insert(asU32(entity.fields[at]));
        }
        records.push_back(std::move(record));
    }

    Writer snapshot;
    snapshot.u8v(static_cast<u8>(MessageType::Snapshot));
    snapshot.u64v(current.tick);
    snapshot.u64v(baseline != nullptr ? baseline->tick : 0);
    snapshot.u64v(checksumOf(current));
    // The last of this peer's intents the authority applied, for its
    // prediction to reconcile against (ADR 0076).
    snapshot.u64v(peer.intentTick);
    // The names this message's fields mention, by the authority's atom. The
    // replica interns each once and keeps the mapping, so a name costs its
    // bytes on the wire when it changes rather than every tick.
    snapshot.u16v(static_cast<u16>(atoms.size()));
    for (const u32 atom : atoms) {
        snapshot.u32v(atom);
        snapshot.text(m_world->atoms().text(core::NameAtom{atom}));
    }
    snapshot.u32v(static_cast<u32>(records.size()));
    for (const Record& record : records) {
        const generated::ClassDesc& desc = generated::Classes[record.entity->schema];
        snapshot.u32v(record.entity->id.value);
        snapshot.u8v(record.entity->schema);
        snapshot.u8v(record.full ? FullRecord : 0);
        snapshot.u16v(static_cast<u16>(record.fields.size()));
        for (const usize at : record.fields) {
            snapshot.u16v(wireIdAt(desc, at));
            encodeField(snapshot.bytes, fieldAt(desc, at)->encoding, record.entity->fields[at]);
        }
    }
    sendBytes(m_transport, peer.id, snapshot.bytes, net::Delivery::UnreliableSequenced, StateChannel, m_stats);
    m_stats.snapshotsSent += 1;
}

// --- Replica ------------------------------------------------------------------

InstanceId ReplicaSession::localOf(NetId id) const noexcept
{
    const auto found = m_locals.find(id.value);
    return found != m_locals.end() ? found->second : InstanceId{};
}

const WorldState* ReplicaSession::stateAt(u64 tick) const noexcept
{
    for (auto at = m_states.rbegin(); at != m_states.rend(); ++at) {
        if ((*at)->tick == tick)
            return at->get();
    }
    return nullptr;
}

void ReplicaSession::receive(scene::World& world, InstanceId root)
{
    std::vector<net::TransportEvent> events;
    (void)m_transport.poll(events, 0);
    for (const net::TransportEvent& event : events) {
        if (!(event.peer == m_authority))
            continue;
        if (event.kind == net::TransportEvent::Kind::Connected) {
            m_connected = true;
            Writer hello;
            hello.u8v(static_cast<u8>(MessageType::Hello));
            hello.u32v(generated::ProtocolVersion);
            sendBytes(m_transport, m_authority, hello.bytes, net::Delivery::Reliable, ControlChannel, m_stats);
            continue;
        }
        if (event.kind == net::TransportEvent::Kind::Disconnected) {
            m_connected = false;
            m_welcomed = false;
            continue;
        }
        if (event.kind != net::TransportEvent::Kind::Message || event.payload.empty())
            continue;
        m_stats.bytesReceived += event.payload.size();
        switch (static_cast<MessageType>(event.payload[0])) {
        case MessageType::Welcome: {
            Reader reader(event.payload);
            (void)reader.u8v();
            const u32 version = reader.u32v();
            const u32 player = reader.u32v();
            (void)reader.u64v();
            if (!reader.ok() || version != generated::ProtocolVersion) {
                m_transport.disconnect(m_authority);
                break;
            }
            m_welcomed = true;
            m_playerId = player;
            // The player at this machine takes the number the authority gave
            // it, so `LocalPlayer.UserId` on a replica is the same number the
            // authority's copy of that player has.
            if (const InstanceId local = scene::localPlayerOf(world); local.valid()) {
                if (scene::PlayerComponent* component = world.players().find(local); component != nullptr)
                    component->userId = player;
                world.setName(local, world.atoms().intern("Player" + std::to_string(player)));
            }
            break;
        }
        case MessageType::Spawn:
            onSpawn(world, event.payload);
            break;
        case MessageType::Despawn:
            onDespawn(world, event.payload);
            break;
        case MessageType::Snapshot:
            onSnapshot(world, root, event.payload);
            break;
        case MessageType::Players:
            onPlayers(world, root, event.payload);
            break;
        default:
            break;
        }
    }
    resolveCharacters(world, root);
    m_serverClock += 1;
    interpolate(world);
}

void ReplicaSession::interpolate(scene::World& world)
{
    // **Everyone else, drawn a few ticks in the past, between two snapshots**
    // -- Valve's entity interpolation. A snapshot every other tick applied as it
    // arrived stepped every remote part at thirty hertz; drawn `delay` ticks
    // behind the server's clock, there are nearly always two samples around the
    // moment being drawn, and the motion between them is a line.
    if (m_interpolationDelay == 0)
        return;
    const u64 target = m_serverClock > m_interpolationDelay ? m_serverClock - m_interpolationDelay : 0;
    for (auto& [netId, samples] : m_samples) {
        // Its own character is predicted, never drawn from the past.
        if (samples.empty() || netId == m_owned)
            continue;
        const auto local = m_locals.find(netId);
        scene::PartComponent* part =
            local != m_locals.end() && world.alive(local->second) ? world.parts().find(local->second) : nullptr;
        if (part == nullptr)
            continue;
        // Past the newest sample, the newest: extrapolating a part the authority
        // has stopped talking about would put it where it is not.
        const Sample* before = &samples.front();
        const Sample* after = nullptr;
        for (const Sample& sample : samples) {
            if (sample.tick <= target)
                before = &sample;
            else if (after == nullptr)
                after = &sample;
        }
        if (after == nullptr || target <= before->tick || after->tick <= before->tick) {
            part->cframe = target < samples.front().tick ? samples.front().cframe : before->cframe;
            continue;
        }
        const f64 alpha = static_cast<f64>(target - before->tick) / static_cast<f64>(after->tick - before->tick);
        part->cframe = core::lerp(before->cframe, after->cframe, alpha);
    }
}

void ReplicaSession::resolveCharacters(scene::World& world, InstanceId root)
{
    // Every player's `Character`, on this machine: the NetId the roster named,
    // through this machine's own copy of it -- which may arrive a message after
    // the roster that names it, so this runs after every receive.
    const InstanceId network = scene::networkServiceOf(world, world.parentOf(root));
    for (InstanceId child = network.valid() ? world.firstChild(network) : InstanceId{}; child.valid();
         child = world.nextSibling(child)) {
        scene::PlayerComponent* player = world.players().find(child);
        if (player == nullptr)
            continue;
        const auto named = m_characters.find(player->userId);
        const auto local = named != m_characters.end() ? m_locals.find(named->second) : m_locals.end();
        player->character = local != m_locals.end() && world.alive(local->second) ? local->second : InstanceId{};
        if (player->local) {
            m_owned = named != m_characters.end() && local != m_locals.end() ? named->second : 0u;
            // What was buffered for it before this machine knew it was its
            // own: the newest of it is where it starts being predicted from.
            if (const auto buffered = m_owned != 0 ? m_samples.find(m_owned) : m_samples.end();
                buffered != m_samples.end()) {
                if (scene::PartComponent* part = world.parts().find(local->second);
                    part != nullptr && !buffered->second.empty())
                    part->cframe = buffered->second.back().cframe;
                m_samples.erase(buffered);
            }
        }
    }
}

void ReplicaSession::sendIntent(const scene::World& world, u64 tick)
{
    if (!m_welcomed)
        return;
    // What this replica predicted for its own character at this tick, for the
    // snapshot that answers this intent to be compared against.
    if (m_owned != 0) {
        const auto local = m_locals.find(m_owned);
        const scene::PartComponent* part =
            local != m_locals.end() && world.alive(local->second) ? world.parts().find(local->second) : nullptr;
        if (part != nullptr) {
            m_predicted.push_back(Sample{tick, part->cframe});
            while (m_predicted.size() > PredictionHistory)
                m_predicted.pop_front();
        }
    }
    const InstanceId local = scene::localPlayerOf(world);
    const scene::PlayerComponent* player = local.valid() ? world.players().find(local) : nullptr;
    if (player == nullptr)
        return;
    Writer intent;
    intent.u8v(static_cast<u8>(MessageType::Intent));
    intent.u64v(tick);
    intent.u16v(static_cast<u16>(std::min<usize>(player->intents.size(), 0xFFFF)));
    for (usize at = 0; at < player->intents.size() && at < 0xFFFF; ++at) {
        const scene::PlayerIntent& one = player->intents[at];
        // **By name**: the authority's atom numbers are not this world's, and
        // an action is what both ends' scripts call it.
        intent.text(world.atoms().text(one.action));
        intent.u8v(static_cast<u8>(one.type));
        intent.u32v(bitsOf(one.axis.x));
        intent.u32v(bitsOf(one.axis.y));
        intent.u32v(bitsOf(one.axis.z));
        intent.u8v(one.pressed ? 1 : 0);
    }
    sendBytes(m_transport, m_authority, intent.bytes, net::Delivery::UnreliableSequenced, IntentChannel, m_stats);
}

void ReplicaSession::onPlayers(scene::World& world, InstanceId root, std::span<const u8> bytes)
{
    Reader reader(bytes);
    (void)reader.u8v();
    const u32 count = reader.u32v();
    std::vector<u32> roster;
    std::map<u32, u32> characters;
    for (u32 at = 0; at < count && reader.ok(); ++at) {
        const u32 userId = reader.u32v();
        const u32 character = reader.u32v();
        roster.push_back(userId);
        characters[userId] = character;
    }
    if (!reader.ok() || !reader.done())
        return;
    m_characters = std::move(characters);
    const InstanceId network = scene::networkServiceOf(world, world.parentOf(root));
    if (!network.valid())
        return;

    // The others, as ordinary `Player`s nobody at this machine drives: gone
    // first, then new, so a script's `PlayerRemoving` sees the list shrink
    // before `PlayerAdded` sees it grow.
    std::vector<InstanceId> leaving;
    for (InstanceId child = world.firstChild(network); child.valid(); child = world.nextSibling(child)) {
        const scene::PlayerComponent* player = world.players().find(child);
        if (player == nullptr || player->local || world.destroyed(child))
            continue;
        if (std::find(roster.begin(), roster.end(), player->userId) == roster.end())
            leaving.push_back(child);
    }
    for (const InstanceId gone : leaving)
        scene::removePlayer(world, network, gone);
    for (const u32 userId : roster) {
        if (userId == m_playerId || scene::playerByUserId(world, userId).valid())
            continue;
        (void)scene::createPlayer(world, network, userId, false);
    }
}

void ReplicaSession::onSpawn(scene::World& world, std::span<const u8> bytes)
{
    Reader reader(bytes);
    (void)reader.u8v();
    const u32 count = reader.u32v();
    for (u32 at = 0; at < count && reader.ok(); ++at) {
        const u32 id = reader.u32v();
        const std::string_view className = reader.text();
        if (!reader.ok() || m_locals.contains(id))
            continue;
        // An id that left this replica's interest and has come back into it.
        m_departed.erase(id);
        // **By name, not by the authority's class number**: the two ends build
        // their registries from the same generated tables, but a name is the
        // fact the protocol version vouches for and a number is not.
        const scene::ClassId classId = world.classes().findId(world.atoms().intern(className));
        if (classId == scene::InvalidClass)
            continue;
        const InstanceId local = world.create(classId);
        if (local.valid())
            m_locals[id] = local;
    }
}

void ReplicaSession::onDespawn(scene::World& world, std::span<const u8> bytes)
{
    Reader reader(bytes);
    (void)reader.u8v();
    const u32 count = reader.u32v();
    for (u32 at = 0; at < count && reader.ok(); ++at) {
        const u32 id = reader.u32v();
        if (!reader.ok())
            break;
        m_departed.insert(id);
        m_samples.erase(id);
        if (const auto found = m_locals.find(id); found != m_locals.end()) {
            if (world.alive(found->second))
                (void)world.destroy(found->second);
            m_locals.erase(found);
        }
        m_written.erase(id);
        // Out of every remembered state too, so a diff against one of them
        // reconstructs what the authority has -- which no longer includes it.
        for (std::shared_ptr<const WorldState>& state : m_states) {
            if (findEntity(*state, id) == nullptr)
                continue;
            auto copy = std::make_shared<WorldState>(*state);
            std::erase_if(copy->entities, [id](const EntityState& entity) { return entity.id.value == id; });
            state = std::move(copy);
        }
    }
}

void ReplicaSession::onSnapshot(scene::World& world, InstanceId root, std::span<const u8> bytes)
{
    Reader reader(bytes);
    (void)reader.u8v();
    const u64 tick = reader.u64v();
    const u64 baseTick = reader.u64v();
    const u64 checksum = reader.u64v();
    const u64 intentTick = reader.u64v();
    // Older than what the world already shows: a reordered straggler, and
    // applying it would move the world backwards.
    if (!reader.ok() || tick <= m_applied)
        return;

    const WorldState* base = baseTick != 0 ? stateAt(baseTick) : nullptr;
    if (baseTick != 0 && base == nullptr)
        return;

    const u16 atomCount = reader.u16v();
    for (u16 at = 0; at < atomCount && reader.ok(); ++at) {
        const u32 atom = reader.u32v();
        const std::string_view text = reader.text();
        if (reader.ok())
            m_names[atom] = world.atoms().intern(text);
    }

    auto state = std::make_shared<WorldState>();
    state->tick = tick;
    if (base != nullptr)
        state->entities = base->entities;

    const u32 recordCount = reader.u32v();
    for (u32 record = 0; record < recordCount && reader.ok(); ++record) {
        const u32 id = reader.u32v();
        const u8 schema = reader.u8v();
        const u8 flags = reader.u8v();
        const u16 fields = reader.u16v();
        if (!reader.ok() || schema >= schemaCount()) {
            reader.fail();
            break;
        }
        const generated::ClassDesc& desc = generated::Classes[schema];
        auto at = findEntity(state->entities, id);
        if (at == state->entities.end() || at->id.value != id) {
            if ((flags & FullRecord) == 0) {
                // A diff for an instance the baseline does not have: the two
                // ends disagree about the baseline, which the checksum would
                // catch anyway -- refused here, where the cause is plain.
                reader.fail();
                break;
            }
            at = state->entities.insert(at, EntityState{NetId{id}, schema, FieldSet(fieldCount(desc))});
        }
        else if ((flags & FullRecord) != 0) {
            *at = EntityState{NetId{id}, schema, FieldSet(fieldCount(desc))};
        }
        for (u16 field = 0; field < fields && reader.ok(); ++field) {
            const u16 wireId = reader.u16v();
            const usize index = indexOfWireId(desc, wireId);
            if (!reader.ok() || index >= at->fields.size()) {
                reader.fail();
                break;
            }
            FieldValue value;
            if (!decodeField(reader.bytes(), reader.at(), fieldAt(desc, index)->encoding, value)) {
                reader.fail();
                break;
            }
            at->fields[index] = value;
        }
    }
    if (!reader.ok() || !reader.done())
        return;

    std::erase_if(state->entities, [this](const EntityState& entity) { return m_departed.contains(entity.id.value); });
    if (checksumOf(*state) != checksum) {
        m_checksumFailures += 1;
        return;
    }

    m_states.push_back(state);
    while (m_states.size() > StateHistory)
        m_states.pop_front();
    m_applied = tick;
    m_stats.snapshotsReceived += 1;
    // The server's clock, as far as this replica can tell: the newest tick it
    // has heard of, advanced one a tick between snapshots (`receive`). Pulled
    // forward by a snapshot from further ahead, and snapped when it has drifted
    // more than a handful of ticks behind.
    if (tick > m_serverClock || m_serverClock - tick > 8)
        m_serverClock = tick;
    m_ackedIntent = intentTick;
    applyToWorld(world, root, *state);

    Writer ack;
    ack.u8v(static_cast<u8>(MessageType::Ack));
    ack.u64v(tick);
    sendBytes(m_transport, m_authority, ack.bytes, net::Delivery::Reliable, ControlChannel, m_stats);
}

void ReplicaSession::reconcile(scene::World& world, InstanceId character, const core::CFrameD& authority)
{
    scene::PartComponent* part = world.parts().find(character);
    if (part == nullptr)
        return;
    // What this replica predicted at the intent the authority last applied. No
    // such prediction -- the authority has applied none of this replica's
    // intents yet, or it answered one this replica no longer remembers -- is
    // nothing to reconcile against, so the authority is simply right.
    const Sample* predicted = nullptr;
    for (const Sample& sample : m_predicted) {
        if (sample.tick == m_ackedIntent)
            predicted = &sample;
    }
    if (m_ackedIntent == 0 || predicted == nullptr) {
        part->cframe = authority;
        m_predicted.clear();
        return;
    }

    // **The error at that moment, carried forward to now.** Re-simulating the
    // intents since would need the physics state then, which the physics
    // backend does not keep; shifting by the error is the correction the error
    // implies wherever the ground does not change underfoot, and the next
    // snapshot corrects what it could not.
    const core::DVec3 error = authority.position - predicted->cframe.position;
    const f64 distance = std::sqrt(error.x * error.x + error.y * error.y + error.z * error.z);
    // The turn the authority disagrees by, the same way.
    const core::Mat3 turn = authority.rotation * core::transpose(predicted->cframe.rotation);
    bool turned = false;
    for (int column = 0; column < 3 && !turned; ++column) {
        for (int row = 0; row < 3; ++row) {
            const core::f32 identity = column == row ? 1.0f : 0.0f;
            if (std::abs(turn.m[column][row] - identity) > 1e-3f) {
                turned = true;
                break;
            }
        }
    }
    // A centimetre is inside what floats and the two ends' frame timing make of
    // the same motion; correcting it every snapshot would be a visible shimmer.
    if (distance < 0.01 && !turned)
        return;
    m_stats.corrections += 1;
    const auto correct = [&](core::CFrameD& frame) {
        frame.position = frame.position + error;
        if (turned)
            frame.rotation = turn * frame.rotation;
    };
    correct(part->cframe);
    for (Sample& sample : m_predicted) {
        if (sample.tick > m_ackedIntent)
            correct(sample.cframe);
    }
}

void ReplicaSession::applyToWorld(scene::World& world, InstanceId root, const WorldState& state)
{
    const usize nameField = commonIndex("Name");
    const usize parentField = commonIndex("Parent");
    for (const EntityState& entity : state.entities) {
        const bool service = entity.id.value >= ServiceNetIdBase;
        if (service && !m_locals.contains(entity.id.value)) {
            // This world's own copy of the service, by its class name.
            const InstanceId dataModel = world.parentOf(root);
            const std::string_view wanted = generated::Classes[entity.schema].name;
            for (InstanceId child = dataModel.valid() ? world.firstChild(dataModel) : InstanceId{}; child.valid();
                 child = world.nextSibling(child)) {
                if (world.atoms().text(world.classes().find(world.classOf(child))->name) == wanted) {
                    m_locals[entity.id.value] = child;
                    break;
                }
            }
        }
        const auto local = m_locals.find(entity.id.value);
        if (local == m_locals.end() || !world.alive(local->second))
            continue; // its spawn has not arrived yet; the next apply writes it whole
        const generated::ClassDesc& desc = generated::Classes[entity.schema];
        const auto written = m_written.find(entity.id.value);
        FieldSet next = entity.fields;

        for (usize at = 0; at < entity.fields.size(); ++at) {
            const FieldValue& value = entity.fields[at];
            if (written != m_written.end() && at < written->second.size() && written->second[at] == value)
                continue;
            // A service keeps its own name and its place under the data model.
            if (service && (at == nameField || at == parentField))
                continue;
            if (at == nameField) {
                const auto name = m_names.find(asU32(value));
                if (name != m_names.end())
                    world.setName(local->second, name->second);
                continue;
            }
            if (at == parentField) {
                const u32 parentId = asNetId(value).value;
                const InstanceId parent = parentId == RootNetId.value ? root : localOf(NetId{parentId});
                if (!parent.valid()) {
                    next[at] = pendingValue();
                    continue;
                }
                if (!(world.parentOf(local->second) == parent))
                    (void)world.setParent(local->second, parent);
                continue;
            }
            const generated::FieldDesc* field = fieldAt(desc, at);
            const bool cframe = field != nullptr && field->name == "CFrame" && field->pool == "parts";
            // A name-shaped component field arrives as the authority's atom,
            // and is this machine's own atom by the time it is written.
            if (field != nullptr && field->encoding == generated::Encoding::NameAtom) {
                const auto name = m_names.find(asU32(value));
                if (name == m_names.end())
                    continue;
                FieldValue translated;
                setU32(translated, name->second.id);
                (void)applyField(world, local->second, desc, FieldDelta{wireIdAt(desc, at), translated});
                continue;
            }
            if (entity.id.value == m_owned && m_owned != 0) {
                // **This machine's own character is predicted** (ADR 0076): the
                // local scripts already moved it, so the authority's value
                // corrects it rather than replacing it. Its motion state is the
                // local simulation's for the same reason.
                if (cframe)
                    reconcile(world, local->second, asCFrame(value));
                if (field != nullptr && field->pool == "characterBodies")
                    continue;
                if (cframe)
                    continue;
            }
            else if (cframe && m_interpolationDelay > 0) {
                std::deque<Sample>& samples = m_samples[entity.id.value];
                samples.push_back(Sample{state.tick, asCFrame(value)});
                while (samples.size() > InterpolationSamples)
                    samples.pop_front();
                continue;
            }
            (void)applyField(world, local->second, desc, FieldDelta{wireIdAt(desc, at), value});
        }
        m_written[entity.id.value] = std::move(next);
    }
}

} // namespace luaug::replication
