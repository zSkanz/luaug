# Scripts, modules and requires

A script is an **instance**. Its code is a property, `Source`, and it lives
wherever it is put in the tree, the way a part does (ADRs 0050, 0092). There are
two kinds:

- **`Script` runs.** Every enabled `Script` in the world starts on its own
  coroutine when the world does. One that is not in the world does not run,
  which is the difference between storing a script and using it.
- **`ModuleScript` is required.** It never starts by itself. `require(module)`
  evaluates it once, and every later require of the same instance gives back the
  same value.

Both are created the way every other class is: from the editor's insert menu,
or with `Instance.new("Script")` and `Instance.new("ModuleScript")`. A script
made in the editor is saved in the scene with its `Source`, like every other
property.

## Where a script can live

Anywhere. A script inside a part, a model, a folder or a service is saved with
that thing and copied with it. `Clone` copies its `Source`, and a stamp carries
the scripts inside it.

```luau
--!strict
local door = Instance.new("Part")
door.Name = "Door"
door.Parent = workspace

local logic = Instance.new("Script")
logic.Source = [[
    local door = script.Parent
    print(door.Name, "is ready")
]]
logic.Parent = door
```

**`script` is the instance that is running.** `script.Parent` is what it was put
in, which is the idiomatic way for a script to find the thing it drives. A
`ModuleScript` is reached the same way, by the tree:

```luau
--!strict
local holder = script.Parent :: Instance
local Inventory = require(holder:WaitForChild("Inventory") :: ModuleScript)
```

In a project, the scene's tree is typed from the scene itself
([ADR 0078](manual:why/reaching-children)), so `require(script.Parent.Inventory)`
type-checks there as well.

## Scripts from files: `src/scripts`

A project written in an outside editor, kept in git and hot-reloaded by
`luaug dev`, can still keep its code in files. At boot, every file under
`src/scripts/**/*.luau` becomes a `Script` under
`game:GetService("ScriptService")`, and each subdirectory becomes a `Folder`. So
`src/scripts/systems/spawn.luau` is a `Script` named `spawn` inside a `Folder`
named `systems`.

**The file is that script's source, and the scene does not duplicate it.** A
script mounted from a file is marked as mounted. The scene does not write it,
and the editor does not move it, because the file decides where it is.
Anything you put **inside** a mounted script in the editor is authored work,
and it is saved and kept. If the file disappears later, those children are kept
in a `Folder` of the same name rather than lost.

Both ways can be used in one project: files for the systems you edit in VS Code,
and instances for the logic that belongs to one object in the scene.

## Starting and stopping

Each script starts on **its own coroutine**, deferred, in the tree's document
order. `DataModel.Loaded` fires once every script has had its first resumption,
so a `game.Loaded:Connect` written at file scope does run, and it observes a
fully booted world.

An error in one script's coroutine kills **only that coroutine**. The traceback
goes to the console and to `DebugService.MessageOut`, and every other script
carries on.

**`Enabled` decides whether a script's threads are resumed, and nothing else**
(ADR 0059):

- **false to true, while the game runs, starts that script** on a new coroutine,
  and its file scope runs against the world as it is now. Enabling a script that
  already ran runs its file scope again: a re-enable is a start, not a resume;
- **true to false stops resumption.** A thread already running continues to its
  next yield. Nothing it connected is disconnected, and nothing it built is
  undone. Its queued `task.defer`, `task.delay` and signal entries are dropped
  when they come up;
- both take effect at the next deferred drain, in document order, so a replay
  reproduces them;
- **while the editor is stopped**, `Enabled` is a scene edit. It decides what
  the next play starts.

Attributes are the idiomatic way to give one script a knob:

```luau
--!strict
local speed = script:GetAttribute("Speed") :: number? or 12
```

## Requiring a module

`require` takes either **a `ModuleScript`** or **a path to a file**.

```luau
--!strict
local Greeting = require("@shared/greeting")

print(Greeting.forPlayer("world"))
```

- **A `ModuleScript`** is found by the tree, as above. Only a `ModuleScript` can
  be required: a `Script` runs when the world does, and requiring one would run
  it a second time somewhere else.
- **A path** is resolved as a real file. `@shared` is an alias declared in the
  project's `.luaurc`, pointing at `src/shared`.

Both follow the standard Luau rules:

- one evaluation per module per VM;
- a cyclic require behaves as the language specifies;
- a module that errors propagates the error to its requirer, and **the failure
  is cached**: a second require of a broken module raises the same error
  without running it again.

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

## Every script is strict

Every script begins with `--!strict`, and the templates, the CLI and the editor's
script editor all assume it. It is not a style preference. A fully typed API
only pays off if the code using it is checked. The script editor checks as you
type, with Luau's own analyzer and the engine's definitions, and `luaug check`
does the same for files, so a wrong property name is an error before the engine
ever runs.

## Reserved names

`src/client/` and `src/server/` are **reserved** directory names, and
`Enum.RunContext` is a reserved enum with `Client` and `Server` items. Neither
does anything yet: a `Script` runs the same whatever its `RunContext` says. The
names are held so that a future client and server split does not have to rename
anybody's directories. The CLI warns if those directories exist.

## Where to look next

- [Anatomy of a project](manual:get-started/project-anatomy): the whole tree
- [What a script may do](manual:concepts/sandbox): the global environment, in
  full
- [Signals](manual:concepts/signals): events, `Collector` and `Promise`
- [Hot reload](manual:guides/hot-reload): what happens to all this when a file
  is saved
