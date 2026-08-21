# M6 Kickoff — Playing the World: Input Actions, UI, Tween, Audio, Minimal Animation

- Started: 2026-08-20
- Roadmap section: docs/roadmap.md#m6--playing-the-world-input-actions-ui-tween-audio-minimal-animation-l

## Goal (restated)

Five milestones built a world that boots, ticks, reloads, renders and falls.
Nothing in it can be *played*. A player steers M5's capsule with a `DevOnly`
keyboard scaffold that was never meant to survive this milestone; there is no
button to press, no menu to open, nothing that fades, nothing that makes a
sound, and the character that walks is a capsule with no legs. This milestone is
the game-feel layer: the five systems that stand between "a simulation you can
watch" and "a game somebody plays".

They are five systems and one deliverable, and the deliverable is the point.
`examples/04-obby` is a game with a main menu that tweens in, a HUD that lays
itself out at two resolutions, checkpoints that make a sound, platforms that
move because a tween drives a kinematic body, and an animated character that
runs from the start to the finish flag — playable start to finish, and driven in
CI by a recorded input stream that has to reach the flag. Each system is
individually testable and each one is easy to certify against itself; the obby
is what makes them certify against each other.

The through-line, and it is a generalisation of M5's Finding 15: **this
milestone adds four new clocks to an engine that has spent five milestones being
careful about one.** A render-rate input dispatch, a tween's timeline, a mixer
thread and an animation clip each have a natural pull toward the wall clock, and
each of them writes into the world. R10 is not a checkbox here — it is the
constraint that decides most of the decisions below.

## Scope checklist (from roadmap)

- [ ] **Input Action System** clone per api-design.md §2.4 (ADR 0029): actions,
      bindings, contexts, gamepad + KB/M, runtime rebinding
- [ ] **UI Instances** (`ScreenGui`/`Frame`/`TextLabel`/`TextButton`/`TextInput`/
      `ImageLabel`/`ImageButton`/`ScrollFrame` + `UIListLayout`/`UIPadding`/
      `UICorner`) over Clay layout, stb_truetype text, UDim2-style coordinates
- [ ] **TweenService** equivalent: property tweens including UI; easing families
      conformant to reference easing tables checked in as test fixtures
- [ ] **miniaudio**: 2D sounds + basic 3D spatialization as `Sound` Instances,
      `AudioGroup` buses, `AudioService`
- [ ] **Minimal skeletal animation**: glTF clip playback + linear blending
      (`AnimationPlayer`/`AnimationTrack` per api-design.md) — no state machines,
      no IK; enough for idle/walk/jump
- [ ] **Solid `Part` rendering** — added to the roadmap 2026-08-20 by human
      decision, **after this brief imported its scope**, so it is appended rather
      than assumed to have been read. A `Part` has no solid path: it reaches the
      frame only as a debug wire box, so `Instance.new("Part")` is invisible and
      M5's physics playground is looked at in wireframe. One unit mesh per
      `Enum.PartShape` member, built once at boot and registered with
      `MeshCache` like any other, scaled by `Size` — five meshes, not one per
      part. `extract` then emits ordinary `DrawItem`s and colour, `Material`,
      `Transparency` and the blended pass come free. **This is M4's
      "engine-generated geometry must reach the renderer" constraint being spent
      for the first time: if the seam was left open correctly the renderer
      changes not at all, and if it has to change, that is the finding.** Two
      things to settle rather than to slide past: `Part.Shape` is extracted and
      ignored today (`submitWorld` calls `wireBox` whatever it says, so a `Ball`
      is a box) and must end this milestone either honoured or marked `Inert`;
      and the segment count for ball, cylinder and capsule is a permanent
      decision, because it is baked into every golden recorded after it. The
      debug wire path stays — `render_world.cpp` says why, and it is still how
      anything is seen when the real path breaks.
- [ ] `examples/04-obby` — main menu (tweened), HUD, checkpoints, moving
      platforms (tweens on physics-kinematic parts), sounds, an animated
      character, fully playable start→finish

