# Raw input, and when to use it

`InputService` carries a raw event surface beside the Input Action System. It is
the direct, familiar option, and it is the right one for a prototype, a debug
key, or anything a player will never remap.

**It is not the one a shipped game should be built on.** An action is what can be
rebound, prompted and recorded; a key code is not.

## Polling

```luau
--!strict
local InputService = game:GetService("InputService")

if InputService:IsKeyDown(Enum.KeyCode.LeftShift) then
    -- sprinting
end
```

`InputService.IsKeyDown` is whether that key, button or trigger is held as of the
current tick. An analogue source counts as down past half deflection — the same
rule a boolean action applies.

**It reads the device snapshot and ignores what the UI consumed**, which is the
opposite of what the events below do. A poll asks what the hardware is doing; an
event asks what happened to the game. If you want the UI-aware answer, you want
an `InputAction`.

## The events

```luau
--!strict
InputService.InputBegan:Connect(function(input: InputObject, uiConsumed: boolean)
    if uiConsumed then
        return
    end
    if input.KeyCode == Enum.KeyCode.F1 then
        toggleDebugPanel()
    end
end)
```

| Event | Fires on |
|---|---|
| `InputService.InputBegan` | A key, button or touch going down. |
| `InputService.InputChanged` | Movement: pointer, wheel, an analogue axis. |
| `InputService.InputEnded` | The same input going up. |

Each carries an `InputObject` and a `uiConsumed` boolean.

**These events come from the Input Action System's own dispatch, not from the
operating system.** Same source, same tick, after the UI has taken what it took
— and in a replay they come from the recorded stream, so a recorded run
reproduces every input a game reads.

They fire on the **simulation** clock, so a handler that writes to the world
replays exactly. Camera look at render rate is an `InputContext` with
`Rate = Enum.InputRate.Render`, not a raw handler.

`uiConsumed` on `InputEnded` carries what it carried when the input began, so a
press that started on a button is still marked consumed when it is released off
one.

## InputObject

A read-only snapshot of one input on one tick. A handler that stashes one is
holding a fact rather than a handle.

| Property | Means |
|---|---|
| `InputObject.KeyCode` | Which key, button or axis. |
| `InputObject.UserInputType` | What kind of input it was. |
| `InputObject.Position` | The pointer in window pixels, origin top-left, with `z` carrying accumulated wheel. For a gamepad axis, the deflection instead. |
| `InputObject.Delta` | The change since the last event. |

`Position` is a three-wide vector rather than a `Vector2` because that is the
engine's native primitive — no userdata and no allocation, which matters on a
value produced several times a tick.

## The pointer

```luau
--!strict
local where = InputService:GetPointerPosition()

InputService.PointerLocked = true    -- captured for mouse-look
InputService.PointerVisible = false
```

`InputService.PointerLocked` captures the pointer for relative motion, which is
what a first-person camera wants. `InputService.PointerVisible` shows or hides
the cursor. They are separate because hiding a cursor and capturing it are
different decisions.

## Window focus

```luau
InputService.WindowFocusChanged:Connect(function(focused: boolean)
    if not focused then
        pauseGame()
    end
end)
```

Losing focus also clears held state, so a key held when the player alt-tabbed
does not stay held forever.

## When to reach for this

- A debug key that will never ship.
- A prototype before the control scheme exists.
- Something genuinely device-specific — reading raw wheel deltas, say.

And when not to: anything a player might want to rebind, anything a gamepad
should also do, anything a prompt should be able to describe, and anything that
must survive being replayed.

## Where to look next

- [Actions, bindings and contexts](manual:input/actions) — the model to prefer
- [`InputService`](api:InputService) · [`InputObject`](api:InputObject)
