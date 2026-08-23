# Actions, bindings and contexts

There is exactly one input model here, and it is built on a single idea:

> **A script asks what the player *did*, not which key they pressed.**

That is the Input Action System. There is no separate user-input service, no
context-action service and no mouse object — one model, rebindable and
promptable by default.

## The three classes

| Class | Is |
|---|---|
| `InputAction` | A named thing the player can do. |
| `InputBinding` | One physical input that drives its parent action. |
| `InputContext` | A group of actions that are live together. |

They nest, and the nesting is the configuration:

```luau
--!strict
local InputService = game:GetService("InputService")

local gameplay = Instance.new("InputContext")
gameplay.Name = "Gameplay"
gameplay.Parent = InputService

local jump = Instance.new("InputAction")
jump.Name = "Jump"
jump.Type = Enum.InputActionType.Bool
jump.Parent = gameplay

local space = Instance.new("InputBinding")
space.KeyCode = Enum.KeyCode.Space
space.Parent = jump

local pad = Instance.new("InputBinding")
pad.KeyCode = Enum.KeyCode.ButtonSouth
pad.Parent = jump
```

Two bindings, one action. Neither the game code nor the player has to care which
one fired.

## Reading an action

Two ways, and they answer different questions.

**Ask for the value**, when you need it every tick:

```luau
--!strict
local RunService = game:GetService("RunService")

RunService.Heartbeat:Connect(function(dt: number)
    local direction = move:GetState() :: Vector2
    character:Move(vector.create(direction.X, 0, -direction.Y) * dt * 60)
end)
```

**Be told about the edge**, when you need the moment it changed:

```luau
jump.Pressed:Connect(function()
    if character.Grounded then
        character:Jump()
    end
end)
```

`InputAction.GetState` returns the action's value as of the current tick: a
boolean for a `Bool` action, a number for `Direction1D`, a `Vector2` for
`Direction2D`, and a vector for `Direction3D`.

It is a **snapshot**: two calls inside one tick agree, and a recorded input
stream hands the same answers back with no hardware attached — which is what
makes a replay a replay of input rather than of a bot.

`InputAction.Pressed`, `InputAction.Released` and `InputAction.StateChanged` are
the edges, and they are events rather than properties — a press is a moment, not
a value you can read later.

`InputAction.Enabled` turns one action off without disturbing its context.

## Action types

`Enum.InputActionType` decides what an action's value *is*, and therefore what
its bindings mean:

| Type | Value | A binding contributes |
|---|---|---|
| `Bool` | `boolean` | Pressed past half deflection. |
| `Direction1D` | `number` | One axis. |
| `Direction2D` | `Vector2` | Two axes, or a composite of four keys. |
| `Direction3D` | `vector` | Three axes. |

A binding on a `Bool` action counts as pressed past half deflection — which is
the rule **every** analogue source follows, so a trigger, a stick and a virtual
button all behave the same way.

## Composite bindings

A binding does not have to be one key. `InputBinding.Up`, `InputBinding.Down`,
`InputBinding.Left` and `InputBinding.Right` make four keys into one
two-dimensional value:

```luau
--!strict
local move = Instance.new("InputAction")
move.Name = "Move"
move.Type = Enum.InputActionType.Direction2D
move.Parent = gameplay

local keys = Instance.new("InputBinding")
keys.Up = Enum.KeyCode.W
keys.Down = Enum.KeyCode.S
keys.Left = Enum.KeyCode.A
keys.Right = Enum.KeyCode.D
keys.Parent = move

local stick = Instance.new("InputBinding")
stick.KeyCode = Enum.KeyCode.LeftThumbstick
stick.Parent = move
```

`InputBinding.Scale` multiplies a binding's contribution, which is how one
binding becomes "walk" and another "sprint" without a second action.

## Contexts

An `InputContext` is what makes a menu opening stop the world from receiving
movement.

| Property | Means |
|---|---|
| `InputContext.Enabled` | Whether its actions resolve at all. |
| `InputContext.Priority` | Which context sees an input first. Highest first. |
| `InputContext.Sink` | Whether an input it consumes is hidden from lower contexts. |
| `InputContext.Rate` | Which clock it is dispatched on. |

```luau
--!strict
local menu = Instance.new("InputContext")
menu.Name = "Menu"
menu.Priority = 100
menu.Sink = true          -- swallow what it uses
menu.Enabled = false
menu.Parent = InputService

-- Opening the menu:
menu.Enabled = true
gameplay.Enabled = false
```

Ties in `Priority` are broken by a stable order the engine reproduces on every
run, so two contexts at the same priority resolve the same way every time rather
than depending on which was created first.

## Which clock an action fires on

`InputContext.Rate` is `Enum.InputRate`, and it has two items:

- **`Simulation`** — the sim tick, with the fixed timestep. **The default,
  because it is the safe one**: an action nobody thought about fires where
  determinism holds and where a replay can see it. Everything a game *decides*
  with belongs here.
- **`Render`** — the render frame, at whatever rate the display managed. This is
  for camera look, where the extra samples are the whole point and where nothing
  written is simulation state.

It is a property of the **context**, not of the action, so a whole group of
camera actions is switched at once.

## The UI gets first refusal

While the pointer is over any UI element, the pointer is already consumed before
any context resolves — so a button drawn over the world does not also fire the
gun. While a text field has focus, the keyboard is consumed the same way.

That behaves like the highest-priority sinking context without being one. See
[Buttons and interaction](manual:ui/interaction).

## Where to look next

- [Rebinding and prompts](manual:input/rebinding)
- [Raw input, and when to use it](manual:input/raw)
- [There is one input model](manual:why/input-model) — why it is shaped this way
- [`InputAction`](api:InputAction) · [`InputContext`](api:InputContext)
