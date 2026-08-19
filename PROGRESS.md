# PROGRESS — LuauG Build Ledger

Fixed format (MASTER_PROMPT.md §11). Hard cap ~300 lines: archive old session
log entries to `docs/progress-archive/YYYY-MM.md`.

## State

- Current milestone: **M0 — not started**
- Last green commit: (docs-only phase; docs-lint CI is the only active gate)
- Gate status: n/a (no engine gates exist before M0 completes)
- Repo created 2026-08-19 by the planning session (Claude Fable) at
  `D:\Projects\LuauG`. Docs package complete; engine code not started.

## Now / Next

- Next: execute **M0 — Bootstrap and First Light** per `docs/roadmap.md`
  (start by writing `docs/briefs/m0-kickoff.md`, then vendor Luau/SDL3/doctest
  into `third_party/` at the manifest-target versions, capturing real SHAs).
- Human has NOT pre-authorized batching milestone reviews — stop at every gate.

## Blocked — needs human

- (none)

## Decisions pending ADR

- (none — ADRs 0001–0030 cover all settled decisions)

## Session Log

- **2026-08-19 (planning session, Claude Fable):** Created the full mission
  package: root files + Apache-2.0 licensing; docs/roadmap.md (M0–M8 with
  gates); ADRs 0001–0030; three frozen research reports + UNCONFIRMED
  registry; MASTER_PROMPT.md; CLAUDE.md; this ledger; architecture and
  api-design docs; repo skeleton, configs, and docs-lint CI. Learned: see
  research reports. Next: M0 per roadmap.

<!-- Format for future entries:
- **YYYY-MM-DD (session N):** did X; learned Y; Next: <literal first action>.
-->
