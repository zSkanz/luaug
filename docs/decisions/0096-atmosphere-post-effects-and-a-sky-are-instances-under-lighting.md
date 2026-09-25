# 0096 — Atmosphere, post effects and a skybox are instances under `Lighting`

- Status: accepted
- Date: 2026-09-24
- Decided by: the owner, 2026-09-24: *"Quero trabalhar um pouco agora na light
  e post-processamento como é feito no roblox saca atmosfera e talls blur tudo
  aqueles fru fru fru"*, together with the ability to change the skybox. The
  shapes below were proposed by the agent under that instruction.
- Supersedes: the 2026-09-24 mandate's **M1** (a post-processing API as
  `Lighting` properties), which this answers more completely and differently.
- Relates to: [0038](0038-visual-fidelity-is-a-v1-target.md) (fidelity is
  judged against a stated reference), [0043](0043-per-instance-vertex-stepping.md)
  (the environment is an octahedral 2D texture prefiltered on the CPU), [0044](0044-graphics-settings-are-host-settings.md)
  (engine settings describe the machine, `Lighting` describes the world),
  [0072](0072-particles-are-a-picture-simulated-on-the-frame.md) (a pass closed
  to sample what it drew), [0084](0084-ambient-is-for-enclosed-spaces-outdoor-ambient-for-open-ones.md)

## Context

The renderer already has a post chain: automatic exposure, a tonemap, bloom,
SSAO, contact shadows and FXAA. It has an analytic sky (a gradient, the sun and,
at night, the moon) that is also the reflection environment. None of it is in a
world author's hands. `Lighting.ExposureCompensation` is the one scriptable
knob (`docs/manual/rendering/post.md`). The quality toggles in `luaug.toml` say
what a *machine* can afford, not what a *world* should look like (ADR 0044).
And `docs/manual/rendering/lighting.md` says it plainly: *"There is no skybox,
no HDRI and no `Sky` instance."*

The reference platform's authors expect the look of a world to be **objects
they add**: an atmosphere, a sky, a bloom, a blur, a colour grade, sun rays and
depth of field, each an instance they insert, tune in Properties and toggle
from a script. That is also the model this engine already uses for everything
else a world has. The mandate's M1 asked for the narrower half of this (bloom
and exposure as `Lighting` properties) and is folded in here.

**Clean-room (R7).** Class and member names follow the reference where that is
the familiar spelling; behaviour, parameter ranges and every piece of
documentation are this engine's own, written from the rendering techniques and
not from anybody's documentation.

## Decision

### The look of a world is instances, and where they live says whose they are

Six new classes, all `Instance`s:

| Class | What it is |
|---|---|
| `Atmosphere` | Haze and aerial perspective: distance and height fog lit by the sun, tinting the sky's horizon |
| `Sky` | A skybox of six images, the sun's and moon's look, stars, and clouds |
| `BloomEffect` | The existing bloom, exposed |
| `ColorCorrectionEffect` | Brightness, contrast, saturation and a tint, applied in the tonemap |
| `BlurEffect` | A full-screen Gaussian blur, for menus and pauses |
| `DepthOfFieldEffect` | Focus by distance, from the scene's depth |
| `SunRaysEffect` | Light shafts from the sun, occluded by what stands in front of it |

The five `*Effect` classes share an abstract `PostEffect` base with `Enabled`.

- **An effect under `Lighting` belongs to the world.** It is saved with the
  scene and, because `Lighting` is on the wire, it replicates.
- **An effect under `Workspace.CurrentCamera` belongs to the viewer.** It is
  local: a pause-menu blur on one player's camera is nobody else's.
- **Anywhere else it does nothing**, and the editor says so on the instance.
  Found twice already with lights (ADR 0095): a silent no-op is the worst
  answer.
- `Atmosphere` and `Sky` count only directly under `Lighting`. The first one in
  document order is the one used, and the editor marks any other as inactive.

### How several of one kind combine

Order matters, so the rule is written down:

- **`ColorCorrectionEffect`**: every enabled one applies, composed in document
  order (`Lighting`'s first, then the camera's).
- **`BlurEffect`**: the sizes combine as a Gaussian does, `sqrt(a² + b²)`, so
  two blurs of 8 are one of about 11.3, not one of 16.
- **`BloomEffect`, `DepthOfFieldEffect`, `SunRaysEffect`**: the first enabled
  one in that same order is used. The editor marks the others as inactive.

### Nothing moves for a world that adds none of this

