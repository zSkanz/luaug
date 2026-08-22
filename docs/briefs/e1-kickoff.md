# E1 Kickoff — The Editor Opens

- Started: 2026-08-22
- Roadmap section: docs/roadmap.md#e1--the-editor-opens-m
- Phase: post-v1 phase 1 (the visual editor), opened by human decision 2026-08-22

## Goal (restated)

Make an editor that opens a real project, draws it, lets a mouse choose
something in it, and lets that something be changed — and stop there. Not
because the rest is unimportant but because manipulators, undo and saving are
each large enough to deserve a milestone, and none of them can be designed
honestly by somebody who has not yet used the selection they all stand on. The
end of this milestone is a person clicking a tower in `examples/10-open-world`
and typing a new colour into it.

## Scope checklist (from roadmap)

- [ ] `luaug edit [path]` in the CLI, with i18n keys and a spawned-process test
- [ ] An editor mode in `engine/app`, gated like `--replay` / `--bench` / `--two-worlds`
- [ ] A dockspace with Explorer, Properties, Viewport, Console and Stats as real windows
- [ ] Layout persistence (`io.IniFilename` back on, with a decided location)
- [ ] The world rendered into a texture and shown in the Viewport panel
- [ ] Picking: a testable screen-point-to-ray function, and a ray that chooses an instance
- [ ] A visible selection in the viewport
- [ ] `PropertyDesc` carries the identity of the enum a property accepts
- [ ] `docKey` stops being emitted empty, so properties can carry tooltips
- [ ] D056: the `shipping` profile compiles, and a gate stage builds it
- [ ] D057: `luaug build` selects a profile deliberately, or says what it selects

## NOT in scope

Manipulators of any kind. Undo or redo. Creating, deleting, renaming or
reparenting instances. Multi-select. Saving anything at all. A scene file
format. An asset browser. Prefabs. Play and stop. A second OS window. ImGui
multi-viewports. Distributing the editor as a downloadable product. Any change
to the game UI layer (ADR 0046 settled that it is not the editor's substrate).

**And one temptation with a name**: `luaug-chunk-source` is the only
instance-tree format on disk and it would be easy to teach it to hold a scene.
Its own header calls it "deliberately NARROW — this is not a general scene
serialization" on purpose. E3 decides that with an ADR; E1 does not decide it by
writing to it.

## Subagent plan

Interfaces frozen first, per MASTER_PROMPT §7. Three parts fan out because they
touch disjoint files and none of them is a seam between two modules; the editor
mode itself stays orchestrator-only, because it is exactly the cross-cutting
work §7 says not to hand out.

| Part | Files | Why it can fan out |
|---|---|---|
| Enum identity + `docKey` on `PropertyDesc` | `api/schema.luau`, `api/generator/gen_cpp.luau`, `engine/scene/include/luaug/scene/class_registry.h`, `engine/app/src/inspector.cpp` | Additive to a generator that already exists; its contract is one struct field and one emit |
| `luaug edit` in the CLI | `tools/cli/main.luau`, `tools/cli/commands/edit.luau`, `tools/cli/i18n/en.json`, `tools/cli/tests/edit.test.luau` | Entirely inside `tools/`; the engine flag it passes is frozen before it starts |
| D056 — the `shipping` profile | `engine/script/src/{modules,runtime}.cpp`, `engine/app/src/debug_overlay.cpp`, the gate scripts | A build-configuration defect with no overlap with editor code |

Orchestrator-only: the editor mode, the dockspace, the viewport render target,
picking, and the selection highlight.

## Gate checklist (verbatim from roadmap)

- [ ] `luaug edit examples/10-open-world` opens, docks, and renders the world in its viewport; a screenshot is attached to the gate record.
- [ ] Clicking a part in the viewport selects it: the Explorer highlights the same instance, the Properties panel shows its class, and the viewport draws the selection. Proven by a headless test that drives a synthetic click through the picking function, not by eye alone.
- [ ] Picking has unit tests over a camera and a viewport rectangle covering: the centre of the screen, each corner, a click on empty space returning nothing, and a non-square viewport — the last because an aspect-ratio bug is invisible at the centre and wrong everywhere else.
- [ ] Editing a property in the editor changes the world: the existing safe-point drain is used unchanged and a test asserts the write lands.
- [ ] An enum-valued property offers its full set of items with no live instance needed to discover them, and a property with a doc string shows it.
- [ ] The `shipping` profile compiles, and a gate stage builds it.
- [ ] `scripts/localgate.ps1` green on all five stages; `luaug check` clean; docs-lint clean.
- [ ] **A human opens the editor on the flagship and says whether it is an editor** — the gate item that is deliberately not automatable, and the one every milestone since M4 has proven is where the real defects come from.

## Findings

Filled during the milestone: the things the docs assumed that reality corrected.

**Finding 1 — the reconnaissance was worth more than it cost, and it changed the
milestone.** Five read-only passes over the repository, run in parallel before a
line was written, produced three things no amount of planning would have: that
ADR 0011 had already named the editor on the ImGui side four milestones earlier,
so the substrate was not an open question; that a Luau editor is *blocked* rather
than expensive, because the sandbox has no filesystem and R4 does not bend for
tooling; and that the deterministic world-hash walk in
`engine/scene/src/world_hash.cpp:182-278` is a whole-world serializer with a
`Hasher` where a writer should be, which is E3's answer found in E1's week.

**Finding 2 — a research claim is a lead, not a fact, and testing one found two
defects in a shipped release.** A reconnaissance pass reported that
`debug_overlay.cpp` defines `captureLog()` outside its own `#if LUAUG_DEBUG_UI`
and that the `shipping` profile should therefore fail to compile. Building it
found a *different* first failure — `luacode.h` included unguarded where the
profile turns the Luau compiler off — and the build died before it ever reached
the claimed one, which is still untested behind it. Following that thread found
D057: `luaug build` searches four presets for a host and `shipping` is not among
them, so the v1.0.0 binary published an hour earlier is a development build,
carrying the debug overlay and a Luau REPL into a release whose notes described
a player. The notes now say so.

## Attempted / abandoned

(append during the milestone)

## Gate Record

Filled at milestone end, before human review.
