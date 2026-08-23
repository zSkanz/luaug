# There is one input model

Every input in this engine goes through the Input Action System. There is no
second path — no separate user-input service to poll, no context-action service
to register with, no mouse object.

## What the alternatives cost

An engine with three input APIs has three answers to "did the player press
jump", and they disagree in ways nobody can enumerate:

- **A polling API** cannot know whether the UI already used that click.
- **A registration API** and a polling API resolve in different orders, so which
  one wins depends on which system happened to run first.
- **A device-specific object** — a mouse, a keyboard — makes every feature
  written against it device-specific, and a gamepad becomes a port rather than a
  binding.

And all three make rebinding a feature you add later, to code that assumed a
key.

## What one model buys

**Rebinding is assigning a property.** An `InputBinding` is an instance; change
its `KeyCode` and the next tick resolves against the new key. There is no
registration to redo and no cache to invalidate — because there was never a
second path that cached anything.

**A prompt has something to read.** `InputAction.GetPreferredBinding` answers
"which binding should I draw for the device this player is holding", which is
only answerable because bindings are data.

**The UI gets first refusal, once.** While the pointer is over a UI element, the
pointer is consumed before any context resolves — so a button drawn over the
world does not also fire the gun. That is the single behaviour every UI system
is judged by, and it is the one that is embarrassing to add afterwards.

**An input replays.** `GetState` is a snapshot, and in a replay it comes from a
recorded stream with no hardware attached. That is what makes a recorded run a
replay of *input* rather than of a bot.

**A touch button is not a special case.** An on-screen control writes a virtual
key with `InputService.SetVirtualState`, and that value binds through an
ordinary binding, resolves in the ordinary order, is eaten by an ordinary
sinking context, and is carried by the recorded stream. A seam that reached past
the device snapshot would be a control the replay could not see.

## Contexts, rather than modes

A menu opening does not switch an input mode. It enables a context with a higher
priority and `Sink` on, and the gameplay context stops seeing what the menu
uses:

```luau
menu.Enabled = true
gameplay.Enabled = false
```

That composes where a mode does not: three overlapping contexts are three
priorities, and adding a fourth does not require anybody to revisit the other
three.

Ties in priority are broken by a stable order the engine reproduces on every
run — not by creation order, which would make behaviour depend on script
scheduling.

## The clock is a property of the context

`InputContext.Rate` decides whether a group of actions dispatches on the
simulation tick or on the render frame. It defaults to **simulation**, because
that is the safe one: an action nobody thought about fires where determinism
holds.

Camera look is the case that wants render rate, and it wants it for the whole
group of camera actions at once — which is why the property is on the context
rather than on the action.

## The raw surface still exists

`InputService.InputBegan` and friends are there, and `InputService.IsKeyDown` is
there. They are the direct, familiar option, and they are right for a debug key
or a prototype.

But they are **fed from the action system's own dispatch**, not from the
operating system: same source, same tick, after the UI has taken what it took,
and in a replay they come from the recorded stream. One pipeline with two
surfaces, rather than two pipelines.

## Where to look next

- [Actions, bindings and contexts](manual:input/actions)
- [Rebinding and prompts](manual:input/rebinding)
- [Raw input, and when to use it](manual:input/raw)
