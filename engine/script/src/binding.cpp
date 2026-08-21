#include "luaug/script/binding.h"

#include <lua.h>
#include <lualib.h>

#include <cassert>
#include <string>

namespace luaug::script {

core::NameAtom VmContext::resolve(int atom) const noexcept
{
    // -1 is Luau's "not interesting", and it is also what a string interned
    // before the callback existed latches at permanently. Either way there is no
    // engine name behind it, and answering atom 0 -- the empty name -- is the
    // honest result rather than an index into whatever sits at the front.
    if (atom < 0 || static_cast<usize>(atom) >= atomToName.size())
        return {};
    return core::NameAtom{atomToName[static_cast<usize>(atom)]};
}

VmContext& context(lua_State* L) noexcept
{
    void* stored = lua_callbacks(L)->userdata;
    assert(stored != nullptr && "the VM context is installed by ScriptRuntime::boot");
    return *static_cast<VmContext*>(stored);
}

const MemberEntry* findMember(const MemberTable& table, core::NameAtom name) noexcept
{
    if (!name.valid())
        return nullptr;
    for (const MemberEntry& entry : table) {
        if (entry.name == name)
            return &entry;
    }
    return nullptr;
}

void raise(lua_State* L, core::TextKey key, std::span<const core::I18nArg> args)
{
    const std::string message = core::formatKeyPrefixed(key, args);
    // Through "%s" rather than as the format string itself: a catalog entry is
    // translated text and may perfectly well contain a percent sign, and
    // `luaL_error` would read it as a conversion and walk off the argument list.
    luaL_error(L, "%s", message.c_str());
}

const char* typeName(UserdataTag tag) noexcept
{
    switch (tag) {
    case UserdataTag::Instance:
        return "Instance";
    case UserdataTag::CFrame:
        return "CFrame";
    case UserdataTag::Color3:
        return "Color3";
    case UserdataTag::Random:
        return "Random";
    case UserdataTag::Signal:
        return "Signal";
    case UserdataTag::Connection:
        return "Connection";
    case UserdataTag::EnumItem:
        return "EnumItem";
    case UserdataTag::RaycastParams:
        return "RaycastParams";
    case UserdataTag::RaycastResult:
        return "RaycastResult";
    case UserdataTag::Vector2:
        return "Vector2";
    case UserdataTag::UDim:
        return "UDim";
    case UserdataTag::UDim2:
        return "UDim2";
    case UserdataTag::Rect:
        return "Rect";
    case UserdataTag::TweenInfo:
        return "TweenInfo";
    case UserdataTag::Tween:
        return "Tween";
    case UserdataTag::Enum:
        return "Enum";
    case UserdataTag::Enums:
        // The plural is the one people forget, and it is deliberate: the global
        // `Enum` is the collection, an enum object is one of its members
        // (api-design.md §2.3).
        return "Enums";
    case UserdataTag::Reserved:
    case UserdataTag::Count:
        break;
    }
    return "userdata";
}

void raiseUnknownMember(lua_State* L, UserdataTag tag, const char* member)
{
    const core::I18nArg args[] = {
        {"typeName", std::string_view{typeName(tag)}},
        {"member", std::string_view{member == nullptr ? "" : member}},
    };
    raise(L, LUAUG_TR("script.err.unknown_member"), args);
}

namespace {

// The tag rides along as an upvalue rather than as a template parameter, so
// there is one copy of each dispatch function and not one per type: what differs
// between the datatypes is the member table each scans, and that is keyed by tag.
[[nodiscard]] UserdataTag closureTag(lua_State* L)
{
    return static_cast<UserdataTag>(lua_tointeger(L, lua_upvalueindex(1)));
}

int memberIndex(lua_State* L)
{
    const UserdataTag tag = closureTag(L);

    int atom = -1;
    // No number-to-string coercion, so it never allocates and never invalidates
    // the stack -- which is what makes it safe in a generic C `__index`
    // (`lapi.cpp:516`). A non-string key returns null and falls through to the
    // unknown-member raise, which is the right answer for `cf[1]`.
    const char* key = lua_tostringatom(L, 2, &atom);
    if (key != nullptr) {
        const VmContext& ctx = context(L);
        const core::NameAtom name = ctx.resolve(atom);
        if (const MemberEntry* entry = findMember(ctx.getters[static_cast<usize>(tag)], name))
            return entry->fn(L);

        // A method reached without calling it -- `signal.Fire`. It allocates a
        // closure per access, which is why `__namecall` exists and why
        // `signal:Fire()` never comes through here. The Instance binding does
        // the same thing for the same reason.
        if (const MemberEntry* entry = findMember(ctx.methods[static_cast<usize>(tag)], name)) {
            lua_pushcfunction(L, entry->fn, typeName(tag));
            return 1;
        }
    }

    raiseUnknownMember(L, tag, key);
}

int memberNamecall(lua_State* L)
{
    const UserdataTag tag = closureTag(L);

    int atom = -1;
    const char* method = lua_namecallatom(L, &atom);
    if (method != nullptr) {
        const VmContext& ctx = context(L);
        if (const MemberEntry* entry = findMember(ctx.methods[static_cast<usize>(tag)], ctx.resolve(atom)))
            return entry->fn(L);
    }

    raiseUnknownMember(L, tag, method);
}

int refuseWrite(lua_State* L)
{
    // Every value type in the v1 surface is immutable: `cf.Position = v` is not
    // a slow way of moving a part, it is a mistake, and the value it would write
    // to is a copy the caller is about to drop.
    int atom = -1;
    const char* key = lua_tostringatom(L, 2, &atom);
    raiseUnknownMember(L, closureTag(L), key);
}

void setDispatch(lua_State* L, const char* event, lua_CFunction fn, UserdataTag tag)
{
    lua_pushinteger(L, static_cast<int>(tag));
    lua_pushcclosure(L, fn, event, 1);
    lua_setfield(L, -2, event);
}

} // namespace

void beginTagMetatable(lua_State* L, UserdataTag tag)
{
    lua_createtable(L, 0, 8);
    lua_pushstring(L, typeName(tag));
    lua_setfield(L, -2, "__type");

    lua_pushvalue(L, -1);
    lua_setuserdatametatable(L, static_cast<int>(tag));

    setDispatch(L, "__index", memberIndex, tag);
    setDispatch(L, "__newindex", refuseWrite, tag);
    // Named `__namecall` on purpose: `laux.cpp:42` special-cases exactly this
    // debug name so an argument error reports the *method* rather than the
    // metamethod. It is free, and it is the difference between a usable message
    // and a confusing one.
    setDispatch(L, "__namecall", memberNamecall, tag);
}

void endTagMetatable(lua_State* L)
{
    lua_setreadonly(L, -1, true);
    lua_pop(L, 1);
}

void addMember(MemberTable& table, core::AtomTable& atoms, const char* name, lua_CFunction fn)
{
    table.push_back(MemberEntry{atoms.intern(name), fn});
}

void installTagMetatable(lua_State* L, UserdataTag tag, lua_CFunction equals, lua_CFunction tostring)
{
    beginTagMetatable(L, tag);
    if (equals != nullptr) {
        lua_pushcfunction(L, equals, "__eq");
        lua_setfield(L, -2, "__eq");
    }
    if (tostring != nullptr) {
        lua_pushcfunction(L, tostring, "__tostring");
        lua_setfield(L, -2, "__tostring");
    }
    endTagMetatable(L);
}

} // namespace luaug::script
