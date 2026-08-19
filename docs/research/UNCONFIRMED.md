# UNCONFIRMED Claims Registry (live)

Claims used in planning that were **not verified against a primary source**, or
that carry material uncertainty. The builder agent must not write code that
depends on an `unverified` row — verify first (vendored headers, upstream docs
matching the pinned version, or a passing test), then update `Status` and
`Verified by`. Add new rows whenever web-derived knowledge flows into code.

Statuses: `unverified` | `confirmed` | `refuted` (refuted rows get an addendum
in the source report).

| ID | Claim | Source | Date added | Status | Verified by | Impact if wrong |
|----|-------|--------|-----------|--------|-------------|-----------------|
| U-01 | Luau `class` syntax ships enabled/unflagged in 0.734 (implementation exists on master; flag state unknown) | luau-2026.md §5 | 2026-08-19 | unverified | — | None for v1 (ADR 0002 keeps `class` out of the idiom); matters for future adoption |
| U-02 | Luau 64-bit `integer` library is included in `luaL_openlibs` by default and is production-hardened | luau-2026.md §5 | 2026-08-19 | unverified | — | None for v1 (kept out of idiom); revisit for entity/network IDs later |
| U-03 | NCG performance figures (1.5–2.5×, 3.2× Mandelbrot) — newest official numbers are from 2023 | luau-2026.md §3 | 2026-08-19 | unverified | — | Perf planning only; re-benchmark on the pinned build (M2+) |
| U-04 | Android NCG scope (ABIs, W^X handling, version floor) beyond "production-tested" | luau-2026.md §3 | 2026-08-19 | unverified | — | Mobile phase planning |
| U-05 | iOS prohibits JIT ⇒ Luau NCG unavailable in App Store builds (well-established policy; no explicit 2026 Luau statement found) | ecosystem-2026.md notes | 2026-08-19 | unverified | — | Mobile perf budget = interpreter (already assumed; rule R16) |
| U-06 | SDL3 GPU Android support still "limited" as of Aug 2026 (FAQ wording may be stale) | ecosystem-2026.md §B.2 | 2026-08-19 | unverified | — | RHI backend strategy; mitigated by NDK compile job + device checkpoint + bgfx hedge (ADR 0005) |
| U-07 | SDL 3.4.x exact latest patch version (sources conflicted: 3.4.8 vs 3.4.14) | ecosystem-2026.md | 2026-08-19 | confirmed | M0 vendor step: upstream `releases/latest` is `release-3.4.14` (published 2026-08-03); pinned at `147a8ee32dbf` in `third_party/manifest.json` | Pin the actual tag at vendor time (M0) |
| U-08 | Jolt 5.6.0 release date year (2026-07-11 inferred) | ecosystem-2026.md §B.3 | 2026-08-19 | unverified | — | None; pin actual tag at vendor time |
| U-09 | `lute compile` does not support cross-compilation (docs silent) | lute-2026.md §7 | 2026-08-19 | unverified | — | CLI distribution plan (build per-OS in CI if true) |
| U-10 | `@lute/vm` child runtimes are OS-threaded (inferred from `runContinuously()` comment) | lute-2026.md §3 | 2026-08-19 | unverified | — | Tooling-only concern |
| U-11 | Roblox frame-phase ordering: `Heartbeat` is a separate, later resumption point than `PostSimulation` (from deferred-events doc, not the scheduler SVG) | ecosystem-2026.md §A.2 | 2026-08-19 | unverified | — | LuauG defines its own documented order (architecture §scheduler); familiarity nuance only |
| U-12 | Diligent Engine's Metal backend still requires a commercial license | ecosystem-2026.md §B.2 | 2026-08-19 | unverified | — | None (Diligent not chosen) |
| U-13 | FMOD/Wwise 2026 pricing tiers (secondary sources) | ecosystem-2026.md §B.4 | 2026-08-19 | unverified | — | Post-v1 plugin planning only |
| U-14 | luau-lsp custom-platform mode types the builtin `vector` with lowercase `x/y/z` correctly for our defs | api-design risk #1 | 2026-08-19 | unverified | — | Week-one spike in M3; affects Vector3 DX polish, not architecture |
| U-15 | "luauengine.org" claims to be an official Roblox Studio fork — no engine source found, no Roblox mention | ecosystem-2026.md §A.1 | 2026-08-19 | unverified (treat as false) | — | Ignore; never base anything on it |
| U-16 | meshoptimizer / assimp / Recast current exact versions (minor source conflicts) | ecosystem-2026.md §B | 2026-08-19 | unverified | — | Pin actual tags at vendor time (M0) |

Full per-report lists live in each report's final section. When the M0 vendor
step captures real tags/SHAs, update U-07/U-08/U-16 and
`third_party/manifest.json` together.
