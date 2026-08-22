# M8 Kickoff — Flagship, Hardening, Docs, v1.0

- Started: 2026-08-22
- Roadmap section: docs/roadmap.md#m8--flagship-hardening-docs-v10-m

## Goal (restated)

Assemble, polish, prove, ship. Every milestone so far built a capability and
proved it in a scene written to exercise that capability alone. This one builds
the scene that has to hold all of them at once — `examples/10-open-world`, a
third-person character walking a streamed world under a moving sun, with a HUD,
ambient sound, physics and hot reload — and then hardens the engine around the
things a person who is not the author will hit first: a quality setting for a
GPU that is not the reference machine, an icon that is theirs and not ours, a
`luaug build` that produces something they can send to somebody else, and a
migration guide that tells a developer arriving from the platform this engine
borrows its shape from what changed.

Two of the items are not about the demo at all, and both are promises made
several milestones ago coming due. **The editor seam** (ADR 0017) was declined
for v1 on the explicit condition that nothing hard-codes an assumption blocking
one, and nobody has checked in four milestones; the concrete check is two
`WorldHost`s alive at once, each with its own VM, rendered into two targets.
**The absolute performance targets** in [`../perf-baselines.md`](../perf-baselines.md)
say "bind at M8" — this is where they stop being aspirations and become numbers
with a scene behind them.

The v1 definition of done is in `MASTER_PROMPT.md` §13, and it ends with a human
playing the demo and saying ship. Everything below is evidence offered to that
decision.

## Scope checklist (from roadmap)

- [ ] **`examples/10-open-world`** — third-person character exploring a large
      open world: streamed chunks (terrain + props via the M7 pipeline), Jolt
      physics, day/night cycle (sun animation + tuned tonemap), HUD, ambient
      audio, all hot-reloadable.
- [ ] **Performance pass to absolute targets** — 60 fps at 1080p on the recorded
      reference machine; the standing targets at the end of
      [`../perf-baselines.md`](../perf-baselines.md) bind here.
- [ ] **`luaug build` packaging** — distributable player + content.
- [ ] **Graphics settings, as a family rather than a number** — shadow
      resolution, cascade count and distance, render scale, light budget and
      post toggles stop being `constexpr`. Engine settings, not `Lighting`
      properties: a scene must not decide the player's GPU budget (ADR 0038).
- [ ] **Prove the editor seam is still open** — two `WorldHost`s alive at once,
      each with its own `ScriptRuntime`, rendered into two targets.
- [ ] **Application identity** — `branding/` wired up; a game built with
      `luaug build` takes its icon from `[project] icon` in `luaug.toml`;
      embedded in the artifact, all sizes in one resource, taskbar identity on
      Windows, and **verified by reading the resource back out of the built
      artifact** rather than by looking at it.
- [ ] **Docs completion** — [`../coming-from-roblox.md`](../coming-from-roblox.md)
      written for real; API reference generated from the defs pipeline; README
      with screenshots.
- [ ] **License/NOTICE audit** of every vendored dependency.
- [ ] **CHANGELOG**; tag `v1.0.0`; GitHub release with Windows binaries + source
      instructions.

## NOT in scope

- Anything on R15's closed list — no editor, no multiplayer/replication, no 2D
  layer, no mobile port, no navmesh. The editor **seam** is proven; no editor is
  built.
- **Any new rendering technique.** M7.5 closed the renderer's feature list. What
  happens here is that its numbers become configurable and its cost becomes a
  budget. A gap found against a reference is a defect with a row, not new scope.
- **A settings persistence layer.** The engine ships no save/load pair for
  graphics settings, for the same reason and with the same wording it ships none
  for input bindings (api-design.md §2.4): persistence is the game's, and a
  settings screen serializes it like any other state.
- **`luaug build` for macOS and Linux targets.** The roadmap's release line says
  Windows binaries plus source instructions, and a packaging path no gate on
  this machine can execute is a packaging path that ships broken. The
  cross-platform halves of application identity — `.icns` plus `Info.plist`,
  `.desktop` plus hicolor — are *specified* here and built when a tier can run
  them.
- **A launcher, an installer, an updater, or a content-delivery story.** A
  folder that runs is what v1 owes.
- **Terrain, particles, decals, `SurfaceGui`.** The open world is streamed
  meshes and parts, which is what M7's pipeline produces. Post-v1 phase 2 owns
  the rest and says so.
- **Cross-platform determinism.** ADR 0025 level B stands; the `churn10k`
  judgement and Jolt's `CROSS_PLATFORM_DETERMINISTIC` switch remain human
  questions in the ledger.
- **A second Luau-facing service beyond the one the settings family needs.**

## Subagent plan

**None.** Every item here is either a cross-cutting seam (the two-world proof
touches `app`, `script`, `render` and `scene` at once), a gate run, or a piece
of prose whose value is that one person held the whole milestone in their head
while writing it. `MASTER_PROMPT.md` §7 names all three as orchestrator-only
work, and this session's operating instruction forbids fan-out besides.

