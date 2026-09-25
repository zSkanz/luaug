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

- [ ] Build DXC from source at the manifest's pinned version (v1.9.2602),
      outside the repository tree, on Windows x64.
- [ ] Compile a trivial shader to DXIL with it, and put **no** `dxil.dll`
      anywhere the process can find one.
- [ ] Create a graphics pipeline from that DXIL through SDL3 GPU's D3D12
      backend on a retail Windows machine with **Developer Mode off**, and draw
      one frame.
- [ ] Record the measured source-build time and output size for each desktop
      platform you can build here (Windows, and Linux in the Tier-2 container).
- [ ] Update `U-64` in `docs/research/UNCONFIRMED.md` to `confirmed` or
      `refuted`, with how it was verified.
- **If `U-64` is refuted, stop and report to the owner.** ADR 0091 lists the two
  fallbacks: fetch Microsoft's `dxil.dll` onto the user's machine as ADR 0032
  fetches DXC, or run user shaders on Vulkan only on Windows. Choosing between
  them is the owner's decision.

## Stage 1 — The toolchain that ships

- [ ] A pinned, hashed **source** row for DXC in `third_party/manifest.json`,
      beside the existing prebuilt row. Each row says which is built by the
      engine's developers and which is redistributed. `THIRD_PARTY_NOTICES.md`
      is regenerated, and carries DXC's NCSA notice and the notices its licence
      lists for its third-party parts.
- [ ] A CI job that builds DXC from source **once per pinned version and per
      desktop platform** (Windows x64, Linux x64, macOS arm64 and x64) and caches
      the result. No ordinary build ever compiles LLVM, and the cache key is the
      source hash.
- [ ] `scripts/package.ps1` and `tools/repo/package.luau` put `dxcompiler` and
      SDL_shadercross beside the editor binary. A package without them fails the
      package step rather than shipping an editor that cannot compile.
- [ ] The engine's own shaders keep ADR 0032's fetched prebuilt compiler. Only
      the source-built one is ever redistributed.

## Stage 2 — The contract

- [ ] `shaders/include/luaug/surface.hlsli`: `SurfaceVertex`, `SurfaceInputs`,
      `SurfaceOutput`, `LUAUG_PARAM`, `LUAUG_TEXTURE`, and a contract version
      constant. Everything ADR 0091 lists as an input is present:
      - the simulation time interpolated to the frame;
      - the camera and the object's transform;
      - world position, normal and tangent;
      - UV0, UV1 and vertex colour;
      - screen position;
      - scene depth, for blended surfaces;
      - scene colour, when the material asks for it.
- [ ] The wrapper that turns the two user functions into every pass variant:
      forward, instanced forward, shadow, depth and blended. A displaced vertex
      casts a displaced shadow.
- [ ] **The proof that the contract is complete enough**: the built-in PBR
      surface, written as a surface shader and forced onto the screenshot
      scenes, matches the existing goldens. This is a test. The built-in path
      stays as it is.

## Stage 3 — Parameters

- [ ] Reflection of `LUAUG_PARAM` and `LUAUG_TEXTURE`: type, name, default and
      annotations (`range(a, b)`, `colour`, `toggle`). The uniform block layout
      and the texture slots are the engine's and never the user's.
- [ ] The material asset's `"shader"` field and `"readsSceneColor"`. Shader
      parameters are written in `properties` like the built-in fields.
- [ ] Shader parameters are eligible for `instanceParameters` (ADR 0090), and a
      part's `SetMaterialParameter` reaches them.
- [ ] Luau: a shader parameter reads off any handle and writes on a clone,
      through the same members as a built-in field. The conformance specs cover
      reading, writing on a clone, the raise on a loaded asset, and a per-part
      override.

## Stage 4 — Compile, cache, report

- [ ] `assetc` compiles a surface shader to SPIR-V, DXIL and MSL for each pass
      variant, through SDL_shadercross. The output is cached by content hash
      (the shader, its includes and the contract version) and stored in the pack
      under a new asset kind.
- [ ] `#include` of `.hlsli` files in the project's `content/` works, and the
      cache key covers them.
- [ ] **Asynchronous compilation in the editor.** A surface draws with the
      default material while its shader compiles, and no frame waits. Measure
      and record a cold compile of the ocean shader and a warm cache hit.
- [ ] **A shader that fails draws with the error material**, and the editor
      lists each error with its file and line.
- [ ] Saving a shader or an include recompiles it and reloads every surface
      using it (ADR 0062).
- [ ] The editor survives a GPU device loss caused by a user shader: it reports
      the loss and recovers, and does not crash.
- [ ] A game packaged by `luaug build` contains no compiler and never compiles.
      The shipping gate checks this.

## Stage 5 — The renderer

- [ ] A pipeline cache keyed by (shader, pass variant). Record pipeline creation
      time and pack size per shader in `docs/perf-baselines.md`.
- [ ] Scene depth bound for blended surfaces, which is what intersection foam
      needs.
- [ ] Scene colour: a copy of the HDR target after the opaque pass, made by a
      full-screen draw (the ADR 0072 pattern, so the RHI does not change), made
      only in a frame where a visible material asks for it, and once per frame.
- [ ] The simulation time handed to `SurfaceInputs` is the same interpolated
      clock the transforms are drawn at (`transform_history.h`), so a GPU wave
      and a Luau wave agree on screen.

## Stage 6 — The ocean, as user code

- [ ] A built-in subdivided grid mesh at a few fixed resolutions.
- [ ] `examples/11-ocean` rewritten on **public pieces only**:
      - a material asset naming an ocean surface shader (Gerstner waves, depth
        colour, foam where the water meets geometry, refraction);
      - a handful of `MeshPart`s that follow the camera;
      - the same wave in Luau for the boat's and the crates' buoyancy.
      **If it needs anything private, fix the contract, not the example.**
- [ ] Its README's budget section re-measured: draws, frame time, and the
      property writes per tick against the 676 it has today.
- [ ] Three more examples, each small, in one folder or three:
      - a flag in the wind (vertex displacement on a non-water mesh);
      - a dissolve (a `Mask` surface driven by a per-part parameter);
      - glass (refraction through scene colour).

## Stage 7 — The editor

- [ ] *New Surface Shader* in the content browser, written from a commented
      template that compiles as it stands.
- [ ] A `.hlsl` file opens in the built-in text editor, with HLSL highlighting.
- [ ] The material panel: a shader picker, the reflected parameters as typed
      fields, the `readsSceneColor` toggle, and the compile status and errors of
      the named shader.

## Stage 8 — Documentation and close

- [ ] A manual section on surface shaders. It covers:
      - the contract reference, generated from `surface.hlsli` if that can be
        done cleanly;
      - how to write the CPU copy of a wave, and why it cannot be read back from
        the GPU (R10);
      - the GPU-hang warning;
      - the ocean as a walkthrough.
- [ ] `docs/architecture.md` §8 (the shader toolchain) updated for a second
      caller and a redistributed compiler.
- [ ] `CHANGELOG.md` under Unreleased.
- [ ] `PROGRESS.md` updated, this ledger ticked, and a **Findings** section
      appended: what ADR 0091 assumed that reality corrected.

## Not in this work

The node graph (the second tier, with its own record), compute shaders, user
keywords and variants, tessellation, geometry shaders, user post-process
shaders, and mobile -- ADR 0091, *Not decided here*.

## Findings

*(Appended as the stages land.)*
