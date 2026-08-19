# 0018 — All Luau `--!strict` under the new type solver, CI-enforced

- Status: accepted
- Date: 2026-08-19

## Context
The new type solver is the default in `luau-analyze` (Roblox GA 2025-11-20;
strict was opt-in at GA due to known bugs; memory/verbosity issues exist at
scale). Greenfield strict-by-default is viable — there is no legacy error
wave. User decision #12. Native codegen performance is proportional to type
annotations, so typing is also a performance feature.

## Decision
**Every Luau file in the project is `--!strict` under the new type solver** —
engine runtime libraries, `@std` implementation, tooling/CLI, API definition
files, examples, templates, and test specs. CI fails on any `luau-analyze`
diagnostic or any file missing strict mode. The engine publishes complete
type definitions (`declare extern type`) + documentation JSON so user games
get fully typed autocomplete via luau-lsp's custom-platform mode (no fork).
Budget CI memory/time for the solver; tolerate occasional upstream ICEs by
pinning (ADR 0002).

## Consequences
A fully typed public surface is a launch feature and a codegen enabler. The
Luau + luau-lsp pair is pinned per engine release and upgraded together.
