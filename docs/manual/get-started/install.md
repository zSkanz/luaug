# Install and toolchain

LuauG is built from source. There is one bootstrap script, it is idempotent, and
it refuses to report success when a step failed — a bootstrap that lies
resurfaces later as a confusing build error.

## What you need first

| | Windows | Linux / macOS |
|---|---|---|
| A C++20 compiler | Visual Studio 2022 with the C++ workload | Clang |
| CMake and Ninja | Bundled with Visual Studio | From your package manager |
| Git | | |

**On Windows, run everything from a Developer Shell.** The build presets use the
Ninja generator and expect `cl`, `cmake` and `ninja` already on `PATH`. If a
configure step says it cannot find CMake, the fix is almost never to install
CMake — it is to run `VC\Auxiliary\Build\vcvars64.bat` first. The bootstrap
detects this and prints the exact command.

## Bootstrap

```powershell
scripts\bootstrap.ps1
```

```bash
./scripts/bootstrap.sh
```

It does four things:

1. **Sets `LUAUG_BUILD_ROOT`** — builds are always out of the source tree, and
   the script refuses a value inside the repository.
2. **Checks the native toolchain** and tells you what is missing.
3. **Installs the pinned Luau toolchain** with rokit: `lute`, `luau-lsp` and
   `stylua`, at exactly the versions this engine is tested against.
4. **Generates the Lute type definitions**, without which the analyzer cannot
   resolve a single `@std` require.

Versions are pinned rather than floating. Upgrading one is a deliberate,
reviewed change, not something a fresh checkout does to you.

## Build

```powershell
cmake --preset win-msvc-dev
cmake --build --preset win-msvc-dev
ctest --preset win-msvc-dev
```

> **On Windows, build with `chcp 65001` first.** CMake writes Ninja's dependency
> prefix as UTF-8; a localised compiler emits that string in the console code
> page, the two never match, and Ninja then records **no header dependencies at
> all**. Every incremental build silently reuses objects compiled against an
> older header, and the symptom is a crash in code that has nothing wrong with
> it.

## The CLI

`luaug` is a wrapper around the pinned Lute runtime rather than a compiled
binary:

```powershell
scripts\luaug.ps1 --version
```

```bash
./scripts/luaug.sh --version
```

Put the `scripts` directory on your `PATH`, or make an alias, and the rest of
this manual's `luaug …` lines work as written.

## Your editor

The project templates ship a VS Code configuration that points `luau-lsp` at the
generated engine definitions, wires the `@shared` alias, and turns the Roblox
platform mode **off**. Two extensions are recommended and both are named in the
template: the Luau language server, and the formatter.

With that in place, `part.Anchorred = true` is a red squiggle rather than a
runtime surprise.

## Check it works

```bash
luaug new hello
cd hello
luaug dev
```

A window opens. Editing `src/scripts/main.luau` and saving rebuilds the world in
well under a second.

## Running the gates

Before you push a change to the engine itself:

```powershell
scripts\localgate.ps1
```

Roughly ninety seconds warm, and it runs everything that can run locally —
documentation checks, the Luau gates, the Windows build and tests, and the Linux
tier in a container. `-Only <stage>` runs one while you iterate.

The Linux stage is not redundant with the Windows one: Clang diagnoses things
MSVC does not, and warnings are errors there.

## Where to look next

- [Your first world](manual:get-started/first-world)
- [Anatomy of a project](manual:get-started/project-anatomy)
- [The luaug CLI](manual:get-started/cli)
