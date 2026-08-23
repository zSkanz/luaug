# 0050 — A script is an ordinary instance, and its source is a property

- Status: accepted
- Date: 2026-08-23
- Reverses: 0048's "a `Script` is created as a FILE"
- Supersedes part of 0047 (scripts are mounted from `src/scripts`)

## Context

ADR 0048 answered "why can I not create a script" with a rule: `Script` is
`NotCreatable` because a script exists only because a file under `src/scripts`
does, and the editor's job is to write that file. That was faithful to ADR 0047
and to the IDL, and it shipped.

The human used it and said the rule was wrong:

> basicamente um script é uma instancia normal saca uma instancia normal!! ou
> seja basicamente ele é criado da mesma forma que cria instancia … ele não deve
> ter uma aba especifica para criar um novo script

and named the second half at the same time:

> no projeto em si teremos 2 tipos de script um script que roda em si e um script
> que é tipo o modulescript do roblox saca onde a gente pode requerir ele

**They are right, and the reason is worth stating rather than conceding.** A
script whose identity is a file cannot go in a prefab, cannot be copied with the
thing it belongs to, cannot live in a content library, and cannot be created the
way every other instance is created. Each of those is a consequence of the file,
not of the script — and each of them is a thing this engine now wants.

## Decision

**A script's source is a PROPERTY of the instance.** `BaseScript` is an abstract
class with `Source`, and two concrete classes inherit it:

- **`Script` RUNS.** Every enabled `Script` in the world starts on its own
  coroutine when the world does. One that is not in the world does not run,
  which is the whole difference between storing a script and using it.
- **`ModuleScript` is REQUIRED.** It never starts by itself. `require(module)`
  evaluates it once and every later require of the same instance gives back the
  same value.

Both are creatable, from the same menu every other class is created from. The
editor's "New Script" dialog is gone: it existed to write a file, and there is
no file.

**`require` accepts an instance.** Only a `ModuleScript`: a `Script` runs when
the world does, and requiring one would run it a second time somewhere else —
which is precisely what the two classes exist to keep apart. The cache is keyed
by instance and has the same three states a path-keyed module has: a value, a
failure that is re-raised rather than re-run (api-design.md §3), and "being
evaluated right now", which is a cycle.

## Consequences

- **The world hash changed**, because every `Script` now carries a property the
  hash reads. `tests/determinism`'s recorded traces were re-recorded on both
  Windows and Linux; the change is every checkpoint of every scenario whose
  world contains a script, which is all of them.
- **Three conformance specs asserted the opposite** and now assert this. They
  were not wrong when they were written — they were the old model, checked. The
  file that held them says so at the top rather than quietly changing.
- **`src/scripts` still mounts**, and that is deliberate for now: every example
  in this repository is built that way, and a mount that sets `Source` on the
  instance it creates is the same instance either way. What changes is that the
  file is no longer the only way to have one.
- **`luaug build` and hot reload still work on the file path.** A script created
  in the editor and saved into a scene is carried by the scene, and a script
  under `src/` is carried by the mount. Neither knows about the other, which is
  what makes this a widening rather than a migration.
- **What is not answered here**: whether `src/scripts` eventually goes away, and
  what a script's `Source` means for the sandbox's compile budget on a world
  holding a thousand of them. Both are real and neither blocks anything today.
