#include "luaug/script/remote.h"

#include "luaug/core/i18n.h"
#include "luaug/scene/players.h"
#include "luaug/scene/world.h"
#include "luaug/script/binding.h"
#include "luaug/script/signals.h"

#include <lua.h>
#include <lualib.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <string_view>
#include <utility>

namespace luaug::script {
namespace {

using core::u32;
using core::u8;
using core::usize;
using scene::World;

// One tag per kind of value. **Permanent numbers**: a payload is bytes on the
// wire between two builds, and a renumbered tag is a value read as another.
enum class Tag : u8
{
    Nil = 0,
    False = 1,
    True = 2,
    Number = 3,
    String = 4,
    Vector = 5,
    Instance = 6,
    Table = 7,
};

[[nodiscard]] World& world(lua_State* L) noexcept
{
    return *context(L).world;
}

void putU32(std::vector<u8>& out, u32 value)
{
    for (int at = 0; at < 4; ++at)
        out.push_back(static_cast<u8>(value >> (8 * at)));
}

struct Encoder
{
    lua_State* L;
    std::vector<u8>& out;
    std::vector<core::InstanceId>& refs;

    void checkSize() const
    {
        if (out.size() > MaxRemotePayload) {
            const core::I18nArg args[] = {{"limit", static_cast<core::i64>(MaxRemotePayload)}};
            raise(L, LUAUG_TR("net.err.remote_too_large"), args);
        }
    }

    void value(int index, int depth, bool key)
    {
        switch (lua_type(L, index)) {
        case LUA_TNIL:
            out.push_back(static_cast<u8>(Tag::Nil));
            break;
        case LUA_TBOOLEAN:
            out.push_back(static_cast<u8>(lua_toboolean(L, index) != 0 ? Tag::True : Tag::False));
            break;
        case LUA_TNUMBER: {
            out.push_back(static_cast<u8>(Tag::Number));
            const auto bits = std::bit_cast<core::u64>(static_cast<double>(lua_tonumber(L, index)));
            for (int at = 0; at < 8; ++at)
                out.push_back(static_cast<u8>(bits >> (8 * at)));
            break;
        }
        case LUA_TSTRING: {
            usize length = 0;
            const char* text = lua_tolstring(L, index, &length);
            if (length > MaxRemotePayload) {
                const core::I18nArg args[] = {{"limit", static_cast<core::i64>(MaxRemotePayload)}};
                raise(L, LUAUG_TR("net.err.remote_too_large"), args);
            }
            out.push_back(static_cast<u8>(Tag::String));
            putU32(out, static_cast<u32>(length));
            out.insert(out.end(), reinterpret_cast<const u8*>(text), reinterpret_cast<const u8*>(text) + length);
            break;
        }
        case LUA_TVECTOR: {
            if (key)
                refuseKey(index);
            const float* vector = lua_tovector(L, index);
            out.push_back(static_cast<u8>(Tag::Vector));
            for (int axis = 0; axis < 3; ++axis)
                putU32(out, std::bit_cast<u32>(vector[axis]));
            break;
        }
        case LUA_TUSERDATA: {
            const core::InstanceId* instance = toInstance(L, index);
            if (instance == nullptr)
                refuse(index);
            if (key)
                refuseKey(index);
            out.push_back(static_cast<u8>(Tag::Instance));
            putU32(out, static_cast<u32>(refs.size()));
            refs.push_back(*instance);
            break;
        }
        case LUA_TTABLE: {
            if (key)
                refuseKey(index);
            // Past the depth, and a table containing itself arrives here too:
            // walking one would never end.
            if (depth >= MaxRemoteDepth) {
                const core::I18nArg args[] = {{"depth", static_cast<core::i64>(MaxRemoteDepth)}};
                raise(L, LUAUG_TR("net.err.remote_nested"), args);
            }
            out.push_back(static_cast<u8>(Tag::Table));
            const usize countAt = out.size();
            putU32(out, 0);
            u32 pairs = 0;
            const int table = lua_absindex(L, index);
            luaL_checkstack(L, 3, "RemoteEvent");
            lua_pushnil(L);
            while (lua_next(L, table) != 0) {
                value(-2, depth + 1, true);
                value(-1, depth + 1, false);
                lua_pop(L, 1);
                ++pairs;
            }
            for (int at = 0; at < 4; ++at)
                out[countAt + static_cast<usize>(at)] = static_cast<u8>(pairs >> (8 * at));
            break;
        }
        default:
            refuse(index);
        }
        checkSize();
    }

