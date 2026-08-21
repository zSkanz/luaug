# 0011 — ImGui for debug UI; Clay behind Roblox-style in-game UI Instances

- Status: accepted, Clay clause amended by ADR 0040
- Date: 2026-08-19

## Context
Two distinct UI problems: (a) engine/editor/debug tooling UI; (b) the in-game
UI that must feel like Roblox's retained Instance tree (ScreenGui/Frame/
TextLabel, UDim2 scale+offset, UIListLayout). Writing a layout solver from
scratch is expensive; adopting an HTML/CSS engine (RmlUi) would impose a
foreign mental model.

## Decision
**Dear ImGui (1.92.x docking tag)** for the debug overlay/DebugShell and the
future editor — dev-facing only, compiled out of shipping builds. In-game UI is
a **retained tree of Roblox-style UI Instances** whose layout compiles to
**Clay** (zlib, single-header, flexbox-like) internally; Clay is never exposed.
Text via stb_truetype (complex-script shaping is a documented i18n gap with a
HarfBuzz seam post-v1). Reactive UI frameworks (Fusion/React-Lua style) stay
userland — the engine ships the Instance tree, not a framework.

## Amendment (2026-08-20, ADR 0040)
The Clay clause did not survive contact with the model it was chosen for. A
`UDim2` placement is two multiplies and an add rather than a constraint, and
Clay cannot express an unclamped scale or a fractional anchor point -- so `ui`
computes the layout directly and does not call Clay in v1. **The retained
Instance tree, the UDim2 coordinates and the layout API are exactly as decided
below**; what changed is what sits underneath them. The ImGui clause and the
stb_truetype clause are untouched. ADR 0040 has the reasoning.

## Consequences
Roblox-familiar UI API without writing a constraint solver; ImGui's docking
branch is maintained-in-practice (parallel tags) which we accept for dev-only
usage.
