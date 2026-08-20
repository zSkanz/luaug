<img src="branding/luaug-logo-512.png" alt="LuauG" width="360">

**A standalone, open-source game engine scripted in Luau — built for developers coming from Roblox.**

LuauG gives you the developer experience you already know — `Instance` trees, `game:GetService`, `task.spawn`, signals with `:Connect` — in an independent, professional engine: a modern C++ core embedding the Luau VM directly, a data-oriented ECS behind a familiar Instance facade, a swappable renderer and physics stack, deterministic fixed-tick simulation, and a code-first workflow (VS Code + CLI + sub-second hot reload). It targets complete 2D and 3D games, from small scenes to huge streamed open worlds, on desktop first, then mobile, with a console-ready architecture.

> **STATUS: pre-alpha, drawing, and simulating.** Seven of eleven milestones are
> complete and human-signed-off. The engine boots a sandboxed Luau VM, opens a
> window, runs a deterministic fixed-tick simulation over an Instance tree on an
> ECS, hot-reloads a saved script into a new world in under two milliseconds,
> renders glTF meshes with forward PBR, a shadow-casting sun, point and spot
> lights, fog, and transparency, and simulates a thousand active rigid bodies in
> two milliseconds a tick — on Windows, Linux and macOS, with an Android
> triangle proving the graphics seam on a real device.
>
> **M5 is signed off**, and it is the milestone that gave the world mass: an
> unanchored part is a Jolt rigid body, a `CharacterBody` walks and climbs and is
> blocked by another one, and the determinism gate replays a recorded keyboard
> stream rather than a bot.
>
> The review gate is not ceremony. M4 was written up complete and tagged on its
> own green gate, and a human using the engine then noticed the shadow never
> moved: the renderer had never once read the `Lighting` service. M5 was signed
> only after a reported defect turned out not to reproduce and the investigation
> found a different, real one underneath it.
>
> It is written autonomously, milestone by milestone, by an AI agent (Claude
> Opus, multi-agent orchestration) following [`MASTER_PROMPT.md`](MASTER_PROMPT.md),
> with a human review gate at the end of every milestone.

## Where it is

| | Milestone | Gate |
|---|---|---|
| ✅ | **M0** — bootstrap: sandboxed VM, i18n'd log, pinned toolchain | signed off, `milestone/m0` |
| ✅ | **M1** — window, RHI (SDL3 GPU / capture / null), fixed-tick loop, screenshot + capture harness | signed off, `milestone/m1` |
| ✅ | **M2** — the kernel: ECS, Instance facade, deferred signals, `task`, services, world hash | signed off, `milestone/m2` |
| ✅ | **M3** — `luaug` CLI, hot reload, generated type definitions, conformance runner | signed off, `milestone/m3` |
| ✅ | **M4** — meshes, materials, camera, lighting; RHI interface freeze (ADR 0037) | signed off, `milestone/m4` |
| ✅ | **M4.5** — correcting the environment the renderer never read, and what was found beside it | signed off, `milestone/m4.5` |
| ✅ | **M5** — Jolt physics, queries, welds, and a character you can steer | signed off, `milestone/m5` |
| 🔨 | **M6** — input actions, UI, tweens, audio, minimal animation | next |
| ⬜ | **M7** — asset pipeline, async IO, streaming, floating origin | |
| ⬜ | **M7.5** — cascaded shadows, clustered lights, image-based lighting, post | |
| ⬜ | **M8** — the flagship open-world demo, hardening, docs, v1.0 | |

**What runs today:** `luaug dev` on a project, edit a `.luau` file, watch the
world rebuild without the window closing. A glTF scene lit by forward PBR with a
single-cascade shadow map and a day/night cycle driven from the simulation clock.
A physical world: gravity, contacts and `Touched` as deferred signals, impulses,
friction, collision groups, raycasts and shape queries, transform welds, and a
capsule character that climbs a kerb, is stopped by a wall, and is stopped by
another character. An in-game explorer and property inspector that writes through
the same setters a script goes through. 969 conformance specs written against
[`docs/api-design.md`](docs/api-design.md) — not against the implementation —
pass on Windows and Linux, alongside a determinism harness that replays a
recorded **input** stream and compares world hashes, and a capture-stream gate
that compares draw commands rather than pixels.

**What does not, yet:** UI, audio, animation, streaming, image-based lighting,
cascaded shadows, and instanced draws — the renderer still submits one draw call
per visible object, which is the measured ceiling for a crowd
([`docs/perf-baselines.md`](docs/perf-baselines.md)). Each arrives with the
milestone that owns it, and [`docs/roadmap.md`](docs/roadmap.md) says which.

