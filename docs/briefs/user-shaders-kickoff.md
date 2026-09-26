# User surface shaders: the kickoff and the ledger

The owner, on 2026-09-24: *"temos que deixar isso de maneira profissional como
outras game engine"*, and then, over the design the agent proposed, *"muito bem
concordo"*. The decision is
[ADR 0091](../decisions/0091-a-material-may-name-a-surface-shader-the-user-writes.md).
**Read it, and [ADR 0090](../decisions/0090-a-material-is-an-asset-a-part-wears-one-and-a-script-clones-one.md)
which it extends, before this file.** This file is the order of work and where
each piece stands.

In one paragraph: a material asset may name a surface shader, which is two HLSL
functions written against a versioned `luaug/surface.hlsli`. The engine builds
every pipeline the surface needs from them, so lights, shadows, IBL, fog and
post still apply. The shader's parameters are material fields. The editor
compiles shaders asynchronously, with a DXC this project builds from source and
ships, and a game only ever carries bytecode. The ocean is rewritten as user
code on top of all of that, which is what proves the contract is enough.

Legend: `[ ]` not started · `[~]` in progress · `[x]` done.

## Precondition

**The materials ledger ([`materials-kickoff.md`](materials-kickoff.md)) is
complete.** This work builds on the material asset, its library and its panel.
Starting it alongside that work, in the same working tree, means two sets of
changes to the same renderer and editor files.

Stage 0 is the one exception. It builds nothing in the repository, and it can
run at any time.

## What must hold at every stage

- `scripts/localgate.ps1` green (all six stages, Linux included), then a push,
  then CI read. Never write to a red `main`.
- **The existing goldens do not move.** Nothing here changes how a surface with
  no shader is drawn. New render-capture and screenshot scenarios for surface
  shaders get goldens of their own.
- **No determinism trace moves.** A shader is render-only, and ADR 0090 already
  keeps a material file's contents out of the hash. A trace that moves is a
  defect.
- R3 (every editor-facing message is an i18n key), R6 (every new notice
  generated from the manifest), R7, R13 (DXC is built from a pinned, hashed
  source archive and never edited in place; any change goes through
  `third_party/patches/`), R14, R17 (no backend type in `surface.hlsli` or in
  the Luau surface).
- **Stage only what you wrote.** Other work may be in this tree. Never
  `git add -A`.

## Stage 0 — Verify `U-64` before anything depends on it

- [x] Build DXC from source at the manifest's pinned version (v1.9.2602),
      outside the repository tree, on Windows x64.
- [x] Compile a trivial shader to DXIL with it, and put **no** `dxil.dll`
      anywhere the process can find one.
- [x] Create a graphics pipeline from that DXIL through SDL3 GPU's D3D12
      backend on a retail Windows machine with **Developer Mode off**, and draw
      one frame. *Through D3D12 directly, with a hash-zeroed control that is
      refused -- see Findings.*
- [x] Record the measured source-build time and output size for each desktop
      platform you can build here (Windows, and Linux in the Tier-2 container).
- [x] Update `U-64` in `docs/research/UNCONFIRMED.md` to `confirmed` or
      `refuted`, with how it was verified.
- **If `U-64` is refuted, stop and report to the owner.** ADR 0091 lists the two
  fallbacks: fetch Microsoft's `dxil.dll` onto the user's machine as ADR 0032
  fetches DXC, or run user shaders on Vulkan only on Windows. Choosing between
  them is the owner's decision.

## Stage 1 — The toolchain that ships

- [x] A pinned, hashed **source** row for DXC in `third_party/manifest.json`,
      beside the existing prebuilt row. Each row says which is built by the
      engine's developers and which is redistributed. `THIRD_PARTY_NOTICES.md`
      is regenerated, and carries DXC's NCSA notice and the notices its licence
      lists for its third-party parts.
- [x] A CI job that builds DXC from source **once per pinned version and per
      desktop platform** (Windows x64, Linux x64, macOS arm64 and x64) and caches
      the result. No ordinary build ever compiles LLVM, and the cache key is the
      source hash.
- [x] `scripts/package.ps1` and `tools/repo/package.luau` put `dxcompiler` and
      SDL_shadercross beside the editor binary. A package without them fails the
      package step rather than shipping an editor that cannot compile.
      *The editor RUNS `shadercross` rather than linking it (see Findings), so
      both are files beside the editor, and the player gets neither.*
- [x] The engine's own shaders keep ADR 0032's fetched prebuilt compiler. Only
      the source-built one is ever redistributed.

## Stage 2 — The contract

