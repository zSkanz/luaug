#include "luaug/script/instance_binding.h"

#include <lua.h>
#include <lualib.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include "luaug/scene/world.h"
#include "luaug/script/datatypes.h"

namespace luaug::script
{
namespace
{

using scene::ClassId;
using scene::World;

[[nodiscard]] World& world(lua_State* L) noexcept
{
    return *context(L).world;
}

// --- Identity ----------------------------------------------------------------

[[nodiscard]] std::string_view className(lua_State* L, core::InstanceId id)
{
    const World& w = world(L);
    const scene::ClassDescriptor* descriptor = w.classes().find(w.classOf(id));
    // A view into the atom table, which is append-only and outlives the VM, so
    // it stays good for as long as an error message needs it.
    return descriptor == nullptr ? std::string_view{"Instance"} : w.atoms().text(descriptor->name);
}

[[noreturn]] void raiseDead(lua_State* L)
{
    raise(L, LUAUG_TR("script.err.instance_dead"));
}

[[nodiscard]] core::InstanceId liveInstance(lua_State* L, int index)
{
    const core::InstanceId* id = toInstance(L, index);
    if (id == nullptr)
    {
        // `luaL_checkudatatagged` produces the argument error naming the type it
        // wanted, which reads better than anything assembled here would.
        luaL_checkudatatagged(L, index, static_cast<int>(UserdataTag::Instance));
    }
    if (id == nullptr || !world(L).alive(*id))
        raiseDead(L);
    return *id;
}

// --- Property dispatch -------------------------------------------------------

[[noreturn]] void raiseUnknownInstanceMember(lua_State* L, core::InstanceId id, const char* member)
{
    const core::I18nArg args[] = {
        {"className", className(L, id)},
        {"member", std::string_view{member == nullptr ? "" : member}},
    };
    raise(L, LUAUG_TR("scene.err.unknown_member"), args);
}

[[noreturn]] void raisePropertyError(
    lua_State* L, core::TextKey key, core::InstanceId id, const scene::PropertyDesc& property)
{
    const World& w = world(L);
    const core::I18nArg args[] = {
        {"className", className(L, id)},
        {"property", w.atoms().text(property.name)},
    };
    raise(L, key, args);
}

int instanceIndex(lua_State* L)
{
    const core::InstanceId id = liveInstance(L, 1);

    int atom = -1;
    const char* key = lua_tostringatom(L, 2, &atom);
    if (key == nullptr)
        raiseUnknownInstanceMember(L, id, key);

    World& w = world(L);
    const core::NameAtom name = context(L).resolve(atom);
    const ClassId classId = w.classOf(id);

    if (const scene::PropertyDesc* property = w.classes().findProperty(classId, name))
    {
        if (property->get == nullptr)
            raisePropertyError(L, LUAUG_TR("script.err.not_implemented"), id, *property);
        pushValue(L, property->get(w, id));
        return 1;
    }

    // A method reached without calling it -- `local f = part.Destroy`. It
    // allocates a closure per access, which is why `__namecall` exists and why
    // `part:Destroy()` never comes through here.
    if (const scene::MethodDesc* method = w.classes().findMethod(classId, name))
    {
        const auto& implementations = context(L).instanceMethods;
        const auto found = implementations.find(method);
        if (found == implementations.end())
        {
            const core::I18nArg args[] = {
                {"className", className(L, id)},
                {"property", w.atoms().text(method->name)},
            };
            raise(L, LUAUG_TR("script.err.not_implemented"), args);
        }
        // The debug name comes from the ATOM TABLE, not from `key`. Luau stores
        // it as a raw `const char*` unless `LuauManagedDebugNames` is on
        // (`lapi.cpp:792-795`), and `key` points into an interned Luau string
        // that the collector may free while the closure outlives it. The atom
        // table is append-only and outlives the VM, so its text is the one
        // pointer here with the right lifetime.
        lua_pushcfunction(L, found->second, w.atoms().text(method->name).data());
        return 1;
    }

    if (const scene::EventDesc* event = w.classes().findEvent(classId, name))
    {
        // Declared and real; the signal objects that carry it arrive with the
        // drain. A distinct key rather than "no such member", because a script
        // that gets `unknown_member` for `ChildAdded` would be told something
        // false about the API.
        const core::I18nArg args[] = {
            {"className", className(L, id)},
            {"property", w.atoms().text(event->name)},
        };
        raise(L, LUAUG_TR("script.err.not_implemented"), args);
    }

    raiseUnknownInstanceMember(L, id, key);
}

int instanceNewIndex(lua_State* L)
{
    const core::InstanceId id = liveInstance(L, 1);

    int atom = -1;
    const char* key = lua_tostringatom(L, 2, &atom);
    if (key == nullptr)
        raiseUnknownInstanceMember(L, id, key);

    World& w = world(L);
    const core::NameAtom name = context(L).resolve(atom);
    const ClassId classId = w.classOf(id);

    const scene::PropertyDesc* property = w.classes().findProperty(classId, name);
    if (property == nullptr)
        raiseUnknownInstanceMember(L, id, key);
    if (property->readOnly || property->set == nullptr)
        raisePropertyError(L, LUAUG_TR("scene.err.read_only_property"), id, *property);

    const std::optional<scene::Value> value = toValue(L, 3, property->type);
    if (!value.has_value())
        raisePropertyError(L, property->errKeyOnInvalidSet, id, *property);

    // `Parent` goes through `World::setParent` directly rather than through the
    // generic setter, because a cycle and a destroyed instance are two different
    // refusals with two different keys and the accessor collapses both to
    // `false` (native_accessors.cpp says so at the collapse).
    if (name == context(L).wellKnown.parent && property->type == scene::ValueType::Instance)
    {
        core::InstanceId target;
        if (const auto* reference = std::get_if<core::InstanceId>(&value.value()))
            target = *reference;

        if (const std::optional<core::TextKey> refusal = w.setParent(id, target))
        {
            const core::I18nArg args[] = {{"instance", w.atoms().text(w.name(id))}};
            raise(L, *refusal, args);
        }
        return 0;
    }

    switch (w.setProperty(id, name, *value))
    {
    case World::SetResult::Changed:
    case World::SetResult::Unchanged:
        return 0;
    case World::SetResult::ReadOnly:
        raisePropertyError(L, LUAUG_TR("scene.err.read_only_property"), id, *property);
    case World::SetResult::InvalidValue:
        raisePropertyError(L, property->errKeyOnInvalidSet, id, *property);
    case World::SetResult::UnknownProperty:
        break;
    }
    raiseUnknownInstanceMember(L, id, key);
}

int instanceNamecall(lua_State* L)
{
    const core::InstanceId id = liveInstance(L, 1);

    int atom = -1;
    const char* method = lua_namecallatom(L, &atom);
    if (method == nullptr)
        raiseUnknownInstanceMember(L, id, method);

    World& w = world(L);
    const scene::MethodDesc* descriptor = w.classes().findMethod(w.classOf(id), context(L).resolve(atom));
    if (descriptor == nullptr)
        raiseUnknownInstanceMember(L, id, method);

    const auto& implementations = context(L).instanceMethods;
    const auto found = implementations.find(descriptor);
    if (found == implementations.end())
    {
        const core::I18nArg args[] = {
            {"className", className(L, id)},
            {"property", w.atoms().text(descriptor->name)},
        };
        raise(L, LUAUG_TR("script.err.not_implemented"), args);
    }
    return found->second(L);
}

int instanceEq(lua_State* L)
{
    const core::InstanceId* a = toInstance(L, 1);
    const core::InstanceId* b = toInstance(L, 2);
    // The generation is what makes this safe across slot reuse: a handle to a
    // destroyed instance compares unequal to a handle to whatever moved into its
    // slot, rather than silently aliasing it.
    lua_pushboolean(L, a != nullptr && b != nullptr && *a == *b);
    return 1;
}

int instanceTostring(lua_State* L)
{
    const core::InstanceId* id = toInstance(L, 1);
    if (id == nullptr || !world(L).alive(*id))
    {
        // Deliberately does NOT raise. `tostring` is what a log line and a
        // debugger call, and a print that throws where a handle happens to be
        // dead is worse than a print that says so.
        lua_pushstring(L, "Instance");
        return 1;
    }
    const std::string_view text = world(L).atoms().text(world(L).name(*id));
    lua_pushlstring(L, text.data(), text.size());
    return 1;
}

// --- Instance methods --------------------------------------------------------

[[nodiscard]] core::NameAtom lookupAtom(lua_State* L, int index)
{
    size_t length = 0;
    const char* text = luaL_checklstring(L, index, &length);
    // `lookup` rather than `intern`: a query for a name nothing in the world has
    // ever carried is a normal answer, and interning it would let a loop calling
    // `FindFirstChild` on random strings grow the table without bound.
    return world(L).atoms().lookup(std::string_view{text, length});
}

[[nodiscard]] ClassId lookupClass(lua_State* L, int index)
{
    return world(L).classes().findId(lookupAtom(L, index));
}

void pushInstanceArray(lua_State* L, const std::vector<core::InstanceId>& ids)
{
    lua_createtable(L, static_cast<int>(ids.size()), 0);
    for (usize index = 0; index < ids.size(); ++index)
    {
        pushInstance(L, ids[index]);
        lua_rawseti(L, -2, static_cast<int>(index) + 1);
    }
}

int methodFindFirstChild(lua_State* L)
{
    pushInstance(L, world(L).findFirstChild(liveInstance(L, 1), lookupAtom(L, 2)));
    return 1;
}

int methodFindFirstChildOfClass(lua_State* L)
{
    pushInstance(L, world(L).findFirstChildOfClass(liveInstance(L, 1), lookupClass(L, 2)));
    return 1;
}

int methodFindFirstChildWhichIsA(lua_State* L)
{
    pushInstance(L, world(L).findFirstChildWhichIsA(liveInstance(L, 1), lookupClass(L, 2)));
    return 1;
}

int methodFindFirstAncestor(lua_State* L)
{
    pushInstance(L, world(L).findFirstAncestor(liveInstance(L, 1), lookupAtom(L, 2)));
    return 1;
}

int methodFindFirstAncestorOfClass(lua_State* L)
{
    pushInstance(L, world(L).findFirstAncestorOfClass(liveInstance(L, 1), lookupClass(L, 2)));
    return 1;
}

int methodGetChildren(lua_State* L)
{
    std::vector<core::InstanceId> children;
    world(L).collectChildren(liveInstance(L, 1), children);
    // A fresh array the caller owns, which is what makes destroying while
    // iterating safe (api-design.md §3.1).
    pushInstanceArray(L, children);
    return 1;
}

int methodGetDescendants(lua_State* L)
{
    std::vector<core::InstanceId> descendants;
    world(L).collectDescendants(liveInstance(L, 1), descendants);
    pushInstanceArray(L, descendants);
    return 1;
}

int methodIsA(lua_State* L)
{
    const core::InstanceId id = liveInstance(L, 1);
    // A string naming no class answers false rather than raising: the point of
    // `IsA` is to test names you do not trust.
    const ClassId base = lookupClass(L, 2);
    lua_pushboolean(L, base != scene::InvalidClass && world(L).isA(id, base));
    return 1;
}

int methodIsAncestorOf(lua_State* L)
{
    const core::InstanceId id = liveInstance(L, 1);
    const core::InstanceId* other = toInstance(L, 2);
    if (other == nullptr)
        luaL_checkudatatagged(L, 2, static_cast<int>(UserdataTag::Instance));
    lua_pushboolean(L, other != nullptr && world(L).isAncestorOf(id, *other));
    return 1;
}

int methodIsDescendantOf(lua_State* L)
{
    const core::InstanceId id = liveInstance(L, 1);
    const core::InstanceId* other = toInstance(L, 2);
    if (other == nullptr)
        luaL_checkudatatagged(L, 2, static_cast<int>(UserdataTag::Instance));
    lua_pushboolean(L, other != nullptr && world(L).isAncestorOf(*other, id));
    return 1;
}

int methodClone(lua_State* L)
{
    pushInstance(L, world(L).clone(liveInstance(L, 1)));
    return 1;
}

int methodDestroy(lua_State* L)
{
    // Synchronous: the subtree is out of the tree when this returns. What waits
    // for the drain is the telling, not the doing -- and the handle keeps
    // resolving until that drain ends, which is what gives a `Destroying`
    // handler something to work with.
    world(L).destroy(liveInstance(L, 1));
    return 0;
}

// The attribute domain (api-design.md §2.2). Inferred rather than expected,
// because `SetAttribute` takes a union and the caller's value is what names the
// member of it.
[[nodiscard]] std::optional<scene::Value> toAttributeValue(lua_State* L, int index)
{
    switch (lua_type(L, index))
    {
    case LUA_TNIL:
    case LUA_TNONE:
        return scene::Value{};
    case LUA_TBOOLEAN:
        return scene::Value{lua_toboolean(L, index) != 0};
    case LUA_TNUMBER:
        return scene::Value{lua_tonumber(L, index)};
    case LUA_TSTRING:
    {
        size_t length = 0;
        const char* text = lua_tolstring(L, index, &length);
        return scene::Value{std::string(text, length)};
    }
    case LUA_TVECTOR:
        return toValue(L, index, scene::ValueType::Vector3);
    default:
        break;
    }

    if (std::optional<scene::Value> cframe = toValue(L, index, scene::ValueType::CFrame))
        return cframe;
    if (std::optional<scene::Value> color = toValue(L, index, scene::ValueType::Color3))
        return color;
    // A table, an Instance or a function: outside the domain, and the caller
    // raises `scene.err.attribute_type` leaving any previous value in place.
    return std::nullopt;
}

[[nodiscard]] core::NameAtom checkAttributeName(lua_State* L, int index)
{
    size_t length = 0;
    const char* text = luaL_checklstring(L, index, &length);
    if (length == 0)
    {
        const core::I18nArg args[] = {{"name", std::string_view{""}}};
        raise(L, LUAUG_TR("scene.err.invalid_name"), args);
    }
    return world(L).atoms().intern(std::string_view{text, length});
}

int methodGetAttribute(lua_State* L)
{
    const core::InstanceId id = liveInstance(L, 1);
    pushValue(L, world(L).getAttribute(id, lookupAtom(L, 2)));
    return 1;
}

int methodSetAttribute(lua_State* L)
{
    const core::InstanceId id = liveInstance(L, 1);
    const core::NameAtom name = checkAttributeName(L, 2);

    const std::optional<scene::Value> value = toAttributeValue(L, 3);
    if (!value.has_value() || !world(L).setAttribute(id, name, *value))
    {
        const core::I18nArg args[] = {
            {"attribute", world(L).atoms().text(name)},
            {"valueType", std::string_view{luaL_typename(L, 3)}},
        };
        raise(L, LUAUG_TR("scene.err.attribute_type"), args);
    }
    return 0;
}

int methodGetAttributes(lua_State* L)
{
    const core::InstanceId id = liveInstance(L, 1);
    scene::AttributeMap attributes;
    world(L).collectAttributes(id, attributes);

    lua_createtable(L, 0, static_cast<int>(attributes.size()));
    for (const auto& [name, value] : attributes)
    {
        const std::string_view text = world(L).atoms().text(name);
        lua_pushlstring(L, text.data(), text.size());
        pushValue(L, value);
        lua_rawset(L, -3);
    }
    return 1;
}

[[nodiscard]] core::NameAtom checkTagName(lua_State* L, int index)
{
    return checkAttributeName(L, index);
}

int methodAddTag(lua_State* L)
{
    const core::InstanceId id = liveInstance(L, 1);
    world(L).addTag(id, checkTagName(L, 2));
    return 0;
}

int methodRemoveTag(lua_State* L)
{
    const core::InstanceId id = liveInstance(L, 1);
    world(L).removeTag(id, checkTagName(L, 2));
    return 0;
}

int methodHasTag(lua_State* L)
{
    const core::InstanceId id = liveInstance(L, 1);
    lua_pushboolean(L, world(L).hasTag(id, lookupAtom(L, 2)));
    return 1;
}

int methodGetTags(lua_State* L)
{
    const core::InstanceId id = liveInstance(L, 1);
    scene::TagSet tags;
    world(L).collectTags(id, tags);

    lua_createtable(L, static_cast<int>(tags.size()), 0);
    for (usize index = 0; index < tags.size(); ++index)
    {
        const std::string_view text = world(L).atoms().text(tags[index]);
        lua_pushlstring(L, text.data(), text.size());
        lua_rawseti(L, -2, static_cast<int>(index) + 1);
    }
    return 1;
}

// --- Model -------------------------------------------------------------------

[[nodiscard]] core::CFrameD modelPivot(World& w, core::InstanceId id)
{
    const scene::ModelComponent* model = w.models().find(id);
    if (model != nullptr && w.alive(model->primaryPart))
    {
        if (const scene::PartComponent* part = w.parts().find(model->primaryPart))
            return part->cframe;
    }

    // No primary part: the centre of the extents box, with no rotation. A pivot
    // that defaulted to the identity would move a model built far from the
    // origin by its whole distance the first time anything pivoted it.
    std::vector<core::InstanceId> descendants;
    w.collectDescendants(id, descendants);

    core::DVec3 total;
    usize count = 0;
    for (const core::InstanceId descendant : descendants)
    {
        if (const scene::PartComponent* part = w.parts().find(descendant))
        {
            total = total + part->cframe.position;
            ++count;
        }
    }

    core::CFrameD pivot;
    if (count > 0)
    {
        const f64 scale = 1.0 / static_cast<f64>(count);
        pivot.position = core::DVec3{total.x * scale, total.y * scale, total.z * scale};
    }
    return pivot;
}

int methodGetPivot(lua_State* L)
{
    pushCFrame(L, modelPivot(world(L), liveInstance(L, 1)));
    return 1;
}

int methodPivotTo(lua_State* L)
{
    const core::InstanceId id = liveInstance(L, 1);
    World& w = world(L);
    const core::CFrameD target = checkCFrame(L, 2);
    // Every descendant part moves by the same transform, so relative layout is
    // preserved: `delta = target * inverse(pivot)` applied on the left.
    const core::CFrameD delta = target * core::inverse(modelPivot(w, id));

    std::vector<core::InstanceId> descendants;
    w.collectDescendants(id, descendants);
    const core::NameAtom cframeProperty = context(L).wellKnown.cframe;
    for (const core::InstanceId descendant : descendants)
    {
        const scene::PartComponent* part = w.parts().find(descendant);
        if (part == nullptr)
            continue;
        // Through `setProperty` rather than by writing the component, so the
        // change is enqueued for anything watching `CFrame`.
        w.setProperty(descendant, cframeProperty, scene::Value{delta * part->cframe});
    }
    return 0;
}

int methodGetExtentsSize(lua_State* L)
{
    const core::InstanceId id = liveInstance(L, 1);
    World& w = world(L);

    std::vector<core::InstanceId> descendants;
    w.collectDescendants(id, descendants);

    bool any = false;
    core::DVec3 minimum;
    core::DVec3 maximum;
    for (const core::InstanceId descendant : descendants)
    {
        const scene::PartComponent* part = w.parts().find(descendant);
        if (part == nullptr)
            continue;

        // The standard OBB-to-AABB bound: the world half-extent along axis i is
        // the sum over the part's own axes j of |R(i, j)| * half_j. `Mat3` is
        // stored column-major as `m[col][row]`, so `m[j][i]` is R(i, j).
        const core::Mat3& r = part->cframe.rotation;
        const f64 half[3] = {
            static_cast<f64>(part->size.x) * 0.5,
            static_cast<f64>(part->size.y) * 0.5,
            static_cast<f64>(part->size.z) * 0.5,
        };
        f64 extents[3] = {0.0, 0.0, 0.0};
        for (int world = 0; world < 3; ++world)
        {
            for (int local = 0; local < 3; ++local)
                extents[world] += std::abs(static_cast<f64>(r.m[local][world])) * half[local];
        }
        const core::DVec3 extent{extents[0], extents[1], extents[2]};

        const core::DVec3 low = part->cframe.position - extent;
        const core::DVec3 high = part->cframe.position + extent;
        if (!any)
        {
            minimum = low;
            maximum = high;
            any = true;
            continue;
        }
        minimum = core::DVec3{std::min(minimum.x, low.x), std::min(minimum.y, low.y), std::min(minimum.z, low.z)};
        maximum = core::DVec3{std::max(maximum.x, high.x), std::max(maximum.y, high.y), std::max(maximum.z, high.z)};
    }

    // A model with no parts has no size, and zero is the honest answer rather
    // than an inverted box.
    pushVector3(L, any ? core::toVec3(maximum - minimum) : core::Vec3{});
    return 1;
}

// --- Instance.new ------------------------------------------------------------

int instanceNew(lua_State* L)
{
    World& w = world(L);
    size_t length = 0;
    const char* text = luaL_checklstring(L, 1, &length);
    const std::string_view requested{text, length};

    const ClassId classId = w.classes().findId(w.atoms().lookup(requested));
    const scene::ClassDescriptor* descriptor = w.classes().find(classId);
    if (descriptor == nullptr)
    {
        const core::I18nArg args[] = {{"className", requested}};
        raise(L, LUAUG_TR("scene.err.unknown_class"), args);
    }

    // Abstract, service and not-creatable are three reasons and one message: the
    // tag names which, so the reader is told what is actually wrong rather than
    // "cannot create".
    const char* tag = nullptr;
    if (hasFlag(descriptor->flags, scene::ClassFlags::Abstract))
        tag = "Abstract";
    else if (hasFlag(descriptor->flags, scene::ClassFlags::Service))
        tag = "Service";
    else if (hasFlag(descriptor->flags, scene::ClassFlags::NotCreatable))
        tag = "NotCreatable";

    if (tag != nullptr)
    {
        const core::I18nArg args[] = {{"className", requested}, {"classTag", std::string_view{tag}}};
        raise(L, LUAUG_TR("scene.err.not_creatable"), args);
    }

    pushInstance(L, w.create(classId));
    return 1;
}

// --- Registration ------------------------------------------------------------

struct MethodBinding
{
    const char* className = nullptr;
    const char* methodName = nullptr;
    lua_CFunction fn = nullptr;
};

// Every method this build implements, by the class that DECLARES it. Binding it
// to the declaring class is what makes inheritance work for free: the registry
// resolves `part:Destroy()` to `Instance`'s descriptor, and this table is keyed
// by descriptor.
//
// `WaitForChild` is absent because it yields and there is no scheduler to park
// it on yet; the signal-returning pair is absent for the same kind of reason.
// Both are reported by the coverage count rather than left to be discovered.
constexpr MethodBinding InstanceMethods[] = {
    {"Instance", "FindFirstChild", methodFindFirstChild},
    {"Instance", "FindFirstChildOfClass", methodFindFirstChildOfClass},
    {"Instance", "FindFirstChildWhichIsA", methodFindFirstChildWhichIsA},
    {"Instance", "FindFirstAncestor", methodFindFirstAncestor},
    {"Instance", "FindFirstAncestorOfClass", methodFindFirstAncestorOfClass},
    {"Instance", "GetChildren", methodGetChildren},
    {"Instance", "GetDescendants", methodGetDescendants},
    {"Instance", "IsA", methodIsA},
    {"Instance", "IsAncestorOf", methodIsAncestorOf},
    {"Instance", "IsDescendantOf", methodIsDescendantOf},
    {"Instance", "Clone", methodClone},
    {"Instance", "Destroy", methodDestroy},
    {"Instance", "GetAttribute", methodGetAttribute},
    {"Instance", "SetAttribute", methodSetAttribute},
    {"Instance", "GetAttributes", methodGetAttributes},
    {"Instance", "AddTag", methodAddTag},
    {"Instance", "RemoveTag", methodRemoveTag},
    {"Instance", "HasTag", methodHasTag},
    {"Instance", "GetTags", methodGetTags},
    {"Model", "GetPivot", methodGetPivot},
    {"Model", "PivotTo", methodPivotTo},
    {"Model", "GetExtentsSize", methodGetExtentsSize},
};

} // namespace

void pushInstance(lua_State* L, core::InstanceId id)
{
    if (!id.valid())
    {
        // nil rather than a dead handle: `nil` is the honest answer and the one
        // `Instance?` is typed for.
        lua_pushnil(L);
        return;
    }

    const VmContext& ctx = context(L);
    lua_getref(L, ctx.instanceCacheRef);
    const int cache = lua_gettop(L);

    // The slot index, not the whole id: a slot holds one live instance at a
    // time, so this is dense and lands in the table's array part. The generation
    // is what the hit is validated against.
    const int slot = static_cast<int>(id.index) + 1;
    lua_rawgeti(L, cache, slot);
    if (const core::InstanceId* cached = toInstance(L, -1); cached != nullptr && *cached == id)
    {
        lua_replace(L, cache);
        return;
    }
    lua_pop(L, 1);

    void* memory = lua_newuserdatataggedwithmetatable(L, sizeof(InstanceUserdata), static_cast<int>(UserdataTag::Instance));
    *static_cast<InstanceUserdata*>(memory) = InstanceUserdata{id};

    lua_pushvalue(L, -1);
    lua_rawseti(L, cache, slot);
    lua_replace(L, cache);
}

const core::InstanceId* toInstance(lua_State* L, int index) noexcept
{
    const void* payload = lua_touserdatatagged(L, index, static_cast<int>(UserdataTag::Instance));
    return payload == nullptr ? nullptr : &static_cast<const InstanceUserdata*>(payload)->id;
}

core::InstanceId checkInstance(lua_State* L, int index)
{
    return liveInstance(L, index);
}

MethodCoverage registerInstanceBinding(lua_State* L)
{
    VmContext& ctx = context(L);
    World& w = *ctx.world;

    // Weak values, so a userdata nothing holds is collected and the cache does
    // not turn every instance a script has ever touched into a permanent one.
    // The keys are integers and are dropped with their values.
    lua_createtable(L, 0, 0);
    lua_createtable(L, 0, 1);
    lua_pushstring(L, "v");
    lua_setfield(L, -2, "__mode");
    lua_setreadonly(L, -1, true);
    lua_setmetatable(L, -2);
    ctx.instanceCacheRef = lua_ref(L, -1);
    lua_pop(L, 1);

    lua_createtable(L, 0, 6);
    lua_pushstring(L, typeName(UserdataTag::Instance));
    lua_setfield(L, -2, "__type");
    lua_pushvalue(L, -1);
    lua_setuserdatametatable(L, static_cast<int>(UserdataTag::Instance));

    lua_pushcfunction(L, instanceIndex, "__index");
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, instanceNewIndex, "__newindex");
    lua_setfield(L, -2, "__newindex");
    // Named `__namecall` on purpose: `laux.cpp:42` special-cases exactly this
    // debug name so an argument error reports the *method* rather than the
    // metamethod, which is free and is the difference between a usable message
    // and a confusing one.
    lua_pushcfunction(L, instanceNamecall, "__namecall");
    lua_setfield(L, -2, "__namecall");
    lua_pushcfunction(L, instanceEq, "__eq");
    lua_setfield(L, -2, "__eq");
    lua_pushcfunction(L, instanceTostring, "__tostring");
    lua_setfield(L, -2, "__tostring");
    lua_setreadonly(L, -1, true);
    lua_pop(L, 1);

