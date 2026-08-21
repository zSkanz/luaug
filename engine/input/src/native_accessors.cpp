// The hand-written half of `input`'s reflection (architecture.md §4).
//
// Same arrangement as `render`'s: the descriptors are generated, the accessor
// pairs they point at are here, and every property that only stores a value is
// three lines. What is worth reading is the small number that are not --
// `InputBinding.DeviceType`, which has no setter at all, and the enum writes,
// which validate against the registry rather than against a literal range.
#include "luaug/input/input.h"
#include "luaug/input/scene_types.h"
#include "luaug/scene/world.h"

// By a path relative to this file rather than through an include directory:
// every module's generated header has the same name, so two of them on one
// include path would make the include resolve by search order. `render` and
// `scene` reach theirs the same way.
#include <cmath>

#include "../generated/class_descriptors.gen.h"

namespace luaug::input {
namespace native {
namespace {

using scene::Value;

[[nodiscard]] bool finite(f64 value) noexcept
{
    return std::isfinite(value);
}

// An enum write, checked against the right enum AND against the item list. The
// id comes from the generated header rather than from a lookup by name, which
// is what that header's own comment recommends: a property write should not pay
// a hash probe to learn a number the generator already decided.
//
// The second check is the one that matters for `KeyCode`: it has ninety-four
// items and a value outside them is refused rather than stored, so a snapshot
// cannot come to hold an enum member that does not exist.
[[nodiscard]] bool takeEnum(const scene::World& world, const Value& value, scene::EnumId enumId, i32& out)
{
    const auto* item = std::get_if<scene::EnumValue>(&value);
    if (item == nullptr || item->enumId != enumId)
        return false;
    if (world.enums().findValue(enumId, item->value) == nullptr)
        return false;
    out = item->value;
    return true;
}

[[nodiscard]] Value enumValue(scene::EnumId enumId, i32 item)
{
    return Value{scene::EnumValue{enumId, item}};
}

[[nodiscard]] const scene::InputContextComponent* readContext(const scene::World& world, core::InstanceId id)
{
    return world.inputContexts().find(id);
}

[[nodiscard]] scene::InputContextComponent* writeContext(scene::World& world, core::InstanceId id)
{
    return world.inputContexts().find(id);
}

[[nodiscard]] const scene::InputActionComponent* readAction(const scene::World& world, core::InstanceId id)
{
    return world.inputActions().find(id);
}

[[nodiscard]] scene::InputActionComponent* writeAction(scene::World& world, core::InstanceId id)
{
    return world.inputActions().find(id);
}

[[nodiscard]] const scene::InputBindingComponent* readBinding(const scene::World& world, core::InstanceId id)
{
    return world.inputBindings().find(id);
}

[[nodiscard]] scene::InputBindingComponent* writeBinding(scene::World& world, core::InstanceId id)
{
    return world.inputBindings().find(id);
}

// The four composite keys differ only in which field they land in, so the
// accessor pair is written once and the field is a member pointer.
using BindingKeyField = i32 scene::InputBindingComponent::*;

[[nodiscard]] Value getBindingKey(const scene::World& world, core::InstanceId id, BindingKeyField field)
{
    const scene::InputBindingComponent* binding = readBinding(world, id);
    return binding == nullptr ? Value{} : enumValue(generated::KeyCodeEnumId, binding->*field);
}

[[nodiscard]] bool setBindingKey(scene::World& world, core::InstanceId id, const Value& value, BindingKeyField field)
{
    scene::InputBindingComponent* binding = writeBinding(world, id);
    if (binding == nullptr)
        return false;
    i32 code = 0;
    if (!takeEnum(world, value, generated::KeyCodeEnumId, code))
        return false;
    binding->*field = code;
    return true;
}

[[nodiscard]] bool takeString(const Value& value, std::string& out)
{
    const auto* text = std::get_if<std::string>(&value);
    if (text == nullptr)
        return false;
    out = *text;
    return true;
}

} // namespace

// --- InputContext ------------------------------------------------------------

Value getInputContextEnabled(const scene::World& world, core::InstanceId id)
{
    const scene::InputContextComponent* context = readContext(world, id);
    return context == nullptr ? Value{} : Value{context->enabled};
}

bool setInputContextEnabled(scene::World& world, core::InstanceId id, const Value& value)
{
    const auto* flag = std::get_if<bool>(&value);
    scene::InputContextComponent* context = writeContext(world, id);
    if (flag == nullptr || context == nullptr)
        return false;
    context->enabled = *flag;
    return true;
}

Value getInputContextPriority(const scene::World& world, core::InstanceId id)
{
    const scene::InputContextComponent* context = readContext(world, id);
    return context == nullptr ? Value{} : Value{static_cast<f64>(context->priority)};
}

bool setInputContextPriority(scene::World& world, core::InstanceId id, const Value& value)
{
    const auto* number = std::get_if<f64>(&value);
    scene::InputContextComponent* context = writeContext(world, id);
    if (number == nullptr || context == nullptr || !finite(*number))
        return false;
    context->priority = static_cast<f32>(*number);
    return true;
}

Value getInputContextSink(const scene::World& world, core::InstanceId id)
{
    const scene::InputContextComponent* context = readContext(world, id);
    return context == nullptr ? Value{} : Value{context->sink};
}

bool setInputContextSink(scene::World& world, core::InstanceId id, const Value& value)
{
    const auto* flag = std::get_if<bool>(&value);
    scene::InputContextComponent* context = writeContext(world, id);
    if (flag == nullptr || context == nullptr)
        return false;
    context->sink = *flag;
    return true;
}

Value getInputContextRate(const scene::World& world, core::InstanceId id)
{
    const scene::InputContextComponent* context = readContext(world, id);
    return context == nullptr ? Value{} : enumValue(generated::InputRateEnumId, context->rate);
}

bool setInputContextRate(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::InputContextComponent* context = writeContext(world, id);
    if (context == nullptr)
        return false;
    i32 rate = 0;
    if (!takeEnum(world, value, generated::InputRateEnumId, rate))
        return false;
    context->rate = rate;
    return true;
}

void attachInputContextComponents(scene::World& world, core::InstanceId id)
{
    world.inputContexts().add(id, scene::InputContextComponent{});
}

void detachInputContextComponents(scene::World& world, core::InstanceId id)
{
    world.inputContexts().remove(id);
}

// --- InputAction -------------------------------------------------------------

Value getInputActionType(const scene::World& world, core::InstanceId id)
{
    const scene::InputActionComponent* action = readAction(world, id);
    return action == nullptr ? Value{} : enumValue(generated::InputActionTypeEnumId, action->type);
}

bool setInputActionType(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::InputActionComponent* action = writeAction(world, id);
    if (action == nullptr)
        return false;
    i32 type = 0;
    if (!takeEnum(world, value, generated::InputActionTypeEnumId, type))
        return false;
    if (type == action->type)
        return true;
    action->type = type;
    // The old value meant something in the old type's currency: a Bool action
    // that was held and became a Direction2D would otherwise report (0,0) AND
    // still be pressed. Reset rather than convert, because there is no honest
    // conversion between a press and a vector.
    action->axis = core::Vec3{};
    action->pressed = false;
    return true;
}

Value getInputActionEnabled(const scene::World& world, core::InstanceId id)
{
    const scene::InputActionComponent* action = readAction(world, id);
    return action == nullptr ? Value{} : Value{action->enabled};
}

bool setInputActionEnabled(scene::World& world, core::InstanceId id, const Value& value)
{
    const auto* flag = std::get_if<bool>(&value);
    scene::InputActionComponent* action = writeAction(world, id);
    if (flag == nullptr || action == nullptr)
        return false;
    action->enabled = *flag;
    return true;
}

void attachInputActionComponents(scene::World& world, core::InstanceId id)
{
    world.inputActions().add(id, scene::InputActionComponent{});
}

void detachInputActionComponents(scene::World& world, core::InstanceId id)
{
    world.inputActions().remove(id);
}

// --- InputBinding ------------------------------------------------------------

Value getInputBindingKeyCode(const scene::World& world, core::InstanceId id)
{
    return getBindingKey(world, id, &scene::InputBindingComponent::keyCode);
}

bool setInputBindingKeyCode(scene::World& world, core::InstanceId id, const Value& value)
{
    return setBindingKey(world, id, value, &scene::InputBindingComponent::keyCode);
}

Value getInputBindingUp(const scene::World& world, core::InstanceId id)
{
    return getBindingKey(world, id, &scene::InputBindingComponent::up);
}

bool setInputBindingUp(scene::World& world, core::InstanceId id, const Value& value)
{
    return setBindingKey(world, id, value, &scene::InputBindingComponent::up);
}

Value getInputBindingDown(const scene::World& world, core::InstanceId id)
{
    return getBindingKey(world, id, &scene::InputBindingComponent::down);
}

bool setInputBindingDown(scene::World& world, core::InstanceId id, const Value& value)
{
    return setBindingKey(world, id, value, &scene::InputBindingComponent::down);
}

Value getInputBindingLeft(const scene::World& world, core::InstanceId id)
{
    return getBindingKey(world, id, &scene::InputBindingComponent::left);
}

bool setInputBindingLeft(scene::World& world, core::InstanceId id, const Value& value)
{
    return setBindingKey(world, id, value, &scene::InputBindingComponent::left);
}

Value getInputBindingRight(const scene::World& world, core::InstanceId id)
{
    return getBindingKey(world, id, &scene::InputBindingComponent::right);
}

bool setInputBindingRight(scene::World& world, core::InstanceId id, const Value& value)
{
    return setBindingKey(world, id, value, &scene::InputBindingComponent::right);
}

Value getInputBindingScale(const scene::World& world, core::InstanceId id)
{
    const scene::InputBindingComponent* binding = readBinding(world, id);
    return binding == nullptr ? Value{} : Value{static_cast<f64>(binding->scale)};
}

bool setInputBindingScale(scene::World& world, core::InstanceId id, const Value& value)
{
    const auto* number = std::get_if<f64>(&value);
    scene::InputBindingComponent* binding = writeBinding(world, id);
    if (number == nullptr || binding == nullptr || !finite(*number))
        return false;
    // Negative is legal and is the point: "invert Y" is a settings screen
    // writing -1 here, and refusing it would make the engine the reason a game
    // could not offer the option.
    binding->scale = static_cast<f32>(*number);
    return true;
}

Value getInputBindingDisplayName(const scene::World& world, core::InstanceId id)
{
    const scene::InputBindingComponent* binding = readBinding(world, id);
    return binding == nullptr ? Value{} : Value{binding->displayName};
}

bool setInputBindingDisplayName(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::InputBindingComponent* binding = writeBinding(world, id);
    return binding != nullptr && takeString(value, binding->displayName);
}

Value getInputBindingImage(const scene::World& world, core::InstanceId id)
{
    const scene::InputBindingComponent* binding = readBinding(world, id);
    return binding == nullptr ? Value{} : Value{binding->image};
}

bool setInputBindingImage(scene::World& world, core::InstanceId id, const Value& value)
{
    scene::InputBindingComponent* binding = writeBinding(world, id);
    return binding != nullptr && takeString(value, binding->image);
}

Value getInputBindingDeviceType(const scene::World& world, core::InstanceId id)
{
    const scene::InputBindingComponent* binding = readBinding(world, id);
    if (binding == nullptr)
        return Value{};
    // Derived, never stored (ADR 0039). There is no setter declared for it, so
    // there is none to write here: a settable device type could disagree with
    // the key beside it.
    return enumValue(generated::InputDeviceTypeEnumId, static_cast<i32>(deviceOf(binding->keyCode)));
}

void attachInputBindingComponents(scene::World& world, core::InstanceId id)
{
    world.inputBindings().add(id, scene::InputBindingComponent{});
}

void detachInputBindingComponents(scene::World& world, core::InstanceId id)
{
    world.inputBindings().remove(id);
}

// --- InputService ------------------------------------------------------------

Value getInputServicePointerLocked(const scene::World& world, core::InstanceId)
{
    return Value{world.engineState().pointerLocked};
}

bool setInputServicePointerLocked(scene::World& world, core::InstanceId, const Value& value)
{
    const auto* flag = std::get_if<bool>(&value);
    if (flag == nullptr)
        return false;
    world.engineState().pointerLocked = *flag;
    return true;
}

Value getInputServicePointerVisible(const scene::World& world, core::InstanceId)
{
    return Value{world.engineState().pointerVisible};
}

bool setInputServicePointerVisible(scene::World& world, core::InstanceId, const Value& value)
{
    const auto* flag = std::get_if<bool>(&value);
    if (flag == nullptr)
        return false;
    world.engineState().pointerVisible = *flag;
    return true;
}

Value getInputServiceLastInputDeviceType(const scene::World& world, core::InstanceId)
{
    return enumValue(generated::InputDeviceTypeEnumId, world.engineState().lastInputDeviceType);
}

} // namespace native

void registerSceneTypes(scene::ClassRegistry& classes, core::AtomTable& atoms)
{
    generated::registerClasses(classes, atoms);
}

} // namespace luaug::input
