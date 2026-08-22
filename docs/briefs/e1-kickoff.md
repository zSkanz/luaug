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

- [x] `luaug edit [path]` in the CLI, with i18n keys and a spawned-process test
- [x] An editor mode in `engine/app`, gated like `--replay` / `--bench` / `--two-worlds`
- [x] A dockspace with Explorer, Properties, Viewport, Console and Stats as real windows
- [x] Layout persistence (`io.IniFilename` back on, with a decided location)
- [x] The world rendered into a texture and shown in the Viewport panel
- [x] Picking: a testable screen-point-to-ray function, and a ray that chooses an instance
- [ ] A visible selection in the viewport — **NOT DONE**, see the Gate Record
- [x] `PropertyDesc` carries the identity of the enum a property accepts
- [x] `docKey` stops being emitted empty, so properties can carry tooltips
- [x] D056: the `shipping` profile compiles, and a gate stage builds it
- [~] D057: the release notes now say what the binary is; choosing the profile is a human decision, see the Gate Record

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

- [x] `luaug edit examples/10-open-world` opens, docks, and renders the world in its viewport; a screenshot is attached to the gate record.
- [~] Clicking a part in the viewport selects it: the Explorer highlights the same instance, the Properties panel shows its class, and the viewport draws the selection. Proven by a headless test that drives a synthetic click through the picking function, not by eye alone.
- [x] Picking has unit tests over a camera and a viewport rectangle covering: the centre of the screen, each corner, a click on empty space returning nothing, and a non-square viewport — the last because an aspect-ratio bug is invisible at the centre and wrong everywhere else.
- [x] Editing a property in the editor changes the world: the existing safe-point drain is used unchanged and a test asserts the write lands.
- [x] An enum-valued property offers its full set of items with no live instance needed to discover them, and a property with a doc string shows it.
- [x] The `shipping` profile compiles, and a gate stage builds it.
- [x] `scripts/localgate.ps1` green on every stage; `luaug check` clean; docs-lint clean.
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

**Finding 3 — a dockspace arranges nothing, and one screenshot said so.** The
first launch of the editor put every panel at ImGui's default position, which is
the same position: five windows in a pile with the viewport at the bottom of it.
Every test passed. `DockSpaceOverViewport` makes docking *possible* and does not
dock anything, so a default layout has to be built with `DockBuilder` on the
first run and only when no saved layout exists. This is the milestone's own
instance of the pattern this project keeps paying for -- and the cheapest one
yet, because looking cost one screenshot.

**Finding 4 — the shipping gate earned itself back on its first real use.** The
stage added for D056 caught three private fields on `DebugOverlay` that nothing
reads when ImGui is compiled out -- one of them a member I had added and never
used. MSVC says nothing about any of them; Clang's `-Wunused-private-field` under
`-Werror` says all three. The header's own design forbids `#ifdef`, so the honest
answer was `[[maybe_unused]]`, which states the true thing: a shipping build is
*meant* to be in that state.

**Finding 5 — `doctest::Approx` holds a double, and the Linux tier is why anybody
finds out.** Every float comparison in the new tests promoted silently under
MSVC and failed under Clang's `-Wdouble-promotion`. The repository already had
the idiom (`static_cast<f64>` at the comparison, `engine/core/tests/math_tests.cpp`);
what it did not have was anything to make a newcomer use it, which is what
`-Werror` on the second compiler is for.

## Attempted / abandoned

(append during the milestone)

## Gate Record

Run 2026-08-22, on the reference machine.

**`scripts/localgate.ps1` — green on all six stages** (the sixth is new this
milestone):

```
  ok    docs (15.3 s)
  ok    luau (10.7 s)
  ok    format (16.4 s)
  ok    windows (73.7 s)
  ok    linux (93.6 s)
  ok    shipping (29.7 s)
green (macOS is Tier-3 and only CI can build it)
```

44 ctest targets on Windows and 41 in the Tier-2 container, both with
1,109 conformance cases passing. `luaug check` clean, docs-lint clean.

**Item by item.**

| Gate item | Result |
|---|---|
| `luaug edit examples/10-open-world` opens, docks, renders the world in its viewport | **Green.** `docs/images/e1/editor-first-light.png` |
| Clicking a part selects it: explorer, properties and viewport agree | **Half.** The pick path is proven headlessly end to end (`engine/app/tests/editor_tests.cpp`, six cases) — a synthetic click at a viewport pixel selects the part in front of the camera, and the inspector holds the selection. **Nobody has clicked one with a mouse**, and the viewport draws no selection at all, so the visible half of this item is not met |
| Picking unit tests: centre, corners, empty space, non-square viewport | **Green.** `engine/app/tests/picking_tests.cpp`, eleven cases, plus a rotated box, a ray starting inside one, and a ray parallel to a slab |
| Editing a property in the editor changes the world | **Green by inheritance, not by new evidence.** The editor drives the existing `Inspector` unchanged, and its safe-point drain is covered by `inspector_tests.cpp`. No test drives a property edit *through the editor shell* |
| An enum property offers its item set with no live instance; a documented property shows its doc | **Green.** `class_registry_tests.cpp` against the real generated tables (`Part.Shape` resolves to five items, `Anchored` names no enum) and `inspector_tests.cpp` |
| The `shipping` profile compiles, and a gate stage builds it | **Green.** D056 fixed; `scripts/gates/shipping-build.sh` builds and links it every run |
| Full local gate, `luaug check`, docs-lint | **Green**, above |
| **A human opens the editor and says whether it is an editor** | **Not done.** This is the milestone's sign-off and it is deliberately not automatable |

**The picture.** `docs/images/e1/editor-first-light.png` is the editor on the
flagship: the explorer showing the real tree from `DataModel` down through
`Workspace`, the character, the welds and all 289 streamed chunks; the viewport
rendering the world at 10.07 ms; properties and stats tabbed; the console
carrying the run's own log. The world is dark in it because the working tree had
`StartHour = 22.5` at the time, which is somebody's edit and not a setting of the
editor's.

**Layout persistence is verified by its artifact rather than by eye.** After a
first launch `examples/10-open-world/.luaug/editor-layout.ini` contains the
built arrangement — explorer 351 px left, viewport centre as `CentralNode=1`,
console below it, properties and stats tabbed right — and a second launch reads
it back instead of rebuilding.

### What E1 deliberately does not have

The three worth carrying into E2, all of them named in NOT-in-scope and none of
them discovered late: **no selection highlight in the viewport**, which is the
one scope item that was written down and not built and therefore the first thing
E2 owes; no manipulator, no undo, no create or delete, and no way to save
anything at all. The editor can change a world and cannot keep the change, which
is E3.

**And one thing that could not be evidenced at all.** The ImGui overlay refuses
to start without a window, so the editor cannot render headlessly and there is no
`--screenshot` path for it. Every picture of this editor has to be taken by
somebody, or by capturing a real window — which is what
`docs/images/e1/editor-first-light.png` is. A milestone that wants a golden of the
editor will have to make the shell renderable headlessly first, and that is worth
knowing before it is promised.