- [x] `shaders/include/luaug/surface.hlsli`: `SurfaceVertex`, `SurfaceInputs`,
      `SurfaceOutput`, `LUAUG_PARAM`, `LUAUG_TEXTURE`, and a contract version
      constant. Everything ADR 0091 lists as an input is present:
      - the simulation time interpolated to the frame;
      - the camera and the object's transform;
      - world position, normal and tangent;
      - UV0, UV1 and vertex colour;
      - screen position;
      - scene depth, for blended surfaces;
      - scene colour, when the material asks for it.
- [x] The wrapper that turns the two user functions into every pass variant:
      forward, instanced forward, shadow, depth and blended. A displaced vertex
      casts a displaced shadow.
- [x] **The proof that the contract is complete enough**: the built-in PBR
      surface, written as a surface shader and forced onto the screenshot
      scenes, matches the existing goldens. This is a test. The built-in path
      stays as it is.

## Stage 3 — Parameters

- [x] Reflection of `LUAUG_PARAM` and `LUAUG_TEXTURE`: type, name, default and
      annotations (`range(a, b)`, `colour`, `toggle`). The uniform block layout
      and the texture slots are the engine's and never the user's.
- [x] The material asset's `"shader"` field and `"readsSceneColor"`. Shader
      parameters are written in `properties` like the built-in fields.
- [x] Shader parameters are eligible for `instanceParameters` (ADR 0090), and a
      part's `SetMaterialParameter` reaches them. By name: a non-field entry of
      `instanceParameters` is a shader parameter (`instanceShaderParameters`,
      compiled material v3), and a part keeps its values in a side table of the
      world (`PartShaderParameters`) -- snapshotted, cloned, hashed where
      present, saved in the scene as `shaderParameters`, and edited in the
      Properties panel. Not textures, and not replicated.
- [x] Luau: a shader parameter reads off any handle and writes on a clone,
      through `Material:GetShaderParameter` and `SetShaderParameter` -- methods
      rather than members, since a shader's parameters are whatever its file
      declares and a datatype's members are the IDL's closed set. The conformance specs cover
      reading, writing on a clone, the raise on a loaded asset, and a per-part
      override.

## Stage 4 — Compile, cache, report

- [x] `assetc` compiles a surface shader to SPIR-V, DXIL and MSL for each pass
      variant, through SDL_shadercross. The output is cached by content hash
      (the shader, its includes and the contract version) and stored in the pack
      under a new asset kind. `AssetKind::Surface`: the source and all three
      targets, about 400 KB a surface; a player reads it through
      `render::PackSurfaceSource`. The compile routine is one function,
      `asset::buildSurface`, shared with the editor's `SurfaceCompiler`.
- [x] `#include` of `.hlsli` files in the project's `content/` works, and the
      cache key covers them.
- [x] **Asynchronous compilation in the editor.** A surface draws with the
      default material while its shader compiles, and no frame waits. Measure
      and record a cold compile of the ocean shader and a warm cache hit.
- [x] **A shader that fails draws with the error material**, and the editor
      lists each error with its file and line.
- [x] Saving a shader or an include recompiles it and reloads every surface
      using it (ADR 0062).
- [x] The editor survives a GPU device loss caused by a user shader: it reports
      the loss and recovers, and does not crash. **Recovers by restarting**: the
      RHI marks the device lost (`IDevice::lost`) from the driver's code in
      SDL's error and drops every call after it; the editor offers to save and
      restart itself, and the surfaces that were on screen are held back until
      they change. `device_loss_survived` proves it on a simulated loss, and a
      real TDR was reproduced by hand before and after.
- [x] A game packaged by `luaug build` contains no compiler and never compiles.
      The player profile has no `SurfaceCompiler` at all (it is behind
      `LUAUG_DEBUG_UI`), and `tests/packaging` asserts the built folder carries
      neither shadercross nor DXC.

## Stage 5 — The renderer

- [x] A pipeline cache keyed by (shader, pass variant). Record pipeline creation
      time and pack size per shader in `docs/perf-baselines.md`: 3 to 5.5 ms
      once per surface per run, about 400 KB a surface in a pack.
- [x] Scene depth bound for blended surfaces, which is what intersection foam
      needs.
- [x] Scene colour: a copy of the HDR target after the opaque pass, made by a
      full-screen draw (the ADR 0072 pattern, so the RHI does not change), made
      only in a frame where a visible material asks for it, and once per frame.
- [x] The simulation time handed to `SurfaceInputs` is the same interpolated
      clock the transforms are drawn at (`transform_history.h`), so a GPU wave
      and a Luau wave agree on screen.

## Stage 6 — The ocean, as user code

