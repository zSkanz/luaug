# Atmosphere, post effects and the skybox: the kickoff and the ledger

The owner, on 2026-09-24: *"Quero trabalhar um pouco agora na light e
post-processamento como é feito no roblox saca atmosfera e talls blur tudo
aqueles fru fru fru"*, and the ability to change the skybox. The decision is
[ADR 0096](../decisions/0096-atmosphere-post-effects-and-a-sky-are-instances-under-lighting.md).
**Read it before this file.** This file is the order of work and where each
piece stands.

In one paragraph: the look of a world becomes instances under `Lighting`, or on
the current camera for a viewer's own: `Atmosphere`, `Sky` (a six-image skybox,
the sun and moon, stars, clouds), `BloomEffect`, `ColorCorrectionEffect`,
`BlurEffect`, `DepthOfFieldEffect` and `SunRaysEffect`. With none of them
present, a world draws exactly as it does today. It closes the 2026-09-24
mandate's M1.

Legend: `[ ]` not started · `[~]` in progress · `[x]` done.

## What must hold at every stage

- `scripts/localgate.ps1` green (all six stages, Linux included), then a push,
  then CI read. Never write to a red `main`.
- **Existing goldens do not move.** A world with none of the new instances draws
  exactly as before, the engine's own bloom included. Each new effect gets
  goldens of its own, recorded only after the owner accepts its look.
- **Determinism traces do not move, except once, in Stage 7**, which adds
  properties to `Lighting` and so moves every trace, for the reason ADR 0060
  gives. Re-record them once, with the reason in the commit.
- **Every effect is render-only.** Nothing here reads a wall clock: clouds drift
  and anything animated steps on the simulation clock (R10).
- **R7, clean-room:** names follow the familiar spelling. Behaviour, ranges and
  every line of documentation are written from the technique, never from the
  reference platform's documentation. `tools/repo` lints and `docs-lint` must
  stay green.
- R3 (every editor message and error is an i18n key), R17 (no backend type in
  the API), and the RHI stays frozen (ADR 0037). If an effect cannot be built
  without an RHI change, stop and ask the owner.
- **Measure cost at 1920×1080 against the ADR's budgets**, and record it in
  `docs/perf-baselines.md`. Until D183 lands, the D3D12 debug layer inflates
  windowed numbers, so say which build was measured.
- **The owner judges the look.** Each visual stage ends with a before/after
  capture set in `docs/briefs/atmosphere-post/` (PNG, same camera, same
  `ClockTime`) and a note of what to look at. The golden is recorded after the
  owner's word, never before.
- **Stage only what you wrote.** Other agents and the owner work in this tree.
  Never `git add -A`.

## Stage 1 — The framework

- [x] IDL: the abstract `PostEffect` (`Enabled`) and the five effect classes.
      `Atmosphere` and `Sky` with their members from ADR 0096. Regenerate
      everything the generators write.
