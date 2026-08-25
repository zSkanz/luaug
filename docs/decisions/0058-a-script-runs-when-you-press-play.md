# 0058 — A script runs when you press play, and not when you open the project

- Status: accepted
- Date: 2026-08-24
- Extends: 0046 (the editor is a mode of the engine binary)
- Relates to: 0047 (the world is data and scripts are behaviour),
  0057 (a script is an instance and the editor edits one thing)

## Context

Opening a project in the editor runs every entry script's file scope before the
first frame. `WorldHost::boot` calls `script::startScripts(m_runtime->state())`
with no condition on it, and the comment below that call says the arrow in
api-design.md §3's lifecycle is load-bearing: *"the scripts have had their first
resumption **before** the first frame renders"*.

That is right for a game and wrong for a tool, and a person found it in about a
minute with the starter template:

> eu to editando o mundo sem estar no play apagando todos os spinner porque
> teoricamente quando o código executa no play deveria criar a instancia spinner
> né esse deveria ser o comportamento, o código executa só no play não??

`templates/starter`'s `main.luau` calls `Instance.new("Part")` at file scope. So
a project that has never been played has a `Spinner` in its Workspace that
nobody authored, that the Explorer shows beside the authored one, and that a
`RunService.Heartbeat` connection made in the same file scope already holds.
Deleting it while stopped leaves a live connection writing to a destroyed
instance.

**The tick is already gated and that is what hid this.** `worldAdvancing` stops
`Heartbeat` from firing while the editor is editing, so everything that runs
*per frame* is correctly quiet. What is not gated is the one-shot: a script's
file scope is not a tick, it runs once, and it had already run.

### It is the same mistake five times

ADR 0046 named the shape at E1's close, after five defects that were one
architectural error: **the editor inheriting the game's decisions instead of
taking them.** The rule it wrote governs the tick, the cursor, the audio, the
camera and the keyboard:

> while the editor is editing, the tool owns the machine, and pressing play
> hands it back.

An entry script that runs because a project was opened is the machine belonging
to the game. This is the sixth, and it is the first one found after that rule
existed — which is worth saying plainly: the rule was written and this call was
not checked against it.

D067 is the nearest neighbour and the same family: a boot scene applied *after*
the entry scripts destroyed what they had built, because the editor took the
game's boot order.

### What the other engines do, and they agree

Unity, Unreal and Godot all open a scene without running its scripts. Play is
what starts behaviour, stop is what ends it, and the editor between them shows
data. Roblox Studio is the same: a `Script` in Studio does not run until you
press Play, and this engine's whole API stance is Roblox-familiar.

Nothing here is a departure. The engine simply never had an editor when
`startScripts` was written, and the call has not been revisited since M1.

## Decision

**Mounting a script and starting it are two things, and the editor does only the
first until somebody presses play.**

### 1. Boot mounts. Play starts.

`WorldHost::boot` builds the `Script` instances — they are in the tree, the
Explorer shows them, `Source` is editable, `luaug edit` can open a tab on one
(ADR 0057) — and does not resume any of them. A windowed game, a headless run,
`luaug dev`, the conformance runner and a replay all start scripts at boot
exactly as they do today. **Only the editor waits.**

### 2. Stop unloads the VM, and that is what makes play repeatable

Today `Editor::stop` restores a `WorldSnapshot` and says so — *"the world is back
where you pressed play"* — while the Luau VM keeps running with every connection
it made. E1's brief already names that as its honest limit.

This ADR makes the VM part of what play owns: **play starts scripts, stop tears
the runtime down and builds a fresh one.** A second play is then the same as the
first, which is the property that makes a play session a session rather than an
accumulation.

That is not a new mechanism. A hot reload already destroys a world and builds
another (ADR 0024), and the editor already re-points the overlay's script target
after one.

### 3. A script that has not run has no connections, and that is the point

The reported error — a `Heartbeat` writing to a deleted instance while the editor
is stopped — stops being reachable, because the connection is never made until
play and never outlives a stop.

**This is deliberately not a fix to connection lifetime.** Whether a connection
should die with the instance its closure captures is a real question and a
separate one; it is not answered here, and a game that deletes something a
running script holds still gets an error, which is correct.

### 4. `game.Loaded` fires when scripts start, not when the world is built

Its contract is "after all entry scripts have had their first resumption", and
that is unchanged. In the editor it now fires on play. A script that connects to
it at file scope still sees it, because file scope is what play runs.

## Consequences

**A project opened in the editor shows what its scene holds and nothing else.**
That is the sentence a person expects and did not get. The Explorer stops showing
instances nobody authored, and Save stops being able to bake one into a scene —
which is D074 arriving from a direction that milestone did not cover.

**`luaug dev` is unaffected and that matters**, because it is the fast loop: it
runs the game, not the editor, so a saved file still rebuilds the world and
scripts still run at boot.

**Two plays in a row are now identical.** They were not: the second inherited
every connection, every `task.defer` and every module the first left in the VM.
Nobody had reported that, which is what a limit written down and left alone looks
like.

**A script cannot set the world up for editing.** Somebody may have leaned on
file scope to build helper geometry they then edit and save. That was never a
supported workflow — it is exactly the two-sources problem D074 closed — and the
answer is to author the geometry in the scene.

**The starter template has to change.** `templates/starter/src/scripts/main.luau`
creates its `Spinner` at file scope, and after this it creates one on play and
the project opens empty. That reads as a broken template unless the scene holds a
Spinner and the script only spins it — which is what the flagship already does
and what the template should have been teaching.

### Rejected

**Gating `Heartbeat` only, and letting file scope run.** That is today, and today
is the defect: the one-shot is what created the instance.

**Running scripts in the editor but discarding what they make.** A world where
some instances are real and some are about to vanish is two kinds of instance in
one tree, and the Explorer would have to explain which is which. ADR 0047 exists
so that a world has one source.

**Leaving the VM alive across a stop.** Cheaper, and it keeps the current limit:
a second play is not a fresh one. The VM teardown is what makes rule 1 worth
having, because a script that starts on play and never stops is a script that
accumulates.
