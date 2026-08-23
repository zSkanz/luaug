# Hot reload

`luaug dev` runs the project with a watcher attached. Save a `.luau` file and
the world is rebuilt from source in **under half a second**, with the window,
the GPU resources, the imported assets and the streamed chunks untouched.

```bash
luaug dev
```

## What actually happens

A reload is a **fast world restart**, not a patch: the game VM is torn down and
a fresh one is built. Nothing in it survives by default.

The order matters and it is not the obvious one:

1. The state bag and the preserved instances are captured from the **old**
   world.
2. A **new** world is booted alongside it.
3. Only on a successful boot is the state restored into it, the frame loop
   pointed at it, and the old world destroyed.

So a reload that fails to boot destroys the half-built new world and **leaves
the previous one running untouched**. A syntax error costs you nothing.

## Keeping something across a reload

Two mechanisms, and they answer different questions.

### A tag, for an instance

```luau
--!strict
character:AddTag("PreserveOnReload")
```

The instance is captured as a **description** — its class, its name, its place in
the tree, its writable properties, its attributes, its tags, and its whole
subtree — and rebuilt in the new world **before the new entry scripts run**. So
a script that looks for what it left behind finds it already there.

A tag on a descendant of an already-tagged instance is redundant and is not
captured twice. An instance under `ScriptService` is skipped: its source is what
rebuilds it.

Properties that reference *other* instances are dropped rather than guessed at,
and the number dropped is counted, so the first class to declare one is a number
that moved rather than a silent nil.

### A state bag, for values

```luau
--!strict
local HotReloadService = game:GetService("HotReloadService")

HotReloadService.PreReload:Connect(function()
    HotReloadService:SaveState("spawn", { at.x, at.y, at.z })
end)

local saved = HotReloadService:LoadState("spawn")
local start = if HotReloadService:IsReload() and typeof(saved) == "table"
    then vector.create(saved[1] :: number, saved[2] :: number, saved[3] :: number)
    else vector.create(0, 5, 0)
```

`HotReloadService.SaveState` accepts nil, booleans, numbers, strings, buffers and
tables of those. A function, a thread, an **Instance** or a cyclic table raises.

> **A `vector` does not survive.** Save three numbers.

`HotReloadService.LoadState` gives back a fresh copy in the new VM; mutating it
changes nothing a later reload sees.

`HotReloadService.IsReload` is a **method**, not a property — a boolean property
may not carry an `Is` prefix under this API's own naming rules, and the rules
were right.

### The two phases

`HotReloadService.PreReload` fires on the **outgoing** world, before anything is
torn down. It is the last moment `SaveState` can be called and have the value
survive, and the reload waits for its handlers.

`HotReloadService.PostReload` fires on the world the reload just built, after
every entry script's first resumption and with preserved state already in place.

## Development builds only

`HotReloadService` carries the **Development builds only** badge: it is compiled
out of a shipping build entirely. A script that reaches for it unconditionally in
a shipped game gets nothing, so guard:

```luau
--!strict
local ok, service = pcall(function(): Instance
    return game:GetService("HotReloadService")
end)
```

## What triggers a reload

Any `.luau` file under `src/` or `tests/` changing content. The watcher does not
trust its own events — a save fires several of them, and on some platforms it
does not see into subdirectories at all — so an event only marks the tree dirty,
and after a short debounce the source set is rescanned and **diffed**.

An editor that touched a file without changing it produces no reload.

## Where the parts live

The dev server is a small process that `luaug dev` starts; the **engine
connects out to it** as a WebSocket client. Only the server listens — the engine
opens no port in any build, which is a security property rather than an
arrangement of convenience.

That is also why the same server can relay to other clients: a gate test, the
overlay console, a tool.

## What does not hot-swap yet

**Assets.** The protocol reserves an asset-changed message and the engine answers
it with "not implemented" rather than ignoring it — a caller that gets silence
cannot tell "not yet" from "lost". Changing a texture means restarting.

## Where to look next

- [Scripts, modules and requires](manual:concepts/scripts)
- [The luaug CLI](manual:get-started/cli)
- [`HotReloadService`](api:HotReloadService)
