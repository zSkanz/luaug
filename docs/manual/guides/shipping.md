# Shipping a game

```bash
luaug build
luaug build --target=win64 --output=dist/win64
```

## The product is a folder

Not an installer, not a single self-extracting executable:

```text
dist/win64/
├─ MyGame.exe        the engine, under your game's name and wearing its icon
├─ content/          the engine's own: catalog, shaders, fonts, runtime modules
└─ game/             your project
   ├─ luaug.toml
   ├─ .luaurc
   ├─ src/
   └─ .luaug/        the compiled content pack and its manifest
```

**The player's executable finds its game by convention**: given no arguments,
the host mounts the `game/` directory beside itself. A convention rather than a
configuration file, because a configuration file is a second thing that can go
missing.

Nothing else about a packaged run differs from a development one — same binary,
same frame loop, same flags.

## What the build does, in order

1. Refuses a target other than `win64`, and refuses to run on a non-Windows
   host. A folder built on Linux would carry a Linux binary named `.exe`: an
   artifact that cannot run and cannot be told apart from one that can.
2. **Builds the content pack first**, because the folder ships the built pack
   rather than the source art — building it afterwards would copy yesterday's.
3. Refuses an output directory that exists and is not one of its own, unless you
   pass `--force`. It writes a marker file so it can tell.
4. Copies the host under your `[project] name`, and the engine's own content
   beside it.
5. Copies the project. **If no pack was built**, it copies the loose `content/`
   and `assets/` trees instead — shipping both would double the size of every
   game, and shipping neither would be a game with no art.
6. Applies `[project] icon` to the executable and then **verifies it by reading
   the resource back out of the artifact**, rather than trusting the tool that
   wrote it.

## One target

`win64`, and an unsupported target exits with an error rather than producing a
folder that cannot run. That is the honest state of this release.

## It ships Luau source

The packaged game contains your `.luau` files as text.

That is a decision rather than an oversight, and it is worth knowing before you
ship:

- The module loader is defined in terms of source at a path. A bytecode pack
  would mean teaching `require`, the entry-script mount and the reload a second
  unit type.
- Hot reload replaces source and recompiles. A packaged build could skip that —
  and then **the packaged path and the development path stop being the same
  path**, which is what makes a shipped game debuggable.
- The benefit would be boot time and source concealment. Boot time is not a
  problem at these sizes, and **concealment is not a promise this engine can
  make honestly**: Luau bytecode is trivially decompiled.

So: **a player who receives your folder has your source.** Anything that must
not be in it — a key, a token, an endpoint you do not want found — belongs on
your backend and not in the game.

## Before you ship

```bash
luaug check
luaug test
luaug build-assets --verify
luaug build
```

`--verify` builds the content twice and compares byte for byte. If the two
differ, something in the pipeline is not deterministic, and it fails rather than
shipping a lottery.

Then run the built executable with no arguments and play it. The packaging gate
in this repository does exactly that, because "it built" and "it runs" are
different facts.

## Development-only services are gone

`HotReloadService` is compiled out. A script that reaches for it unconditionally
gets nothing:

```luau
--!strict
local ok, hotReload = pcall(function(): Instance
    return game:GetService("HotReloadService")
end)
if ok and hotReload ~= nil then
    -- development
end
```

`DebugService` remains, with its overlay compiled out — the methods become
no-ops, so debug drawing left in shared code costs nothing.

## Where to look next

- [The asset pipeline](manual:assets/pipeline)
- [The luaug CLI](manual:get-started/cli)
- [Talking to a backend](manual:guides/backend)