**The transparent pass is inherited, not built.** The roadmap moved it to M4.5
on 2026-08-20, when the human asked for a `Transparency` that fades rather than
one that switches, so UI's blended back-to-front pass already exists and is
already tested against world geometry — the harder of its two callers. What
remains here is UI's own use of it.

### Carried debts this milestone pays

- [ ] **D017 — the `DebugShell` has no memory-category table and no log/REPL
      pane**, both named by `architecture.md` §app. Scheduled here by the M5
      close, because this is the milestone that gives the shell its remaining
      panes.
- [ ] **D021 — a range refusal reports the key for a type.** The register says
      the fix "belongs with a milestone that has other reasons to touch the
      generator". This one has more of those than any milestone since M2: five
      datatypes, twelve enums, twenty-odd classes and four services all land
      through the IDL. See Decision 14.

## NOT in scope

Written before the work, so that a gap found later is measured against a
decision rather than against a hope.

1. **No `RichText`, and no complex-script shaping.** stb_truetype gives glyphs
   and kerning; Arabic, Devanagari and Thai need a shaper, and ADR 0011 already
   records HarfBuzz as a post-v1 seam and this as a flagged i18n gap. A label
   renders its codepoints left to right.
2. **No `SurfaceGui` and no billboards.** UI is screen-space this milestone;
   api-design §2.2 already lists both as not-in-v1.
3. **No `ParticleEmitter`, no video, no 2D sprite layer** (R15 for the last).
4. **No UI modifier beyond the three api-design names**: `UIListLayout`,
   `UIPadding`, `UICorner`. No `UIGridLayout`, `UIScale`, `UIStroke`,
   `UIGradient`, `UIAspectRatioConstraint` — each is a real feature and each is
   a v2 row, not an afternoon.
5. **No animation state machines, no IK, no root motion, no additive or masked
   blending.** Linear blending of concurrent tracks by weight. The roadmap's
   words are "enough for idle/walk/jump" and that is the bar.
6. **No animation retargeting.** A clip plays on the skeleton it was authored
   against; a mismatch is an error with a key, not a best effort.
7. **No ragdolls and no physics on skinned meshes.** A `CharacterBody` is still
   a capsule; the animated mesh rides on it through the M5 weld.
8. **No audio DSP graph.** No reverb zones, no occlusion, no filters, no submix
   effects. `AudioGroup` is a volume bus and nothing else.
9. **No streamed audio.** A sound asset is fully decoded into memory when it
   loads. Streaming a long music track off disk is asset work, and M7 owns the
   streaming budget; a half-streamed sound with no budget to spend is worse than
   one that costs its megabytes honestly.
10. **No touch input and no on-screen virtual gamepad.** `Enum.InputDeviceType`
    carries `Touch` because the enum is designed once (ADR 0029); nothing
    produces it in v1, and mobile is R15.
11. **No haptics or rumble.**
12. **No rebinding *UI*.** `saveBindings`/`loadBindings` and
    `GetPreferredBinding` ship, because they are the engine's half. The settings
    screen that calls them is game code, and the obby does not need one.
13. **No `Players` service and no `PlayerGui`.** `UIService` is the mount point,
    and `Players` stays a reserved name (api-design §2.1).
14. **No IME or composition editing** in `TextInput` beyond what SDL's text
    events deliver: typed text, backspace, and a caret. No selection, no
    clipboard, no undo.
15. **D022 is not fixed here.** Every `Part` still renders as a wireframe box;
    it is M7.5's, it moves every render golden, and the obby is built out of
    parts. The UI goldens this milestone records are of *UI*, and the world
    behind them is the world M5 shipped.
16. **No instanced draws.** Named M7.5 scope with a measured number attached
    (`docs/perf-baselines.md`); the obby is not a horde.
17. **No parallel dispatch.** Input resolution, layout, tween stepping and clip
    sampling all run on the sim thread. The `Phase` windows exist; wiring them
    is M7's job-system work.

## Gate checklist (verbatim from roadmap)

- [ ] UI capture goldens at two resolutions (proves layout scaling)
- [ ] tween output vs. easing fixture tables
- [ ] input replay of a full obby run completes to the finish flag in CI
      headless (the E2E gate for the whole stack so far)
