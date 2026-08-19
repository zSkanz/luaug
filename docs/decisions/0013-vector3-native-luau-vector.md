# 0013 — Vector3 IS the native Luau vector (3-wide, f32)

- Status: accepted
- Date: 2026-08-19

## Context
Luau's `vector` is a true VM primitive (inline value, zero allocation, SIMD
lowering in native codegen). `LUA_VECTOR_SIZE` must be 3 or 4 — 4-wide costs
+33% memory per value; `LUA_VECTOR_DOUBLE=1` makes vectors heap-allocated
(slow) and changes the `lua_Type` ABI. The ecosystem research initially
suggested 4-wide; the Luau deep-dive resolved the conflict.

## Decision
Build Luau with **`LUA_VECTOR_SIZE=3`, f32, `LUA_VECTOR_DOUBLE=0`** — matching
Roblox and the Luau default. The engine's `Vector3` **is** the native vector
(compile options `vectorLib="Vector3"`, `vectorCtor="new"`,
`vectorType="Vector3"` give constant folding + fastcalls). Canonical component
fields are lowercase `x`/`y`/`z` (the primitive's own fields, per the official
vector stdlib RFC, which LuauG implements for Lute/Roblox compatibility).
Open-world precision is solved engine-side (ADR 0014), never by changing the
script vector type. This is an ABI-defining flag decided once.

## Consequences
Zero-allocation vector math in scripts; SSE/NEON-aligned interop.
`part.Position` (f32) is millimeter-exact within ±131 km and documented as
lossy beyond; `CFrame` carries the f64 translation as the source of truth
(Model A — see `docs/api-design.md`). Risk logged: luau-lsp typing of the
builtin vector under a custom platform — week-one spike in M3.
