# 0056 — The shell has one theme, it is data, and it is square

- Status: accepted
- Date: 2026-08-24
- Extends: 0011, 0046, 0055

## Context

E1 through E6 built an editor and a launcher. Nothing in any of them decided
what they should *look* like, and the default filled in for all six milestones:

- `debug_overlay.cpp` called `ImGui::StyleColorsDark()` once, at construction,
  and never touched the style again.
- No font was ever loaded, so all three shells drew in ImGui's built-in
  ProggyClean — a 13-pixel bitmap face designed for a debugger, at one size,
  with no hinting and no scaling. Meanwhile `content/fonts/Inter.ttf` has been
  staged beside the binary since M7 for the *game's* text, and the wordmark in
  `branding/` is set in the same face.
- Nine colours were written out as literals at their call sites, three of them
  the same orange in three places, because there was nowhere else to put them.
- Nothing scaled with the display. `platform::windowDisplayScale` has existed
  since M6 and had one caller, in `engine/ui`, for `UIService.DisplayScale`.

That is a shell that looks like the debug overlay it grew out of, because that
is exactly what it is. The human's brief was three words with a lot in them:
**clean, simple, professional** — and one shape: **square**.

**Square is the part that is a decision rather than a preference.** ImGui has
eleven separate rounding members, and a shell that is square except for its menu
items is one somebody notices without being able to say why. Committing to zero
everywhere makes borders load-bearing: with no radius to separate two panels,
the one-pixel line is what does it, and the palette has to carry a border
colour rather than treat it as decoration.

**And there is one measurement that decides most of the rest.** ImGui's default
dark palette was drawn to sit on top of a running game at 85% alpha, not to be
read for six hours; its `TextDisabled` clears 4.5:1 against the window
background and does not clear it against a hovered row. Which is a defect nobody
can find by looking, and one line of arithmetic finds every time.

## Decision

**A theme is data — eleven colours and a table of numbers — the shell's shape is
not part of it, and the whole thing is asserted without a window.**

Five parts:

1. **`engine/app/ui_theme.h` is the only file that decides a colour or a corner
   radius.** `ThemePalette` carries eleven tokens; `applyTheme` derives ImGui's
   sixty-odd slots from them. A palette somebody has to fill in sixty times is a
   palette nobody writes a second one of.

2. **The metrics, including every rounding, are shared by every theme and the
   rounding is zero.** A theme that could round its own corners would be a second
   answer to what this engine's shell looks like. Somebody switching from dark to
   light is changing the light, not the product.

3. **Every foreground token clears 4.5:1 against every ground it is drawn on**,
   computed by `contrastRatio` and asserted in `ui_theme_tests.cpp` — against the
   window background, against a field, and against a hovered row, because a
   palette checked only against the window is a palette whose text goes grey the
   moment somebody points at it. The threshold is WCAG 2.1 AA for body text,
   which is the only widely published number for this and is the same bar
   `icons/README.md` already holds the icon palette to.

   **This is R18's habit applied to the shell.** A stated reference beats an
   opinion; "looks fine" is not a result.

4. **The brand colour is one hue and two values.** `#12B0FF` from
   `branding/luaug-mark.svg` is the dark theme's accent unmodified; against a
   near-white ground it measures 2.2:1, so the light theme carries the same hue
   darkened until it clears the bar. That is not two decisions — it is the rule
   `icons/README.md` states for the icon roles, for the reason it states: a
   single colour cannot clear 4.5:1 against both a near-white panel and a dark
   one, and everything in the band that does is muddy.

   **And the accent is spent on one thing at a time.** Selection, focus, and the
   single primary action on a screen. A button that is merely a button is a
   surface: paint every button with the brand colour and the brand colour stops
   saying anything.

5. **Inter is the shell's typeface**, loaded from the content already staged
   beside the binary, and the shell scales with the display. `themeMetrics`
   carries the base size; `Appearance` carries a per-user override where zero
   means "ask the display", which is the default so that somebody on a 200%
   monitor does not have to find a setting before they can read the menu bar.

**The choice is per user, not per project.** `<userDir>/appearance.json`, beside
the launcher's recents list — because which panels are open is a fact about a
project, and how big the text is, is a fact about a person's eyes and their
monitor. A theme that reset itself when you opened a second project is a theme
nobody would bother to change. It is therefore also the one setting the LAUNCHER
has, which has no project to keep anything in.

## Consequences

**There is now an answer to "what colour is a warning here", and it has one
definition.** Nine literals at call sites became six token reads. The streaming
map is the case that shows why it matters: three of its five state colours were
picked against a dark ground and were invisible on a light one, which nobody
could have known while there was no light one.

**The icon atlas needed no change at all, and that is a decision from M-something
paying off.** `IconAtlas::tintFor` picks its light/dark role colour from the
panel's own background luminance rather than from a setting, so a theme switch
re-tints every icon for free. `icons.h` says it was written that way "so it
follows a style change for free"; this is the style change.

**Every panel was renamed to Title Case**, which is a visual decision with a
mechanical cost: ImGui keys a saved layout by window name, so a file written
against `explorer` docks nothing once the window is called `Explorer`. The
layout file is `editor-layout.v2.ini` for that reason. It costs whoever has
arranged their panels one arrangement, once.

**Two themes rather than one is what makes this a system.** A single palette
behind an abstraction is an abstraction with no second case, and the second case
is what found the streaming map's colours and the light-mode accent value. It is
not a promise of a third: a theme is a struct in this file, and somebody wanting
a fourth writes one — but a theme loaded from JSON, chosen by a plugin, or
authored outside the repository is deliberately NOT decided here.

**What this does not decide.** Per-theme metrics. A syntax-highlighting palette
for the console or a future script editor, which is a different problem with a
different set of tokens. Icon themes, which `icons/README.md` already owns and
which are chosen separately. And a second typeface: the engine has one face by
human decision (M7), and a shell that shipped a second would be relitigating
that where nobody would look for it.
