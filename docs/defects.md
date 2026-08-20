# Defect register

**Append-only. Numbered without gaps. Nothing is ever deleted from this file.**

It exists because three human-reported defects were removed from `PROGRESS.md`
while it was being rewritten to close M4 — not archived, removed — on the day
the human was being asked to sign that milestone off. A milestone close is
exactly when a ledger is least able to afford losing its open items and most
likely to: the file is rewritten wholesale, and a bullet that disappears leaves
nothing behind.

A row cannot disappear the same way. `scripts/gates/docs-lint.sh` checks that
the ids run `D001`, `D002`, … with no holes and no duplicates, so a deleted row
is a gap the gate names, and an edited row is a diff a reviewer sees. That is the
whole mechanism: not a database, a shape a check can hold.

## What goes here

Defects **observed** — by a human using the engine, or by the agent finding one
in something that already shipped. Not a backlog, not a design list, not
milestone scope: those live in `docs/roadmap.md`. If nobody has seen it go
wrong, it is not a defect yet.

`PROGRESS.md` still carries whatever is being worked on right now, in prose,
with the reasoning. This file carries the fact that it exists.

## Format

One row per defect. `State` is one of:

- `open` — reproducible and unfixed.
- `fixed` — corrected, with the commit that did it.
- `not-a-defect` — investigated, and the behaviour is correct. Kept, because a
  defect closed silently gets re-reported.
- `scheduled` — real, understood, and deliberately owned by a later milestone.

