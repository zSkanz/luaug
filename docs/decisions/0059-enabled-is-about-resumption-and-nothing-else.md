# 0059 — `Enabled` is about resumption, and nothing else

- Status: accepted
- Date: 2026-08-24
- Extends: 0058 (a script runs when you press play)
- Relates to: 0057 (a script is an instance), 0050 (a script carries its `Source`)

## Context

`Script.Enabled` gates `startScripts` and does nothing afterwards. The IDL says
so rather than being silent about it:

> whether this script starts when the world does; **writing it afterwards has no
> effect in v1**, neither stopping a running script nor starting one that did not
> run, because what sets it acts before the start it applies to.

The human asked for Roblox parity, and Roblox's behaviour is narrower and more
mechanical than "it stops the script":

- Setting `Disabled = true` on a running script **drops the engine's references
  to that script's threads.** A thread that is yielded, or that yields later, is
  never resumed. A thread *actively executing* runs on until it next yields or
  returns.
- **Connections are not disconnected.** They stop being invoked because nothing
  resumes the threads that would run them. Roblox tells developers to disconnect
  manually if they need it.

So the contract is one sentence — *stop resuming this script's threads* — and it
is deliberately not "unload the script" and not "undo what it did".

That is the right contract to copy, and the reason is not familiarity. Anything
larger answers a question this repository has twice declined to answer by
accident: **whether a connection should die with the thing its closure
captures.** ADR 0058 says that is a separate question. A property setter is the
worst place to answer it.

## Decision

**`Enabled` decides whether this script's threads are resumed. It decides
nothing else.**

Six rules, and five of them exist to keep the sixth small.

### 1. False to true, after boot, starts that script

On its own coroutine, through the same path `startScripts` uses for one instance
(ADR 0057). Its file scope runs **now**, against the world as it is — not
against the world at boot.

**A re-enable is a start, not a resume.** A script that ran, was disabled and is
enabled again runs its file scope a second time. There is no old thread to hand
back, and pretending otherwise would make `Enabled` two different operations
depending on history.

### 2. True to false stops resumption, and nothing else

The script's threads are never resumed again. A thread already executing runs to
its next yield or return — a coroutine cannot be preempted, here or in Roblox.

**Nothing is disconnected, nothing is destroyed, and nothing it made is undone.**
What it built stays in the world. That is what makes the rule one sentence.

### 3. It takes effect at the next deferred drain, in document order

An enable is a property write, so it lands in the tick stream like any other, and
that is what makes a replay reproduce it (R10). But *when* inside the tick has to
be decided rather than fall out of the implementation:

**At the deferred drain**, which is where boot already starts scripts. If several
scripts are enabled by one tick's writes, they start in
`World::collectDescendants` order — depth-first preorder, the same order
`startScripts` uses and the same order the `Find` family tie-breaks on.

Pool order would be an allocation artefact reaching observable output, which R10
forbids and which ADR 0057 already refused for the same reason.

### 4. A queued resumption belonging to a disabled script is DROPPED at the drain

`task.defer`, `task.delay` and a signal fire are all queue entries. A disabled
script's entries are not swept out of the queue when it is disabled; they are
**discarded when they come up**.

Lazy rather than eager, for two reasons. It matches "never resumed" exactly
rather than approximating it. And a sweep would make `task.cancel` mean something
different before and after a disable — a thread cancelled after being swept would
raise `script.err.task_not_scheduled` for a thread the caller still holds.

The queue does not grow, because a dropped entry is a drained one.

### 5. While the editor is stopped, `Enabled` is a scene edit and nothing more

ADR 0058 made play the thing that starts scripts. So a toggle in the properties
grid while stopped changes **what the next play starts**, which is already how
`Enabled` works and needs no new machinery. Rules 1 through 4 are about a toggle
*during* play.

### 6. The properties grid agrees with all of the above

A checkbox that does nothing is worse than one that is absent, and today it does
nothing after boot. Once this lands it does something in both states, which is
what the grid was always claiming.

## Consequences

**The IDL sentence stops being true and has to be rewritten.** It is currently
the honest description of a limit; it becomes a wrong description of a feature.
It is also, usefully, the test for whether this shipped.

**A script can now start against a world it did not boot into.** That is the
point and it is also the sharp edge: a file scope that assumes what the boot
scene held will find something else. Nothing here protects against that, and
nothing should — it is the same contract a `require`d module has always had.

**Disabling does not stop a `while true do task.wait() end`** until its next
`task.wait` returns, at which point it is never resumed. That reads as a delay
and is the correct behaviour; a preemptive stop is not available to a coroutine
scheduler.

**What a disabled script built stays.** Somebody will expect a disable to clean
up. It does not, Roblox's does not, and the alternative is rule 2 growing into
ownership semantics for every instance a script touched.

### Rejected

**Disconnecting a disabled script's connections.** Not what Roblox does, and it
answers the question ADR 0058 declined. If connection lifetime is ever decided,
it is decided on its own and applies to every connection rather than to the ones
whose script happened to be toggled.

**Sweeping the deferred queue on disable.** Eager, and it changes what
`task.cancel` raises for a thread the caller still holds. Rule 4's lazy drop has
the same observable behaviour with none of that.

**Resuming the old threads on re-enable.** Would make `Enabled` mean "pause" for
a script that has run and "start" for one that has not, and a person cannot tell
which they have without knowing the script's history.

**Doing nothing, and keeping the documented limit.** Defensible while there was
no editor: a property nobody could toggle at runtime is a property with no
callers. There is an editor now, the grid shows the checkbox, and a control that
lies is worse than a missing one.
