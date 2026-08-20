# examples/ — Always-Runnable Milestone Artifacts

Numbering matches `docs/roadmap.md`; every example is a real project plus an
automated headless gate script (screenshot/capture + asserted behavior):

| Example | Born in | Proves |
|---|---|---|
| `boot` | M0 | the host boots a sandboxed VM, runs Luau, and routes output through the i18n'd log (unnumbered: numbering starts at the first milestone with a window) |
| `00-clear` | M1 | window, RHI clear, debug draw, Luau-driven visuals, screenshot harness |
| `01-instances` | M2 | Instance tree over ECS, deferred signals, task, 500 scripted cubes |
| `02-meshes` | M4 | glTF loading, PBR, shadows, camera, day/night slider |
| `03-physics-playground` | M5 | Jolt bodies, contacts→Touched, CharacterBody, third-person camera |
| `04-obby` | M6 | IAS input, UI, tweens, audio, minimal animation — playable end-to-end |
| `05-streaming` | M7 | chunk streaming, floating origin, LOD/HLOD, memory ceilings |
| `10-open-world` | M8 | the v1 flagship: streamed open world + character + day/night + hot reload |

Assets used by examples must be permissively licensed and recorded in
`THIRD_PARTY_NOTICES.md`. Keep binary assets tiny until the git-LFS ADR (M4);
the streaming example generates its world procedurally for this reason.

## Running one by hand

Every example folder carries a `run.bat`. It resolves the host binary under
`LUAUG_BUILD_ROOT` — builds are out-of-tree (R14), so the path is not something
worth remembering — and passes any extra flags straight through:

```
examples\01-instances\run.bat
examples\01-instances\run.bat --headless --frames=120 --exit --screenshot=out.png
```

It works from any working directory, and `LUAUG_PRESET` selects a different
build profile (`win-msvc-debug`, `win-msvc-shipping`); it defaults to
`win-msvc-dev`. If the binary is missing the script says which preset to build
rather than failing with a path.

These are a convenience for humans and nothing more: the automated gates invoke
`luaug-host` directly from CMake, so no gate depends on a shell script. A
`run.bat` that rots is a broken convenience, not a red milestone.

**Adding one to a new example.** Copy the `run.bat` from the example whose shape
matches and change only its last statement — that line is the single place the
two supported project shapes differ (api-design.md §4):

| Shape | Copy from | Last statement |
|---|---|---|
| One file mounted as one `Script` | `00-clear` | `"%LUAUG_HOST%" "%~dp0init.luau" %*` |
| A project directory whose `src/scripts/**/*.luau` become entry `Script`s | `01-instances` | strip the trailing backslash from `%~dp0`, then pass the directory |

The trailing-backslash strip in the directory form is not decoration: `%~dp0`
always ends in one, and `"...\"` escapes the closing quote and corrupts the
argument.
