# Signals and connections

A `Signal` is how anything in LuauG tells you something happened. Every engine
event is one, and `Signal.new` gives you your own.

```luau
local connection = part.Touched:Connect(function(other: BasePart)
    print(other.Name, "touched it")
end)

connection:Disconnect()
```

`Signal.Connect` returns a `Connection` with `Connection.Disconnect` and a
read-only `Connection.Connected`. `Signal.Once` disconnects itself on
invocation. `Signal.Wait` parks the calling coroutine until the next fire and
returns that fire's arguments.

Your own signals are values, not instances — there is no `BindableEvent`:

```luau
local scored: Signal<number, string> = Signal.new()

scored:Connect(function(points: number, reason: string)
    print(`+{points} for {reason}`)
end)

scored:Fire(10, "checkpoint")
```

## Deferred-only, and what that means

**Nothing in LuauG fires immediately.** There is no immediate mode, no
configuration to turn one on, and no plan to add one. A fire is *enqueued*, and
handlers run later, at a resumption point.

That single rule is what the rest of this page is about, because it is the one
thing that will surprise somebody arriving from an engine with two modes. It is
argued out in full on [Signals are deferred-only](manual:why/deferred-signals);
what follows is the contract.

### One queue

There is exactly one deferred queue. Engine-raised fires, your own
`Signal.Fire`, and `task.defer` callbacks all enter the *same* queue in the
order they were raised, with no priority and no per-signal queues.

That is what makes this well defined:

```luau
myEvent:Fire("first")
part.Parent = workspace  -- raises ChildAdded second
```

The `ChildAdded` handler runs after the `myEvent` handler, because that is the
order the two were raised.

### Where it drains

The queue drains at each resumption point of the frame: `RunService.PreRender`
at render rate, then per simulation tick `RunService.PreAnimation`,
`RunService.PreSimulation`, `RunService.PostSimulation` and
`RunService.Heartbeat`. `task` timers resume in their own phase between
`PostSimulation` and `Heartbeat`, and anything they defer drains at
`Heartbeat`. [The frame, phase by phase](manual:concepts/frame) lays the whole
sequence out.

### A drain runs to fixpoint

A handler that fires another signal appends to the same queue, and the drain
keeps going until the queue is empty. It does not snapshot the queue and stop at
its original end.

### A drain does not block on a yielding handler

A handler that calls `task.wait`, `Signal.Wait`, or any other yielding call is
left parked, and the drain moves straight to the next entry. The parked
coroutine resumes later on its own terms.

If a drain blocked, one `task.wait(5)` in one listener would stall every other
listener of every other signal — which is exactly the property the fixed tick
exists to prevent.

### What a fire captures

Enqueuing a fire records its arguments **and the identity of the connection list
at that moment**. Two consequences, and they are the two people rely on:

- A connection made **after** the fire does not run for it. It was not listening
  when the thing happened.
- A connection disconnected **before it is invoked** does not run — including a
  disconnect performed by an earlier handler in the same drain.
  `Connection.Disconnect` is reliable, not advisory.

Arguments are captured, **not copied**. Tables and instances pass by reference,
so mutating a table between `Fire(t)` and the drain is visible to the handlers.
Pass values you do not intend to change.

### Order among handlers

Handlers of one fire run in the order they were connected. Order *between
different signals* is queue order, not connection order — relying on it is a
bug waiting for a scheduling change.

`Signal.Wait` registers a one-shot at the moment of the call and takes its place
among the connections by registration order: a `Wait` registered after handler A
and before handler B resumes after A runs and before B does.

### Errors are contained

Each handler runs on its own coroutine. An error in one handler does not stop
the other handlers of that fire, does not stop the drain, and does not stop the
script that fired. It goes to the console and to `DebugService.MessageOut` with
its traceback.

A handler that errors **stays connected** — an error is a fact about one
invocation, not a disconnect. A `Once` handler that errors has still been
consumed.

### Re-entrancy is capped at ten

Every queue entry carries a depth. An entry raised outside any handler has depth
0; one raised by a handler running at depth *d* has depth *d*+1. An entry that
would exceed depth 10 is **dropped** and logs `script.err.reentrancy_limit`.

So a handler that re-fires its own signal is invoked **exactly eleven times**,
and the twelfth fire is the one dropped. The cap counts `task.defer` callbacks
too — without that, a callback that defers again would be an unbounded drain,
which is not a wrong number but a hang.

Because it is a generation depth rather than a call-stack depth, a wide fan-out
never trips it.

## Destroy and queued fires

`Instance.Destroy` enqueues `Instance.Destroying`, then closes the instance's
other signals. Fires already queued for them find no live connections and invoke
nothing.

Until the end of that drain the handle still works, so `Connect` still succeeds
— the new handler simply does not run for the already-enqueued fire, which
captured the connection list before that connection existed. After the drain,
every access raises `script.err.instance_dead`.

`Signal.Destroy` on a signal you made follows the same rule, and every
`Connection` reports `Connected == false` once the drain ends.

## Disconnecting is your job

A connection holds its handler alive. A system that connects per spawned
instance and never disconnects is the standard leak, and the standard fix is to
keep the `Connection` beside whatever it belongs to and disconnect in the same
place you destroy it.

`Instance.Destroying` is the hook for that, and `Signal.Once` is the right tool
whenever the answer is "the first one only".
