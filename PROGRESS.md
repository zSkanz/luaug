# PROGRESS — LuauG Build Ledger

Fixed format (MASTER_PROMPT.md §11). Hard cap ~300 lines: archive old session
log entries to `docs/progress-archive/YYYY-MM.md`.

## State

- **M3 — Tooling Loop: CLI, Hot Reload, Types, Tests — COMPLETE and SIGNED OFF
  by the human on 2026-08-20**, tagged `milestone/m3` (brief:
  [`docs/briefs/m3-kickoff.md`](docs/briefs/m3-kickoff.md), which carries the
  Gate Record and seventeen Findings). All four gate items green on both tiers,
  plus ADR 0024's own <500 ms requirement.
- **M4 — Seeing the World: Meshes, Materials, Camera, Lighting — OPEN** since
  2026-08-20 (brief: [`docs/briefs/m4-kickoff.md`](docs/briefs/m4-kickoff.md) —
  sixteen decisions, nineteen NOT-in-scope items, seven entering risks, and a
  ten-step build order). The M3 gate was re-run green on both tiers before it
  opened. **Scope was extended by human decision the same day**: the
  `DebugShell`'s explorer and properties panel land here, because ADR 0017
  declines a visual editor on the grounds that an in-game shell stands in for
  inspection, and four milestones in that shell did not exist. Still needing the
  human: the **Android device checkpoint**, which blocks the RHI freeze at the
  end of the milestone.
- **M2 — Kernel — signed off 2026-08-20**, tagged `milestone/m2`; **M1** signed
  off 2026-08-19 (`milestone/m1`); **M0** signed off 2026-08-19
  (`milestone/m0`).
- **CI is green on `main`**, and the three steps M3 added to the workflow — the
  toolchain install, `luaug test` and the hot-reload suite — have now run on
  hosted runners on both tiers. macOS built green on the `milestone/m3` tag,
  its first compile since M1.

### M3: what exists

- **The loop the engine sells.** `luaug dev` watches a project, and a saved file
  becomes a new world in **1.7 ms** on the M2 example — against a 500 ms budget.
  The engine dials out to the dev server as a WebSocket client (ADR 0035) and
  opens no port in any profile; commands are applied at the FrameStart safe
  point, never mid-tick, which is what keeps within-run determinism true with a
  watcher attached.
- **The reload is a restart, and that is testable.** `app::reloadWorld` destroys
  and rebuilds `WorldHost` and nothing above it, building the new world before
  destroying the old so a syntax error leaves the running one alone. The
  assertion that matters, in C++ and again end to end: a reloaded world hashes
  the same as a cold boot of the edited source at the same tick.
- **`HotReloadService`** (`SaveState` / `LoadState` / `IsReload()` / `PreReload`
  / `PostReload`), a state bag that lives in C++ because the VM that held the
  value is what a reload destroys, and `PreserveOnReload` instances captured as
  descriptions and re-materialised before the new entry scripts are deferred.
- **`engine/net` (L2)** — the RFC 6455 client, tested against the RFC's own
  published vectors; and **`core::JsonWriter`**, which `json.h` said would have
  no caller until this milestone gave it two.
- **`tools/cli`** — `dev`, `test`, `check`, `fmt`, `new` on the pinned Lute, a
  TOML-subset reader for `luaug.toml`, the dev server, and the `starter`
  template with the `.vscode` settings that make `luaug new` plus opening the
  folder a complete setup.
- **The gate got teeth.** `luau-check.sh` calls `luaug check`; the i18n lint
  enforces R3's second half over 203 sink calls; the CLI's own tests and the
  end-to-end hot-reload suite both run in the gate, on both tiers.

### M3: the gate

**4 of 4, plus ADR 0024's own.** The Gate Record in the brief carries the
numbers, the commands and the deliverable's transcript.

### M3: what does NOT exist yet

Nothing from M3's own scope. The brief's NOT-in-scope list names nine things
deliberately left: `luaug build`, `asset`, `add`, `doctor`, `lute compile`
packaging, module-level `__hotreload`, asset and shader hot swap, the `eval` dev
console, engine-side `luaug.toml`, and the `obby`/`openworld-demo` templates.
The bytecode cache ADR 0024 names is also not built — the budget did not need
it, and building it on speculation is what §5 rejects.