- [ ] audio smoke test (device opens, buffer underrun counter zero in a 60 s
      soak)
- [ ] animation clip sampling determinism covered by the replay hash
- [ ] the M5 example migrated to the Action System with no regression

And the standing items every gate here carries: both tiers green through
`scripts/localgate.ps1`, the docs gate, the formatting gate, a screenshot of the
deliverable looked at *against the scene it describes*, and the defect register
in `docs/defects.md` reconciled.

## Build order

1. **Grounding pass, and vendor Clay and miniaudio.** Resolve both pins off
   upstream, sync, wire the CMake, and answer from vendored source — quoting
   file and line — the questions the rest of the milestone rests on: Clay's
   arena and measure-text contract and whether its solver is reentrant;
   miniaudio's null backend and its underrun counter; what stb_truetype needs in
   order to kern. Every web-derived claim gets a row in `UNCONFIRMED.md`.
2. **The IDL wave.** Five datatypes, twelve enums, the class tree, the four
   services, and D021's per-property error keys. First, because every other step
   binds against generated descriptors — and because a generator change this
   size wants to be one commit that moves nothing else.
3. **The `platform` device layer.** Mouse motion, buttons and wheel; text input;
   gamepad buttons and axes with connect/disconnect; window focus. The recorded
   input stream M5 built grows to carry them.
4. **`engine/input`** — the IAS: contexts, actions, bindings, resolution order,
   the dispatch split, `GetPreferredBinding`, persistence. `InputService`.
5. **Migrate `examples/03-physics-playground` and delete `KeyboardService`.**
   Early rather than last: it is the gate item that proves the IAS is a real
   input path, and everything after it is then built on a path a human has
   driven.
6. **`TweenService`** — the smallest of the five, and the only one that needs
   nothing from the other four.
7. **`engine/ui`** — the instance tree, Clay behind it, the stb_truetype atlas,
   the ui2d draw list, hit-testing through an engine-owned context, `UIService`.
8. **`engine/audio`** — miniaudio, the sim-owned timeline, `Sound`,
   `AudioGroup`, `AudioService`.
9. **Animation** — glTF skins and clips through fastgltf, the skinned pipeline
   variant, `AnimationPlayer`/`AnimationTrack`, blending.
10. **D017** — the DebugShell's remaining panes, which by then have a UI
    milestone's worth of stats to show.
11. **`examples/04-obby`**, and the recorded run that drives it to the flag.
12. **Gates, budgets, docs, ledger.**

## The decisions this brief makes

### 1. Vendoring Clay and miniaudio is a row fill, and the pins are resolved rather than recalled

Both rows have been in `third_party/manifest.json` since planning, each with an
accepted ADR — 0011 for Clay, 0009 for miniaudio. That makes this the same act
as vendoring Jolt at M5: filling in a commit for a decision already taken, not
adding a dependency, which R5 would send to the human.

One difference is worth naming rather than discovering. Jolt's row carried a
real target version (`5.6.0`) from the research report; **Clay's row carries no
version at all** — `"version": "pinned-commit"`, `"commit": "TBD-AT-M0"` — so
*which* commit becomes the pin is a decision this milestone makes rather than
one it inherits. It is resolved off upstream with `vendor.luau resolve`, at the
newest release tag, and the row's version string becomes that tag. §9 forbids
recalling a version from memory, and a single-header library that has reshaped
its API across releases is exactly the case where memory is confidently wrong.
miniaudio's row names 0.11.25; that is the pin unless upstream says the tag does
not exist.

### 2. The `platform` event surface grows into a device layer, and that layer is not the API

`platform/event.h` is five event types and a keyboard. The IAS needs mouse
motion and buttons, a wheel, text input, gamepad buttons and axes with
connect/disconnect, and window focus. All of that grows in `platform`, where SDL
already lives, and none of it is public (R17).

`platform::Key` stays exactly what its own comment says it is — physical keys by
the US legend — and `Enum.KeyCode`, which spans keyboard *and* mouse *and*
gamepad, is declared once in the IDL and mapped from device events by `input`.
Two tables rather than one, because they answer different questions: one is
"which key did the OS report", the other is "what may a binding name". The
comment M5 left in that header predicted this split, and it holds.

