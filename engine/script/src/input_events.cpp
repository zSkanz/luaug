#include "luaug/script/input_events.h"

#include "luaug/scene/world.h"
#include "luaug/script/binding.h"
#include "luaug/script/datatypes.h"
#include "luaug/script/instance_binding.h"
#include "luaug/script/services.h"
#include "luaug/script/signals.h"

#include <lua.h>
#include <lualib.h>

#include <array>
#include <new>

// scene's generated enum and class ids, through the include directory
// `luaug_scene` exports.
#include "class_descriptors.gen.h"

namespace luaug::script {
namespace {

// The payload of an `InputObject` userdata: exactly what the system produced,
// minus the two fields that are the event's rather than the object's. Trivially
// copyable, like every other value type here.
struct InputObjectData
{
    i32 userInputType = 0;
    i32 keyCode = 0;
    core::Vec3 position;
    core::Vec3 delta;
};

static_assert(sizeof(InputObjectData) == 32, "the InputObject payload's size is an ABI decision");

[[nodiscard]] const InputObjectData& checkInputObject(lua_State* L, int index)
{
    return *static_cast<InputObjectData*>(luaL_checkudatatagged(L, index, static_cast<int>(UserdataTag::InputObject)));
}

void pushInputObject(lua_State* L, const input::RawInputEvent& event)
{
    void* memory =
        lua_newuserdatataggedwithmetatable(L, sizeof(InputObjectData), static_cast<int>(UserdataTag::InputObject));
    InputObjectData* data = new (memory) InputObjectData{};
    data->userInputType = static_cast<i32>(event.userInputType);
    data->keyCode = event.keyCode;
    data->position = event.position;
    data->delta = event.delta;
}

int inputObjectGetUserInputType(lua_State* L)
{
    pushEnumItem(L, scene::EnumValue{scene::generated::UserInputTypeEnumId, checkInputObject(L, 1).userInputType});
    return 1;
}

int inputObjectGetKeyCode(lua_State* L)
{
    pushEnumItem(L, scene::EnumValue{scene::generated::KeyCodeEnumId, checkInputObject(L, 1).keyCode});
    return 1;
}

int inputObjectGetPosition(lua_State* L)
{
    pushVector3(L, checkInputObject(L, 1).position);
    return 1;
}

int inputObjectGetDelta(lua_State* L)
{
    pushVector3(L, checkInputObject(L, 1).delta);
    return 1;
}

int inputObjectToString(lua_State* L)
{
    const InputObjectData& data = checkInputObject(L, 1);
    // The two enums by name rather than by number: a `print` in a handler is the
    // first thing anybody does with one of these, and "InputObject(Keyboard,
    // Space)" answers the question a number would not.
    const scene::World& w = *context(L).world;
    const scene::EnumItemDesc* kind = w.enums().findValue(scene::generated::UserInputTypeEnumId, data.userInputType);
    const scene::EnumItemDesc* key = w.enums().findValue(scene::generated::KeyCodeEnumId, data.keyCode);
    lua_pushfstring(L, "InputObject(%s, %s)", kind == nullptr ? "?" : w.atoms().text(kind->name).data(),
                    key == nullptr ? "?" : w.atoms().text(key->name).data());
    return 1;
}

// Which of `InputService`'s events one phase fires. Resolved per drain rather
// than cached, because the descriptor lives in static storage and the lookup is
// a hash probe on a name that is already interned.
[[nodiscard]] const char* eventNameOf(input::RawInputEvent::Phase phase) noexcept
{
    switch (phase) {
    case input::RawInputEvent::Phase::Began:
        return "InputBegan";
    case input::RawInputEvent::Phase::Changed:
        return "InputChanged";
    case input::RawInputEvent::Phase::Ended:
        return "InputEnded";
    }
    return "InputBegan";
}

} // namespace

void registerInputTypes(lua_State* L)
{
    VmContext& ctx = context(L);
    core::AtomTable& atoms = ctx.world->atoms();

    MemberTable& getters = ctx.getters[static_cast<usize>(UserdataTag::InputObject)];
    addMember(getters, atoms, "UserInputType", inputObjectGetUserInputType);
    addMember(getters, atoms, "KeyCode", inputObjectGetKeyCode);
    addMember(getters, atoms, "Position", inputObjectGetPosition);
    addMember(getters, atoms, "Delta", inputObjectGetDelta);

    // No `__eq`: two snapshots of one press are two facts about one tick, and
    // the bitwise comparison a trivially-copyable payload gives is the right
    // answer for a value type anyway.
    installTagMetatable(L, UserdataTag::InputObject, nullptr, inputObjectToString);
}

void fireInputEvents(lua_State* L, std::span<const input::RawInputEvent> events)
{
    if (events.empty())
        return;

    VmContext& ctx = context(L);
    scene::World& w = *ctx.world;

    // `InputService` is created on first `GetService` like most services, so a
    // world whose scripts never read input has none -- and firing into nothing
    // is the common case rather than an error.
    const scene::ClassId serviceClass = w.classes().findId(w.atoms().lookup("InputService"));
    if (serviceClass == scene::InvalidClass)
        return;
    const core::InstanceId root = ctx.services->dataModel;
    if (!root.valid())
        return;
    const core::InstanceId service = w.findFirstChildOfClass(root, serviceClass);
    if (!service.valid())
        return;

    for (const input::RawInputEvent& event : events) {
        const scene::EventDesc* descriptor =
            w.classes().findEvent(serviceClass, w.atoms().intern(eventNameOf(event.phase)));
        if (descriptor == nullptr)
            continue;

        pushInputObject(L, event);
        lua_pushboolean(L, event.uiConsumed);
        fireInstanceEvent(L, service, descriptor->slot, lua_gettop(L) - 1, 2);
        lua_pop(L, 2);
    }
}

} // namespace luaug::script
