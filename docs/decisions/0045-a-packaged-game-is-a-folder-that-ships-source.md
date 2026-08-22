# 0045 — A packaged game is a folder, and it ships Luau source

- Status: accepted
- Date: 2026-08-22
- Amends: `docs/api-design.md` §4's `luaug build` row, which said "Luau bytecode
  compile (O2), content-addressed asset pack, single-folder/exe output".

## Context

M8 owes a distributable player. `api-design.md` has carried a one-line
description of `luaug build` since the planning phase, and building it turned
two of its three clauses into questions.

**What a packaged game IS.** The engine has no installer, no updater and no
content-delivery story, and v1 is not going to grow one. Whatever `luaug build`
produces has to be runnable by every gate on this machine and sendable to a
person by copying it.

**How the packaged player finds its game.** `luaug-host <project>` is the
development invocation. A double-clicked artifact passes no arguments.

**Whether the shipped scripts are source or bytecode.** The api-design line says
bytecode at optimisation level 2. The engine compiles from source at boot: the
module loader hands `script` source text at a canonical project-relative path,
which is what makes a require deterministic (R10) and what hot reload replaces.

## Decision

**The product is a folder**, laid out as:

```
MyGame/
  MyGame.exe        the host binary, copied, wearing the game's icon
  content/          the engine's own: message catalog, shaders, fonts, @luaug
  game/             the project: luaug.toml, src/, its built content pack
```

**`luaug-host` given no script mounts `game/` beside its own executable.** A
convention rather than a configuration file, because a configuration file is a
second thing that can go missing. Nothing else about the packaged run differs
from a development one — same binary, same frame loop, same flags.

**The binary is copied and then edited, never relinked.** `tools/iconpatch`
replaces the icon resource in the copy with the game's, from `[project] icon`.
An engine whose games all wear the engine's face is a template, not an engine —
and the check that it worked reads the resource back out of the artifact rather
than looking at it.

**The game ships as Luau source, not bytecode.** This is the amendment, and the
reason is that bytecode is not a packaging step here — it is an engine feature
that does not exist:

- The module loader is defined in terms of SOURCE at a project-relative path
  (`world_host.h`). A bytecode pack means teaching it a second unit type, and
  teaching every consumer — `require`, the entry-script mount, the reload — to
  accept both.
- Hot reload replaces source and recompiles. A packaged build with no reload
  could skip that, but then the packaged path and the development path stop
  being the same path, which is the property that makes a shipped game
  debuggable at all.
- The benefit is boot time and source concealment. Boot time is not a v1
  problem: the largest project in this repository compiles in milliseconds.
  Concealment is not a promise this engine can make honestly — Luau bytecode is
  trivially decompiled, and saying otherwise in a release note would be worse
  than shipping the source plainly.

`api-design.md` §4 is corrected in the same commit as this ADR, per the
docs-follow-reality rule.

**Windows is the only target `luaug build` produces**, and it refuses rather
than approximating. A folder built on Linux would carry a Linux binary named
`.exe` and no icon resource: a shipped artifact that cannot run and cannot be
told apart from one that can. The roadmap's release line is Windows binaries
plus source instructions, and this matches it.

## Consequences

- **The packaging chain is gated end to end** (`tests/packaging`): `luaug new`
  scaffolds, `luaug build` builds, the built executable is run *with no
  arguments* and has to play the game, and `iconpatch verify --not` asserts the
  artifact is not still wearing the engine's icon. It runs in the Windows stage
  of `scripts/localgate.ps1`, beside the hot-reload gate, because it drives the
  CLI and the CLI is a Lute application.
- **That gate found D045 on its first run** — `luaug new` could not find its own
  template, and had not been able to since the CLI's commands moved into
  `commands/`. A scaffolder nothing ever scaffolds with is a scaffolder that
  rots.
- **A game's source is in the folder a player receives.** Written down here so
  that nobody has to discover it, and so that the milestone that wants
  otherwise knows what it is signing up for: a second unit type in the module
  loader, not a flag on the packager.
- **The `dist/` directory is ignored by git** and cleared on every rebuild —
  guarded by a marker file, so pointing `--output` at something that is not a
  build directory refuses instead of deleting it.
