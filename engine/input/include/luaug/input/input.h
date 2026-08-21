// The Input Action System (ADR 0029, api-design.md §2.4).
//
// Three jobs, and the split between them is the design:
//
//   1. **The device snapshot.** `pumpFrame` folds a frame's platform events
//      into "what is held right now" -- a key set, a button set, axis values, a
//      pointer position, and the motion and wheel deltas ACCUMULATED since the
//      last dispatch. A snapshot rather than an event stream, because two reads
//      inside one tick must agree and because a recorded stream has to be able
//      to hand the same answers back with no hardware attached (M5 settled this
//      for the keyboard; it is the same contract).
//
//   2. **Resolution.** `dispatch*` walks the `InputContext` tree in priority
//      order, computes each enabled action's value from its bindings, and
//      writes it into the action's component. Sinking contexts consume the
//      inputs they name, so a lower-priority context bound to the same key sees
//      nothing.
//
//   3. **Telling `script`.** A value that changed enqueues a `Change` on the
//      world's queue, exactly as a property write does; `script` turns it into
//      a deferred fire. Nothing here knows what a Luau value is (R17,
//      architecture §2 rule 2).
//
// The dispatch split is ADR 0039's: an `InputContext` declares its own rate,
// `Simulation` by default. A `Render`-rate context is dispatched once per
// rendered frame and is NOT part of the recorded input stream -- a render frame
// is not a unit the replay has.
#pragma once

#include "luaug/core/id.h"
#include "luaug/core/math.h"
#include "luaug/core/types.h"
#include "luaug/platform/event.h"
#include "luaug/scene/types.h"

#include <array>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace luaug::scene {
class World;
}

namespace luaug::input {

using core::f32;
using core::f64;
using core::i32;
using core::u32;
using core::u64;
using core::usize;

// `Enum.InputActionType`'s values, spelled once so that neither the resolver nor
// the accessors compare against a literal.
enum class ActionType : i32
{
    Bool = 0,
    Direction1D = 1,
    Direction2D = 2,
    Direction3D = 3,
    ViewportPosition = 4,
};

// `Enum.InputDeviceType`'s values.
enum class DeviceType : i32
{
    KeyboardMouse = 0,
    Gamepad = 1,
    Touch = 2,
};

// `Enum.InputRate`'s values.
enum class Rate : i32
{
    Simulation = 0,
    Render = 1,
};

// The number of `Enum.KeyCode` items. It is not derived from `platform`'s enums
// because it is not their union: the KeyCode table adds two composite sticks
// that no device event names. `input.cpp` static_asserts the arithmetic against
// the generated enum descriptor at registration, which is the check that keeps
// this number honest.
inline constexpr usize kKeyCodeCount = 94;

// Which device family a `KeyCode` belongs to. `InputBinding.DeviceType` is this
// function and nothing else (ADR 0039): a settable device type could disagree
// with the key beside it, and the engine would then have to choose which of the
// two to believe.
[[nodiscard]] DeviceType deviceOf(i32 keyCode) noexcept;

// Whether a `KeyCode` reports a continuous value rather than a press. Bound to a
// `Bool` action, an analogue source counts as pressed past half deflection --
// which is a choice `Enum.KeyCode`'s own doc states, because refusing at bind
// time would let a rebinding UI hand the player an unusable option.
[[nodiscard]] bool isAnalog(i32 keyCode) noexcept;

// A `KeyCode` by the name `Enum.KeyCode` gives its item, and back. This is the
// spelling a recorded input stream is written in -- `120 + Space` -- so that a
// recording stays readable, stays reviewable as a diff, and stays valid across
// a renumbering of the enum.
//
// The keyboard, mouse-button, gamepad-button and gamepad-axis names come from
// `platform`'s own tables, which is why those tables and the enum are one
// spelling space. The four this module adds are the ones no device event names:
// `MouseMovement`, `MouseWheel`, `LeftThumbstick` and `RightThumbstick`.
//
// 0 for a name no item carries, and an empty view for `Unknown`.
[[nodiscard]] i32 keyCodeFromName(std::string_view name) noexcept;
[[nodiscard]] std::string_view keyCodeName(i32 keyCode) noexcept;

// What the resolver reads. One frame's worth of device state, owned by the
// system below and rebuilt by `pumpFrame`.
//
// The two delta fields are accumulated rather than sampled: several motion
// events arrive per frame and a tick that read only the last one would lose most
// of a fast flick. They are cleared when a dispatch consumes them, which is why
// `Simulation` and `Render` contexts get their own copies -- see `Snapshot`.
struct DeviceState
{
    std::array<bool, kKeyCodeCount> held{};
    // -1..1 for a stick, 0..1 for a trigger, indexed by `Enum.KeyCode` value.
    std::array<f32, kKeyCodeCount> axis{};
    core::Vec2 pointer;
    core::Vec2 pointerDelta;
    core::Vec2 wheel;
    bool focused = true;
    DeviceType lastDevice = DeviceType::KeyboardMouse;
};

// The system's own state. Held by `app`, handed to `script` the way the physics
// mirror is: `scene` cannot own it (it would make L3 depend on `platform`), and
// a process-global would make two worlds in one process share a keyboard.
class InputSystem
{
public:
    // Folds this frame's events into the snapshot. Idempotent per frame and
    // safe to call with an empty span, which is what a headless run does.
    void pumpFrame(std::span<const platform::Event> events);

    // Replaces the device snapshot wholesale. This is the seam a recorded input
    // stream drives: the replay hands the state a tick should see instead of
    // reading a device, so what is replayed is a keystroke's whole path to the
    // game rather than a bot calling the API underneath it.
    void setSnapshot(const DeviceState& state) noexcept;

    [[nodiscard]] const DeviceState& snapshot() const noexcept { return m_state; }

    // Resolves every `Simulation`-rate context and writes the result into the
    // world. Enqueues a `Change` per action whose value moved.
    void dispatchSimTick(scene::World& world, u64 tick);

    // The same for `Render`-rate contexts. Called once per rendered frame and
    // never in a headless run, because there is no render frame to dispatch on.
    void dispatchRenderRate(scene::World& world);

    // Clears every held input and dispatches, so that anything down is released.
    // Called when the window loses focus: an alt-tab that left a key held is how
    // a character keeps walking into a wall while its window is in the
    // background.
    void releaseAll(scene::World& world);

private:
    void dispatch(scene::World& world, Rate rate);

    DeviceState m_state;
    // Accumulated since the last `Simulation` dispatch and since the last
    // `Render` one, separately: a frame may carry several ticks or none, and a
    // delta consumed by one rate must still be there for the other.
    core::Vec2 m_simPointerDelta;
    core::Vec2 m_simWheel;
    core::Vec2 m_renderPointerDelta;
    core::Vec2 m_renderWheel;

    // Reused across dispatches so a steady-state frame allocates nothing.
    // A context and its priority, sorted highest first by a STABLE sort, so
    // that two contexts at one priority resolve in the order the pool reports
    // them -- which is a pure function of the operation sequence and therefore
    // the same on every run (R10).
    std::vector<std::pair<f32, core::InstanceId>> m_contexts;
    std::array<bool, kKeyCodeCount> m_consumed{};
};

} // namespace luaug::input
