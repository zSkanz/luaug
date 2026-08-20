# 0038 — Visual fidelity is a v1 target, and M7.5 is where it is paid for

- Status: accepted
- Date: 2026-08-20

## Context
The human, after watching `examples/02-meshes` through a full day, said the
engine looks far from Unity and Unreal and that closing that gap is a
requirement rather than a preference: *"temos que parecer uma game engine como as
outras com uma boa sombra otimizada, luz otimizada e bons reflexos."*

That is a scope change, which R15 makes an escalation item, and the human is the
escalation. This ADR records what was decided, because a roadmap row is not a
record of why.

**What M4 actually shipped, stated plainly**, because the gap is specific rather
than a vibe:

- **One shadow cascade**, a fixed 60-unit half-extent box centred on the camera,
  2048², 3×3 PCF with a non-comparison sampler and a slope-scaled depth bias.
  At `examples/02-meshes` one texel is 5.9 cm and the box covers 120 units for a
  scene of about 25. Shadows visibly break up at low sun, which is projective
  aliasing: with the ground near edge-on to the light, one texel spans a long
  stretch of receiver.
- **Eight lights per draw**, unculled, iterated in the forward pass.
- **No image-based lighting.** `pbr.hlsl` applies `Lighting.Ambient` flat to both
  lobes, with `1 - sqrt(roughness)` standing in for a horizon term. Its own
  comment says what that is: "the degenerate case of the split-sum approximation
  where the environment is one colour". A metal reflects nothing, because there
  is nothing to reflect.
- **No post chain** beyond the tonemap resolve: no bloom, no exposure, no
  ambient occlusion, no anti-aliasing beyond whatever the swapchain does.

The single largest visual difference between this and a mainstream engine is the
third one. Punctual lights plus flat ambient is the look of the early 2010s; a
prefiltered environment is what makes metal read as metal and what makes every
surface sit in its scene.

## Decision

**1. Visual fidelity is a v1 target and gets a milestone, not a backlog.** A new
**M7.5 — Looking Like an Engine** lands between streaming (M7) and the flagship
(M8), so the flagship demo is built on it rather than shipping without it, and
so cascades are designed against a streamed world rather than retrofitted to
one.

**2. It is scoped to the three the human named — shadows, lights, reflections —
plus the post chain that makes them read.** Named techniques, with the
parameters the literature actually publishes, so the milestone is buildable
rather than aspirational:

- **Cascaded shadow maps.** Four cascades, which is where the reference
  implementations settle. Splits by the practical scheme of GPU Gems 3 chapter
  10 — a blend of uniform and logarithmic partitioning, `λ = 0` uniform, `λ = 1`
  logarithmic, and the useful range in between. **Normal-offset bias** replacing
  the depth-only bias, since displacing the sample along the surface normal is
  what survives a grazing receiver; the published starting point is a small
  manual bias around 0.0015 with an offset scale of 1 to 2. A **hardware
  comparison sampler** so a tap degrades instead of switching, and a PCF kernel
  between 2×2 and 7×7 — the measured cost of going all the way to 7×7 at 1080p
  is about 0.4 ms, which prices the whole question.
- **Clustered forward shading**, so the light count stops being eight.
  Olsson and Assarsson's clustered shading, with the grid Doom 2016 used —
  16×9×24 — and exponential depth slicing,
  `slice = max(log2(linearDepth) · scale + bias, 0)`, which is what makes one
  grid serve a near plane at 0.1 and a far plane in the thousands.
- **Image-based lighting by the split-sum approximation** (Karis, 2013). A
  prefiltered environment cubemap whose mip chain is indexed by roughness, and a
  2D BRDF lookup table indexed by (N·V, roughness) supplying the Fresnel scale
  and bias. Karis's assumption that the view direction equals the normal is what
  makes the environment pre-integrable at all, and its cost is the known one:
  reflections do not stretch with view angle.
- **The post chain that makes the above visible**: exposure, bloom, an ambient
  occlusion term, and anti-aliasing. A correctly lit frame presented through a
  naive resolve still looks unlike the reference.

**3. The graphics-settings family lands at M8**, and is a family rather than a
number: shadow resolution, cascade count and distance, render scale, light
budget, post toggles. It is an **engine** setting and not a `Lighting` property —
a scene must not decide the player's GPU budget. M8 is the right milestone
because until M7.5 exists there is nothing worth exposing, and hardening is
where a quality slider belongs.

**4. The gap is measured, not judged.** M7.5's gate compares the same scene
against a reference render and against the previous milestone, and the perf
table records what each feature costs. "Looks better" is not a gate result.

## Alternatives considered

- **Fold it into M8.** Rejected: M8 is 6% and is the flagship plus hardening plus
  docs plus release. A renderer's second half is not a hardening task, and the
  flagship would be built against the renderer this ADR exists to replace.
- **Defer to post-v1.** Rejected by the human, and correctly: the engine's
  visible surface is the one thing a person judges it by, and the gap named here
  is not a polish gap.
- **Keep the eight-light forward loop and add only IBL.** Tempting, because IBL
  is the largest single visual win for the least work. Rejected as a stopping
  point rather than as an ordering — IBL first is likely the right build order,
  but shipping it alone leaves the light count and the shadows exactly where the
  human said they were.

## Consequences

- **Weights are renormalized**, since a share of v1 is a share: M7.5 takes 10,
  and every other milestone gives up roughly a tenth of its share. The total is
  still 100 and no work was deleted.
- **The RHI freeze (ADR 0037) will be tested by this.** Cubemaps, mip-chain
  generation, compute for prefiltering, and array textures for cascades are the
  most likely places the frozen 32 calls turn out to be 32 minus something.
  Whatever is needed is an ADR, which is the freeze working rather than the
  freeze failing.
- **R16 binds harder than it looks.** Interpreter-first is about Luau, but the
  same logic — the low end is where the budget is real — applies to every choice
  here, and `docs/perf-baselines.md` already asks for a reduced-CPU row for
  exactly this reason.
- **R15's list is unchanged.** No editor, no multiplayer, no 2D layer, no mobile
  port, no navmesh. This ADR adds fidelity to what v1 renders; it does not open
  the closed list.