- [x] Where an effect counts: directly under `Lighting` (the world's, saved,
      replicated) or under `Workspace.CurrentCamera` (the viewer's, local).
      Anywhere else is inactive.
- [x] The combination rules from ADR 0096, applied in one place in `extract`,
      into a POD block in `RenderWorld`: colour corrections compose in document
      order, blur sizes combine as `sqrt(a² + b²)`, and for bloom, depth of
      field and sun rays the first enabled one wins.
- [x] The editor: each class insertable under `Lighting` and under the camera,
      and an **inactive** marker (with a reason, as an i18n key) on anything that
      does not count: wrong parent, second `Sky`, second `Atmosphere`, a losing
      bloom. Record the missing class icons for the owner. The fallback icon is
      used until the owner draws them.
- [x] Tests: the combination rules; placement; that a world with none of these
      has an identical `RenderWorld` post block to today's.

## Stage 2 — `BloomEffect` and `ColorCorrectionEffect`

- [x] `BloomEffect` (`Intensity`, `Size`, `Threshold`) drives the existing bloom
      chain. With none present, today's bloom applies unchanged. With
      `Enabled = false`, bloom is off.
- [x] `ColorCorrectionEffect` (`Brightness`, `Contrast`, `Saturation`,
      `TintColor`) applied in the tonemap pass, after exposure and before the
      curve. Several compose in order.
- [x] A 2D check: `examples/20-platformer` with a disabled `BloomEffect` has
      sprites that no longer glow. That is M1's own
      acceptance.
- [~] Captures for the owner (`atmosphere-post/stage2/`), then goldens -- awaiting the owner.

## Stage 3 — `BlurEffect`

- [x] `Size` in pixels at 1080p, scaled with the render resolution so a blur
      looks the same at any window size. Separable Gaussian, downsampled for
      large sizes. Several combine as `sqrt(a² + b²)`.
- [~] Budget ≤ 0.3 ms. A blur of `Size = 0` costs nothing (it builds no pass); the cost is measured with the others on the packaged build.
- [x] The UI is drawn **after** the blur, so a pause menu over a blurred world is
      sharp.

## Stage 4 — `DepthOfFieldEffect`

- [x] `FocusDistance`, `InFocusRadius`, `NearIntensity`, `FarIntensity`. A
      circle of confusion from the scene depth, a gather at half resolution, and
      a composite. The sky counts as infinitely far.
- [~] Budget ≤ 0.6 ms (measured with the others on the packaged build). The machine switch `depth_of_field` in `luaug.toml`
      (ADR 0044) turns it off.

## Stage 5 — `SunRaysEffect`

- [x] `Intensity`, `Spread`. A radial gather towards the sun's screen position
      over a mask of what is sky in the depth buffer, so geometry occludes the
      rays. It fades to nothing as the sun leaves the view or goes below the
      horizon.
- [~] Budget ≤ 0.4 ms (measured with the others on the packaged build). The machine switch `sun_rays`, off in the Low preset -- landed.

## Stage 6 — `Atmosphere`

- [x] `Density`, `Offset`, `Color`, `Decay`, `Glare`, `Haze`, each defined in
      the manual by what it does to the picture, in this engine's own words:
      - distance and height fog, lit by the sun's colour and direction;
      - a glare lobe around the sun;
      - a horizon band tinting the sky.
- [x] Applied in the forward pass, on terrain, voxels, parts and particles
      alike, and in the sky pass, so the horizon and the ground meet.
- [x] With an `Atmosphere`, the linear fog is off: `FogStart`, `FogEnd` and
      `FogColor` are kept and ignored, and the Properties panel says so. Without
      one, nothing changes.
- [~] Budget ≤ 0.2 ms (measured with the others on the packaged build). Captures at noon, dusk and night for the owner -- awaiting the owner.

## Stage 7 — `Lighting` properties, in one commit

- [ ] `EnvironmentDiffuseScale`, `EnvironmentSpecularScale`, `ShadowSoftness`,
      `GlobalShadows`, `AutoExposure`. Defaults reproduce today's image exactly,
      and the goldens prove it.
- [ ] **All determinism traces move once, here.** Re-record them all, with the
      semantic change named in the commit (ADR 0060's rule).
- [ ] M1 in `docs/briefs/mandate-2026-09-24.md` is ticked, pointing here.

## Stage 8 — `Sky`

- [ ] Six faces (`SkyboxBack`, `SkyboxDown`, `SkyboxFront`, `SkyboxLeft`,
      `SkyboxRight`, `SkyboxUp`) and `SkyboxOrientation`. Resample them on the
      CPU, **off the frame thread**, into one octahedral image the sky pass
      samples by direction.
- [ ] The same image feeds the existing environment prefilter (ADR 0043's
      octahedral path), so reflections and diffuse ambient come from the skybox.
      The previous sky keeps drawing until the bake is done. Scale the result by
      `EnvironmentDiffuseScale` and `EnvironmentSpecularScale`.
- [ ] The celestial layer on top: `SunTexture`, `MoonTexture`, `SunAngularSize`,
      `MoonAngularSize`, `StarCount` and `CelestialBodiesShown`, with or without
      images. **The sun's direction stays a function of `ClockTime` and
      `GeographicLatitude` alone** (ADR 0096).
- [ ] The editor: image pickers for the faces, and a drop target for a folder of
      six images named by the common suffixes. A shipped sample skybox under
      `examples/` with a licence that R6 allows, recorded in
      `THIRD_PARTY_NOTICES.md`, or drawn procedurally by a script in
      `tools/repo`.
- [ ] Bake ≤ 50 ms, off the frame thread. A frame never waits for it.
      Captures for the owner.

## Stage 9 — Clouds

- [ ] `Sky.CloudCover`, `CloudDensity`, `CloudColor`: a procedural cloud layer
      in the sky pass, lit by the sun, drifting on the simulation clock, and
      included in the environment bake. Captures for the owner.

## Stage 10 — Wire, quality, showcase, documentation

- [ ] The wire schema carries the new classes under `Lighting`. Protocol bump.
      A two-world test: the authority enables a `BlurEffect` under `Lighting`
      and the replica draws it. A camera's effect does not cross.
- [ ] `luaug.toml`'s `depth_of_field` and `sun_rays`, in the quality presets
      (ADR 0044), and in the editor's graphics settings.
- [ ] `examples/22-atmosphere`: a small valley with a day that runs on
      `ClockTime`, a `Sky`, an `Atmosphere`, and every effect toggled from keys,
      with an on-screen list of what is on.
- [ ] Manual: `rendering/post.md` and `rendering/lighting.md` rewritten for the
      instances; a new `rendering/atmosphere-and-sky.md`; a divergence row for
      the face names; and the sentence "there is no skybox" removed.
- [ ] `CHANGELOG.md`, `PROGRESS.md`, this ledger ticked, and **Findings**
      appended.

## Not in this work

An HDR panorama sky, a user-written sky or post shader (ADR 0091's territory),
volumetric clouds and fog, screen-space reflections, motion blur, film grain,
lens flare, a vignette, and a per-world tonemapper -- ADR 0096, *Not decided
here*.

## Findings

1. **The class icons already exist** (Stage 1). The brief and the mission
   prompt both said the eight new classes had none; the owner drew them before
   this work began (`art/editor-icons/orbit/class/`, committed in `cb9beabf`),
   and they are in the atlas under `icons/default/class/` with their theme
   entries. The editor shows them. **Nothing is needed from the owner here.**
2. **The rules live in one function, and the editor asks it** (Stage 1).
   `render::resolveLook` (`engine/render/src/look.cpp`) is the only place the
   placement and combination rules are written; `extract` calls it, and the
   editor's inactive marker calls `render::lookStanding`, which replays the
   same walk. A marker that said one thing while the picture did another would
   be the ADR 0095 failure again.
3. **Several colour corrections cost one multiply** (Stage 1). Each effect's
   tint, saturation, contrast and brightness are affine maps of linear colour,
   and so is any composition of them, so `resolveLook` folds every enabled one
   into a single 3x4 matrix the tonemap applies. The contrast pivots about 0.45
   -- where exposure puts a frame's average -- and saturation mixes towards
   Rec. 709 luminance.
4. **The properties land `Inert` and come alive stage by stage** (Stage 1).
   The IDL, the storage and the rules are Stage 1's; what draws each class is
   its own stage's. Until then the Properties panel says "stored" rather than
   implying the number does something, which is the D030 lesson.
5. **`Lighting`'s children do not travel today** (Stage 1, for Stage 10). The
   wire carries `Lighting`'s own fields but not its contents -- it has no
   `Contents = true` (ADR 0080) -- so the effects are listed as excluded,
   "not yet", until Stage 10 carries them.
6. **A new effect never touches an old shader** (Stage 2). The command-stream
   goldens record every shader's size and every uniform block's size and
   digest, so a field added to the tonemap's block, or a branch added to its
   source, would have moved all of them for a world that uses none of this.
   So each effect is its OWN pipeline, made the first frame it is needed, as
   the decals' and the particles' are: the colour grade is `tonemap_graded`,
   which includes `tonemap.hlsl` whole, renames its entry point away and adds
   its block at the fragment stage's second slot. A world without these
   instances builds none of them, and the captures stayed byte-identical.
7. **A 2D game's bloom is subtle, and it is there** (Stage 2). The
   platformer's sprites never blow out; what bloom gives them is a faint halo
   under the brick platform and round the coins, which a disabled
   `BloomEffect` removes. M1's complaint is real at that size, and now
   answered.
8. **The captures and the costs come from one scene and one script**
   (Stage 2). `tests/look` holds the valley, and `tools/repo/look_captures.py`
   copies it once per variant with its one `Variant` line rewritten, so a
   before and an after differ in the effects and in nothing else.
9. **A blur of any size costs about the same** (Stage 3). The Gaussian runs at
   whichever level of the downsample chain puts it between two and four texels
   wide -- down to a thirty-second of the frame -- and is resampled back, so a
   blur of 80 pixels is the same dozen taps as one of 6, at far fewer pixels.
   The downsample is the bloom chain's own thirteen-tap box, reused.
10. **The blur works on light, before the tone curve** (Stage 3). A lamp or the
   sun blurred stays a bright disc, as it does through a real lens, rather
   than spreading into a grey smudge. It is the choice the stage-3 captures ask
   the owner to judge.
11. **Depth of field is three passes, and what is behind may not bleed forward**
   (Stage 4). Half resolution with each texel's circle of confusion from the
   NEAREST of the four depths it covers; a thirty-two-tap sunflower gather in
   which a farther neighbour contributes only as far as this texel's own
   circle reaches; and a full-resolution composite that recomputes the circle
   from depth, so the line between a sharp object and its blurred background
   is drawn at the frame's own resolution. A blurred foreground spreads over
   what is behind it, as through a lens. The machine switch landed here rather
   than in Stage 10, with the Low and Medium presets off.
12. **Sun rays need something to stream past, and half resolution to show it**
   (Stage 5). The mask is the open sky near the sun, from the depth buffer --
   anything nearer is a hole in it -- and a sixty-four-tap gather towards the
   sun turns the holes into shafts. At a quarter of the frame, which the brief's
   budget suggested, a post in front of the sun was a texel or two of the mask
   and its shaft drowned in the glow; at half it reads. The test scene gained
   a slatted fence across the sun for the same reason: a thin frame alone gave
   a glow and no shafts, which is the right answer for that scene and no way to
   judge the effect.
13. **The air is laid over the opaque world and the sky in one pass, not in
   every forward shader** (Stage 6). The ledger said "applied in the forward
   pass". Doing that literally means a second build of every surface shader --
   parts, instanced, skinned, terrain, blocks, particles -- because the old ones
   must stay byte-identical for a world without air. Instead the forward pass
   is closed after the opaque surfaces, as it already is for decals, and one
   pass integrates the air along each pixel's ray from the depth buffer and
   BLENDS it on (`dst = air * (1 - T) + dst * T`), so it never reads the image
   it changes. The sky and the far ground are the same integral of the same
   air, so they meet by construction. What the pass cannot see behind -- glass
   and particles -- gets a linear fog standing in for the same air at the
   camera's height.
14. **Over the open sky the air counts for a share** (Stage 6). The whole
   integral again, on top of a gradient that already is the air above, turned
   a clear afternoon's blue grey thirty degrees up; the sky counts 30 % of it,
   which still buries the horizon, where the integral is largest.
15. **With air, the horizon IS the air's colour** (Stage 6). `Atmosphere.Color`
   takes `FogColor`'s place in the sky's derivation, so it is tinted by the hour
   as the horizon always was -- warm at dusk, dark at night -- and the sky, the
   reflections and the air over the world agree on it.