## Now / Next

- **Next: build-order step 3 — `engine/asset` (L2)**: mount, `asset://` URN
  resolution, `MeshData`/`MaterialDef`/`TextureData`, the load-state machine,
  and the glTF importer behind it, tested against a checked-in `.gltf` fixture
  before any of it draws. This is the commit that first wires fastgltf,
  meshoptimizer and simdjson into the build.
- **Step 2 is done.** `core::AABB` and `core::Frustum` exist with the sign
  conventions pinned by tests rather than by comment: planes point **inward**,
  the near plane is `row2` alone because depth is [0, 1], and culling is the
  conservative positive-vertex test. Both tiers green.
- **Step 1 is done.** fastgltf 0.9.0, meshoptimizer 1.2 and simdjson 3.12.3 are
  vendored and pinned, the notices are regenerated, and fastgltf carries the
  first patch this repository has ever applied (ADR 0036). Nothing is wired into
  the build yet, on `third_party/CMakeLists.txt`'s own rule: a tree is added
  when a module links it, which is step 3.
- **The Android device checkpoint still needs the human**, before the RHI freeze
  at the end of M4 — the agent does not hold phones. The glTF sample-asset
  question is answered: a CC0 model under ~1 MB, recorded in
  `THIRD_PARTY_NOTICES.md`, beside a hand-authored fixture for the importer's
  unit tests.
- **Read the vendored `CMakeLists.txt`, not the library's documentation.** M4's
  first three findings all came from doing that at kickoff: fastgltf has an
  undocumented mandatory dependency that it downloads unpinned into our tree,
  the patch mechanism that was supposed to fix such things reported success
  while doing nothing, and no vendored tree here has ever matched its pinned
  commit byte for byte. None of the three cost more than an hour at kickoff;
  each would have cost a milestone at the gate.
- **The dogfooding claim binds from here and is weaker than it sounds.** What
  `luaug dev` reloads is the *scene* — mesh, camera, sun, materials — while the
  renderer is C++ and needs a rebuild. The brief's Decision 13 commits to
  recording the real reload-vs-rebuild count in the Gate Record, so the claim
  ends the milestone either evidenced or as a finding.
- **`fs.watch` cannot be trusted on either platform, and Linux is the worse
  one** — a change below the watched directory produces no event at all there.
  Anything that watches files in a later milestone watches every directory and
  rescans.
- **A gate that can pass while doing nothing keeps being built by accident.**
  This milestone caught three more: the i18n lint looking for a call spelling
  nobody uses, a `luaug test` that would have reported success on a stale report
  file, and a dev server that noticed saves and did nothing because the caller
  supplied no callback. All three now fail loudly.
- **Run `scripts/localgate.ps1` before every push.** Both tiers, ~50 s warm now
  that it carries the conformance suite and the hot-reload gate.
- Carried forward, none blocking:
  - **Three of the five generated artifacts api-design.md §5 lists do not
    exist.** `.luaug/types/std/**` and `.luaug/types/luaug/**` (typed stubs for
    `@std`/`@luaug`), `api-dump.json`, and `docs/reference/**`. The stubs were
    named in M3's own goal and planned as fan-out; the api-dump was assigned to
    M3 by M2's Decision 12 and never picked up by M3's brief, which is how it
    fell between the two. None blocked M3's gate — the gate asks for defs that
    lint clean and are diff-checked, and those are (1). Recorded here because a
    list that reads as present tense is how an artifact stays missing without
    anyone deciding that it should be.
  - **The shipping profile does not configure.** `engine/script/src/modules.cpp`
    and `runtime.cpp` include `<luacode.h>` unconditionally while
    `LUAUG_LUAU_COMPILER` is forced off in shipping (ADR 0002).
  - **`architecture.md` §9 lists a clang-format gate that does not exist.**
  - **`Luau.Analysis` is still compiled and never linked** — carried from M0.
  - **DXIL produced on Linux is never verified as signed.**

## Blocked — needs human