- [x] A built-in subdivided grid mesh at a few fixed resolutions.
- [x] `examples/11-ocean` rewritten on **public pieces only**:
      - a material asset naming an ocean surface shader (Gerstner waves, depth
        colour, foam where the water meets geometry, refraction);
      - a handful of `MeshPart`s that follow the camera;
      - the same wave in Luau for the boat's and the crates' buoyancy.
      **If it needs anything private, fix the contract, not the example.**
- [x] Its README's budget section re-measured: draws, frame time, and the
      property writes per tick against the 676 it has today.
- [x] Three more examples, each small, in one folder or three
      (`examples/23-surfaces`):
      - a flag in the wind (vertex displacement on a non-water mesh);
      - a dissolve (a `Mask` surface driven by a per-part parameter);
      - glass (refraction through scene colour).

## Stage 7 — The editor

- [x] *New Surface Shader* in the content browser, written from a commented
      template that compiles as it stands. `Editor::createSurfaceShader`; the
      template passes reflection in `editor_tests.cpp`, and a `.surface.hlsl`
      is a content kind of its own (`ContentKind::Shader`).
- [x] A `.hlsl` file opens in the built-in text editor, with HLSL highlighting.
      A tab of `ScriptOrigin::File` -- no instance, saved to its path under the
      content root -- over a document whose `ScriptLanguage` is HLSL: a
      hand-written line lexer, no Luau parser, no completion, no breakpoints,
      and the compiler's errors on their lines.
- [x] The material panel: a shader picker, the reflected parameters as typed
      fields, the `readsSceneColor` toggle, and the compile status and errors of
      the named shader. The parameters are reflected from the file by the panel
      itself, so they show in a build with no compiler too.

## Stage 8 — Documentation and close

- [x] A manual section on surface shaders (`docs/manual/rendering/surface-shaders.md`).
      It covers:
      - the contract reference -- summarised in tables and pointing at
        `surface.hlsli`, which documents every member; generating it would
        have meant a parser for the header's comments, for three tables;
      - how to write the CPU copy of a wave, and why it cannot be read back from
        the GPU (R10);
      - the GPU-hang warning;
      - the ocean as a walkthrough.
- [x] `docs/architecture.md` §8 (the shader toolchain) updated for a second
      caller and a redistributed compiler.
- [x] `CHANGELOG.md` under Unreleased.
- [x] `PROGRESS.md` updated, this ledger ticked, and a **Findings** section
      appended: what ADR 0091 assumed that reality corrected.

## Not in this work

The node graph (the second tier, with its own record), compute shaders, user
keywords and variants, tessellation, geometry shaders, user post-process
shaders, and mobile -- ADR 0091, *Not decided here*.

## Findings

- **Stage 0, 2026-09-25 -- U-64 holds, and the proof is a refusal as well as
  an acceptance.** DXC built from the `v1.9.2602` source (commit `21d28f72`)
  signs its own DXIL: a probe created a pipeline from it on retail D3D12 with
  Developer Mode off and no `dxil.dll` anywhere, drew a frame and read back the
  colour the shader writes; the same bytecode with its container digest zeroed
  was refused (`E_INVALIDARG`). The probe called D3D12 directly rather than
  through SDL3 GPU -- SDL hands the same bytes to the same
  `CreateGraphicsPipelineState`, and the direct call is what makes the control
  possible. The source-built output is **not** byte-identical to the prebuilt
  compiler's (the container differs from byte 5, and a vertex shader was four
  bytes longer), so "compare the bytes" is not a substitute for the device.
- **The source build is minutes, not hours.** Configure and build: 5 min 23 s
  on Windows x64 at `-j14`, 295 s on Linux x64 in the Tier-2 image; the whole
  build tree is 4 GB, what ships is `dxcompiler` (21 MB on Windows, 37.5 MB on
  Linux) and a 1 MB `dxc`. That is why CI caches only what ships.
- **Stage 2 and 5, 2026-09-25 -- the proof found a hole, and then drew to the
  bit.** The built-in surface written as a surface shader, forced onto every
  static part (`--force-surface=pbr`), first differed on 234 pixels, all on
  shadow edges: a surface could not tell a normal map that was not set from a
  flat one, and a 1x1 flat texel is 128/255, which tilts the normal the shadow
  bias reads. The contract gained `LUAUG_TEXTURE_SET`; after it, `contact`,
  `specular`, `localshadow`, `meshes` and `daystrip` draw identically, maximum
  channel delta 0, and two of them are now ctest gates.
- **SDL_GPU allows sixteen samplers a stage, not "enough".** The lighting takes
  nine and the built-in maps four; a surface's textures reuse the built-in
  maps' four slots, which a surface never reads, and then the last three. So a
  surface has seven textures, not the eight the plan said.
