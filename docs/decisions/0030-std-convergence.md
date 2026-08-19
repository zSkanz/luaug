# 0030 — Implement Lute's `@std` surface in the game runtime

- Status: accepted
- Date: 2026-08-19

## Context
The Luau team positions Lute's `@std` as the shared cross-runtime standard
library, and states that the Roblox engine will support the same `@std` APIs
in the future. Code written against `@std` would then run unchanged in Roblox,
Lute, and LuauG.

## Decision
LuauG's game runtime implements an **`@std`-compatible surface** (the
convergence bet): `task` (IS the global task), `json`, `path`, `stringext`,
`tableext` always; `net` (client always; `serve`/sockets dev-mode and behind
`[permissions] net_serve` when shipped); `fs` **virtualized** (`asset://`
read-only content, `save://` per-user writable; raw OS paths only behind a dev
flag); a `test`-compatible shim for engine headless test runs.
`@std/process` and `@std/luau` are **never** available in the game VM
(tooling-only). A **shared conformance suite runs against both Lute and the
LuauG runtime in CI** — the insurance policy on the bet.

## Consequences
Utility code and community libraries port trivially between Roblox, Lute, and
LuauG; sandbox guarantees are preserved by the virtualized/gated modules. If
upstream `@std` shifts, the conformance suite localizes the damage and stubs
absorb divergences.
