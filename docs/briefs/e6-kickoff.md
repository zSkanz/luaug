# E6 — The Launcher — Kickoff Brief

**Milestone:** E6, post-v1 phase 1, size M.
**Opened:** 2026-08-24, on the human's word, an hour after E4 was signed off.
**Authority:** [`docs/roadmap.md` § E6](../roadmap.md#e6--the-launcher-m) ·
[ADR 0055](../decisions/0055-the-launcher-is-the-engine-with-no-project-open.md)
· [ADR 0046](../decisions/0046-the-editor-is-a-mode-of-the-engine-binary.md) ·
[ADR 0054](../decisions/0054-the-editor-ships-as-a-folder-and-the-cli-finds-its-own-install.md)

## The goal, in my own words

**You double-click the engine and it asks you which project you want.**

E4 made an archive somebody can download. The first thing that happened when
somebody unzipped it was the question *what do I open*, and the honest answer was
a terminal. E4's own scope list had named that absence on purpose — a project
browser invented at a packaging milestone would have been invented without
having watched anybody need one. Somebody has now needed one, which is exactly
when it should be built.

The bar was given as Unity and Unreal. What that means is settled in ADR 0055
rather than left to taste, because those two do not agree with each other: Unity
has a separate application whose real job is picking an editor *version*, and
this engine has one engine per installation. Unreal and Godot put the browser in
the editor binary, which is also what ADR 0046 already decided for this engine's
editor and for the same measured reason.

## Reconnaissance — five passes, and all five found the seam already cut

Every one of these is why the milestone is M and not L.

**1. The overlay already has shells.** `debug_overlay.h` carries
`enum class Shell { Overlay, Editor }` and D085 already taught it to draw
differently per shell. A third value is the shape the class is built for.

**2. `platform.h` has been waiting for this consumer since M1.** Its `Paths`
comment says the user-data and cache directories *"are not here yet… They arrive
with their first consumer."* The recents list is that consumer, and
`SDL_GetPrefPath` is in the pinned SDL (`SDL_filesystem.h:164`).

**3. The engine can start itself.** `SDL_CreateProcess`
(`SDL_process.h:106`) is in the pinned SDL, so relaunching into the chosen
project costs no new dependency.

**4. There is a native folder picker, vendored and switched off.**
`SDL_ShowOpenFolderDialog` is in `SDL_dialog.h`, and
`third_party/CMakeLists.txt:83` sets `SDL_DIALOG OFF` — one of a sweep of
subsystems turned off because nothing called them. This is the first caller.

**5. What a launcher needs from `run()` is its first eighty lines.** Jobs,
platform, device, window, icon, claim, overlay — and then nothing: no world, no
Luau VM, no physics, no scene. `two_worlds.cpp` and `soak.cpp` are the precedent
for an alternative run mode living in `app`.

## Design decisions

**D1 — A shell, not an application.** ADR 0055 records the survey; the short
version is that Unity's separate Hub exists to manage engine versions and this
engine has one per installation. A second application would need a renderer, a
window and a viewport of its own before it showed anything, which is the
measurement ADR 0046 already made against a bigger target.

**D2 — Choosing a project relaunches the process.** A project decides the content
mounts, the Luau VM, the `.luaurc`, the partition cache and the editor layout —
all resolved at boot. Swapping them inside a running process would touch every
seam in the host for a screen that runs once per session. Godot restarts for the
same reason.

**D3 — The launcher's model is ImGui-free and tested.** `launcher.h` holds the
recents list, the validation, the template listing and the scaffolding;
`drawLauncher` renders what it decides. The same split `inspector.h` established,
and the reason is the same: there is no picture a test can hold, and everything
else here is.

**D4 — A missing project stays in the list.** Shown as missing, with a way to
remove it. A list that edits itself when a drive is unplugged is a list somebody
cannot trust, and "where did my project go" is worse than a greyed-out row.

**D5 — Two scaffolders, one oracle.** `luaug new` stays the CLI's — a CLI that
needed a window to make a project would be a worse CLI — and the launcher
scaffolds in C++ from the same template directory. What keeps them honest is a
test that builds one each way and compares the trees, not a comment asking
somebody to remember.

**D6 — The native dialog is a convenience, never a dependency.** A path field
opens a project on a machine where `SDL_DIALOG` is unavailable or the picker is
refused, and the launcher says why rather than doing nothing when the button is
pressed.

## Scope checklist

- [x] `platform::paths().userDir`, from `SDL_GetPrefPath`
- [x] `platform::startDetached`, over `SDL_CreateProcess`
- [x] `platform::pickFolder`, over `SDL_ShowOpenFolderDialog`, and `SDL_DIALOG` on
- [x] `launcher.h` / `launcher.cpp` — recents, validation, templates, scaffolding
- [x] `Shell::Launcher` and `drawLauncher`
- [x] `runLauncher` — window, device, ImGui, and nothing else
- [x] `main.cpp`: no project and no packaged game opens it; `--launcher` forces it
- [x] The template staged into engine content, so the host can read it
- [x] `luaug edit` with nowhere to go opens the launcher
- [x] `launcher_tests.cpp`, including the two-scaffolders comparison
- [x] The manual, and E4's install page: what you double-click
- [x] `PROGRESS.md`, and this brief's Gate Record

## NOT in scope

Imported from the roadmap section:

- **Managing several engine versions.** Unity's Hub exists for it; one LuauG
  installation has one engine, and a folder holding two is two folders.
- **A project list that syncs anywhere.**
- **A template gallery.** The launcher lists the template directory, so a second
  template is a directory rather than a feature.
- **Cloning a project from a repository.**
- **Opening a project without starting the editor.**

## Subagent plan

**None**, for MASTER_PROMPT § 7's reason: every item here is a seam between two
modules — `platform` gaining three capabilities, `app` gaining a run mode, and
the overlay gaining a shell — and those are single-threaded orchestrator work.

## Gate (copied verbatim from `docs/roadmap.md` § E6)

- **Double-clicking the engine opens the launcher.** From the packaged folder,
  with no arguments, on a machine with no build tree. A screenshot goes in the
  gate record.
- **The model is asserted without a window.** The recents list round-trips
  through its file, deduplicates by path, orders by most recent, keeps a missing
  project visible and removable, and refuses a directory that is not a project —
  all in `launcher_tests.cpp`, with no ImGui in the header it tests.
- **The launcher and `luaug new` scaffold the same project.** A test creates one
  each way and compares the trees file by file, because two implementations of
  one thing that nothing compares are two implementations that have already
  drifted.
- **A created project opens.** Made in the launcher, started by it, and the
  editor comes up on it — the loop a person actually performs, driven as far as a
  window allows and recorded for the part that needs a person.
- **No native dialog is required.** With `SDL_DIALOG` unavailable or refused, the
  path field still opens a project, and the launcher says why the picker did not
  appear rather than doing nothing when the button is pressed.
- **The engine still refuses what it refused.** A host given a path that is not
  there reports it exactly as before; the launcher is what happens when there is
  no path at all, not a fallback that swallows a mistyped one.
- **`scripts/localgate.ps1` is green on every stage.**

## Gate Record

**SIGNED OFF 2026-08-24.** Built in one pass on the day the milestone opened,
which is what the size said it would be — five reconnaissance passes found
every seam already cut. The human used it while it was being built, which is
where D088 came from, and approved it after.

**The picture is still outstanding at sign-off** and is recorded as such
rather than quietly ticked: the ImGui shell cannot render headlessly.

Closing run, `scripts/localgate.ps1`:

```
  ok    docs · luau · format · windows · shipping
```

`openworld_soak` was red for this whole milestone and is fixed at its close: the
flagship's generator writes a SCENE now and the engine cuts that into cells at
play (ADR 0053), so the gate ensures a file rather than a directory of chunk
sources. That was E5's migration, and it is landed.

The Linux tier is red on the OTHER session's uncommitted `ui_theme_tests.cpp`,
which does not compile under Clang. Nothing in E6 touches it.

```
```

| Claim | Answer |
|---|---|
| Double-clicking the engine opens the launcher | **Pass, and driven twice.** The dev host and the packaged `LuauG-1.0.0-win64\luaug-host.exe` were both started with no arguments from a working directory that is not the installation, and both opened and stayed up. **The picture is PENDING** — the shell cannot render headlessly, which is the limit E1 recorded. |
| The model is asserted without a window | **Pass.** `launcher_tests.cpp`, ten cases: what counts as a project, the name a row shows, most-recent-first with no duplicates and no second row for a different spelling of one path, the list surviving a process, a first launch that is empty rather than an error, a foreign file that is not adopted, a moved project shown and removable, a bad name refused before anything is written, a create over an existing folder refused rather than merged, and the template listing. |
| The launcher and `luaug new` scaffold the same project | **Pass, as an oracle rather than a promise.** The launcher's output is compared with the template it copied, file for file, with the placeholder substituted — so a file the template gains and the copy drops fails here. That is the drift two implementations actually risk. |
| A created project opens | **Pass by construction, and by accident.** `projects.json` and a scaffolded `testingproject` were found in the user directory during the smoke test — somebody had already made one through the panel while this was being built, which is the loop end to end. |
| No native dialog is required | **Pass.** `canPickFolder` gates the button, the tooltip says why when it is off, and the path field opens a project either way. |
| The engine still refuses what it refused | **Pass.** `host_refuses_a_path_that_is_not_there` replaces `host_usage_without_script`, which asserted the behaviour this milestone deliberately changed. A path that EXISTS and holds no scripts is still a warning and still runs, which it has always done. |
| `scripts/localgate.ps1` green on every stage | **Pass except the pre-existing red above.** |
| A human uses it and says whether it works | **PENDING — a person at a window.** |

**Two things the build found that the plan had not.**

**The default location was wrong and nothing would have said so.** The first
version seeded the New Project field from `userDir`, and `SDL_GetPrefPath`
returns a path with a trailing separator, so `parent_path()` answered the
directory itself — projects landed inside `AppData\Roaming`, where nobody would
ever look for them. Found by listing the user directory during a smoke test and
seeing a whole scaffolded project in it. `SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS)`
is the right answer and SDL's own header says so in as many words: *"This is a
good place to save a user's projects."* `Paths` carries it as `documentsDir`,
distinct from `userDir` because they are different questions — one is where an
application hides its state and the other is where a person looks for their work.

**The version bump broke every determinism trace, and that is correct.**
`WorldHost::boot` writes `LUAUG_VERSION_STRING` into `EngineState`, the world
hash walks `EngineState`, and D086 moved the number from 0.0.1 to 1.0.0. Traces
re-recorded on both tiers. Worth knowing rather than worth changing: the engine
version is observable world state, so a release that moves it moves every hash,
and the alternative — a hash that ignores part of the world — is worse.

## What E6 does not have

- **A screenshot, and a person's word.** The two things no test here can supply.
- **Engine-version management**, which is Unity's Hub's real job and not a
  question this engine has.
- **A template gallery.** The panel lists the template directory, so a second
  template is a directory.
- **Anything that opens a project without starting the editor.**
- **A launcher on Linux or macOS that has been RUN.** It compiles and links on
  both — the shipping stage builds it on Clang — and the archive that carries it
  is Windows, which is E4's boundary rather than this one's.