### 3. A context declares its dispatch rate; `Priority` does not imply it — ADR 0039

ADR 0029 splits dispatch into render-rate and sim-tick, and `architecture.md` §2
describes the render-rate half as firing "UI/camera-priority contexts", which
reads as though `Priority` selects the rate. It cannot, and the reason is worth
writing down: `Priority` orders *fallthrough*, so overloading it would make "a
low-priority context that still wants render-rate mouse delta" inexpressible
and — much worse — would silently change an action's determinism class the day
somebody re-tunes a number for an unrelated reason.

So `InputContext` gains **`Rate: Enum.InputRate`**, items `Simulation` (default)
and `Render`, and `Enum.InputRate` joins the §2.3 list. The default is the safe
one: an action nobody thought about fires on the sim tick, where R10 holds and
the replay gate can see it. `Render` is opt-in, for camera look and for UI, and
the class doc says in one line that a gameplay decision taken from a render-rate
action is frame-rate-dependent by construction.

This is a divergence from api-design.md §2.4 as written, so it ships as
**ADR 0039** with the api-design edit in the same commit (§5).

### 4. An action's state is a tick-coherent snapshot, which is what makes the replay a replay of input

M5's `KeyboardService` already settled this contract and paid for it: what a
poll reads is the device *as of the current sim tick*, so two polls inside one
tick agree, and a recorded stream can answer with no hardware attached.
`InputAction:GetState()` inherits it exactly. That is what makes the roadmap's
"input replay of a full obby run" a replay of input rather than of a bot calling
`Move` — the distinction M5's Decision 6 made blocking, and the reason this
milestone's obby gate is the end-to-end gate for everything built so far.

The recorded stream grows to carry the new axes; it is not replaced. A
`Render`-rate context is **not** recorded and not replayed, because a render
frame is not a unit the replay has — which is a second reason the default rate
is `Simulation`.

### 5. The UI tree is the authority; Clay is called on it, never the reverse

Clay is an immediate-mode layout solver: a caller declares a tree each pass and
gets back a command list. Ours is a retained tree of Instances living in
`scene`. The compile direction is therefore one-way and once per relayout: walk
the UI subtree in document order, emit Clay elements, run the solver, write the
results back into `AbsolutePosition` and `AbsoluteSize`.

**Clay's command list is not the draw list.** The ui2d pass draws from our own
instances, ordered by `ZIndex` then document order, because Clay does not know
what an `ImageLabel`'s slice-9 is or that a `TextButton` has two layers — and a
renderer driven by a solver's output would turn every new UI property into a
solver change. Clay stays internal (ADR 0011): no Clay type appears in any
public header, and `checklayers` sees `ui` at L5 including `render` and `input`
and nothing above.

### 6. Relayout is dirty-driven, and the proof is a counter rather than a stopwatch

The roadmap asks for UI cost measured as relayout separately from draw, and for
"a static-UI case whose relayout cost is expected to be ~zero". That is a design
constraint stated as a measurement: a screen nothing changed must not run the
solver at all. A layout-affecting write (`Position`, `Size`, `AnchorPoint`,
`Text`, `Visible`, `AutomaticSize`, any modifier property, any tree change)
marks the nearest `ScreenGui` dirty; a purely visual write (`BackgroundColor`,
`TextColor`, `ImageColor`, `BackgroundTransparency`) does not.

The benchmark asserts **zero solver invocations** on an idle frame, not a small
number of microseconds. At this scale a timing assertion measures the clock, and
"~zero" measured as a duration is precisely the shape of gate that passes while
doing nothing — the register is at thirteen of those, and this milestone is not
adding the fourteenth.

### 7. Text is an atlas per (font, pixel size), kerned, and a missing glyph is visible

stb_truetype for rasterization and metrics, stb_rect_pack for the atlas — both
already inside `third_party/stb`, so text costs no new manifest row. One R8
atlas per font and integer pixel size, grown by re-packing rather than by
sprouting a second texture. No SDF in v1: `TextScaled` re-rasterizes at the size
it lands on, which is the honest cost of sharp text without a distance-field
pipeline nothing else needs yet.

