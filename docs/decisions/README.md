# Architecture Decision Records

Every settled decision lives here as one numbered file. ADRs are the project's
decision memory: before re-debating anything, read the ADR; to change a
decision, write a **new** ADR that supersedes the old one (never edit history).

## Process

- Format: MADR-lite (template below). Keep them short — context and
  consequences, not essays.
- Numbering: `NNNN-kebab-title.md`, monotonically increasing.
- Status: `accepted` | `superseded by NNNN` | `proposed` (proposed ADRs are
  escalation items — see `MASTER_PROMPT.md` §10; only a human accepts them).
- **Docs-follow-reality rule:** when implementation legitimately diverges from
  `docs/architecture.md` or `docs/api-design.md`, write the ADR *and* update
  the doc **in the same commit**. A stale spec is a bug.
- ADRs 0001–0030 record the decisions made during the planning phase
  (August 2026), by the user (Skanz) and the planning session, informed by the
  frozen research reports in `docs/research/`.

## Template

```markdown
# NNNN — Title

- Status: accepted
- Date: YYYY-MM-DD
- Supersedes: — (or NNNN)

## Context
Why a decision was needed; the forces at play.

## Decision
What was decided, stated imperatively.

## Consequences
What becomes easier/harder; costs accepted; follow-ups.
```

## Index

| # | Decision |
|---|----------|
| [0001](0001-apache-2-license.md) | Apache-2.0, single license |
| [0002](0002-luau-pinned-direct-embed.md) | Luau 0.734 pinned, embedded directly in the C++ core |
| [0003](0003-lute-unmodified-tooling-runtime.md) | Lute 1.0.0 as the unmodified tooling runtime |
| [0004](0004-sdl3-platform-layer.md) | SDL3 as the platform layer |
| [0005](0005-custom-rhi-sdlgpu-default-bgfx-mobile.md) | Custom RHI; SDL3 GPU default backend; bgfx in the mobile phase |
| [0006](0006-hlsl-via-sdl-shadercross.md) | HLSL authored shaders via SDL_shadercross |
| [0007](0007-jolt-3d-physics.md) | Jolt 5.6 as the default 3D physics backend |
| [0008](0008-box2d-2d-physics-post-v1.md) | Box2D 3.1 for the post-v1 2D layer |
| [0009](0009-miniaudio-module-is-the-seam.md) | miniaudio; the audio module itself is the swappable seam |
| [0010](0010-asset-stack.md) | Asset stack: fastgltf + meshoptimizer + basis/KTX2 + stb; assimp offline-only |
| [0011](0011-imgui-debug-clay-game-ui.md) | ImGui for debug UI; Clay behind Roblox-style in-game UI Instances |
| [0012](0012-networking-v1-primitives-only.md) | v1 networking = low-level primitives only (GNS + ENet behind ITransport) |
| [0013](0013-vector3-native-luau-vector.md) | Vector3 IS the native Luau vector (3-wide, f32) |
| [0014](0014-f64-world-coords-floating-origin.md) | f64 world coordinates + per-region floating origin |
| [0015](0015-deferred-only-signals.md) | Deferred-only signals; no legacy scheduling globals |
| [0016](0016-fixed-tick-rollback-oriented.md) | Fixed-tick deterministic simulation with rollback-oriented foundations |
| [0017](0017-no-editor-v1-code-first.md) | No visual editor in v1; code-first DX |
| [0018](0018-strict-luau-new-solver.md) | All Luau `--!strict` under the new type solver, CI-enforced |
| [0019](0019-i18n-day-one.md) | i18n from day one: key + catalog, English-only launch |
| [0020](0020-clean-room-legal-posture.md) | Clean-room legal posture toward Roblox |
| [0021](0021-vendoring-in-tree.md) | Vendoring: in-tree `third_party/` + manifest + patches |
| [0022](0022-recast-detour-post-v1.md) | Recast/Detour integration deferred post-v1 (seam only) |
| [0023](0023-backend-selection-build-time.md) | Backend selection at build time; explicit factory, no plugin ABI |
| [0024](0024-hot-reload-fast-world-restart.md) | Hot reload: fast world restart is the canonical v1 model |
| [0025](0025-determinism-guarantee.md) | The precise v1 determinism guarantee |
| [0026](0026-child-name-index-duplicates.md) | Child-name index supports duplicate sibling names |
| [0027](0027-irenderer-renderworld-contract.md) | IRenderer contract with RenderWorld as the stable boundary |
| [0028](0028-instance-facade-over-ecs.md) | Instance facade over a hand-rolled deterministic ECS |
| [0029](0029-input-action-system-only.md) | Input Action System is the only input model |
| [0030](0030-std-convergence.md) | Implement Lute's `@std` surface in the game runtime |
| [0031](0031-build-provenance-header.md) | Build provenance header; M0 grounding gate amended |
| [0032](0032-binary-toolchain-artifacts-fetched-not-vendored.md) | Binary toolchain artifacts are fetched and hash-pinned, never vendored |
| [0033](0033-hand-written-json-reader-in-core.md) | One hand-written JSON reader in `core`, not a dependency |
| [0034](0034-luau-casing-objects-modules-files.md) | Luau casing: objects vs modules, and the file-level rule |
| [0035](0035-engine-is-a-websocket-client-of-the-dev-server.md) | The engine is a WebSocket client of the dev server; only the dev server listens |
| [0036](0036-simdjson-vendored-for-fastgltf.md) | simdjson is vendored for fastgltf, and fastgltf's downloader is made unreachable |
| [0037](0037-rhi-interface-frozen-at-m4.md) | The RHI interface is frozen at the end of M4 |
| [0038](0038-visual-fidelity-is-a-v1-target.md) | What the renderer draws is judged against a stated reference, and visual fidelity is a v1 target |
| [0039](0039-input-context-rate-and-total-enums.md) | An InputContext declares its dispatch rate, and the IAS's enums are total |
| [0040](0040-udim2-layout-is-arithmetic-not-a-solver.md) | UDim2 layout is arithmetic, not a constraint problem; v1 does not call Clay |
