# Contributing to LuauG

Thanks for your interest! A few things are unusual about this project's
current phase — read this before opening anything.

## Current phase: autonomous build, human-gated

Pre-v1, the primary contributor is an autonomous AI agent (Claude Opus,
multi-agent) executing [`MASTER_PROMPT.md`](MASTER_PROMPT.md) milestone by
milestone, with a human reviewing at every gate in
[`docs/roadmap.md`](docs/roadmap.md). External PRs are welcome for docs and
tooling; engine-code PRs are best held until v1 unless coordinated in an
issue first.

## Ground rules (the same rules the agent follows)

- **English everywhere** — code, comments, identifiers, commits, docs (R1).
- **All Luau is `--!strict`** under the new type solver (R2).
- **No hardcoded user-facing strings** — i18n keys + catalogs (R3).
- **Clean room**: public Roblox API *concepts* only; never Roblox code,
  assets, branding, or decompiled material (R7 / ADR 0020).
- **Permissive licenses only**; adding a dependency requires an ADR (R5/R6).
- Never edit `third_party/` in place — use `third_party/patches/` (R13).
- Style: `.clang-format` / `stylua.toml`; comments explain *why*, never
  narrate code.

## Mechanics

- **License:** Apache-2.0. Contributions are accepted under the
  **Developer Certificate of Origin** — sign off each commit
  (`git commit -s`, adding `Signed-off-by: Your Name <email>`).
- **Commits:** conventional (`feat:`, `fix:`, `docs:`, `test:`, `chore:`),
  scope = module name (e.g. `feat(scene): …`).
- **PRs:** green CI required; API changes need matching conformance specs
  (written from [`docs/api-design.md`](docs/api-design.md), not from the
  implementation) and regenerate the api artifacts; behavior/design changes
  need an ADR in [`docs/decisions/`](docs/decisions/) and a doc update in the
  same PR ("docs follow reality").
- **Decisions:** don't relitigate settled ADRs in PR threads — open an issue
  proposing a superseding ADR instead.

## Getting set up

`scripts/bootstrap.ps1` (Windows) or `scripts/bootstrap.sh` (Linux/macOS),
then `rokit install`. Build/test commands are in [`CLAUDE.md`](CLAUDE.md)
(they activate at milestone M0).