## Not affiliated with Roblox

LuauG is an independent project. It is **not** affiliated with, endorsed by, or sponsored by Roblox Corporation. LuauG replicates **public API concepts and idioms only** — it contains **no Roblox source code, no Roblox assets, no Roblox branding**, and it is not derived from any Roblox software. Luau is an open-source language (MIT) created by Roblox and used here under its license. "Roblox" is a trademark of Roblox Corporation, referenced only nominatively.

## Why LuauG

- **Familiar, not identical.** Same concepts and idioms as the platform you came from (`Instance.new("Part")`, `Workspace`, `Touched:Connect(...)`, `TweenService`), but a clean, fully typed, deliberately improved API — deferred-only signals, real file modules instead of ModuleScripts, the modern Input Action System, SI units, `--!strict` everywhere.
- **Optimized by architecture.** Instance tree on the outside, data-oriented ECS on the inside. Native Luau `vector` as `Vector3` (zero-allocation script math). Atom-based property dispatch. Buffer-based bulk APIs. Fixed-tick deterministic simulation with interpolated rendering. Chunked streaming with 64-bit world coordinates and floating origin.
- **Swappable subsystems, sane defaults.** Renderer, RHI backend, physics, audio, and network transport all sit behind explicit seams selected at build time.
- **Open toolchain.** File-based projects, `.luaurc` + require-by-string, full `luau-lsp` support with generated type definitions, `rokit`-pinned tools, `pesde` packages, a CLI (`luaug`) running on the official Lute runtime.

## Default stack (pinned)

| Layer | Choice | License |
|---|---|---|
| Scripting VM | Luau **0.734** (embedded directly, `LUA_VECTOR_SIZE=3`) | MIT |
| Tooling runtime | Lute **1.0.0** (unmodified, CLI/tooling only) | MIT |
| Platform / window / input | SDL **3.4.x** | zlib |
| GPU | Custom RHI → **SDL3 GPU** (default backend); **bgfx** (mobile-phase backend) | zlib / BSD-2 |
| Shaders | HLSL via **SDL_shadercross** → SPIR-V / DXIL / MSL | zlib |
| Physics 3D | **Jolt 5.6** | MIT |
| Physics 2D (post-v1) | **Box2D 3.1** | MIT |
| Audio | **miniaudio 0.11.25** | MIT-0 / Unlicense |
| Assets | fastgltf (+ **simdjson 3.12.3**, its required parser) · meshoptimizer · basis_universal/KTX2 · stb (assimp: offline importer only) | MIT / Apache-2.0 / BSD / PD |
| Debug UI | Dear ImGui (docking) | MIT |
| In-game UI layout | Clay (behind Roblox-style UI Instances) | zlib |
| Navigation (post-v1) | Recast & Detour | zlib |
| Net transport primitives | GameNetworkingSockets · ENet | BSD-3 / MIT |

Everything in the default path is permissively licensed. Exact pinned commits live in [`third_party/manifest.json`](third_party/manifest.json).

## Repository map

- [`MASTER_PROMPT.md`](MASTER_PROMPT.md) — mission constitution for the autonomous builder agent
- [`PROGRESS.md`](PROGRESS.md) — the build ledger (current state, session log)
- [`docs/architecture.md`](docs/architecture.md) — native core architecture (modules, layering, scheduler, ECS bridge, streaming)
- [`docs/api-design.md`](docs/api-design.md) — the Luau-facing API and developer experience
- [`docs/roadmap.md`](docs/roadmap.md) — milestones M0 through M8 with verification gates
- [`docs/decisions/`](docs/decisions/) — architecture decision records (ADRs)
- [`docs/research/`](docs/research/) — frozen research reports (Luau, Lute, ecosystem — August 2026)
- [`docs/coming-from-roblox.md`](docs/coming-from-roblox.md) — the migration guide (habit-by-habit mapping)
- [`CONTRIBUTING.md`](CONTRIBUTING.md) — how contributions work

## v1 definition of done

A third-person character exploring a large open world with chunk streaming, Jolt physics, a simple day/night cycle, and live hot reload — plus a playable obby example, a physics playground, headless conformance/determinism test suites, and the `luaug` CLI. See [`docs/roadmap.md`](docs/roadmap.md).

## License

[Apache-2.0](LICENSE). See [`NOTICE`](NOTICE) and [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
