# Anatomy of a project

```text
my-game/
├─ luaug.toml               what this project is
├─ .luaurc                  strict mode, and the require aliases
├─ rokit.toml               the pinned toolchain
├─ stylua.toml
├─ .vscode/                 analyzer and formatter, preconfigured
├─ src/
│  ├─ scripts/main.luau     an entry Script
│  └─ shared/greeting.luau  a module
├─ content/                 art, audio, fonts, scenes
├─ assets/i18n/en.json      the game's own strings
├─ tests/example.test.luau
└─ .luaug/                  generated, gitignored
```

A directory is a project when it holds a `luaug.toml` **or** a `src/scripts`
directory. A project without a `luaug.toml` is legal — it just takes every
default.

## src/scripts is the tree

Every `.luau` file under `src/scripts/` is mounted as a `Script` at boot, with
subdirectories becoming `Folder`s. `src/scripts/systems/spawn.luau` becomes a
`Script` named `spawn` inside a `Folder` named `systems`.

Everything else is a **module**, reached by `require`, and never in the tree.
See [Scripts, modules and requires](manual:concepts/scripts).

## src/shared and the alias

`.luaurc` declares the aliases, and both the analyzer and the engine read them:

```json
{
  "languageMode": "strict",
  "aliases": { "shared": "src/shared" }
}
```

```luau
local Greeting = require("@shared/greeting")
```

Resolution order for a require: engine-provided `@` modules first; then `@self`,
meaning the requiring file's own directory; then the `.luaurc` aliases; then
`./` and `../` relative to the requiring file; then a bare specifier as a path
from the project root.

`.luau` is appended if absent, then `init.luau` is tried.

> **`.luaurc` takes no `$comment` key.** The runtime treats an unknown key as an
> error rather than ignoring it, so a comment there breaks requires.

## luaug.toml

```toml
[project]
name = "My Game"
id = "com.example.mygame"
version = "0.1.0"
icon = "branding/icon.ico"
scene = "scenes/main.scene.json"

[window]
title = "My Game"
size = [1280, 720]

[dev]
port = 4560

[assets]
content = "content"

[graphics]
quality = "high"
```

| Section | Keys |
|---|---|
| `[project]` | `name` (becomes the built executable's name), `id` (reverse-DNS; groups taskbar buttons on Windows), `version`, `icon`, `scene` |
| `[window]` | `title`, `size` |
| `[dev]` | `port` — default 4560 |
| `[assets]` | `content` — where the asset compiler reads from |
| `[graphics]` | The quality family. See [Graphics quality settings](manual:rendering/quality) |

The TOML subset is deliberately small: comments, tables, strings, numbers,
booleans and single-line arrays. A multi-line string, an inline table, an array
of tables or a date is an **error** rather than a silent misread.

Two things to know about it as it stands:

- **`[assets] content` is read by the asset compiler and not by the engine**,
  which mounts `content/` by name. Renaming it will compile from one place and
  mount another.
- **`[permissions]`, `[memory]` and `[build]` parse and are reserved.** Nothing
  reads them yet.

## content/

What `asset://` names. Meshes, textures, audio, fonts and scenes, in whatever
directory layout suits you — the URN is the path relative to this directory.

In development it is mounted directly, so a file dropped in is available with no
build step.

## .luaug/

Generated, gitignored, and safe to delete:

| Path | Is |
|---|---|
| `types/engine.d.luau` | The engine's type definitions, for the analyzer. |
| `content.lpack` · `content.manifest.json` | The compiled content. |
| `content/**.lchunk` · `content.chunks.json` | Compiled streaming chunks. |
| `editor-layout.v2.ini` · `editor.json` | Editor panel layout and last-open scene. |

## tests/

`tests/**/*.test.luau` is your project's own suite, run by the pure runner.
`tests/conformance/**/*.spec.luau` is the engine's shape, run against a headless
engine. See [Testing](manual:guides/testing).

## Reserved names

`src/client/` and `src/server/` are reserved directory names, and
`Enum.RunContext` is a reserved enum. Neither does anything today — they are
held for the multiplayer phase so the eventual split does not have to rename
anybody's directories. The CLI warns if those directories exist.

## Where to look next

- [Your first world](manual:get-started/first-world)
- [The luaug CLI](manual:get-started/cli)
- [Content and asset URNs](manual:assets/content)
