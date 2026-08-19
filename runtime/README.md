# runtime/ — Luau Shipped Inside the Engine

- `std/` — the `@std` implementation (Lute-compatible surface, ADR 0030),
  `--!strict`. Scope and sandbox gating per `docs/api-design.md` §7. The
  shared conformance suite runs these modules against BOTH Lute and the
  engine in CI.
- `luaug/` — pure-Luau engine libraries exposed as `@luaug/*`
  (`camera`, `signal`, `testing`, dev-only `imgui`), `--!strict`.
- `types/` — **GENERATED** `.d.luau` definitions + luau-lsp docs JSON
  (`api/generator/`). Checked in; CI regenerates and diffs — never edit by
  hand.

Populated starting at M2/M3 per `docs/roadmap.md`.
