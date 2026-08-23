# Buttons and interaction

Three events, all on `UIObject`, all carrying **no arguments**:

| Event | Fires when |
|---|---|
| `UIObject.Activated` | A pointer press **and** release both land on this object. |
| `UIObject.PointerEntered` | The pointer moves onto it. |
| `UIObject.PointerExited` | The pointer moves off it. |

```luau
--!strict
local button = Instance.new("TextButton")
button.Size = UDim2.new(0, 120, 0, 40)
button.Text = "START"
button.Parent = screen

button.Activated:Connect(function()
    startGame()
end)
```

They are on `UIObject` rather than on the button classes because **anything can
be pressed** — a plain `Frame` fires `Activated` too. `TextButton` and
`ImageButton` add no property; they exist because they are the classes a reader
recognises.

They are **device-neutral by design**: a click, a tap and a bound gamepad button
all produce `Activated`, which is why there is no mouse-button-specific event.
For the same reason the hover pair is named for a *pointer* rather than a mouse
— a finger is one too.

Like every signal here, all three are **deferred**: your handler runs at the
next resumption point, not inside the click.

## A press that slides off is cancelled

`Activated` requires the press and the release on the **same** element. A press
that started on a button and released elsewhere is a cancelled press — which is
what every UI in the world does, and what people rely on to change their mind.

## What answers a click

Hit-testing walks every enabled `ScreenGui`, ordered by
`ScreenGui.DisplayOrder`; within one tree the topmost `UIObject.ZIndex` wins,
ties broken by document order.

Three rules decide whether an element is reachable at all:

- **`Visible = false` prunes it and its descendants.** A transparent background
  does not: `BackgroundTransparency = 1` still hit-tests.
- **Clipping removes what is clipped away.** An element scrolled off the end of
  a `ScrollFrame` does not answer a click that lands where it would have been.
- Interaction runs **after** layout, deliberately — a hit test against last
  frame's rectangles is a click that lands where a button used to be.

## Hover is a pair of edges

When the pointer moves from one element to another, `PointerExited` fires on the
old one and then `PointerEntered` on the new one. A destroyed element stops
being hovered, pressed or focused rather than leaving a stale reference behind.

## Focus

Focus moves on a **press**. Pressing anywhere that is not a `TextInput` —
including empty space — drops it and fires `TextInput.FocusLost`.

```luau
field.Focused:Connect(function()
    -- keystrokes come here now
end)

field.FocusLost:Connect(function()
    commit(field.Text)
end)
```

`FocusLost` is declared with a `submitted` boolean meaning "left by pressing
Return rather than by clicking away". **That argument is not delivered in this
release** — the event is raised with no arguments, so `submitted` arrives as
`nil` either way. Treat it as the fact that focus ended.

## The UI and the rest of the input system

While the pointer is over any `UIObject`, the UI **claims the pointer**: mouse
buttons, movement and wheel are marked consumed before any `InputContext`
resolves. So a button drawn over the world does not also shoot the gun — the one
behaviour every UI system is judged by.

While a `TextInput` has focus, the UI claims the **keyboard** the same way.

Two consequences worth knowing:

- **Mouse codes only, unless a field is focused.** A key pressed while the
  pointer merely rests over a HUD is still the game's.
- `InputService.IsKeyDown` deliberately **ignores** what the UI consumed. A poll
  asks what the hardware is doing; an event asks what happened to the game. The
  raw events carry a `uiConsumed` flag so a handler can tell.

## Driving an action from a button

There is no property binding a `UIObject` to an `InputAction`. The shipped path
is a **virtual key**, which is a real input the whole system already understands:

```luau
--!strict
local InputService = game:GetService("InputService")
local RunService = game:GetService("RunService")

touchJump.Activated:Connect(function()
    InputService:SetVirtualState(Enum.KeyCode.Virtual1, 1)
end)

-- Cleared once per tick, so the action sees exactly one press.
RunService.Heartbeat:Connect(function()
    InputService:SetVirtualState(Enum.KeyCode.Virtual1, 0)
end)
```

The value binds through an ordinary `InputBinding`, resolves in the ordinary
order, is eaten by an ordinary sinking context, and is carried by a recorded
stream — which is what makes it one input model rather than two.

`SetVirtualState` carries a **value, not a press**: write 1 and 0 for a button,
anything between for a slider or a thumbstick axis. Bound to a boolean action it
counts as pressed past half deflection.

## Where to look next

- [Actions, bindings and contexts](manual:input/actions)
- [The UI tree](manual:ui/tree)
- [`UIObject`](api:UIObject)
