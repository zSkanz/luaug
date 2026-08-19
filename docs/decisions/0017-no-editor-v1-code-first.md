# 0017 — No visual editor in v1; code-first DX

- Status: accepted
- Date: 2026-08-19

## Context
A Studio-like editor multiplies scope and freezes engine design prematurely.
Unity/Godot grew editors on top of working engines. User decision #5.

## Decision
v1 is **code-first**: VS Code + the `luaug` CLI + sub-second hot reload, with
an in-game ImGui debug overlay (DebugShell: explorer, properties, stats, log)
standing in for inspection needs. The **visual editor is phase 2**, built on
the finished engine. Nothing in v1 may hard-code assumptions that block an
editor (the reflection/API-definition layer is editor-ready by construction).

## Consequences
Faster path to a real, running engine; the editor inherits a mature
Instance/reflection substrate instead of dictating it.
