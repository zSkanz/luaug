# E7 Kickoff — The Look

- Started: 2026-08-24
- Roadmap section: [docs/roadmap.md § E7](../roadmap.md#e7--the-look-m)
- ADR: [0056 — The shell has one theme, it is data, and it is square](../decisions/0056-the-shell-has-one-theme-and-it-is-square.md)

## Goal (restated)

Six milestones built an editor and a launcher and none of them ever decided what
either should look like, so `ImGui::StyleColorsDark()` decided — a palette drawn
for a translucent debug window over a running game — and ImGui's built-in
13-pixel bitmap face drew every word in both shells. This milestone makes the
look a decision: one theme as data, square everywhere, Inter, a palette that is
measured rather than argued about, and a launcher laid out for the person who
just unzipped the archive rather than for whoever wired it up.

The brief was given in three words and one shape: **clean, simple, professional**
— and **square**.

## Scope checklist (from roadmap)

- [x] A theme is data: `engine/app/ui_theme.h`, eleven palette tokens and one
      metrics table, from which `applyTheme` derives ImGui's sixty-odd slots.
- [x] Square, in all eleven of ImGui's rounding members, with a border colour
      that is load-bearing rather than decorative.
- [x] A palette measured at 4.5:1 (WCAG 2.1 AA) against every ground each token
      is drawn on — window, field, and raised surface.
- [x] Two themes, dark and light.
- [x] Inter as the shell's typeface, from the content already staged beside the
      binary, with the built-in face as a stated fallback.
- [x] The shell scales with the display, overridable per user in
      `<userDir>/appearance.json`.
- [x] The launcher laid out for somebody arriving: header band, the project list
      as the subject, making a project before opening one, messages in one place.
- [x] The editor's panels named the way an application names them.

## NOT in scope

Themes loaded from a file, chosen by a plugin, or authored outside the
repository — a theme is a struct, and a fourth one is somebody writing a struct.
Per-theme metrics. A syntax-highlighting palette for the console or a future
script editor: different problem, different tokens. A second typeface (the
engine has one face by human decision, M7). Icon themes, which
`icons/README.md` already owns. **The OS title bar**, which stays in the
system's own light/dark setting because SDL exposes no way to ask otherwise and
a DWM call would be a Windows-only path in the platform layer for one strip of
pixels. And anything that changes what a panel *does*.

## Subagent plan

None. This is one seam touched in one file plus a new module, which
`MASTER_PROMPT.md` §7 names as orchestrator-only work — and the part that would
have been worth fanning out (the palette) is the part where two independent
answers would have had to be reconciled by measuring them anyway.

## Gate checklist (verbatim from roadmap)

- [x] **The palette is legible, and the test says so rather than the author.**
      Every theme, every foreground token, against the window background, a
      field and a raised surface, at 4.5:1 — in `ui_theme_tests.cpp`, with no
      ImGui in the header it tests. Two of the first values committed failed this
      and were changed because of it.
- [x] **The shell is square, asserted.** `themeMetrics().rounding == 0`, and a
      border that is not also zero.
- [x] **The appearance survives a process, and a broken file does not break the
      shell.** Round-trip through `appearance.json`; a missing file, an
      unparseable one, a theme name this build does not carry and a scale that is
      not a number all open on the default.
- [x] **Both themes are looked at, in both shells.** Screenshots in the gate
      record. The same limit E1 recorded applies — the ImGui shell cannot render
      headlessly and SDL does not accept injected input — so this one is a
      person, and the pictures are taken off the running window.
- [x] **The typeface actually reaches the window.** A screenshot in Inter rather
      than in ProggyClean, and the fallback said out loud in the log when the
      content is not staged.
- [x] **`scripts/localgate.ps1` is green on every stage.**

## Findings

**Finding 1 — the palette had two failures and the test found both, on its first
run.** `textMuted` and `danger` were chosen against the window background,
measured 5.24:1 and 5.43:1 there, and measured **4.23:1 and 4.38:1 over a
hovered row** — which is the ground a person is looking at precisely when they
are about to act. The light theme's accent had the same shape of failure against
its raised surface. Nothing about this is visible by looking: all three are
perfectly pleasant colours that fail a threshold, which is the entire argument
for the threshold. **A palette checked only against the window background is a
palette whose text goes grey the moment somebody points at it.**

**Finding 2 — the launcher was already scaling its heading wrong, and only a
setting could expose it.** `drawLauncher` did `PushFont(nullptr, ImGui::GetFontSize() * 1.6f)`;
`GetFontSize()` is the size *after* the global scale factors, so feeding it back
into `PushFont` multiplies the scale in a second time. ImGui's own header says so
in capitals. Nothing had ever seen it because the scale was one until this
milestone gave it a number — a latent defect that a new feature made reachable
rather than a defect the new feature introduced.

**Finding 3 — the icon atlas needed nothing, and it is worth saying why.**
`IconAtlas::tintFor` picks its light or dark role colour from *the panel's own
background luminance* rather than from a setting, and `icons.h` says it was
written that way "so it follows a style change for free and there is no second
setting to keep in sync". A light theme was the first style change there has ever
been, and every icon in both shells re-tinted with no line of code. **A decision
written down as a reason survives to pay off for a caller that did not exist.**

**Finding 4 — a build agreed with a file it had not read, again, and the cause is
new.** A `localgate -Only windows` reported green over a `debug_overlay.cpp`
whose edits were **not** in the resulting binary: the object file's timestamp sat
between two writes of the source. This is not D040 (that was ninja recording no
header dependencies at all, fixed by `chcp 65001`); this is an edit landing while
the compile was already reading the file. **The cheap check is not the build's
exit code, it is the artifact**: `grep -a "<a string only the new code has>"` on
the executable took one second and settled it, where re-reading the diff would
have settled nothing.

**Finding 5 — renaming a panel throws away everybody's layout, silently.** ImGui
keys a saved `.ini` by window NAME, so `explorer` → `Explorer` means the saved
file docks nothing — and the shell's own "has anybody arranged this yet" test
looks for a split node with no windows in it, sees a split node with *old*
windows in it, and declines to rebuild. The result is a first launch with every
panel floating and no obvious way back. `editor-layout.v2.ini` is the fix and it
costs one arrangement, once.

**Finding 6 — a hand-written JSON reader and a Windows PowerShell `Set-Content`
do not agree about BOMs.** Writing `appearance.json` with `-Encoding utf8` from
PowerShell 5.1 prepends `EF BB BF`, and `core::JsonDocument::parse` refuses it —
so the file was silently ignored and the theme silently stayed on the default.
This cost one wrong diagnosis before the real cause was found (which was
simply that the screenshot predated the build). **It is left recorded rather than
fixed**: it is a `core` question rather than a shell one, it affects every JSON
file this engine reads, and somebody editing `appearance.json` in Notepad will
hit it. That is a defect for the reviewer to file against `core`, not something
to fix from inside a theme.

## Attempted / abandoned

**Following the monitor.** `io.ConfigDpiScaleFonts` exists in the pinned ImGui
and its own comment says it scales fonts "but _NOT_ ... sizes/padding for now",
which produces a shell whose text and whose padding disagree. The display scale
is therefore read **once**, when the window opens, and a person who drags the
editor to a second monitor reopens it — which is what every editor in this shape
does today.

## Gate Record

Filled 2026-08-24, before human review.

| Gate item | Result |
|---|---|
| The palette is legible, asserted | **Green.** `ui_theme_tests.cpp`, 22 contrast assertions across 2 themes × 11 foreground/ground pairs, all ≥ 4.5:1. Two values failed on the first run and were changed (Finding 1). |
| The shell is square, asserted | **Green.** `themeMetrics().rounding == 0`, `borderSize >= 1`. |
| The appearance survives a process | **Green.** Round-trip, missing file, empty path, unparseable file, unknown theme name and non-numeric scale — six cases, all opening on the default rather than failing. |
| Both themes looked at, in both shells | **Green, by a person.** `docs/images/e7/launcher-dark.png`, `editor-dark.png`, `launcher-light.png`, `editor-light.png` — captured off the running window, since the shell cannot render headlessly. |
| The typeface reaches the window | **Green.** Every word in all four pictures is Inter; the same shells a commit earlier were ProggyClean. The fallback path is a `LUAUG_TR("engine.overlay.warn.font_missing")` warning rather than a silent substitution. |
| `scripts/localgate.ps1` green on every stage | **Green.** docs 19.2 s · luau 12.4 s · format 15.6 s (366 files) · windows 84.5 s (44 tests) · linux 86.3 s (41 tests) · shipping 56 s. macOS is Tier-3 and unverified, for the reason the ledger records: Actions has executed zero steps since 2026-08-21. |

### What the pictures show

`launcher-dark.png` — the header band with the wordmark, the version beside it,
and the one accent rule on the screen under it; the project list as the subject
of the screen with a hairline per row; and the right-hand column reading NEW
PROJECT before OPEN AN EXISTING FOLDER, which is the order somebody who has no
projects needs and the reverse of what it shipped with.

`editor-dark.png` — Title Case panels, the selected tab marked by an accent
overline rather than by a fill, square frames everywhere, and the viewport's
active toggles carrying the accent.

The two light pictures are the same two shells with one word changed in
`appearance.json`, including **every icon re-tinted with no code** (Finding 3).
The OS title bar stays dark in them: that is Windows' own setting and is named in
NOT-in-scope above.
