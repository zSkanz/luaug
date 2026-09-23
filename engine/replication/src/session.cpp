#include "luaug/replication/session.h"

#include "luaug/scene/class_registry.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <string_view>

#include "wire_schema.gen.h"

namespace luaug::replication {
namespace {

using core::InstanceId;
using generated::MessageType;

// Channels by the schema's numbering. Named here once so a send cannot pick one
// by a bare digit.
constexpr u8 ControlChannel = 0;
constexpr u8 StateChannel = 1;

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

void AuthoritySession::receive()
{
    std::vector<net::TransportEvent> events;
    (void)m_transport.poll(events, 0);
    for (const net::TransportEvent& event : events) {
        switch (event.kind) {
        case net::TransportEvent::Kind::Connected:
            if (peerFor(event.peer) == nullptr) {
                m_peers.push_back(Peer{event.peer, false, 0, {}});
                std::sort(m_peers.begin(), m_peers.end(),
                          [](const Peer& a, const Peer& b) { return a.id.value < b.id.value; });
            }
            break;
        case net::TransportEvent::Kind::Disconnected:
            std::erase_if(m_peers, [&](const Peer& peer) { return peer.id == event.peer; });
            break;
        case net::TransportEvent::Kind::Message: {
            Peer* peer = peerFor(event.peer);
            if (peer == nullptr || event.channel != ControlChannel)
                break;
            m_stats.bytesReceived += event.payload.size();
            Reader reader(event.payload);
            const auto type = static_cast<MessageType>(reader.u8v());
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
                Writer welcome;
                welcome.u8v(static_cast<u8>(MessageType::Welcome));
                welcome.u32v(generated::ProtocolVersion);
                welcome.u32v(peer->id.value);
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

        std::vector<InstanceId> children;
        for (InstanceId child = world.firstChild(id); child.valid(); child = world.nextSibling(child))
            children.push_back(child);
        stack.insert(stack.end(), children.rbegin(), children.rend());
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
    for (Peer& peer : m_peers) {
        if (peer.welcomed)
            sendTo(peer, current);
    }
}

void AuthoritySession::sendTo(Peer& peer, const WorldState& current)
{
    // --- Spawns and despawns, reliable, before the snapshot that needs them.
    std::vector<u32> now;
    now.reserve(current.entities.size());
    for (const EntityState& entity : current.entities)
        now.push_back(entity.id.value);

    std::vector<u32> entering;
    std::set_difference(now.begin(), now.end(), peer.known.begin(), peer.known.end(), std::back_inserter(entering));
    std::vector<u32> leaving;
    std::set_difference(peer.known.begin(), peer.known.end(), now.begin(), now.end(), std::back_inserter(leaving));

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

    // --- The snapshot, against what this peer last proved it holds.
    const WorldState* baseline = peer.acked != 0 ? historyAt(peer.acked) : nullptr;

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
        const EntityState* before = baseline != nullptr ? findEntity(*baseline, entity.id.value) : nullptr;
        Record record{&entity, before == nullptr || before->schema != entity.schema, {}};
        for (usize at = 0; at < entity.fields.size(); ++at) {
            if (record.full || !(before->fields[at] == entity.fields[at]))
                record.fields.push_back(at);
        }
        if (record.fields.empty())
            continue;
        if (std::find(record.fields.begin(), record.fields.end(), nameField) != record.fields.end())
            atoms.insert(asU32(entity.fields[nameField]));
        records.push_back(std::move(record));
    }

    Writer snapshot;
    snapshot.u8v(static_cast<u8>(MessageType::Snapshot));
    snapshot.u64v(current.tick);
    snapshot.u64v(baseline != nullptr ? baseline->tick : 0);
    snapshot.u64v(checksumOf(current));
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
        default:
            break;
        }
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
        if (!reader.ok() || m_locals.contains(id) || m_departed.contains(id))
            continue;
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
    applyToWorld(world, root, *state);

    Writer ack;
    ack.u8v(static_cast<u8>(MessageType::Ack));
    ack.u64v(tick);
    sendBytes(m_transport, m_authority, ack.bytes, net::Delivery::Reliable, ControlChannel, m_stats);
}

void ReplicaSession::applyToWorld(scene::World& world, InstanceId root, const WorldState& state)
{
    const usize nameField = commonIndex("Name");
    const usize parentField = commonIndex("Parent");
    for (const EntityState& entity : state.entities) {
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
            (void)applyField(world, local->second, desc, FieldDelta{wireIdAt(desc, at), value});
        }
        m_written[entity.id.value] = std::move(next);
    }
}

} // namespace luaug::replication
