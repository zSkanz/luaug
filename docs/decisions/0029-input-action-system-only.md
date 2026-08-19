# 0029 — Input Action System is the only input model

- Status: accepted
- Date: 2026-08-19

## Context
Roblox's Input Action System (IAS) reached full release in 2026 and supersedes
ContextActionService/UserInputService. It is a data-driven, device-neutral
model — exactly right for an engine spanning desktop, mobile, and console.
Cloning the legacy services would ship 2020's design debt.

## Decision
LuauG's only input model is an **IAS clone**: `InputContext` (Enabled,
Priority, Sink) → `InputAction` (Bool / Direction1D / Direction2D /
Direction3D / ViewportPosition; `Pressed`/`Released`/`StateChanged`) →
`InputBinding` (KeyCode, directional composites, UIButton, Scale, DisplayName
[localization key allowed], Image, per-device bindings), with
`GetPreferredBinding(deviceType?)` for prompt glyphs and runtime rebinding
with persistence. There is no UserInputService/ContextActionService/Mouse
API. Input dispatch is split: render-rate (UI/camera contexts) and
deterministic sim-tick (gameplay contexts).

## Consequences
Rebindable, promptable, device-neutral input by default. IAS is new even to
Roblox veterans — the migration guide ships copy-paste recipes (launch
blocker, see api-design risks).
