# Scripts, modules and requires

LuauG has two kinds of Luau file and the difference is where the file lives, not
what it contains.

- **An entry script** is any `.luau` file under `src/scripts/`. The engine
  mounts it as a `Script` instance and runs it.
- **A module** is every other `.luau` file, canonically under `src/shared/`. It
  never appears in the tree; you `require` it by path.

There are no `ModuleScript` instances and no `require(instance)`. Modules are
real files on a real filesystem, which is what makes the analyzer able to follow
a require and give you types on the other side of it.

## Mounting

At boot, every file under `src/scripts/**/*.luau` becomes a `Script` under
`game:GetService("ScriptService")`, and each subdirectory becomes a `Folder`. So
`src/scripts/systems/spawn.luau` is a `Script` named `spawn` inside a `Folder`
named `systems`.

Each entry script starts on **its own coroutine**, deferred, in deterministic
path-sorted order. `DataModel.Loaded` fires once every entry script has had its
first resumption — so a `game.Loaded:Connect` written at file scope does run,
and a handler on it observes a fully booted world rather than a half-built one.

An error in one script's coroutine kills **only that coroutine**. The traceback
goes to the console and to `DebugService.MessageOut`; every other script carries
on.

Dynamic script creation is not supported: `Instance.new("Script")` raises.

## The script global

`script` is the `Script` instance the running file was mounted as. It has a
`Instance.Name`, a `Instance.Parent` and attributes like any other instance, and
attributes are the idiomatic way to give one script a knob:

```luau
--!strict
local speed = script:GetAttribute("Speed") :: number? or 12
```

## Script.Enabled

`Script.Enabled` is read at boot. A script whose `Enabled` is `false` when the
world is built is still mounted and still appears in the tree, but no coroutine
is created for it.

**Writing `Enabled` after boot has no effect in v1.** It neither stops a running
coroutine nor starts a script that did not run. The property carries that limit
in its own documentation rather than being left with no stated behaviour,
because what sets it is build configuration and the hot-reload world restart —
and both act before the boot they apply to. Re-running a script is what the
restart is for.

## Requiring a module

```luau
--!strict
local Greeting = require("@shared/greeting")

print(Greeting.forPlayer("world"))
```

`@shared` is an alias declared in the project's `.luaurc`, pointing at
`src/shared`. Requires are **by string**, resolved as real paths, with standard
Luau semantics:

- one evaluation per module per VM;
- cyclic requires behave per the language specification;
- a module that errors propagates to its requirer, and **the failure is cached**
  — the second require of a broken module raises the same error without
  re-running it.

A module returns a value, and the idiomatic value is a table of functions:

```luau
--!strict
local Greeting = {}

function Greeting.forPlayer(name: string): string
    return `Hello, {name}`
end

return Greeting
```

Note the casing: `Greeting.forPlayer` is camelCase because it is reached through
a **module**, not through an object. That rule is uniform across the whole API
and has [a page of its own](manual:why/casing).

## Every file is strict

Every `.luau` file in a LuauG project begins with `--!strict`, and the templates
and the CLI both assume it. It is not a style preference — a fully typed API
surface only pays off if the code using it is checked, and `luaug check` runs
the analyzer with the engine's generated definitions so a wrong property name is
an error before the engine ever runs.

## Reserved names

`src/client/` and `src/server/` are **reserved** directory names, and
`Enum.RunContext` is a reserved enum with `Client` and `Server` items. Neither
does anything in v1 — they are held for the multiplayer phase so that the
eventual split does not have to rename anybody's directories. The CLI warns if
those directories exist.

## Where to look next

- [Anatomy of a project](manual:get-started/project-anatomy) — the whole tree
- [What a script may do](manual:concepts/sandbox) — the global environment, in
  full
- [Hot reload](manual:guides/hot-reload) — what happens to all this when a file
  is saved