| Id | Reported | What | State | Where |
|---|---|---|---|---|
| D001 | human, 2026-08-20 | `BasePart.Transparency` changes nothing | fixed | `e86f9b0b` — the value could not reach the renderer at all; `DrawItem` had no field for it. Sorted blended pass, M4.5 |
| D002 | human, 2026-08-20 | The sun never moves and shadows never lengthen | fixed | `4e4e3fc2` — `Lighting` was created on first `GetService`, after the host cached its id; a boot service now |
| D003 | human, 2026-08-20 | The sun's shadow flickers | fixed | `27c47549` — the shadow texel grid slid with the camera; snapped to whole texels. The rotational half is named and deliberately out of M4.5 |
| D004 | human, 2026-08-20 | A crash while dragging `Size`/`CFrame` in the inspector, no log | open | **Not reproduced, and two halves ruled out.** The write path was driven through zero, negative, 1e30 and infinity for 32 frames with a render extraction each time and did not fault (`world_host_tests`); the window was minimized and restored 25 times over 900 windowed frames and the host exited 0. What remains is the ImGui half. The handler and log file that would have captured it exist as of `7cc1c77c` |
| D005 | human, 2026-08-20 | The F3 panel is unreadable while running | fixed | `e7aa645f` — sampled at 4 Hz and held, worst frame beside the mean |
| D006 | human, 2026-08-20 | "go" on `RunService.Parent` crashes the host | fixed | `a0e41ac1` — an editor trusted the declared type over an absent value |
| D007 | human, 2026-08-20 | F5 in the editor launched an unrelated extension | fixed | `.vscode/launch.json`, four configurations against the real binaries |
| D008 | human, 2026-08-20 | Type errors on `engine.d.luau` in the editor | not-a-defect | `selene`, from an extension outside this project's toolchain. `.vscode/settings.json` did also point luau-lsp at a deleted file, and that is fixed |
| D009 | human, 2026-08-20 | The Android triangle is stretched in portrait | not-a-defect | The sample's vertices are in NDC, so it fills whatever aspect the device has. Recorded at the M4 device checkpoint |
| D010 | agent, 2026-08-20 | The capture backend recorded uniform blocks by SIZE, so the blocking render gate could not see a matrix, a light or a material colour | fixed | `4e4e3fc2` — a content digest, quantized onto the same grid the stream already uses |
| D011 | agent, 2026-08-20 | Debug wire boxes and `DebugService` lines were drawn in world space through a camera-relative view-projection, displacing every one by the camera's distance from the origin | fixed | `e86f9b0b` — the buffer is rebased once, after extraction |
| D012 | agent, 2026-08-20 | `Model:PivotTo` was `PrimaryPart.CFrame = cf` under a new name: `PivotOffset` did not exist, so nothing could hinge | fixed | `721f6194` — `PVInstance` with a real pivot offset |
| D013 | agent, 2026-08-20 | `modelPivot` said "the centre of the extents box" and averaged part positions, which is a different point whenever parts differ in size | fixed | `721f6194` |
| D014 | agent, 2026-08-20 | `schema.luau` documented `Property.Native` backwards — absent meant unbacked, where the generator derives a name and `Native = ""` means unbacked | fixed | `98a5a866` |
| D015 | agent, 2026-08-20 | `fopen_s`/`_wfopen_s` open for exclusive access, so nothing could read the engine's log while it ran | fixed | `7cc1c77c` — `_wfsopen` with `_SH_DENYWR` |
| D016 | agent, 2026-08-20 | `BindToClose` has no capped grace period: a close handler that yields is cut off at the next drain rather than waited for. `architecture.md` §app promises the cap | fixed | `c18f12f4` — the host keeps ticking while a handler is parked, up to a wall-clock cap, and warns when the cap runs out. Both halves tested: a handler that yields and finishes, and one that never does |
| D017 | agent, 2026-08-20 | The `DebugShell` has no memory-category table and no log/REPL pane, both named by `architecture.md` §app | scheduled | M6, with the UI milestone that gives the shell its remaining panes. Found by the same audit |
| D018 | agent, 2026-08-20 | `luaug_net_tests` hung once on Windows, holding CTest until it was killed; a re-run passed | open | Observed once, 2026-08-20. §12 quarantines on the second occurrence; this is the ledger entry that makes a second one countable |
| D019 | CI, 2026-08-20 | `engine/app/src/engine.cpp` used `std::sort` with no `#include <algorithm>`: it compiled on Windows and in the Tier-2 container by reaching the header transitively, and failed on the CI runner's libstdc++ | fixed | `main` was red for one push. Found because the run the milestone push cancelled had already reported it. Neither local tier can see this class of defect — the transitive graph differs by libstdc++ version |
| D020 | CI, 2026-08-20 | `rokit install` resolves every pinned tool through api.github.com unauthenticated, which is 60 requests an hour shared across the runner's IP; it returned 403 and killed a job whose build and 25 tests had already passed | fixed | `rokit authenticate github --token` with the workflow's own `GITHUB_TOKEN`, before `install`. Subcommand read off the pinned 1.2.0 binary's `--help` rather than from documentation for another version |
| D021 | agent, 2026-08-20 | A property that refuses a value for being out of RANGE reports the key for its TYPE: `PhysicsService.FixedTimestep = 1/10` raises "it takes a number" about a number | open | A setter answers the caller with a bool and `PropertyDesc` carries one key per value type (`gen_cpp.luau:110`), so there is nowhere for a range refusal to say so. Every M5 property with a range is affected -- `Density`, `Restitution`, `MaxSlopeAngle`, `FixedTimestep`. The fix is a per-property error-key override in the IDL, which is surface and belongs with a milestone that has other reasons to touch the generator |
| D022 | agent, 2026-08-20 | A `Part` never reaches the solid renderer: only a `MeshPart` does, so every primitive in the world -- crate, ramp, floor, character -- draws as a debug wireframe box | scheduled | M7.5 ("Looking Like an Engine"), which owns how this engine renders. True since M2 and unchanged by M4, which shipped mesh rendering; found by looking at the M5 deliverable's first screenshot, where a whole physics playground is outlines. The fix is a built-in primitive mesh per `Enum.PartShape` scaled by `Size`, and it moves every render golden -- which is why it is a milestone's work rather than an afternoon's |