- **Every vendored tree in `third_party/` is CRLF-mangled and has never been
  byte-identical to its pinned commit.** `vendor.luau` checks out through its
  own git dir, which `.gitattributes` cannot reach, so `core.autocrlf=true`
  applied on Windows; then `third_party/** -text` — the rule written to protect
  byte-exactness — disabled normalization and committed the mangling faithfully.
  Affects luau, sdl3, imgui, stb, doctest, spirv_cross and sdl_shadercross, i.e.
  everything vendored before this milestone. ADR 0021's central claim has been
  false since M0. Found because it was the second reason the first patch this
  repository has ever applied refused to apply. Full write-up: M4 brief,
  Finding 3.

  **Fixed at the source:** the checkout now forces
  `core.autocrlf=false core.eol=lf`, and the three trees vendored this milestone
  are correct.

  **The question: re-vendor the historical trees now, or leave them?** It is a
  purely mechanical `lute tools/repo/vendor.luau sync` over the pinned commits
  — no version changes — but it rewrites on the order of 20,000 files in one
  commit and roughly doubles the pack's third_party footprint. Nothing is
  functionally wrong today: compilers do not care about line endings, and the
  only mechanism that does is patching, which no historical tree uses.

  Recommendation: do it, once, in a commit that touches nothing else, so that
  ADR 0021 is true and any future "does this tree match upstream?" check has a
  clean baseline. It does not block M4.

## Decisions pending ADR

- (none — ADR 0035 was written at M3 kickoff)

## Session Log

Entries for the planning session and for M0, M1, M2 and M3 are in
[`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md), moved
there when this file passed its ~300-line cap.

- **2026-08-20 (session 7, Claude Opus): opened M4.** Ran the §2 boot sequence —
  the repo had moved two commits past the ledger and carried an uncommitted
  api-design correction from the previous session, so the repo won and that
  landed on its own first. Re-ran the M3 gate green on both tiers (903
  conformance, 3 hot-reload, reload 0.3 ms against 500 ms). Deep-read
  architecture §2, §3, §7 and §9, api-design §2.1–§2.3, ADRs 0005, 0006, 0010
  and 0027, and the ecosystem report's asset section, then wrote
  `docs/briefs/m4-kickoff.md`.
  Then build-order step 1, which produced three findings before any engine code
  existed — all from reading vendored `CMakeLists.txt` files rather than
  documentation. **fastgltf has a mandatory dependency no document here
  mentions**, and with no `simdjson::simdjson` target present it downloads
  simdjson's amalgamated pair, unpinned and unhashed, into
  `third_party/fastgltf/deps/` at configure time: R5, R13, R14 and ADR 0032's
  fetch rule in one upstream default. Escalated; the human approved vendoring
  simdjson (ADR 0036), pinned at the 3.12.3 fastgltf itself targets, with a
  patch turning the download branch into a `FATAL_ERROR`.
  Which exercised **the patch mechanism R13 rests on for the first time in four
  milestones, and it reported success while doing nothing**: `git apply`
  resolves paths against the repository root even from a subdirectory, and
  *skips* — exit 0 — anything outside the current directory. Patches now apply
  from the root with `--directory=`, a `Skipped patch` is an error, and each
  patch is verified with `--check --reverse` immediately after.
  And then the patch still would not apply, which found the sharpest one:
  **no vendored tree in this repository has ever been byte-identical to its
  pinned commit.** `vendor.luau` checks out through its own git dir, where
  `.gitattributes` cannot reach it, so `core.autocrlf=true` mangled every file
  on Windows — and `third_party/** -text`, the rule written to guarantee
  byte-exactness, then committed the mangling faithfully. Every tree since M0 is
  affected. The checkout forces `core.autocrlf=false core.eol=lf` now and this
  milestone's three trees are correct; whether to re-vendor the historical ones
  is a ~20,000-file question left for the human.
  Next: implement `core::AABB` and `core::Frustum` per architecture §2's math
  list, with the plane/box tests, then specs for them in
  `engine/core/tests/math_tests.cpp`.

<!-- Format for future entries:
- **YYYY-MM-DD (session N):** did X; learned Y; Next: <literal first action>.
-->
