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

## Addendum — 2026-08-19: the precision figure above is wrong

The Consequences paragraph claims `part.Position` is "millimeter-exact within
±131 km". It is not, and the number it borrows is the bound for a different
claim.

f32 carries a 24-bit significand, so the ULP at a magnitude in
[2^e, 2^(e+1)) is 2^(e−23). At 131072 m = 2^17 that is 2^17 / 2^23 = 1/64 m ≈
**15.6 mm** — sixteen times coarser than a millimetre. Millimetre exactness
means ULP ≤ 0.001 m, i.e. 2^(e−23) ≤ 2^−10, i.e. e ≤ 13, so it survives only
to 2^13 = **8192 m**. ±131 km is where the *useful exponent range* ends, at
roughly 16 mm resolution; it was never where millimetre precision ends.

**Corrected statement**, and the one `docs/api-design.md` §2.3 now carries:
`part.Position` is millimetre-exact to roughly ±8 km, degrading to ~16 mm
resolution at ±131 km, where the exponent range ends.

The decision is unaffected. If anything the corrected figure strengthens it:
f32 positions run out an order of magnitude sooner than the original text
implied, which is exactly why the f64 translation lives on `CFrame`
(ADR 0014) and why open-world math is routed through it rather than through
`Position`.