A codepoint the font has no glyph for draws the font's `.notdef` box rather than
nothing. A label that silently drops characters is the failure mode that ships;
a row of boxes is the one that gets reported.

### 8. Tweens write through the same setter scripts write through, and step on the sim tick

The roadmap's performance note says a second write route would forfeit the
equality filter worth roughly a third of the 10k-parts measurement. The
correctness argument is stronger than the performance one: a second route would
also bypass `GetPropertyChangedSignal`, the physics mirror's change tracking and
the range refusals, so a tweened `Size` and an assigned `Size` would *mean*
different things. Tweens call the same setter.

And they advance on the **SimClock**. `TweenInfo.Time` is in the same seconds
`task.wait` is, a tween is therefore a whole number of ticks, and a replay
reproduces it exactly. The cost is that a tween is as smooth as the tick rate;
the alternative is a system whose entire purpose is writing properties reading
the wall clock to decide what to write, which R10 forbids for good reason.
Rendering already interpolates transforms with `alpha`, so a tween on a `CFrame`
is smooth on screen anyway — which is where smoothness belongs.

### 9. `Sound` is driven by the SimClock; the device is downstream

Audio is the first system in this engine whose backend owns a clock and a
thread. If `Sound.TimePosition` reported the mixer's position, a script reading
it would be reading the wall clock through a side door (R10), and the same
replay would diverge between two machines with different buffer sizes.

So the simulation owns the timeline. `TimePosition` advances by
`FixedTimestep * PlaybackSpeed` per tick, `Ended` fires from *that* timeline at a
drain, and `audio::update` pushes the resulting voice state to miniaudio once per
frame, after the tick. What the speakers do is a consequence of the simulation
and never an input to it — which is also what makes a headless run with no
device produce the same `Ended` on the same tick as a run with speakers.

### 10. The audio gate runs on a null device, because CI has no speakers — and on a real one here

The Linux gate container's SDL reports `Audio drivers: dummy`, and no CI runner
in this project has audio hardware. miniaudio ships a null backend. `audio::init`
selects it when the process is headless or when no device opens, logs that once
at `Info`, and does not fail — a game that cannot open a device still has to
run.

The roadmap's 60-second soak with a zero underrun counter therefore runs
everywhere. It is recorded **twice**: on the null backend in CI, and on this
machine's real device with the number written into the Gate Record. A soak that
only ever ran against silence would certify a mixer that never mixed.

### 11. Skinning is a separate vertex stream and a separate pipeline, not a branch in the lit one

Joints and weights are eight to sixteen bytes a vertex. Adding them to
`asset::Vertex` would spend that on every static vertex in every world, forever,
so that a character can have elbows. Instead: a second vertex buffer bound only
by skinned draws, and a `pbr_skinned` pipeline variant beside `pbr`. An
unskinned mesh's vertex layout stays byte-identical to M4's and its draw is
unchanged — which is also what keeps the M4.5 render goldens meaningful through
this milestone.

R16's mindset, applied to the GPU: a feature's cost belongs to the things that
use it.

### 12. Clip sampling is deterministic, happens at `PreAnimation`, and the palette interpolates like everything else

`architecture.md` §3 already placed it: step 5b, "animation clip sampling after
drain". Track time, `Speed` and `Weight` all advance on the SimClock. Blending
is a weighted average of local-space TRS, renormalized, **in track load order** —
R10 forbids the order coming out of a hash map, and two tracks at weight 0.5 must
blend the same way on every run.

The joint palette the renderer uses is interpolated between the two most recent
sim ticks by the same `alpha` that `render::extract` already applies to
transforms. That is what lets a 30 Hz sim animate smoothly, and it means
animation introduces no new interpolation concept — it reuses the seam ADR 0027
froze.

### 13. `KeyboardService` is deleted this milestone, not deprecated

It was tagged `DevOnly` at M5 precisely so that its removal would be structural
rather than a promise (M5 Decision 7). The gate item is the migration of
`examples/03-physics-playground`; the *proof* is that the class is gone from the
IDL, from the generated defs, from the api-dump and from the binary, so that
ADR 0029's "the only input path" is a property of the code rather than a
sentence in a document.

