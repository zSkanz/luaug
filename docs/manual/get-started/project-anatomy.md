# Anatomy of a project

A new project, from `luaug new` or from the editor's project browser, is two
things: a `luaug.toml` and a scene.

```text
my-game/
├─ luaug.toml                   what this project is
├─ content/
│  └─ scenes/main.scene.json    the world, its scripts included
└─ .luaug/                      generated, gitignored
```

The starter scene holds a small level and its code, each script inside the
instance it belongs to (ADR 0092):

- the spinner's own `Script` turns it;
- a `ModuleScript` in `ReplicatedStorage` holds the settings it requires;
- a `Script` in `ScriptService` greets you when you press Play.

A directory is a project when it holds a `luaug.toml` **or** a `src/scripts`
directory. A project without a `luaug.toml` is legal: it takes every default.

## Code in files, when you want it

A project can also keep code in files, edited in VS Code, kept in git and
hot-reloaded by `luaug dev`. None of this is required, and the two ways mix in
one project:

```text
my-game/
├─ .luaurc                  strict mode, and the require aliases
├─ src/
│  ├─ scripts/main.luau     a Script, mounted at boot
│  └─ shared/greeting.luau  a module, reached by require
├─ assets/i18n/en.json      the game's own strings
└─ tests/example.test.luau
```

- **Every `.luau` file under `src/scripts/`** is mounted as a `Script` under
  `ScriptService` at boot, and each subdirectory becomes a `Folder`.
  `src/scripts/systems/spawn.luau` becomes a `Script` named `spawn` inside a
  `Folder` named `systems`. The file is that script's source, so the scene does
  not write it.
- **Every other `.luau` file is a module**, reached by `require` with a path and
  never in the tree.

See [Scripts, modules and requires](manual:concepts/scripts) for both.

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
| `types/scene.d.luau` | The scene's own tree, typed, so `workspace.Level.Ground` type-checks. |
| `editor-layout.v3.ini` · `editor.json` | Editor panel layout and last-open scene. |

## tests/

`tests/**/*.test.luau` is your project's own suite, run by the pure runner.
`tests/conformance/**/*.spec.luau` is the engine's shape, run against a headless
engine. See [Testing](manual:guides/testing).

## Reserved names

`src/client/` and `src/server/` are reserved directory names, and
`Enum.RunContext` is a reserved enum. Neither does anything yet: a script runs
the same whatever its `RunContext` says. The names are held so that a future
client and server split does not have to rename anybody's directories. The CLI
warns if those directories exist.

## Where to look next

- [Your first world](manual:get-started/first-world)
- [The luaug CLI](manual:get-started/cli)
- [Content and asset URNs](manual:assets/content)
