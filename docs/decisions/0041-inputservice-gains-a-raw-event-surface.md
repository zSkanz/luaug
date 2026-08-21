# 0041 — `InputService` gains a raw event surface, fed from the IAS's own pipeline

- Status: accepted
- Date: 2026-08-21
- Amends: ADR 0029 (its "only input model" clause; everything else stands)

## Context
ADR 0029 made the Input Action System the engine's only input model, and M6 built
it: contexts, actions, bindings, resolution order, a per-context dispatch clock
(ADR 0039), and `KeyboardService` deleted as the scaffold it was declared to be.

Two things then happened, hours apart, and together they are this ADR.

**The simple case is expensive.** Reading one key costs a context, an action, a
binding and the parenting between them — fifteen lines for a jump. A sugar
module (`@luaug/input`) was scoped to hide that, on the argument that a second
input path would lose sinking, replay and rebinding.

**And the human named the cost that argument ignored.** This engine's stated
purpose, in the first line of its own README, is the developer experience a
Roblox developer already has. A person arriving from that platform reaches for
`UserInputService.InputBegan` and finds nothing, and no amount of sugar over a
different model answers that. "We are drifting from the focus" is a product
judgement, it is the human's to make, and it is correct.

**What made the technical objection dissolve is where the events come from, not
whether they exist.** The objection was never to events. It was to events read
from the operating system, which arrive on the wall clock, know nothing of what
the UI consumed, and cannot be replayed. Fired from the pipeline the IAS already
runs, every one of those is answered:

- **Sinking already exists.** `938522b6` built it for the UI: `ui` reports
  whether the pointer landed on an element, `input` consumes the mouse codes for
  that frame. Roblox solved the same problem the same way and named it
  `gameProcessedEvent`.
- **Replay already exists.** The recorded input stream is what the M6 gate
  replays. Events derived from that stream replay identically, because they are
  the stream.
- **The clock is a choice we already know how to make**, since ADR 0039 made it
  one for contexts.

What remains is rebinding, and it is real: a key read from a raw event cannot
appear in a remapping screen. Roblox has the identical split —
`UserInputService` is not rebindable there either, and `ContextActionService` is
the answer — so this is a familiar cost rather than a new one.

## Decision

**1. `InputService` gains the surface a Roblox developer reaches for**:
`InputBegan`, `InputChanged` and `InputEnded`, each carrying an `InputObject`
(`UserInputType`, `KeyCode`, `Position`, `Delta`) and a second argument saying
whether the UI already consumed it; plus `IsKeyDown`. `InputObject` is a new
datatype and is a read-only snapshot, not a live object.

**2. They are fed from the IAS's dispatch and never from the OS directly.**
Same source, same frame, after the UI has consumed what it consumed. In a
replay they come from the recorded stream, which is what keeps M6's gate — an
obby run replayed headless to the finish flag — able to see every input a game
reads.

**3. They fire on the `Simulation` clock.** A handler that writes to the world is
then replayable by construction, which is the property R10 exists for and the
reason ADR 0039 made `Simulation` a context's default. A caller who wants
render-rate input for a camera uses an `InputContext` with `Rate = Render`, which
`examples/03-physics-playground` already demonstrates. Firing raw events at
render rate was the alternative and is rejected: it would make the easy path the
non-deterministic one.

**4. The IAS stays, and stays the recommended path for a shipped game.** It is
what the engine's own examples use, it is what a rebinding screen can enumerate,
and it is what binds a keyboard key and a gamepad button to one action. Raw
events are the direct, familiar, unrebindable option beside it — the same shape
Roblox has.

**5. `@luaug/input` is dropped from M6.** It existed to make the simple case
cheap, and `InputService:IsKeyDown` plus the events make it cheap. Shipping both
would be two answers to one question.

## Consequences

- **ADR 0029's "only input model" clause no longer holds** and this ADR is the
  amendment. Its reasoning about *why* an action system is worth having is
  untouched, and clause by clause everything else in it stands.
- **`Enum.UserInputType` joins the IDL**, and `Enum.KeyCode` already spans
  keyboard, mouse and gamepad, which is the table these events need.
- **The rebinding cost is documented where it is read**, in the doc text of the
  events themselves: a key handled here will not appear in a remapping screen,
  and an action will. That is the sentence that lets an author choose.
- **M6's scope shrinks by one item and grows by one**, which is roughly even —
  the sugar module leaves and the service surface arrives. The `InputObject`
  datatype and `Enum.UserInputType` are the new work.
