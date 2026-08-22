# 0044 — Graphics settings are host settings, in three layers

- Status: accepted
- Date: 2026-08-22
- Supersedes nothing. Delivers ADR 0038 §3's deferred item.

## Context

Every dial in the renderer was `constexpr` until M8: shadow tile resolution and
cascade count, shadow distance, the light budget, and whether bloom, ambient
occlusion, anti-aliasing and automatic exposure run at all. ADR 0038 §3 put the
work here on purpose — *"until M7.5 exists there is nothing worth exposing, and
a quality slider is a hardening concern"* — and the roadmap's M8 entry adds the
sentence that decides the design:

> They are **engine** settings and not `Lighting` properties — a scene must not
> decide the player's GPU budget.

The question this ADR answers is therefore not "should these be configurable"
but **who configures them**, and the answer has to survive one specific case: a
game built with `luaug build` runs `luaug-host` with no Lute anywhere near it,
so any setting only the CLI can read is a setting that stops applying the moment
the game ships.

## Decision

**One `render::GraphicsSettings` struct, resolved through three layers, each
overriding the one before.**

1. **A quality preset** — `low`, `medium`, `high`, `ultra` — which is a named
   set of every field. `high` is *exactly* what the engine shipped through M7.5,
   to the value, and `engine/render/tests/settings_tests.cpp` asserts it field
   by field. That is a requirement rather than a coincidence: every pixel golden
   in the repository was recorded against those constants, and a default that
   differed by one field would have re-recorded all of them at once — which
   hides whatever else moved with them (M7.5 Finding 4).
2. **`luaug.toml` `[graphics]`** — the game author's default for their own
   content. Read by the ENGINE, through a TOML subset reader that now exists in
   `core` (see Consequences).
3. **`luaug-host` flags** — `--quality=`, `--render-scale=`,
   `--shadow-resolution=`, `--shadow-cascades=`, `--shadow-distance=`,
   `--light-budget=`, and `--bloom`/`--no-bloom` and its three siblings. This is
   what a gate, a perf sweep and a person debugging use.

Each layer is expressed as an **override** rather than a value, so that "nobody
said anything" and "somebody asked for the default" stay different answers. The
result is clamped once, at the last door, so a hand-edited file cannot ask for a
render scale of zero.

**A preset the player names replaces the file's per-key entries as well as its
level** — added at D052, and it is the one rule in this stack that is not
implied by "each layer overrides the one before". A file's `shadow_resolution`
is a refinement *of the level that file names*: "high, but the shadows reach
further, because this world's landmarks are far away". Somebody typing
`--quality=low` is saying that level is not available on this machine, and
carrying its refinements across would hand a weak machine the single heaviest
dial in the file while every other one was turned down. So layer 2 applies only
when layer 3 named no preset; layer 3's own per-key flags always do, because
they were typed by the same person as the preset. Layer 2's non-graphics half —
the window title, the identity, the icon — is untouched by any of this: those
are not performance dials.

**A running script is not a fourth layer, and that is the decision inside the
decision.** The roadmap's own sentence forbids it: a script is the scene, and a
scene that could write these would be deciding a stranger's GPU budget. What
v1 ships is a family a *player* or a *packager* configures; what it does not
ship is an in-game quality slider, because that needs a Luau surface and the
line above says a script may not have one.

**They are structurally outside the simulation.** `GraphicsSettings` lives in
`render`, which is L4; `scene` is L3 and may not include it. That is not a
convention — `tools/repo/checklayers.luau` derives the layering from real
`#include` edges and the Luau gate runs it, so a settings value reaching the
world hash is a red gate rather than a discovery. The replay harness makes the
same point from the other side: it creates no device and no renderer at all
(`replay.h`), so there is nothing for a quality level to change.

## Consequences

- **`core` gains a TOML subset reader** (`luaug/core/toml.h`), which
  `tools/cli/toml.luau` predicted in as many words: *"The engine gets a TOML
  reader in the milestone that first needs engine-side configuration, and not
  before."* It reads the same subset the CLI's does, deliberately: a project
  file that `luaug dev` accepts and the shipped player rejects is worse than
  either being stricter. It pays for itself three times in this milestone —
  `[graphics]`, `[window] title` and `size`, and `[project] icon`.
- **`WindowDesc` gains the passthrough title it reserved at M1.** A title from
  `[window] title` is the game's own string and there is nothing to translate it
  against, which is the same split `log()` and `logText()` already draw (R3).
- **`IRenderer` gains `setSettings`/`settings`.** ADR 0027's seam, not the
  frozen RHI (ADR 0037): no backend interface changed, and none needed to.
- **The renderer's dials cost what they say.** Fewer cascades buy *submission*
  and not memory — the atlas stays two tiles by two, and `shadow_resolution` is
  the dial that buys memory. A cascade nothing renders into is a cleared tile,
  and a cleared depth reads as lit, which is what lets the cascade count vary
  with no shader change at all.
- **A post pass that a setting switches off still clears the texture downstream
  of it.** Binding an untouched target and multiplying by zero would turn an
  uninitialised NaN into a black frame; a clear is both cheaper than the pass it
  replaces and independent of what was in memory.
- **The family is gated by a differential, not a golden.** `low` and `ultra`
  must render the same scene to *different* images
  (`tests/screenshots/run_settings_differential.cmake`). A preset that is parsed
  and then reaches nothing renders exactly what it did before, and no golden can
  see that — which is D043's lesson applied before the fact instead of after it.

## Alternatives considered

- **`Lighting` properties.** Rejected by the roadmap's sentence, and rightly: a
  scene would then ship its author's GPU budget to every player.
- **A `SettingsService` in the IDL.** This is the in-game quality slider, and it
  is the thing a post-v1 phase should take up deliberately. It needs a home for
  values that must not enter the world hash, and the honest version is a service
  whose backing store is the host's rather than `scene`'s. Declaring a class
  nothing implements is what `instances.api.luau` forbids, so the name is not
  reserved either.
- **A user-level settings file the engine writes.** That is persistence, and the
  engine ships none — for graphics settings for the same reason it ships none
  for input bindings (api-design.md §2.4): persistence is the game's, and a
  settings screen serializes it like any other state.
- **Making the cascade count a shader constant per variant.** Four pipelines'
  worth of permutations for a dial that a cleared tile already answers.
