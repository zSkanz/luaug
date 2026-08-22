# 0017 — No visual editor in v1; code-first DX

- Status: accepted
- Date: 2026-08-19

## Context
A Studio-like editor multiplies scope and freezes engine design prematurely.
Unity/Godot grew editors on top of working engines. User decision #5.

## Decision
v1 is **code-first**: VS Code + the `luaug` CLI + sub-second hot reload, with
an in-game ImGui debug overlay (DebugShell: explorer, properties, stats, log)
standing in for inspection needs. The **visual editor is phase 2**, built on
the finished engine. Nothing in v1 may hard-code assumptions that block an
editor (the reflection/API-definition layer is editor-ready by construction).

## Consequences
Faster path to a real, running engine; the editor inherits a mature
Instance/reflection substrate instead of dictating it.

## Addendum — the condition, checked (2026-08-22, M8)

"Nothing in v1 may hard-code assumptions that block an editor" is a condition
nobody had tested in the four milestones since it was written. M8 tested it,
because the roadmap made it scope, and because the value of the check decays
with every milestone that adds code before it.

**The seam is open.** `luaug-host --two-worlds=<dir>` boots two `WorldHost`s at
once -- two `scene::World`s, two `ScriptRuntime`s, therefore two Luau VMs --
renders each into its own target, and asserts that each world's image is
byte-identical to the same world rendered alone while the two differ from each
other. `engine/app/src/two_worlds.cpp` explains why both halves of that
differential are needed. Three doctest cases hold the same floor without a GPU:
separate trees, separate module state, and one host destroyed while another
keeps running.

**One thing it found, and it is a property of the renderer rather than a
blocker.** A renderer is not stateless per view: it carries the exposure it has
adapted to and the environment chain it bakes one level per frame. Two worlds
therefore need **two renderer instances**, not one shared between them -- the
first version of the proof shared one and every image drifted. `renderer.h`
already said the shape of that answer for a different reason ("a caller that
renders into two formats needs two renderers"); a caller rendering two worlds is
the same sentence. Nothing in the interface changed.

**Networking is the second caller.** Post-v1 phase 4 puts an authoritative world
and a replica in one process over a loopback transport, which is the same
requirement with a different consumer -- so this gate protects two futures, and
a seam with two callers is much harder to quietly drop than a seam with one.

