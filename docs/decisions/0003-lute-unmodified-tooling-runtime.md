# 0003 — Lute 1.0.0 as the unmodified tooling runtime

- Status: accepted
- Date: 2026-08-19

## Context
Lute (luau-lang org, first-party, v1.0.0 2026-04-16, C++/libuv) is the emerging
standard standalone Luau runtime, with fs (incl. watch), net (HTTP+WS client &
server), process, task (Roblox-shaped), crypto, CST parser, test runner,
profiler (Perfetto), package manager, and `lute compile` to standalone
executables. But it has **no graphics/audio/window**, no native-module loading
(issue #1047 unanswered), no embedding docs, and no install/export — embedding
or forking it as the engine host would be high-risk (research:
`docs/research/lute-2026.md`).

## Decision
Use Lute **1.0.0, unmodified, as a rokit-pinned binary**, exclusively as the
runtime for tooling: the `luaug` CLI, the dev server (fs.watch + WebSocket hot
reload channel), the asset pipeline, the API generator, and repo automation.
The engine runtime never depends on Lute. Lute is not vendored;
`scripts/bootstrap` installs it via rokit.

## Consequences
The tooling gets a batteries-included, first-party runtime for free, written in
the same language users script in. The engine remains insulated from Lute's
API churn (its module docs still allow breaking changes across majors). Gaps to
work around in tooling: no streaming I/O yet (upstream issue #128), thin `io`.
