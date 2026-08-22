# 0046 — The editor is a mode of the engine binary, drawn in ImGui

- Status: accepted
- Date: 2026-08-22
- Supersedes: — (extends 0011 and 0017)

## Context

The human opened post-v1 phase 1 on 2026-08-22: the visual editor, which
`docs/roadmap.md` carries as a paragraph of intent and not as a milestone with a
gate. Before it becomes one, the shape has to be settled, because the three
candidate shapes do not cost the same and the difference is not a matter of
taste — five reconnaissance passes over the repository measured it.

**Three shapes were on the table.**

**(a) A separate process driving the engine over the dev-server channel.** The
transport is real and better than expected: the engine is a WebSocket client of
the dev server (ADR 0035), commands are applied at the frame's safe point, and
`tools/cli/devserver.luau:147-149` already relays any observer's JSON object
straight through to the engine — an outside process can drive a running host
today without pretending to be the engine. What does not exist is anything worth
sending. The four verbs are `reload`, `sample`, `ping` and `shutdown`
(`engine/app/src/engine.cpp:806-859`); there is no message that reads a tree or
writes a property, `eval` is reserved and unimplemented
(`engine/app/include/luaug/app/dev_control.h:50-53`), and a screenshot is written
once at exit rather than on request. So this shape needs a whole scene protocol
invented before it can show anything — and then it still needs a renderer, a
window and a viewport of its own, which is a second application, not an editor.

**(b) A mode of the engine binary.** Almost everything it needs is already
built and, more to the point, already *tested*: `engine/app/src/inspector.cpp` is
a property grid that walks the generated descriptor tables with no switch on any
class name, edits queue as `PendingWrite` and drain through `World::setProperty`
at the FrameStart safe point, refusals come back as five distinguishable
`SetResult` values, and nine doctest cases hold it against classes it has never
seen. ImGui is vendored on the docking branch with tables, multi-select,
drag-and-drop and `ImGuiListClipper` all present. `--two-worlds` proves two
`WorldHost`s, two `scene::World`s and two Luau VMs alive in one process, which is
what a play button is made of.

**(c) A Luau application on the engine.** The most appealing on paper — the
editor would dogfood the game's own UI — and the only one that is *blocked*
rather than merely expensive. The game VM has no filesystem (`io` and `os.remove`
are removed by design), so a Luau editor could not save what it authored;
`@std/net` is an HTTP client with no server and no WebSocket, so it could not
hand the work to a tool that can; and the two-worlds seam is C++ with no binding,
so it could not run what it built. Each of those is a hole in the sandbox if
filled carelessly, which is R4, and R4 does not bend for tooling.

## Decision

**The editor is a mode of the `luaug-host` binary, drawn in Dear ImGui, launched
by `luaug edit [path]`.**

This is not a new decision so much as the honouring of an old one: ADR 0011:14
put "the debug overlay/DebugShell **and the future editor**" on the ImGui side of
its line four milestones before anybody needed it to be true.

Three consequences follow and are part of this decision:

- **The `Inspector` model is the editor's model.** `inspector.h` is ImGui-free,
  headless and tested; the editor drives it unchanged and grows it rather than
  forking it. A `switch` on a class name appearing anywhere in the editor is a
  finding about ADR 0017's promise, exactly as `inspector.h:12-17` already says.
- **Every world mutation the editor makes goes through `World::setProperty` and
  friends at the frame's safe point.** No editor path writes a component
  directly. This is what keeps determinism (R10) and hot reload true of a world
  a person has been editing.
- **The game UI layer is not the editor's substrate, and this is a measurement
  rather than a preference.** Building the editor's shell on the M6 Instance tree
  would mean first implementing pointer-down/move/up events, wheel delivery to
  `ui` at all, drag, popups, virtualisation, text selection, clipboard, nested Z
  stacking, splitters and theming — every one of which ImGui already ships and
  receives for free from the raw SDL stream the overlay is already handed
  (`engine/app/src/debug_overlay.cpp:713`). That work would be a UI framework,
  not an editor.

**R3 does not apply to what the editor draws**, on the same grounds
`debug_overlay.h:14-16` already claims for the overlay: it exists for whoever is
building a game, never for a player. Strings a *game* shows still obey R3, and
the editor showing a game's own UI is showing the game's strings, not its own.

## Consequences

**What this buys.** The first milestone starts from a working property grid, a
working tree view, a working Luau REPL against the live VM, a safe-point write
discipline and a proven two-world seam. What is genuinely absent is smaller than
what is present: there is no picking — a repo-wide search for
`ScreenPointToRay`, `unproject` or `WorldToScreen` finds only prose in
`docs/api-design.md` — no selection highlight, no manipulator, no undo, no
dockspace, and no way for the engine to write a file at all
(`engine/platform/include/luaug/platform/file.h` reads and never writes).

**What it costs, and one of them is a human's.** `LUAUG_DEBUG_UI` is currently
`OFF` in the shipping profile and `OFF` without the SDL_GPU backend
(`cmake/luaug_options.cmake:77-79`), because ADR 0011 scoped ImGui to dev builds.
An editor is a dev build by nature — it is not shipped inside anybody's game, and
`luaug build` packages a player that has no editor in it — so the gate is right
and stays. But **the editor being a product that a person downloads, rather than
a thing you get by building the repository, is a distribution question this ADR
does not answer** and phase 1 must not answer by accident.

**What it does not decide.** The scene file format. The editor can already move a
part; it cannot save one, and nothing in the repository can. That is the phase's
hardest question, it has a strong candidate already written — the deterministic
reflective walk in `engine/scene/src/world_hash.cpp:182-278`, which visits every
instance in slot order, class by name, parent, children in sibling order,
attributes, tags and every property of every superclass through the generated
accessors — and it gets its own ADR when the milestone that needs it starts.
Writing that format is not a licence to widen `luaug-chunk-source`, whose header
calls itself "deliberately NARROW — this is not a general scene serialization"
on purpose.
