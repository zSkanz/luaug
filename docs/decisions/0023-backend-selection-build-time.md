# 0023 — Backend selection at build time; explicit factory, no plugin ABI

- Status: accepted
- Date: 2026-08-19

## Context
RHI, renderer, physics, audio, and net transports are swappable seams. Consoles
and iOS want fully static, LTO-dead-strippable binaries; dynamic loading is
prohibited or painful there; static-initializer self-registration breaks under
aggressive dead-stripping and makes binary-size audits opaque.

## Decision
Backends are selected **primarily at build time** via CMake options
(`LUAUG_RHI_SDLGPU`, `LUAUG_RHI_BGFX`, `LUAUG_PHYSICS_JOLT`,
`LUAUG_AUDIO_MINIAUDIO`, `LUAUG_NET_GNS`, …). The entire wiring lives in one
explicit, hand-written factory in `app` (a plain `switch` over compiled-in
backends); runtime selection exists only among compiled-in options
(`--rhi=capture` for tests). **No universal runtime C++ plugin ABI in v1.** A
future backend SDK, if ever, uses a carefully versioned C ABI decided by its
own ADR.

## Consequences
Minimal shipping binaries (one backend per seam), console-safe linking, and a
~20-line cost. Test injection stays easy via the capture/null backends.
