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

// `Enum.UserInputType`'s values -- what KIND of input an `InputObject` carries
// (ADR 0041).
enum class UserInputType : i32
{
    None = 0,
    Keyboard = 1,
    MouseButton1 = 2,
    MouseButton2 = 3,
    MouseButton3 = 4,
    MouseMovement = 5,
    MouseWheel = 6,
    Gamepad = 7,
    Touch = 8,
};

// One raw input, as `InputService.InputBegan` / `InputChanged` / `InputEnded`
// carry it.
//
// **Produced by the IAS's own dispatch and never read from the OS** (ADR 0041).
// That is the whole reason this surface is allowed to exist: the events come
// from the same snapshot the actions do, on the same tick, after the UI has
// consumed what it consumed -- so they sink correctly, they replay from the
// recorded stream, and a handler that writes to the world is deterministic.
//
// POD and trivially copyable, because it crosses the same kind of seam a
// `scene::Change` does and for the same reason: `script` turns one into a
// userdata, and nothing here may know what a Luau value is (R17).
struct RawInputEvent
{
    enum class Phase : core::u8
    {
        Began,
        Changed,
        Ended,
    };

    Phase phase = Phase::Began;
    UserInputType userInputType = UserInputType::None;
    // `Enum.KeyCode`'s value, or 0 (`Unknown`) for motion and the wheel.
    i32 keyCode = 0;
    // Pointer position in window pixels with `z` carrying the accumulated
    // wheel, or a gamepad axis's deflection. `vector` rather than a `Vector2`
    // because `vector` is the native primitive (ADR 0013) and this is produced
    // several times a tick.
    core::Vec3 position;
    core::Vec3 delta;
    // Whether the interface already took this input -- the second argument of
    // every one of the three events, and the one a handler that ignores it
    // regrets: it is what stops a click on a button also firing the gun and a
    // `w` typed into a text box also jumping.
    bool uiConsumed = false;
};

// The number of `Enum.KeyCode` items. It is not derived from `platform`'s enums
// because it is not their union: the KeyCode table adds two composite sticks
// that no device event names. `input.cpp` static_asserts the arithmetic against
// the generated enum descriptor at registration, which is the check that keeps
// this number honest.
inline constexpr usize kKeyCodeCount = 100;

// Whether a `KeyCode` is one of the four virtual axes or the two composites over
// them -- the ones `InputService:SetVirtualState` may write and nothing else
// may. A HUD button writing to `Space` would be a script pretending to be a
// keyboard, and there would then be no way for anything to tell the difference.
[[nodiscard]] bool isVirtual(i32 keyCode) noexcept;

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

    // Whether one `Enum.KeyCode` is held, by the same rule a `Bool` action
    // applies -- an analogue source counts as down past half deflection. What
    // `InputService:IsKeyDown` answers, and it deliberately ignores what the UI
    // consumed: a poll asks what the HARDWARE is doing, and an event asks what
    // happened to the game.
    [[nodiscard]] bool isKeyDown(i32 keyCode) const noexcept;

    // Writes one of the virtual axes. Ignores a code that is not virtual, which
    // is what keeps the seam one-way: a script drives the four channels the
    // engine set aside for it and nothing else.
    //
    // The value STICKS until it is written again, because a HUD button that is
    // held is a value nobody has cleared -- and `releaseAll` clears it with
    // everything else, so a press cannot survive losing focus.
    void setVirtualState(i32 keyCode, f32 value) noexcept;

    // Resolves every `Simulation`-rate context and writes the result into the
    // world. Enqueues a `Change` per action whose value moved.
    void dispatchSimTick(scene::World& world, u64 tick);

    // The same for `Render`-rate contexts. Called once per rendered frame and
    // never in a headless run, because there is no render frame to dispatch on.
    void dispatchRenderRate(scene::World& world);

    // Whether the UI took the pointer this frame.
    //
    // This is architecture.md §2's "engine-owned high-priority InputContext with
    // Sink", without the Instance: a context in the tree would be an object a
    // game can see, reparent and destroy, and destroying it would silently turn
    // off every button. A flag has the same effect and nothing to break.
    //
    // It consumes the MOUSE codes only. A key pressed while the pointer happens
    // to rest over a HUD is still a key the game gets, which is what stops a
    // health bar from eating the jump button.
    void setPointerCapturedByUi(bool captured) noexcept { m_uiCapturedPointer = captured; }

    // Whether a `TextInput` has focus. The keyboard half of the same claim, and
    // it consumes the KEYBOARD codes for that frame -- so a player typing `w`
    // into a chat box does not also walk forward, which is the same defect the
    // pointer flag fixes one device over.
    void setKeyboardCapturedByUi(bool captured) noexcept { m_uiCapturedKeyboard = captured; }

    // The raw events this tick's `Simulation` dispatch produced, in a stable
    // order: keys first by `KeyCode`, then the pointer, then the wheel, then the
    // gamepad axes. Drained rather than pushed as a `scene::Change`, because a
    // `Change` is sixteen POD bytes about an Instance and an `InputObject` is
    // neither -- the same reasoning `AnimationHost::drainEnded` carries.
    //
    // Empty for a `Render` dispatch: ADR 0041 puts these on the `Simulation`
    // clock so that a handler which writes to the world replays by
    // construction.
    [[nodiscard]] std::span<const RawInputEvent> drainRawEvents() noexcept;

    // Clears every held input and dispatches, so that anything down is released.
    // Called when the window loses focus: an alt-tab that left a key held is how
    // a character keeps walking into a wall while its window is in the
    // background.
    void releaseAll(scene::World& world);

private:
    void dispatch(scene::World& world, Rate rate);

    // Fills `m_rawEvents` from the difference between `m_previous` and the
    // current snapshot. Called at the top of a `Simulation` dispatch, after the
    // UI's claims are known and before any context resolves -- these events
    // describe what the DEVICE did, and a sinking context is a fact about
    // actions rather than about hardware.
    void collectRawEvents(core::Vec2 pointerDelta, core::Vec2 wheel);

    DeviceState m_state;
    // Accumulated since the last `Simulation` dispatch and since the last
    // `Render` one, separately: a frame may carry several ticks or none, and a
    // delta consumed by one rate must still be there for the other.
    core::Vec2 m_simPointerDelta;
    core::Vec2 m_simWheel;
    core::Vec2 m_renderPointerDelta;
    core::Vec2 m_renderWheel;
    bool m_uiCapturedPointer = false;
    bool m_uiCapturedKeyboard = false;

    // What the last `Simulation` dispatch saw, so the next one can tell a press
    // from a hold. Held here rather than derived from the event stream, because
    // the snapshot IS the contract: a replay hands over a state and this has to
    // produce the same events from it as a live device would.
    DeviceState m_previous;
    bool m_hasPrevious = false;
    // Whether each held input was UI-consumed when it began, so that
    // `InputEnded` reports what `InputBegan` reported -- a press that started on
    // a button is still consumed when it is released off one.
    std::array<bool, kKeyCodeCount> m_beganConsumed{};
    std::vector<RawInputEvent> m_rawEvents;
    std::vector<RawInputEvent> m_rawDrained;

    // Reused across dispatches so a steady-state frame allocates nothing.
    // A context and its priority, sorted highest first by a STABLE sort, so
    // that two contexts at one priority resolve in the order the pool reports
    // them -- which is a pure function of the operation sequence and therefore
    // the same on every run (R10).
    std::vector<std::pair<f32, core::InstanceId>> m_contexts;
    std::array<bool, kKeyCodeCount> m_consumed{};
};

} // namespace luaug::input
