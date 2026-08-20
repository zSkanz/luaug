#include "luaug/script/binding.h"

#include <lua.h>
#include <lualib.h>

#include <cassert>
#include <string>

namespace luaug::script
{

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
    for (const MemberEntry& entry : table)
    {
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
    switch (tag)
    {
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

} // namespace luaug::script
