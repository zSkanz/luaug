# 0039 — An InputContext declares its dispatch rate, and the IAS's enums are total

- Status: accepted
- Date: 2026-08-20

## Context
ADR 0029 makes the Input Action System the engine's only input model, and
`api-design.md` §2.4 specifies its three classes. Implementing them at M6 found
three places where the specification as written cannot be built without deciding
something it does not say.

**1. Which clock a context is dispatched on.** ADR 0029 says "Input dispatch is
split: render-rate (UI/camera contexts) and deterministic sim-tick (gameplay
contexts)", and `architecture.md` §2 describes the render-rate half as firing
"UI/camera-priority contexts". Read literally that makes `Priority` select the
clock. It cannot: `Priority` orders *fallthrough*, and one number cannot answer
two questions. A low-priority context that still wants render-rate mouse delta
would be inexpressible, and — worse — re-tuning a priority for an unrelated
reason would silently move an action from the deterministic clock to the
variable one. That is a change in an action's **determinism class** made by
someone editing a number about layering.

**2. Optional enum properties.** §2.4 writes `KeyCode: Enum.KeyCode?` and
`DeviceType: Enum.InputDeviceType?`. The engine's property system has a closed
value domain (`scene::Value`), and `nil` is a member of it only for `Instance`,
where an unparented `Parent` needs it. There is no `EnumItem` representation of
"absent": `toValue` for an enum-typed property refuses `nil`, which means an
optional enum property could be declared and never actually set to nothing.

**3. What `InputAction.StateChanged` carries.** §2.4 writes
`StateChanged(newValue)`. The queue between `scene` and `script` carries 16-byte
POD facts and no Luau values — that shape is what keeps L3 free of the VM
(`change_queue.h`), and it is not an implementation detail but the reason the
split exists at all. An action's value is one of four types. A fire whose
argument had to be rebuilt when the queue drained would be a fire that captured
nothing, which is precisely the trap §3.1 warns about.

## Decision

**1. `InputContext` gains `Rate: Enum.InputRate`**, with items `Simulation` and
`Render`, defaulting to `Simulation`. `Enum.InputRate` joins §2.3's enum list.
The default is the safe one: an action nobody thought about fires on the sim
tick, where R10 holds and the input replay can see it. `Render` is opt-in for
camera look and for UI, and the property's own documentation states that a
gameplay decision taken from a render-rate action is frame-rate-dependent by
construction and is not recorded by the replay — a render frame is not a unit
the replay has.

**2. The IAS's enum-typed properties are total, and one is derived.**

- `InputBinding.KeyCode`, `Up`, `Down`, `Left` and `Right` are
  `Enum.KeyCode` and not optional. `Enum.KeyCode` carries an explicit `Unknown`
  item, which is what "unbound" means, so an unset binding is expressible
  without the value domain gaining a second kind of nothing.
- `InputBinding.DeviceType` is `Enum.InputDeviceType`, **read-only, and derived
  from `KeyCode`**. Gamepad buttons, axes and sticks report `Gamepad`;
  everything else, `Unknown` included, reports `KeyboardMouse`. A settable
  device type could disagree with the key beside it — `DeviceType = Gamepad` on
  a binding whose `KeyCode` is `W` — and the engine would then have to choose
  which of the two to believe. Deriving it means the question never arises and
  `GetPreferredBinding` cannot be given a wrong answer to work from.
- `InputBinding.Image` is `Content` and not optional; `Content` is an alias of
  `string` in v1 and the empty string is "none".

**3. `InputAction.StateChanged` carries no arguments.** The handler reads
`GetState()`. This is the arrangement `Destroying` and
`GetPropertyChangedSignal` already use, and §2.2 already gives the reason: it
keeps the signal's type independent of the value's. The value read in the
handler is the value that caused the fire, because input dispatches exactly once
per tick and the drain that carries the fire is the same tick's.

`api-design.md` §2.4 is edited to match, in the commit that ships this.

## Consequences
The three classes are implementable exactly as documented, with no member that
accepts a write and means nothing.

The cost is one enum and one property more than §2.4 listed, and a `StateChanged`
that is one call less convenient than a handler argument. The benefit on the
first is that "which clock" becomes a thing a reviewer can see in a diff rather
than a thing derived from a number; on the second, that `GetPreferredBinding`
answers from a fact rather than from an annotation somebody had to keep true.

A note for whoever ships `InputBinding.UIButton`, which §2.4 also lists and this
milestone defers to the UI step: it is an `Instance?` reference and therefore
genuinely optional, which the value domain already supports. Nothing here
applies to it.

`input::saveBindings`/`loadBindings`, sketched in `architecture.md` §2, are not
implemented in v1 and are removed from that sketch in the same commit. Runtime
rebinding is a property write — `binding.KeyCode = Enum.KeyCode.J` — and
persistence is the game serializing its own settings. An engine-side pair with
no caller would be speculative (§5), and the two things it would have done are
things a game can already do.