## Gate checklist (verbatim from roadmap)

- [ ] 10-minute scripted soak (walk + fly path) with zero crashes and bounded
      memory delta
- [ ] 60 fps at 1080p on the recorded reference machine
- [ ] every example launches and its automated run passes
- [ ] clean-machine CI job: fresh clone → bootstrap → build → `luaug new`
      template project runs
- [ ] determinism replay green
- [ ] `luaug check` clean repo-wide
- [ ] docs-lint clean
- [ ] **a human plays the demo and signs off** — the one gate that is
      deliberately not automatable

## The decisions this brief makes

### 1. Graphics settings are a process-level struct, and the game may write it

`Lighting` describes the world; a quality setting describes the machine. The two
must not be the same object, because a scene that shipped with a 4096-texel
shadow map would be deciding how a stranger's laptop spends its frame — which is
the whole of ADR 0038 §3's argument for putting this here rather than in
`Lighting`.

So the settings live in one `GraphicsSettings` struct, sourced in three layers,
each overriding the one before:

1. A **quality preset** — `Low`, `Medium`, `High`, `Ultra` — which is a named
   set of every field.
2. **`luaug.toml` `[graphics]`**, which is the game author's default for their
   own content.
3. **`luaug-host` flags**, which is what the gate and a person debugging use.

And a fourth source that is not a layer because it happens later: **the running
game may write them**, through a service, exactly the way an options menu needs
to. Persistence is the game's — this is the `InputBinding` precedent quoted in
the NOT-in-scope list, and it is deliberate that the two answers match.

**They are outside the world hash.** A replay does not record them and a
determinism gate does not read them: nothing in `render` may reach the
simulation (R10), so a machine that renders at half scale must still hash
identically to one that does not. That is an assertion this milestone makes with
a differential rather than a claim it makes in prose.

### 2. The editor-seam proof is a differential, not a boot

Two `WorldHost`s that both construct without crashing prove nothing: a global
that both of them scribble into would still construct. What the phase-2 editor
needs — and what post-v1 phase 4's loopback multiplayer needs, which is why this
has two callers — is that the two worlds are *independent*. So the test renders
both, and asserts:

- each world's image is **byte-identical to the same world rendered alone**, and
- the two images **differ from each other**.

The first half is what catches a shared static: if world B's contents leak into
world A's frame, A stops matching its solo render. The second half is what
catches a test that proved nothing because both worlds were empty. D043 is the
reason both halves are written down — three green instruments agreed about a
frame that was not drawing anything.

### 3. `luaug build` produces a folder, and the folder is the product

Not an installer, not a self-extracting executable, not a single file. A folder
containing the renamed host binary, the engine's `content/` (catalogs, compiled
shaders), the project's compiled bytecode and its built asset pack. It is what
every gate on this machine can execute end to end, and it is what "send it to
somebody" means without a distribution story v1 does not have.

The binary is the host, **copied and then edited**: on Windows its icon resource
group is replaced with the game's, so the artifact carries the game's identity
rather than the engine's. That is a Win32 `UpdateResource` call on a copy of an
already-linked PE — no relink, no per-game build of the engine.

### 4. Application identity is asserted by reading the artifact back

An icon is the single most regression-prone thing in a build, because nothing
fails when it is wrong: the program runs, the window opens, and it wears the
wrong face. So the check is not a screenshot. It opens the built `.exe`, walks
its PE resource directory, finds the icon group, and fails if it is absent or if
its bytes are still the engine's default. `docs/roadmap.md` asks for exactly
this and it is worth restating why: an icon nobody can assert is an icon that
silently regresses.

### 5. The flagship's world is generated, and its soak path is scripted

`examples/05-streaming` established both and for reasons that hold here: a
committed world is a binary asset the roadmap's own deliverable line forbids,
and a hand-flown path is not a gate. The world comes from a deterministic
generator keyed on cell coordinates; the soak path is a recorded route the
character walks and then flies, on the simulation clock, so that the same frame
of the same run is the same picture on every machine.

**The character is the streaming focus**, which is the thing `05-streaming`
deliberately did not do — it used the camera, and noted that a game would hand
it the character instead. This is that game.

## Build order

1. **The editor seam** — first, because its value decays with every line added
   before it and because whatever it finds is cheapest now.
2. **Graphics settings** — before the flagship, so the flagship is authored
   against a real quality family rather than retrofitted with one.
3. **Application identity and `luaug build`** — together, because the icon
   rewrite is a step of the packaging and neither is testable without the other.
4. **`examples/10-open-world`** — the flagship, on top of all three.
5. **The performance pass** — against the flagship, which is the only scene that
   loads every system at once.
6. **Docs, licences, CHANGELOG** — last, because they describe what the first
   five turned out to be.
7. **The full gate**, then stop for the human.

## Attempted / abandoned

_(appended during the milestone)_

## Findings

_(appended during the milestone)_

## Gate Record

_(filled at milestone end, before human review)_
