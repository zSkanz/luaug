# The luaug CLI

Eight commands. Unknown flags are **refused**, not ignored.

```bash
luaug --help
luaug --version
```

## The commands

### luaug new

```bash
luaug new my-game
luaug new my-game --template=starter
```

Scaffolds a project. The name must be letters, digits, underscores and hyphens.
`starter` is the only template.

### luaug dev

```bash
luaug dev
luaug dev examples/06-scene
luaug dev --port=4700
luaug dev --headless
```

Runs the project with a watcher attached: a saved `.luau` file rebuilds the
world. Defaults to the current directory.

`--port` overrides `[dev] port`. `--headless` runs without a window, which is
what a gate does.

A project that mounts **no entry scripts** is refused rather than started —
otherwise it would attach, watch and reload an empty world forever while
reporting success.

### luaug edit

```bash
luaug edit
luaug edit examples/06-scene
```

Runs the project with the editor in place of the debug overlay. No flags: an
editor needs a window, so there is no headless form.

### luaug test

```bash
luaug test
luaug test tests/conformance/tween
luaug test --junit=results.xml --report=report.json
```

Runs the conformance suite on the headless engine with the null renderer.
Defaults to `tests/conformance`. TAP goes to standard output either way;
`--junit` adds XML.

Exits non-zero for a failure, for **zero cases**, and for a report it could not
read.

### luaug check

```bash
luaug check
luaug check --definitions=runtime/types/engine.d.luau
```

The analyzer with the engine's generated definitions, plus the formatter in
check mode. `--definitions` takes a comma-separated list.

An empty check — zero files collected — is a **failure**.

### luaug fmt

```bash
luaug fmt
```

Formats every Luau file.

### luaug build-assets

```bash
luaug build-assets
luaug build-assets --verify --output=dist/content.lpack
```

Compiles the content directory into a pack and a manifest. `--verify` builds
twice and compares byte for byte.

### luaug build

```bash
luaug build
luaug build --target=win64 --output=dist/win64 --force
```

The distributable folder. `win64` is the only target, and building it requires
running on Windows. `--force` permits clearing an output directory the tool did
not create.

## Exit codes

| Code | Means |
|---|---|
| 0 | Success. |
| 1 | The work failed: a test failed, the analyzer complained, a build broke. |
| 2 | You asked for something impossible: an unknown command, an unknown flag, an unsupported target, a path that is not a project. |
| 3 | The tool could not load its own message catalog. |

The split between 1 and 2 is the useful one: **2 is a usage error and 1 is a
result**. A script driving the CLI can tell "I typed it wrong" from "the tests
failed".

## The engine binary

The CLI launches a separate host executable. Running it directly is what a gate,
a benchmark or a capture does:

```text
luaug-host [script.luau | project-dir]
  [--headless --frames=N --exit] [--width=N --height=N]
  [--screenshot=FILE] [--frame-stats] [--rhi=NAME]
  [--quality=low|medium|high|ultra --render-scale=F
   --shadow-resolution=N --shadow-cascades=N --shadow-distance=F
   --light-budget=N --[no-]bloom --[no-]ambient-occlusion
   --[no-]anti-aliasing --[no-]auto-exposure]
  | --run-tests=DIR | --replay=DIR [--record-replay] | --version | --help
```

Four refusals worth knowing, all of them exit 2:

- `--headless` needs `--frames=N`, or nothing would ever stop it.
- `--screenshot` needs `--headless`.
- `--capture-out` needs the capture backend.
- The editor needs a window.

**Given no arguments at all**, the host looks for a `game/` directory beside its
own executable and mounts it. That is what makes a packaged build
double-clickable — see [Shipping a game](manual:guides/shipping).

## What the CLI is

A wrapper around the pinned Lute runtime rather than a compiled binary. A
downloaded LuauG carries that runtime and a `luaug.cmd` beside it, so putting the
folder on your `PATH` is the whole installation; a source tree has
`scripts/luaug.ps1` and `scripts/luaug.sh` doing the same job for the same
reason. Either way, every command on this page works as written.

**It finds its own engine.** The host it launches is the one in the installation
it belongs to, whatever directory you are standing in — or the one in your build
tree, when you have one. `LUAUG_HOST` overrides both, and `luaug build`
deliberately ignores that override: what you pointed a dev server at must not
decide what your players get.

## Where to look next

- [Anatomy of a project](manual:get-started/project-anatomy)
- [Hot reload](manual:guides/hot-reload)
- [Shipping a game](manual:guides/shipping)
