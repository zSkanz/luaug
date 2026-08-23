# Signals are deferred-only

Nothing here fires immediately. A fire is enqueued, and handlers run at the next
resumption point of the frame.

There is no immediate mode, no setting that turns one on, and no plan to add
one.

## The problem with two modes

An engine that offers both immediate and deferred signals has not made a choice
— it has moved the choice onto every author of every library, and then made
every combination of those choices somebody else's debugging session.

Immediate delivery means a handler runs **inside** whatever raised the signal.
That has three consequences, and none of them is theoretical:

- **Re-entrancy is normal rather than exceptional.** A handler that parents an
  instance raises `ChildAdded` inside the parent assignment, and the tree is
  observed halfway through being mutated.
- **A yielding handler stalls its raiser.** One `task.wait(5)` in one listener
  and everything waiting behind it is late.
- **Order depends on the call stack**, which depends on which system happened to
  run first — so the answer changes when an unrelated system is added.

Deferred delivery removes all three by construction. The queue is drained at
known points, in the order things were raised, on coroutines the raiser is not
standing on.

## What you get instead

**One order, and it is written down.** Engine fires, your own `Signal.Fire`, and
`task.defer` all enter the same queue in raise order. So this is well defined
rather than incidental:

```luau
myEvent:Fire("first")
part.Parent = workspace   -- ChildAdded is second, because it was raised second
```

**A drain that cannot stall.** A handler that yields is parked and the drain
moves on. Drain duration stays bounded, which is exactly the property the fixed
tick exists to protect.

**A fire that captured its world.** Enqueuing records the arguments *and* the
connection list. A connection made after the fire does not run for it; a
connection disconnected before it is invoked does not run. `Disconnect` is
reliable rather than advisory.

**A bounded loop.** Every queue entry carries a generation depth, and depth is
capped at ten. A handler that re-fires its own signal runs exactly eleven times
and the twelfth is dropped with a logged message. Not a hang, and not an
arbitrary number to discover at three in the morning.

## What it costs

**A handler does not run "now".** Code that assigns a property and immediately
expects a handler to have run is wrong here, and the fix is to do the work
rather than to signal about it.

**A tree mutation is visible before its signal.** `Destroy` is synchronous —
when it returns, `Parent` is already `nil` — and only the signals are deferred.
So the world and the notifications about it are briefly out of step, in one
direction, predictably.

That is the trade, stated plainly: you give up "the handler ran before this line
returned", and you get an order you can reason about, a drain that cannot stall,
and a world that replays.

## Where it puts the rules

The full contract — what a fire captures, connection order, `Once`, `Wait`,
`Destroy` against a queued fire — is in
[Signals and connections](manual:concepts/signals). It is written as a contract
rather than as documentation because the conformance suite is written from it,
and because the engine's own world hash depends on every rule in it.

## Where to look next

- [Signals and connections](manual:concepts/signals)
- [The frame, phase by phase](manual:concepts/frame) — where the drains are
- [The tick is fixed](manual:why/fixed-tick)
