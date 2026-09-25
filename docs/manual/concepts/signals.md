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

`Signal:DisconnectAll()` disconnects every connection of a signal at once. The
signal stays usable, and nothing fires.

## Collector: cleaning up in one place

A system that makes connections, instances and threads usually has to undo all
of them together. A `Collector` holds them and ends each one the way it is
ended (ADR 0094):

| What you add | What cleaning does |
|---|---|
| a function | calls it |
| a thread | cancels it |
| a `Connection` | disconnects it |
| an `Instance`, a `Signal`, or a table with `Destroy` | destroys it |
| a `Promise` | cancels it |

```luau
--!strict
local enemy = Instance.new("Part")
enemy.Parent = workspace

local collector = Collector.new()
collector:Add(enemy.Touched:Connect(function(other: Instance)
    print("hit", other.Name)
end))
collector:Add(function()
    print("cleaned up")
end)

-- Everything above ends when the enemy is destroyed.
collector:LinkToInstance(enemy)
```

- `Add(object, method?, key?)` takes an object and returns it. `method` names
  another cleanup method, or `true` calls the object itself. `key` names the
  entry for `Get` and `Remove`, and adding under a key that is already held
  cleans what was there first.
- **`Cleanup` runs newest first**, the order destructors run in, and the same on
  every run: a connection made after the instance it listens to is disconnected
  before that instance is destroyed. The collector stays usable. `Destroy`
  cleans and retires it.
- `LinkToInstance(instance)` cleans the collector when that instance is
  destroyed.

## Promise: work that finishes later

A `Promise` stands for a value that is not ready yet: a delay, a request, the
next fire of a signal. Its constructors are namespace functions and its methods
are PascalCase, as the casing rule asks.

```luau
--!strict
local door = Instance.new("Part")
door.Parent = workspace

Promise.delay(2)
    :AndThen(function()
        door.Anchored = false
        return "opened"
    end)
    :Catch(function(problem)
        warn("the door did not open:", problem)
    end)

-- Parks this thread until the first touch, then goes on.
local ok, other = Promise.fromEvent(door.Touched):Await()
if ok then
    print("touched by", (other :: Instance).Name)
end
```

- **The executor runs at once, on its own thread**, so it may yield. Resolving
  with another promise adopts it, and a handler that errors rejects the promise
  it returns.
- **Waiting**: `Await` gives `true` and the values, or `false` and the
  rejection. `AwaitStatus` gives an `Enum.PromiseState` in place of the boolean.
  `ExpectAsync` gives the values or raises.
- **Combining**: `Promise.all`, `allSettled`, `race`, `any` and `some`.
  Retrying: `retry` and `retryWithDelay`.
- **Cancelling**: `Cancel` stops the executor and runs its cancel hook, cancels
  every consumer, and cancels the parent once none of its consumers is left.
- **Time is the simulation's**: `Promise.delay` and `Timeout` run on the tick
  clock, like `task.delay`, never on the wall clock (R10).
- A rejection nothing handled is reported with a warning after the drain.

Both are part of the engine, installed as globals. There is nothing to
`require`.