    [[noreturn]] void refuse(int index) const
    {
        const core::I18nArg args[] = {{"type", std::string_view{luaL_typename(L, index)}}};
        raise(L, LUAUG_TR("net.err.remote_value"), args);
    }

    [[noreturn]] void refuseKey(int index) const
    {
        const core::I18nArg args[] = {{"type", std::string_view{luaL_typename(L, index)}}};
        raise(L, LUAUG_TR("net.err.remote_key"), args);
    }
};

struct Decoder
{
    lua_State* L;
    std::span<const u8> bytes;
    std::span<const core::InstanceId> refs;
    usize at = 0;

    [[nodiscard]] bool read(usize width, core::u64& value)
    {
        if (bytes.size() - at < width)
            return false;
        value = 0;
        for (usize byte = 0; byte < width; ++byte)
            value |= static_cast<core::u64>(bytes[at + byte]) << (8 * byte);
        at += width;
        return true;
    }

    // Pushes one value; false, with the stack as it was, on anything malformed.
    [[nodiscard]] bool value(int depth)
    {
        core::u64 tag = 0;
        if (depth > MaxRemoteDepth || !read(1, tag) || !lua_checkstack(L, 3))
            return false;
        switch (static_cast<Tag>(tag)) {
        case Tag::Nil:
            lua_pushnil(L);
            return true;
        case Tag::False:
        case Tag::True:
            lua_pushboolean(L, static_cast<Tag>(tag) == Tag::True ? 1 : 0);
            return true;
        case Tag::Number: {
            core::u64 bits = 0;
            if (!read(8, bits))
                return false;
            lua_pushnumber(L, std::bit_cast<double>(bits));
            return true;
        }
        case Tag::String: {
            core::u64 length = 0;
            if (!read(4, length) || bytes.size() - at < length)
                return false;
            lua_pushlstring(L, reinterpret_cast<const char*>(bytes.data() + at), static_cast<usize>(length));
            at += static_cast<usize>(length);
            return true;
        }
        case Tag::Vector: {
            std::array<float, 3> axes{};
            for (float& axis : axes) {
                core::u64 bits = 0;
                if (!read(4, bits))
                    return false;
                axis = std::bit_cast<float>(static_cast<u32>(bits));
            }
            lua_pushvector(L, axes[0], axes[1], axes[2]);
            return true;
        }
        case Tag::Instance: {
            core::u64 index = 0;
            if (!read(4, index) || index >= refs.size())
                return false;
            // An instance the receiver does not have arrives as nil: it was
            // out of this machine's interest, or never replicated.
            pushInstance(L, refs[static_cast<usize>(index)]);
            return true;
        }
        case Tag::Table: {
            core::u64 pairs = 0;
            // Each pair is at least two bytes, which bounds a count a mangled
            // payload claims before anything is allocated for it.
            if (!read(4, pairs) || pairs > (bytes.size() - at) / 2)
                return false;
            const int top = lua_gettop(L);
            lua_createtable(L, 0, 0);
            for (core::u64 pair = 0; pair < pairs; ++pair) {
                if (!value(depth + 1) || !value(depth + 1)) {
                    lua_settop(L, top);
                    return false;
                }
                // A key that arrived as nil -- an instance the receiver does
                // not have -- has nowhere to go.
                if (lua_isnil(L, -2)) {
                    lua_pop(L, 2);
                    continue;
                }
                lua_rawset(L, -3);
            }
            return true;
        }
        }
        return false;
    }
};

// Who this machine is, as far as a message is concerned.
[[nodiscard]] bool onReplica(const World& w) noexcept
{
    return w.engineState().networkTopology == scene::NetworkTopology::Replica;
}

[[nodiscard]] bool networked(const World& w) noexcept
{
    return w.engineState().networkTopology != scene::NetworkTopology::Solo;
}

[[noreturn]] void refuseOnReplica(lua_State* L, std::string_view method)
{
    const core::I18nArg args[] = {{"method", method}};
    raise(L, LUAUG_TR("net.err.remote_authority_only"), args);
}

int remoteFireServer(lua_State* L)
{
    const core::InstanceId remote = checkInstance(L, 1);
    World& w = world(L);
    scene::RemoteMessage message;
    message.remote = remote;
    message.toServer = true;
    encodeRemoteArguments(L, 2, lua_gettop(L) - 1, message.payload, message.refs);
    if (onReplica(w)) {
        w.engineState().remoteOutbox.push_back(std::move(message));
        return 0;
    }
    // **The authority is its own server**: solo or hosting, its player's
    // message is delivered here, the way a replica's would be.
    const core::InstanceId local = scene::localPlayerOf(w);
    if (!local.valid())
        raise(L, LUAUG_TR("net.err.remote_no_player"));
    message.player = local;
    w.engineState().remoteInbox.push_back(std::move(message));
    return 0;
}

int remoteFireClient(lua_State* L)
{
    const core::InstanceId remote = checkInstance(L, 1);
    World& w = world(L);
    if (onReplica(w))
        refuseOnReplica(L, "FireClient");
    const core::InstanceId player = checkInstance(L, 2);
    const scene::PlayerComponent* who = w.players().find(player);
    if (who == nullptr)
        raise(L, LUAUG_TR("net.err.remote_not_player"));
    scene::RemoteMessage message;
    message.remote = remote;
    encodeRemoteArguments(L, 3, lua_gettop(L) - 2, message.payload, message.refs);
    if (who->local) {
        w.engineState().remoteInbox.push_back(std::move(message));
        return 0;
    }
    if (networked(w)) {
        message.userId = who->userId;
        w.engineState().remoteOutbox.push_back(std::move(message));
    }
    return 0;
}

int remoteFireAllClients(lua_State* L)
{
    const core::InstanceId remote = checkInstance(L, 1);
    World& w = world(L);
    if (onReplica(w))
        refuseOnReplica(L, "FireAllClients");
    scene::RemoteMessage message;
    message.remote = remote;
    encodeRemoteArguments(L, 2, lua_gettop(L) - 1, message.payload, message.refs);
    // A host's own player is a client too; a dedicated server has none.
    if (scene::localPlayerOf(w).valid())
        w.engineState().remoteInbox.push_back(message);
    if (networked(w))
        w.engineState().remoteOutbox.push_back(std::move(message));
    return 0;
}

constexpr InstanceMethodBinding RemoteMethods[] = {
    {"RemoteEvent", "FireServer", remoteFireServer},
    {"RemoteEvent", "FireClient", remoteFireClient},
    {"RemoteEvent", "FireAllClients", remoteFireAllClients},
};

} // namespace

void encodeRemoteArguments(lua_State* L, int first, int count, std::vector<u8>& payload,
                           std::vector<core::InstanceId>& refs)
{
    if (count > MaxRemoteArguments) {
        const core::I18nArg args[] = {{"count", static_cast<core::i64>(count)},
                                      {"limit", static_cast<core::i64>(MaxRemoteArguments)}};
        raise(L, LUAUG_TR("net.err.remote_too_many"), args);
    }
    payload.clear();
    refs.clear();
    payload.push_back(static_cast<u8>(std::max(count, 0)));
    Encoder encoder{L, payload, refs};
    for (int at = 0; at < count; ++at)
        encoder.value(first + at, 0, false);
}

int decodeRemoteArguments(lua_State* L, std::span<const u8> payload, std::span<const core::InstanceId> refs)
{
    if (payload.empty())
        return -1;
    const int top = lua_gettop(L);
    const int count = payload[0];
    Decoder decoder{L, payload, refs, 1};
    for (int at = 0; at < count; ++at) {
        if (!decoder.value(0)) {
            lua_settop(L, top);
            return -1;
        }
    }
    // Trailing bytes are a payload somebody else wrote; nothing here did.
    if (decoder.at != payload.size()) {
        lua_settop(L, top);
        return -1;
    }
    return count;
}

void fireRemoteMessages(lua_State* L)
{
    World& w = world(L);
    std::vector<scene::RemoteMessage> inbox;
    inbox.swap(w.engineState().remoteInbox);
    if (inbox.empty())
        return;
    const core::NameAtom serverEvent = w.atoms().intern("ServerReceived");
    const core::NameAtom clientEvent = w.atoms().intern("ClientReceived");
    for (const scene::RemoteMessage& message : inbox) {
        if (!w.alive(message.remote))
            continue;
        const scene::EventDesc* event =
            w.classes().findEvent(w.classOf(message.remote), message.toServer ? serverEvent : clientEvent);
        if (event == nullptr)
            continue;
        const int top = lua_gettop(L);
        int count = 0;
        if (message.toServer) {
            pushInstance(L, message.player);
            count = 1;
        }
        const int decoded = decodeRemoteArguments(L, message.payload, message.refs);
        if (decoded < 0) {
            lua_settop(L, top);
            continue;
        }
        fireInstanceEvent(L, message.remote, event->slot, top + 1, count + decoded);
        lua_settop(L, top);
    }
}

std::span<const InstanceMethodBinding> remoteMethodBindings() noexcept
{
    return RemoteMethods;
}

} // namespace luaug::script
