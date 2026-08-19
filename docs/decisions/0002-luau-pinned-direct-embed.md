# 0002 — Luau 0.734 pinned, embedded directly in the C++ core

- Status: accepted
- Date: 2026-08-19

## Context
Luau releases weekly with no semver/LTS; its C API had breaking changes through
2025–2026 (`lua_Type` gained `LUA_TINTEGER/TCLASS/TOBJECT`; `lua_CompileOptions`
grew fields; Analysis renamed `ClassType`→`ExternType`). The engine needs a
stable, controllable language core with maximum performance (safeenv fastpaths,
native codegen, vector/buffer primitives, tag-based userdata).

## Decision
Embed the **Luau VM directly in the C++ core** (the Roblox approach) — not via
any intermediate runtime. **Pin Luau 0.734** (2026-08-14) in
`third_party/manifest.json`; upgrades are deliberate, human-approved events run
through the full CI matrix, never silent tracking. Shipping game clients link
only `Luau.VM` + `Luau.CodeGen` (bytecode precompiled offline; no Compiler or
Analysis in the runtime binary). `luaL_sandbox`/`luaL_sandboxthread` are always
on. Never persist `lua_Type` tag values (the enum is not stable across build
configs). 2026 language features `class` and 64-bit `integer` stay **out of the
engine idiom** until upstream stabilizes them (flag state unconfirmed /
maturing) — but no API may be shaped in a way that conflicts with adopting them
later.

## Consequences
Full control of the VM, compile options, allocator hooks, memory categories and
codegen. Cost: we own upgrades (patch set + recompile) and must read vendored
headers — not memory — for API truth (`MASTER_PROMPT.md` §9).
