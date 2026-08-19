-- Type definitions for the M1 preview binding (engine/app/src/preview_api.cpp).
--
-- Hand-written, and temporary in the same way the API is. From M3 the public
-- surface is GENERATED from the IDL in `api/` into `runtime/types/` and checked
-- for freshness by CI (architecture.md §5); this file is not that, and must not
-- grow into a stand-in for it. It exists so `examples/00-clear/init.luau` can be
-- `--!strict` and pass the analysis gate (R2) rather than being excluded from
-- it -- an exemption is the kind of thing that spreads.
--
-- Loaded by scripts/gates/luau-check.sh. When M2 replaces the `luaug` global
-- with `require("@luaug/...")` over real services, delete both.
--
-- Parameter names live in the comments rather than in the signatures purely for
-- readability at these arities; named parameters in a function type are valid
-- here and luau-lsp accepts them.
--
-- What is NOT valid is running StyLua over this file: `declare` is a luau-lsp
-- extension rather than Luau syntax, so the formatter cannot parse it at all.
-- `.styluaignore` excludes `*.d.luau` for that reason, and the parse errors it
-- produced were briefly and wrongly blamed on the signatures above.

declare luaug: {
    -- OnFrame(callback) -- callback receives (tick, elapsed).
    -- `tick` is the fixed-step counter, `elapsed` is tick * fixedDt in seconds.
    -- Neither is wall-clock time, which is what makes a headless run
    -- reproducible.
    OnFrame: ((number, number) -> ()) -> (),

    -- SetClearColor(r, g, b) -- linear, not sRGB-encoded. The swapchain format
    -- decides the encoding.
    SetClearColor: (number, number, number) -> (),

    -- WireBox(cx, cy, cz, hx, hy, hz, r, g, b) -- centre, half-extents, then a
    -- linear colour. Half-extents rather than a size because every caller has a
    -- centre and a radius, and halving at the call site is where sign errors
    -- live.
    WireBox: (number, number, number, number, number, number, number, number, number) -> (),

    -- Axes(length) -- a world triad at the origin: X red, Y green, Z blue.
    Axes: (number) -> (),
}
