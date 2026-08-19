# Coming from Roblox — The LuauG Migration Guide

> **Status: outline.** This is the keystone onboarding document, written for
> real during milestone M8 (roadmap). The structure below is final; each
> section fills in as the features it describes ship. The deliberate-
> divergences table it centers on is maintained in
> [`api-design.md` §2.5](api-design.md) and is embedded here verbatim at M8.

## Planned contents

1. **What transfers directly** — your mental model survives: `game` and
   `GetService`, the Instance tree (`Instance.new("Part")`, `Parent`,
   `FindFirstChild`, `WaitForChild`), Attributes and Tags, signals with
   `:Connect`/`:Once`/`:Wait`, `task.spawn/defer/delay/wait`, `TweenService`
   with the same easing names, `Vector3`/`CFrame` math, `--!strict` typing.

2. **Familiar but different — read this twice**
   - Signals are **deferred-only** (what that changes, with examples).
   - The frame loop: `PreRender` is render-rate; `PreAnimation`/
     `PreSimulation`/`PostSimulation`/`Heartbeat` are fixed-tick;
     `task.wait` resumes on the simulation clock.
   - **No ModuleScripts** — real files + `require("@shared/…")` (and why
     that's better: instant analyzer parity, no WaitForChild-require dance).
   - Streaming lives in `StreamingService` (not Workspace properties);
     streamed-out instances still reparent to nil.
   - Units are SI meters, not studs.
   - `Vector3` components are lowercase `x/y/z` (it IS the Luau `vector`
     primitive); `part.Position` is f32 — `part.CFrame` carries full-precision
     position for huge worlds.
   - Prefabs: `Instance:Clone()` works as you expect;
     `AssetService:LoadModelAsync` + `.prefab.luau` replace "model in
     ReplicatedStorage" (see api-design §2.6).

3. **Habit-by-habit cookbook** (each with before/after code)
   - `wait()` → `task.wait()`
   - UserInputService / ContextActionService → **Input Action System**
     (five copy-paste recipes: jump button, WASD movement, camera look,
     touch button, rebindable action with prompt glyphs)
   - BindableEvent → `Signal.new()`
   - ModuleScript + WaitForChild → `require("@shared/…")`
   - `rbxassetid://…` → `asset://…` project paths
   - Humanoid + HumanoidRootPart → `CharacterBody`
   - PlayerGui → `UIService` + `ScreenGui`
   - StreamingEnabled properties → `StreamingService`
   - RemoteEvent → `@std/net` (v1) / official replication (future)
   - DataStore → `save://` files or your own backend (Lute sibling project)
   - Roblox Studio → VS Code + `luaug dev` (hot reload, debug overlay)

4. **The deliberate divergences table** — embedded verbatim from
   api-design §2.5, with rationale per row.

5. **Honest "not yet in LuauG" list** — Terrain, particles, constraints,
   replication/multiplayer, navmesh pathfinding, the visual editor, 2D
   workflow, mobile — each with its post-v1 phase from the roadmap.

6. **Glossary** — LuauG term ↔ nearest Roblox concept.
