# 0004 — SDL3 as the platform layer

- Status: accepted
- Date: 2026-08-19

## Context
The engine needs windowing, input (keyboard/mouse/gamepad/touch), events, async
file IO, and a portability path spanning desktop, mobile, and (eventually)
consoles. GLFW covers desktop windowing only — no mobile, no console story.

## Decision
Use **SDL3 (3.4.x, zlib)** as the single platform layer. Only the `platform`
module (and the SDL GPU RHI backend) may touch SDL APIs directly. SDL provides
window + input + events + `SDL_AsyncIO` + the NDA console forks that make the
architecture console-ready.

## Consequences
One dependency covers the entire platform matrix, mobile first-class. The
`platform` module wraps it so nothing above depends on SDL types (layer rules,
`docs/architecture.md`).