- **With no `BloomEffect`, the engine's own bloom applies exactly as today.**
  Every existing scene, example and golden has bloom and none has the instance,
  and a new rule that turned their bloom off would change every one of them. A
  `BloomEffect` present **governs** bloom. One with `Enabled = false` turns it
  **off**, which is what a 2D game wants (M1's case).
- **With no `Atmosphere`**, `Lighting.FogStart`, `FogEnd` and `FogColor` work as
  they do now. **With one**, it replaces the linear fog: the properties are kept
  and ignored, and the manual and the Properties panel say so.
- **With no `Sky`**, the analytic sky draws as it does now, day and night.
- **Every other effect is off until it exists.**

### The sun stays on the clock, whatever the sky shows

A skybox is a backdrop and a reflection source. **The sun's direction still
comes from `ClockTime` and `GeographicLatitude` alone**, so shadows, sun rays,
the atmosphere's glare and the drawn sun disc all agree, and a replay lights
the same way (R10). A skybox image with a sun painted into it will disagree with
the real sun. That is the author's to avoid, and the manual says how:
`CelestialBodiesShown = false`, and a sky without a painted sun. This is also
what the reference does. The owner was asked to choose between this and a sun
derived from the image, and this record chooses the familiar model.

### A skybox without a cube texture

The frozen RHI has no cube type (ADR 0037) and no compute (ADR 0043). So a `Sky`:

- **names six images**: `SkyboxBack`, `SkyboxDown`, `SkyboxFront`, `SkyboxLeft`,
  `SkyboxRight`, `SkyboxUp` (whole words, where the reference abbreviates),
  plus `SkyboxOrientation` in degrees;
- **is resampled on the CPU, once per change and off the frame thread**, into
  one octahedral image the sky pass samples by direction. The same resampled
  image feeds ADR 0043's existing prefilter, so reflections and diffuse ambient
  come from the skybox. Until the bake finishes, the previous sky keeps drawing:
  a sky that went black while it loaded would be worse;
- keeps the celestial layer on top: `SunTexture`, `MoonTexture`,
  `SunAngularSize`, `MoonAngularSize`, `StarCount` and `CelestialBodiesShown`.
  With no images, `Sky` still governs these over the analytic sky.

The images are colour data (sRGB, ADR 0073). An HDR panorama is not in this
record.

### Clouds are a layer of the sky

`Sky` carries `CloudCover`, `CloudDensity` and `CloudColor`. They draw as a
procedural layer in the sky pass, lit by the sun and drifting on the simulation
clock (never a wall clock, R10), and they are included in the environment bake,
so reflections see them. Volumetric clouds are out of scope.

### A few `Lighting` properties, added together

`EnvironmentDiffuseScale` and `EnvironmentSpecularScale` (how much the sky
lights and reflects), `ShadowSoftness`, `GlobalShadows`, and `AutoExposure`
(M1's other half). **Adding a property to `Lighting` moves the hash of every
world**, because every world has one (ADR 0060's surviving rule). So they land
in **one** commit that re-records every determinism trace once, and names why.

### Machine settings still win

`luaug.toml`'s quality keys (ADR 0044) gain `depth_of_field` and `sun_rays`, and
keep `bloom`. A world that has a `DepthOfFieldEffect` on a machine set to
`depth_of_field = false` draws without it. **`BlurEffect` and
`ColorCorrectionEffect` have no machine switch**, because a game uses them to
say something: a paused menu, a flash of damage. Turning them off would change
meaning, not cost.

### Each effect has a stated reference and a budget

Per ADR 0038, each effect lands with:

- a before/after capture set that the owner judges;
- goldens that lock the result once the owner accepts it;
- a measured cost at 1920×1080 on the reference machine, recorded in
  `docs/perf-baselines.md`.

Budgets, which a stage may miss only by reporting it:

| Effect | GPU budget |
|---|---|
| `Atmosphere` | ≤ 0.2 ms |
| `BlurEffect` | ≤ 0.3 ms |
| `SunRaysEffect` | ≤ 0.4 ms |
| `DepthOfFieldEffect` | ≤ 0.6 ms |
| `ColorCorrectionEffect` | ~0, since it lives in the tonemap |
| `Sky` bake | ≤ 50 ms off the frame thread; nothing on the frame thread |

## Consequences

- **The mandate's M1 closes by this record, not by `Lighting` properties.** Its
  ledger line points here.
- **No existing golden and no determinism trace moves**, except in the one
  commit that adds the `Lighting` properties, which moves all of them once. New
  classes are hashed only in worlds that create one.
- **The wire schema grows** by the new classes under `Lighting`. A protocol bump.
- **The editor gains** insertion of each class under `Lighting` and the camera,
  inactive markers, and image pickers for the six faces. It needs **class icons
  that do not exist yet**. Until the owner draws them, the fallback icon is
  used, and the brief lists them for the owner.
- **Six classes and about thirty members join the public API**, with a
  divergence row for the face names.

### Not decided here

- An HDR panorama sky, and a sky or post-process written as a user shader
  (ADR 0091's territory).
- Volumetric clouds and volumetric fog.
- Screen-space reflections, motion blur, film grain, lens flare and a
  vignette.
- A tonemapper chosen per world.

### Rejected

- **Everything as `Lighting` properties** (M1's shape). One flat bag cannot
  hold two blurs, cannot put an effect on one player's camera, and cannot be
  inserted, toggled and deleted as a thing, which is how the reference
  platform's users already think.
- **A sun derived from the skybox image.** It breaks the rule that the sun is a
  pure function of the clock, and it would make `ClockTime` do nothing while a
  sky exists.
- **Adding a cube texture type to the RHI for the skybox.** The octahedral path
  already exists for the environment, and a frozen interface is not reopened
  for a backdrop.