    MethodCoverage coverage;
    for (const MethodBinding& binding : InstanceMethods)
    {
        const ClassId classId = w.classes().findId(w.atoms().lookup(binding.className));
        const scene::MethodDesc* descriptor =
            w.classes().findMethod(classId, w.atoms().lookup(binding.methodName));
        if (descriptor == nullptr)
        {
            // A binding for a method no definition declares. Nothing generated
            // this surface, so nothing else knows about it -- which makes it a
            // stale hand-written entry rather than a new feature.
            ++coverage.boundWithoutDeclaration;
            continue;
        }
        ctx.instanceMethods.emplace(descriptor, binding.fn);
        ++coverage.bound;
    }

    // The other half of the cross-check `MethodDesc` exists for: walk every
    // class the registry holds and count the declared methods with no binding.
    for (ClassId classId = 1; classId < static_cast<ClassId>(w.classes().classCount()); ++classId)
    {
        const scene::ClassDescriptor* descriptor = w.classes().find(classId);
        if (descriptor == nullptr)
            continue;
        for (const scene::MethodDesc& method : descriptor->methods)
        {
            ++coverage.declared;
            if (ctx.instanceMethods.find(&method) == ctx.instanceMethods.end())
                ++coverage.declaredWithoutBinding;
        }
    }

    const luaL_Reg constructors[] = {{"new", instanceNew}, {nullptr, nullptr}};
    luaL_register(L, "Instance", constructors);
    lua_setreadonly(L, -1, true);
    lua_pop(L, 1);

    return coverage;
}

} // namespace luaug::script
