# tests/ — Test Suites

Four suites (details: `docs/architecture.md` §9):

- **C++ unit** — doctest, one exe per module under `engine/<mod>/tests/`,
  plus `integration/` here for multi-module tests against the headless host.
- **`conformance/`** — `*.spec.luau` behavior specs run by the REAL headless
  engine (`luaug test` → `luaug-host --headless --run-tests`). Written from
  `docs/api-design.md` **only** — spec authors must not read the
  implementation (that is how the spec stays the contract; rule R7 also
  forbids probing Roblox). Covers deferred signal ordering, re-entrancy cap,
  Instance semantics (including duplicate sibling names, ADR 0026), task,
  streaming reparent behavior, IAS, tween easings vs fixture tables.
- **`determinism/`** — recorded seed+input scenarios; WorldHash compared
  across runs and processes (guarantee per ADR 0025). Merge gate from M5.
- **`rendercapture/`** — golden canonical command streams for the
  `rhi_capture` backend (the blocking, GPU-less render regression gate);
  real-image goldens run nightly, non-blocking.

The `@std` conformance suite additionally runs against BOTH Lute and the
engine (ADR 0030). Populated starting at M0.
