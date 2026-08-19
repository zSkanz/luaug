# 0021 — Vendoring: in-tree `third_party/` + manifest + patches

- Status: accepted
- Date: 2026-08-19

## Context
Dependencies must be pinned exactly (ADR 0002), patchable (Luau), buildable
offline/hermetically (console CI environments are network-restricted), and
auditable for licenses. Git submodules are friction-prone on Windows and make
carrying patches miserable; FetchContent hits the network at configure time
and pins poorly in practice. The autonomous agent also needs stable in-repo
paths to read vendored headers as API ground truth.

## Decision
Vendor **full source trees in `third_party/`**, governed by
`third_party/manifest.json` (name, version, upstream URL, commit SHA, license,
patch list) with local patches in `third_party/patches/<dep>/*.patch`,
re-importable by the `tools/vendor` Lute script. **No submodules, no
FetchContent.** Vendored trees are never edited in place (rule R13); changes go
through the patch set. `THIRD_PARTY_NOTICES.md` is generated from the
manifest. Lute itself is NOT vendored (it is a rokit-installed binary,
ADR 0003).

## Consequences
Hermetic builds, reviewable patch sets, trivial license audits, stable
grounding paths for the agent. Cost: repository size (accepted; marked
`linguist-vendored`).
