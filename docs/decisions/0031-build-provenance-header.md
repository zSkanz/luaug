# 0031 — Build provenance header; M0 grounding gate amended

- Status: accepted
- Date: 2026-08-19
- Supersedes: —

## Context

The M0 gate in `docs/roadmap.md` required that `--version` print "the pinned
Luau version **read from the vendored header** (grounding proof)". The intent
is sound and important: the host must report a version it *derived* from the
vendored tree, not a string a human or an agent typed from memory.

The wording turns out to be unsatisfiable. **Luau 0.734 ships no version
constant anywhere.** Verified by exhaustive search of the vendored tree at
`3fc82b1071ab387531175869afc4fb528464afa4`: there is no `LUAU_VERSION` or
`LUA_VERSION` macro in `VM/include/lua.h`, `VM/include/luaconf.h`,
`VM/include/lualib.h`, or `Common/include/`; no `project(... VERSION ...)` in
`CMakeLists.txt`; no `VERSION` file; and the literal string `0.734` appears
nowhere in the tree. Luau's version exists only as a git tag and a GitHub
release name — consistent with its release model (weekly, monotonic integer
minor, no semver, no LTS; `docs/research/luau-2026.md` §1).

What the vendored headers *do* expose are meaningful, version-discriminating
constants: `LBC_VERSION_TARGET` and `LBC_TYPE_VERSION_TARGET`
(`Common/include/Luau/Bytecode.h`), and the ABI-defining `LUA_VECTOR_SIZE` /
`LUA_VECTOR_DOUBLE` (`VM/include/luaconf.h`). Separately,
`third_party/manifest.json` is already the authority on the pin (ADR 0021) and
now carries the real commit SHA.

Changing a milestone gate is an escalation item (`MASTER_PROMPT.md` §10). This
ADR records the human's decision, given during the M0 session.

## Decision

Generate a **build provenance header** (`luaug/core/build_info.h`) at CMake
configure time from `third_party/manifest.json`, and make `luaug-host --version`
report, in this order:

1. the engine version (from the CMake `project()` version);
2. the pinned Luau version **and commit SHA**, both read from the manifest at
   configure time — never typed into C++ source;
3. Luau ABI constants read from the **vendored headers** at compile time:
   `LBC_VERSION_TARGET`, `LBC_TYPE_VERSION_TARGET`, `LUA_VECTOR_SIZE`,
   `LUA_VECTOR_DOUBLE`.

The M0 gate item in `docs/roadmap.md` is amended, in the same commit as this
ADR, to require exactly the above. A `ctest` case asserts that the
header-derived constants match what ADR 0013 mandates (`LUA_VECTOR_SIZE == 3`,
`LUA_VECTOR_DOUBLE == 0`) and that the configure-time SHA is a full 40-character
hash matching the manifest. Configure fails if the manifest lacks a resolved
commit for Luau.

Deliberately rejected: patching Luau to add a version macro. It would satisfy
the original wording literally, but ADR 0021 requires the patch set stay as
close to empty as possible, and a patch in the hottest-churn dependency would
need rebasing on every upgrade forever — real, permanent cost for a cosmetic
gain.

## Consequences

Grounding is *stronger* than the original wording bought: the host now reports
the exact commit, not just a release name, and the build fails rather than
reporting a version nobody verified. The compile-time constants make an
accidental `LUA_VECTOR_SIZE` or `LUA_VECTOR_DOUBLE` change — an ABI break that
would silently invalidate every vendored assumption (ADR 0013) — a test
failure instead of a mystery.

Cost: `--version` output depends on the manifest being honest. That is
acceptable because `tools/repo/vendor.luau` verifies at import time that the
fetched commit equals the pinned commit, so a manifest that lies cannot
produce a successfully vendored tree.