- **The render snapshot is relative to the camera** (the floating origin), so
  `WorldPosition` is the draw's position plus the camera's, carried in the
  block's header; without it a wave laid out in world space would slide with
  the camera.
- **Stage 4 -- the editor runs the compiler; it does not link it.** Linking
  SDL_shadercross would make `dxcompiler` a load-time import of the editor --
  an editor that does not start where the library is missing -- and a DXC
  crash on somebody's shader would take the editor down with it. Running the
  `shadercross` program the engine's own build uses, on a worker, costs a
  process per variant and stage and buys both back; SDL3's process API was
  already vendored. Errors come back on its output as `file:line: error:` and
  reach the console with their file and line.
- **An identical picture proves nothing on its own.** The first end-to-end run
  of a user surface drew identically to the built-in one because the scene had
  no camera and neither ran the real renderer. The gate now also renders a
  copy whose surface paints everything green and fails unless that differs.
- **Stage 5 and 6 -- D3D12 links a pixel shader to its vertex shader by
  position.** The ocean's fragment never read its uv or tangent, DXC stripped
  them from its input signature, and every forward pipeline of it was refused
  (`E_INVALIDARG`); the built-in surface's copy read everything and never
  showed it. The wrapper now reads every interpolant under a test no value can
  pass.
- **A cache keyed on the user's files alone serves stale bytecode.** The key
  now covers the generated wrapper and the engine's headers too.
- **Sixteen samplers again**: a blended surface reads the scene's depth and
  colour at t14 and t15, so a surface has five textures, not seven.
- **The grids are built when named, not at boot**: three extra meshes with the
  primitives moved every capture golden of every scene without water.
- **Gerstner waves were the wrong brief.** A Gerstner crest moves sideways, so
  the height at a given (x, z) has no closed form and the boat would float on
  an approximation of the water under it; the ocean uses the directional sine
  waves both sides can evaluate exactly. Cold compile 882 ms, warm cache 1 ms;
  6.7 ms a frame before, 0.67 ms after.
- **SDL_shadercross links `dxcompiler` at load time** (`DxcCreateInstance` is
  an import, not a `LoadLibrary`), so whatever links it needs the library
  beside it. The player never links it; the editor and `assetc` will.
- **Stage 3 -- a shader's parameters are methods, not members.** The brief
  asked for them "through the same members as a built-in field", and a
  datatype's members are the IDL's closed set: `Material.Clarity` could not be
  typed, documented or checked. `GetShaderParameter` and `SetShaderParameter`
  are what the type checker can see.
- **Stage 4 -- the build and the editor compile with one function.** Moving
  `buildSurface` into `asset` was what made the pack's bytecode and the
  editor's the same bytes: a second copy of "wrap, run shadercross, read back"
  would have been a second answer. Built cold and built from the cache, the
  pack of `examples/23-surfaces` is byte-identical.
- **Stage 4 -- a build with no compiler must still build.** Refusing would stop
  every project on a machine without DXC (every macOS host); dropping the file
  would hide it. The surface goes in as source alone and the player says, once,
  which surface and which backend it cannot draw.
- **Stage 7 -- every file tab was one window.** The script editor keyed a tab's
  window on its instance, and a file has none; the first two shaders opened
  shared a window. `scriptWindowId` keys a file on its path.
- **A lost device crashed inside SDL, not in the engine.** Reproduced with a
  shader that loops two billion times a pixel: after the reset, SDL's D3D12
  backend dereferenced the NULL uniform buffer its pool returned
  (`D3D12_INTERNAL_PushUniformData`), so the process died before the engine
  could learn anything. `third_party/patches/sdl3/0002` drops the push instead.
- **SDL_GPU has no device-lost query or event.** The only signal is the
  driver's code inside the message it sets on a failed call --
  `0x887A0006` in a message otherwise in the system's language -- so the RHI
  matches on the codes, never on the words.
- **Recovery is a restart, not a rebuilt device.** Every GPU object the engine
  owns -- meshes, textures, pipelines, the editor's own interface -- would have
  to be recreated in place; a new process does that already, and the one thing
  it must not do is draw the same shader again, which is what the held-back
  list is for.
- **A part's shader parameters are not a component field.** `PartComponent` is
  copied as bytes, the wire's fields are fixed cells, and a name is neither;
  the values live in a table beside the pools, which is what the material
  clones already did. It is hashed only where present, so every recorded world
  hash and replay is unchanged.
- **"Sparkle" stopped being unknown.** A name no built-in field has may now be
  a shader parameter, so on a part whose material does not declare it the
  error is *not declared*, not *not a parameter*; *not a parameter* is left for
  a name no parameter can have.
