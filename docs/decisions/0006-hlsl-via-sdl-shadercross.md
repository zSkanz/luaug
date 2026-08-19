# 0006 — HLSL authored shaders via SDL_shadercross

- Status: accepted
- Date: 2026-08-19

## Context
Each GPU backend consumes a different shader format (SPIR-V / DXIL / MSL).
Authoring per-backend shaders does not scale.

## Decision
Author all engine shaders in **HLSL**; compile with **SDL_shadercross** to
SPIR-V, DXIL, and MSL at build time (`cmake/luaug_shaders.cmake`), with an
on-disk cache and dev-mode recompilation for shader hot reload. Shared code in
`shaders/include/*.hlsli`.

## Consequences
One authoring language across all backends; the pipeline also serves the
future bgfx path (which can consume SPIR-V-derived output or grow its own
transform in the mobile phase — decided then, via ADR).
