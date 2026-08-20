#include "luaug/script/reload_state.h"

#include <lua.h>
#include <lualib.h>

#include <cstring>
#include <type_traits>

namespace luaug::script {
namespace {

// Deep enough for any configuration a game keeps across a save, shallow enough
// that a pathological table cannot walk the C stack into the ground. A cycle is
// caught by the path set below; this catches the merely absurd.
constexpr int kMaxDepth = 32;

struct Conversion
{
    lua_State* L = nullptr;
    std::string* reason = nullptr;
    // Tables on the current path, by identity. A vector rather than a set: the
    // depth cap is 32, and a linear scan of at most 32 pointers beats a hash.
    std::vector<const void*> path;

    [[nodiscard]] bool onPath(const void* table) const
    {
        for (const void* seen : path) {
            if (seen == table)
                return true;
        }
        return false;
    }
};

[[nodiscard]] std::optional<BagValue> convert(Conversion& state, int index, int depth);

[[nodiscard]] std::optional<BagValue> convertTable(Conversion& state, int index, int depth)
{
    lua_State* L = state.L;

    const void* identity = lua_topointer(L, index);
    if (state.onPath(identity)) {
        *state.reason = "the table refers to itself";
        return std::nullopt;
    }
    state.path.push_back(identity);
    const struct PathScope
    {
        Conversion& owner;
        ~PathScope() { owner.path.pop_back(); }
    } pathScope{state};

    // The array form is the exact run 1..n and nothing else. A table that is
    // partly an array is neither shape, and guessing which half to keep would
    // lose the other half silently.
    const int length = lua_objlen(L, index);

    BagValue::Array array;
    BagValue::Map map;
    bool sawStringKey = false;
    core::i64 integerKeys = 0;

    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        const int valueIndex = lua_gettop(L);
        const int keyIndex = valueIndex - 1;

        // Checked before any conversion: `lua_tolstring` on a number key
        // rewrites it in place, and `lua_next` would then resume from a key
        // that is no longer in the table.
        const int keyType = lua_type(L, keyIndex);

        if (keyType == LUA_TNUMBER) {
            const double number = lua_tonumber(L, keyIndex);
            const auto whole = static_cast<core::i64>(number);
            if (static_cast<double>(whole) != number || whole < 1 || whole > length) {
                *state.reason = "a table key is a number outside the array run 1..n";
                lua_pop(L, 2);
                return std::nullopt;
            }
            ++integerKeys;
        }
        else if (keyType == LUA_TSTRING) {
            sawStringKey = true;
        }
        else {
            *state.reason = "a table key is neither a string nor an array index";
            lua_pop(L, 2);
            return std::nullopt;
        }

        std::optional<BagValue> converted = convert(state, valueIndex, depth + 1);
        if (!converted.has_value()) {
            lua_pop(L, 2);
            return std::nullopt;
        }

        if (keyType == LUA_TSTRING) {
            usize keyLength = 0;
            const char* text = lua_tolstring(L, keyIndex, &keyLength);
            map.emplace_back(std::string(text, keyLength), std::move(*converted));
        }
        else {
            const auto slot = static_cast<usize>(lua_tointeger(L, keyIndex));
            if (array.size() < slot)
                array.resize(slot);
            array[slot - 1] = std::move(*converted);
        }

        lua_pop(L, 1);
    }

    if (sawStringKey && integerKeys != 0) {
        *state.reason = "the table mixes array entries and named keys";
        return std::nullopt;
    }

    if (sawStringKey)
        return BagValue(BagValue::Storage(std::move(map)));

    // An empty table is an empty array. It has to be *something*, and a script
    // that saved `{}` and read back a map would find `next` answering nil
    // either way -- so the cheaper of the two identical answers wins.
    return BagValue(BagValue::Storage(std::move(array)));
}

std::optional<BagValue> convert(Conversion& state, int index, int depth)
{
    if (depth > kMaxDepth) {
        *state.reason = "the value nests deeper than the engine will copy";
        return std::nullopt;
    }

    lua_State* L = state.L;
    switch (lua_type(L, index)) {
    case LUA_TNIL:
        return BagValue{};
    case LUA_TBOOLEAN:
        return BagValue(BagValue::Storage(lua_toboolean(L, index) != 0));
    case LUA_TNUMBER:
        return BagValue(BagValue::Storage(static_cast<f64>(lua_tonumber(L, index))));
    case LUA_TSTRING: {
        usize length = 0;
        const char* text = lua_tolstring(L, index, &length);
        return BagValue(BagValue::Storage(std::string(text, length)));
    }
    case LUA_TBUFFER: {
        usize length = 0;
        const void* bytes = lua_tobuffer(L, index, &length);
        const auto* first = static_cast<const u8*>(bytes);
        return BagValue(BagValue::Storage(BagValue::Bytes(first, first + length)));
    }
    case LUA_TTABLE:
        return convertTable(state, index, depth);
    default:
        break;
    }

    // Everything left is a handle into something the reload is about to
    // destroy: a closure over this VM's upvalues, a coroutine belonging to this
    // scheduler, an Instance in the world being rebuilt. None of them can be
    // copied forward under any interpretation, so none is quietly dropped.
    *state.reason = luaL_typename(L, index);
    return std::nullopt;
}

} // namespace

void ReloadState::save(std::string_view key, BagValue value)
{
    for (auto& entry : m_entries) {
        if (entry.first == key) {
            entry.second = std::move(value);
            return;
        }
    }
    m_entries.emplace_back(std::string(key), std::move(value));
}

const BagValue* ReloadState::load(std::string_view key) const
{
    for (const auto& entry : m_entries) {
        if (entry.first == key)
            return &entry.second;
    }
    return nullptr;
}

std::optional<BagValue> toBagValue(lua_State* L, int index, std::string& reason)
{
    Conversion state;
    state.L = L;
    state.reason = &reason;
    return convert(state, index, 0);
}

void pushBagValue(lua_State* L, const BagValue& value)
{
    std::visit(
        [L](const auto& held) {
            using Held = std::decay_t<decltype(held)>;
            if constexpr (std::is_same_v<Held, std::monostate>) {
                lua_pushnil(L);
            }
            else if constexpr (std::is_same_v<Held, bool>) {
                lua_pushboolean(L, held ? 1 : 0);
            }
            else if constexpr (std::is_same_v<Held, f64>) {
                lua_pushnumber(L, held);
            }
            else if constexpr (std::is_same_v<Held, std::string>) {
                lua_pushlstring(L, held.data(), held.size());
            }
            else if constexpr (std::is_same_v<Held, BagValue::Bytes>) {
                void* target = lua_newbuffer(L, held.size());
                if (!held.empty())
                    std::memcpy(target, held.data(), held.size());
            }
            else if constexpr (std::is_same_v<Held, BagValue::Array>) {
                lua_createtable(L, static_cast<int>(held.size()), 0);
                for (usize i = 0; i < held.size(); ++i) {
                    pushBagValue(L, held[i]);
                    lua_rawseti(L, -2, static_cast<int>(i) + 1);
                }
            }
            else {
                lua_createtable(L, 0, static_cast<int>(held.size()));
                for (const auto& entry : held) {
                    // rawset with an explicit length rather than `lua_setfield`:
                    // a Luau string may contain a NUL, and a C string would
                    // truncate the key at it.
                    lua_pushlstring(L, entry.first.data(), entry.first.size());
                    pushBagValue(L, entry.second);
                    lua_rawset(L, -3);
                }
            }
        },
        value.storage());
}

} // namespace luaug::script
