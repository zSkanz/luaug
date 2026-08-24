# 0055 — The launcher is the engine with no project open

- Status: accepted
- Date: 2026-08-24
- Extends: 0046, 0054

## Context

E4 shipped an archive somebody can download, and the first thing a person did
with it was unzip it and ask what to open. The honest answer was "a terminal":
`luaug-host.exe` given no project prints a usage error and exits, and E4's own
scope list names the gap out loud — *"it does not contain a project browser… an
editor opened with no project would need one"*.

Every comparable tool has answered this, and they have not answered it the same
way:

- **Unity** has a separate application. The Hub is its own program, with its own
  window and its own update channel, and its main job is not really the project
  list — it is managing *which editor version* each project opens with.
- **Unreal** puts a project browser inside the editor binary. Run it with no
  project and it shows recent projects and templates, drawn with the same UI
  toolkit the editor is drawn with.
- **Godot** does the same in a smaller shape: the project manager is the same
  executable, and choosing a project starts that project.

**The version-management job is the one that makes Unity's shape necessary**, and
this engine does not have it. There is one engine in an installation, it is the
one beside the launcher, and a folder holding two LuauG versions is two folders.
A separate application here would buy the one thing the other two do not need it
for.

**And the decision is already made in spirit.**
[ADR 0046](0046-the-editor-is-a-mode-of-the-engine-binary.md) rejected a separate
process for the editor after measuring what it would cost: a second application
needs a renderer, a window and a viewport of its own before it shows anything.
A launcher is smaller than an editor and the arithmetic is the same one.

**What the reconnaissance found, and it is why this is affordable:**

- `DebugOverlay` already has a `Shell` enum with two values — the F3 overlay over
  a running game, and the editor's dockspace. A third is the shape the class is
  already built for.
- `platform.h`'s `Paths` says, in as many words, that the user-data directory
  *"arrive[s] with [its] first consumer"*. The recents list is that consumer, and
  `SDL_GetPrefPath` is in the pinned SDL.
- `SDL_CreateProcess` is in the pinned SDL too, so the engine can start itself
  with no new dependency.
- `SDL_ShowOpenFolderDialog` is in the pinned SDL and `SDL_DIALOG` is one of the
  subsystems `third_party/CMakeLists.txt` turns off. It was turned off because
  nothing called it.

## Decision

**The launcher is a third shell of the `luaug-host` binary, drawn in ImGui, shown
when the host is started with no project — and choosing a project starts the
editor as a new process.**

Four parts:

- **A shell rather than an application.** `Shell::Launcher` beside `Overlay` and
  `Editor`, drawn by the same overlay over the same window, with no world, no
  Luau VM and no physics behind it. A launcher that booted a simulation to show a
  list of folders would be absurd, so `runLauncher` is its own loop: window,
  device, ImGui, and nothing else.
- **Opening a project relaunches.** The chosen project is started as
  `luaug-host <path> --edit` through `SDL_CreateProcess`, and the launcher quits.
  A project decides the content mounts, the Luau VM, the `.luaurc`, the partition
  cache and the editor layout; swapping all of that inside a running process
  would touch every seam in the host for a screen that runs once. Godot restarts
  for the same reason, and the seam it avoids is real.
- **The recents list is per user, not per project.** `SDL_GetPrefPath` gives the
  directory `platform.h` has been describing since M1, and `projects.json` in it
  is the list. A project that has been moved or deleted is shown as missing and
  removable rather than silently dropped — a list that edits itself is a list
  somebody cannot trust.
- **Creating a project copies the template out of engine content**, so the
  launcher and `luaug new` scaffold from the same bytes. They are two
  implementations of "copy a tree and substitute the name", and **a test asserts
  they produce identical trees** rather than a comment asking somebody to keep
  them in step.

**`SDL_DIALOG` turns on**, which is a vendored subsystem gaining its first
caller rather than a new dependency (R5 is untouched: no version moves). The
launcher does not depend on it: a platform with no picker gets a path field and
a message, because a launcher that cannot be used without a native dialog is one
that cannot be used in a container.

## Consequences

**Double-clicking the engine does something.** That is the whole user-visible
change, and it is the one E4 could not make: the archive is now a thing you
unzip and open, rather than a thing you unzip and then read instructions about.

**The launcher's model is headless and tested, and the ImGui half draws what it
decides.** The same split `inspector.h` established and ADR 0046 made a
consequence: the recents list, the validation, the template listing and the
scaffolding are `launcher.h`, with no ImGui in it; `drawLauncher` renders them.
There is no picture a test can hold, and everything else here is.

**A second scaffolder exists, and it is watched rather than trusted.** `luaug new`
stays the CLI's, because a CLI that needed a window to make a project would be a
worse CLI. The duplication is a directory copy and one substitution; what makes
it safe is that both read the same template and a test compares their output.

**What this does not decide.** Managing several engine versions, which is the job
Unity's Hub exists for and which this engine does not have. A project list that
syncs anywhere. Templates beyond the one `luaug new` already has — the launcher
lists what is in the template directory, so a second one is a directory rather
than a decision.