### 14. D021 is paid here, because this is the milestone that touches the generator anyway

The register says the fix "belongs with a milestone that has other reasons to
touch the generator". This one has more of those than any milestone since M2.
The fix is a per-property error-key override in the IDL, so that a range refusal
names the range: `PhysicsService.FixedTimestep = 1/10` stops answering "it takes
a number" about a number. Every M5 property with a range is covered, and the new
ones this milestone adds — `Volume`, `PlaybackSpeed`, `Weight`, `Speed`,
`Transparency`, `ZIndex` — are written with it from the start rather than
inheriting the defect.

### 15. Hit-testing is an engine-owned context at the top of the priority order, and it sinks

`architecture.md` §2 says it: "hit-testing routes through an engine-owned
high-priority `InputContext` with Sink". Concretely — the `ui` module registers
one context above every script-created one; when a pointer press lands on a
`UIObject` whose `Visible` chain is true, that context consumes the press and
fires `Activated`, and nothing below it sees the event. A button over the world
therefore does not also shoot the gun, which is the single behaviour every UI
system is judged by and the one that is embarrassing to add later.

The mechanism is also what makes `InputBinding.UIButton` work: a binding naming
a `TextButton` is fed by the same hit-test, so an on-screen button and a key are
two bindings of one action rather than two code paths.

## Subagent plan

**Orchestrator-only this milestone.** MASTER_PROMPT §7 permits fan-out and rates
coherence above it, and every one of M6's five systems crosses at least two
module seams — input↔scene↔script, ui↔render↔input↔scene, audio↔asset↔scene,
animation↔asset↔render↔scene. §7 names exactly that ("anything touching two
modules' seams") as work that does not fan out, and the IDL wave is a
cross-cutting refactor, which it names too.

Where fan-out would be correct if it were used: conformance specs written from
`docs/api-design.md` alone, by an author who has not read the implementation.
That is the one part of this milestone whose value comes from *not* sharing
context. It is recorded here as an option taken deliberately rather than
overlooked.

## Attempted / abandoned

(append during the milestone — §12)

## Findings

Things the documents assumed that reality corrected, in the order they cost
time.

1. **The audio gate names a counter miniaudio does not have.** The roadmap's
   words are "buffer underrun counter zero in a 60 s soak", and the string
   `underrun` appears six times in the whole of `miniaudio.h` (0.11.25) --
   every one of them a comment or an ALSA log message
   (`miniaudio.h:29882`, `:29931`, `:29944`), none of them a counter a caller
   can read. There is no `ma_device_get_underruns` and nothing equivalent. So
   the counter is ours to build and, more to the point, ours to *define*: it
   counts the data-callback invocations in which a voice that should have been
   audible was mixed as silence, plus the commands dropped because the ring
   between the sim and the mixer was full. Both are real failure modes of the
   design in Decision 9, and both are zero in a healthy run. Written down
   because "the counter reads zero" is worthless until somebody says what it
   counts -- and a counter nothing ever increments is the fourteenth gate that
   passes while doing nothing.

2. **Clay has exactly one current context, in a global.** `Clay__currentContext`
   (`clay.h:1018`) is a file-scope pointer that `Clay_SetCurrentContext` writes
   and every layout call reads. The solver is therefore not reentrant and not
   thread-safe, which is fine -- Decision 17 of the NOT-in-scope list already
   keeps layout on the sim thread -- but it is a constraint that has to be
   known before somebody moves relayout into a job at M7. It also means the
   arena is sized once, up front, from `Clay_MinMemorySize()`
   (`clay.h:824`), and that arena exhaustion arrives as
   `CLAY_ERROR_TYPE_ARENA_CAPACITY_EXCEEDED` through the error handler passed to
   `Clay_Initialize` (`clay.h:778`, `:837`) rather than as a crash. That handler
   is the one place a UI too large for its arena can be reported, so it gets a
   real i18n key rather than a default.

## Gate Record

(filled at milestone end, before human review)
