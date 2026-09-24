// Shared Orbit shell palette, metrics and typography (ADR 0056, refreshed 2026-09-23).
// Theme data is testable without a window; applyTheme owns the ImGui mapping.
// Light and dark share shape and spacing. Text and semantic foregrounds retain
// the 4.5:1 contrast floor. See docs/briefs/orbit-shell.md for the visual brief.
// The development shell's labels follow the debug_overlay.h R3 exemption.
#pragma once

#include "luaug/core/math.h"
#include "luaug/core/types.h"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

struct ImFont;

namespace luaug::app {

// The colours a shell draws with.
//
// Eleven, and the count is deliberate: ImGui has sixty-odd slots and every one
// of them is derived from these by `applyTheme`. A palette somebody has to fill
// in sixty times is a palette nobody writes a second one of.
struct ThemePalette
{
    // The ground a window sits on. Everything else is judged against it.
    core::Color3 background;
    // A recessed area: a text field, a list, a child region. Darker than the
    // ground in a dark theme and lighter in a light one, which is the same
    // decision either way -- it reads as a hole rather than as a card.
    core::Color3 surface;
    // A raised area: a header, a hovered row, the selected tab.
    core::Color3 surfaceRaised;
    // Quiet separators between adjacent panels and popup boundaries.
    core::Color3 border;
    core::Color3 text;
    // Secondary text: a path under a name, a hint, a unit. Still 4.5:1 -- muted
    // means quieter, not unreadable, and the usual failure of a "subtle" grey is
    // that it is subtle to whoever has the same monitor as its author.
    core::Color3 textMuted;
    // Orbit teal, adjusted per surface to retain the text contrast floor.
    core::Color3 accent;
    // What is legible ON the accent -- the label of a primary button. Not
    // derivable from the accent by a rule anybody would trust, so it is a token.
    core::Color3 onAccent;
    // Something is wrong but nothing is lost: a project that has moved, a
    // template directory that is not there, a name the field will not take.
    core::Color3 warning;
    // Something failed, or is about to destroy work.
    core::Color3 danger;
    core::Color3 success;
};

// What code looks like (ADR 0057).
//
// ADR 0056 left this open in as many words -- "a syntax-highlighting palette for
// the console or a future script editor, which is a different problem with a
// different set of tokens" -- and this is the different set. One colour per
// class Luau's own lexer distinguishes (`TokenKind` in `script_document.h`), so
// there is no kind the highlighter can produce that nobody named a colour for.
//
// **Judged against the code pane's ground, which is `surface` rather than
// `background`.** A code pane is a recessed area like a text field, and a
// palette measured against the window would be measured against a colour the
// code is never drawn on.
struct SyntaxPalette
{
    core::Color3 keyword;
    // Ordinary text: a variable, a field, a function's name. Usually the pane's
    // own foreground, and a token of its own so that a theme MAY say otherwise.
    core::Color3 identifier;
    core::Color3 number;
    core::Color3 string;
    core::Color3 comment;
    // Punctuation. Quieter than an identifier on purpose: an operator is
    // structure rather than content, and colouring it as loudly as a name makes
    // a line of arithmetic unreadable.
    core::Color3 operatorToken;
    // `@native`, `@checked`.
    core::Color3 attribute;
    // A name in a type annotation: `number` in `local x: number`.
    core::Color3 typeName;
    // A broken string, comment or codepoint -- what a half-typed line produces.
    core::Color3 errorToken;
};

// The numbers. One set, shared by every theme -- see the header note.
struct ThemeMetrics
{
    // Compact controls and larger containers share the Orbit rounded geometry.
    core::f32 rounding = 5.0f;
    core::f32 containerRounding = 8.0f;
    core::f32 borderSize = 1.0f;

    core::f32 windowPaddingX = 14.0f;
    core::f32 windowPaddingY = 12.0f;
    core::f32 framePaddingX = 10.0f;
    core::f32 framePaddingY = 6.0f;
    core::f32 itemSpacingX = 8.0f;
    core::f32 itemSpacingY = 8.0f;
    core::f32 itemInnerSpacingX = 6.0f;
    core::f32 itemInnerSpacingY = 5.0f;
    core::f32 cellPaddingX = 7.0f;
    core::f32 cellPaddingY = 4.0f;
    core::f32 indentSpacing = 18.0f;
    core::f32 scrollbarSize = 10.0f;
    core::f32 grabMinSize = 12.0f;
    // A quiet baseline and a mint overline identify the active document.
    core::f32 tabBarBorderSize = 1.0f;
    core::f32 tabBarOverlineSize = 2.0f;
    core::f32 dockingSeparatorSize = 4.0f;

    // Inter at 16 px. The default ImGui font is a 13 px bitmap face designed for
    // a debugger, and drawing an application in it is most of why the editor
    // read as an overlay.
    core::f32 fontSize = 16.0f;
};

struct Theme
{
    // Stable, lower-case, and what `appearance.json` stores. Renaming one
    // silently resets everybody's choice, so it is an identifier and not a
    // label.
    std::string_view id;
    // What the Preferences dialog shows.
    std::string_view name;
    // Whether the ground is dark. Read by nothing in ImGui -- the icon atlas
    // works this out from the window background's own luminance, which is
    // better because it follows a style change for free (`icons.h`) -- and here
    // for whoever needs to state it rather than measure it.
    bool dark = true;
    ThemePalette palette;
    SyntaxPalette syntax;
};

// Every theme this build carries, in the order the Preferences dialog lists
// them. The first is the default.
[[nodiscard]] std::span<const Theme> themes() noexcept;

// The theme called `id`, or the default when nothing is. A file naming a theme
// this build does not have is a file from another build, not a broken one --
// the same rule the scene format and `editor.json` already follow.
[[nodiscard]] const Theme& themeById(std::string_view id) noexcept;

[[nodiscard]] ThemeMetrics themeMetrics() noexcept;

// The theme `applyTheme` last applied, or the default when none has been.
//
// **So that a second panel does not need a second copy of the answer.** Before
// the script editor there was one drawing translation unit and it kept the
// chosen appearance in a file-local global; a second one asking "what colour is
// a keyword" would either thread a `Theme` through every call or keep its own
// idea of which is current, and the second of those goes wrong the first time
// somebody changes theme with two panels open.
[[nodiscard]] const Theme& currentTheme() noexcept;

// WCAG 2.1 contrast between two colours, in the range [1, 21]. Symmetric.
//
// Here rather than in a test so that the number a theme is judged by and the
// number anybody else computes are the same code. sRGB in, relative luminance
// out; the transfer function is the specification's, not an approximation.
[[nodiscard]] core::f32 contrastRatio(core::Color3 a, core::Color3 b) noexcept;

// The bar every foreground token clears against its ground. WCAG AA for body
// text.
inline constexpr core::f32 kMinimumContrast = 4.5f;

// How the shell is scaled. Below 0.75 the icons are illegible and above 2.5 a
// dialog stops fitting a 1080p screen, which is what the clamp is for rather
// than taste.
inline constexpr core::f32 kMinimumUiScale = 0.75f;
inline constexpr core::f32 kMaximumUiScale = 2.5f;

// What this USER chose, for every project and for the launcher alike.
//
// Per user rather than per project (`.luaug/editor.json`), and the difference is
// what the setting IS: which panels are open is a thing about this project, and
// how big the text is, is a thing about this person's eyes and this person's
// monitor. A theme that reset itself when you opened a second project would be
// a theme nobody bothered to change.
struct Appearance
{
    std::string themeId;
    // **Zero means "ask the display"**, and that is the default rather than 1.0:
    // a shell pinned at 1.0 on a 200% monitor is half-size, and a person on a
    // scaled display should not have to find a setting before they can read the
    // menu bar. A number stored here is somebody overriding that on purpose.
    core::f32 scale = 0.0f;
};

// `<userDir>/appearance.json`. Empty when the platform has no user directory,
// which is a real answer and not a failure -- `platform::Paths` says so, and a
// launcher that cannot remember still runs.
[[nodiscard]] std::filesystem::path appearanceFile();

// Reads it. A file that is not there, or does not parse, is the default
// appearance rather than an error: this is a convenience and nothing the engine
// needs to run lives in it.
[[nodiscard]] Appearance loadAppearance(const std::filesystem::path& file);

// Writes it. False when there is nowhere to write.
[[nodiscard]] bool saveAppearance(const std::filesystem::path& file, const Appearance& appearance);

// What to actually scale by: the stored override when there is one, the
// display's own scale when there is not, clamped either way.
[[nodiscard]] core::f32 resolveUiScale(core::f32 requested, core::f32 displayScale) noexcept;

// Where the shell's typeface is staged. `content/fonts/Inter.ttf`, which the
// game's own text renderer already reads (`engine/ui/src/text.cpp`) -- one file,
// one face, and the wordmark in `branding/` is set in it too.
[[nodiscard]] std::filesystem::path uiFontFile();

// And the code pane's, which is a different question (ADR 0057).
//
// **Inter is still the engine's typeface.** This is the face the script editor
// draws CODE in, and it is monospace because a code editor set in a proportional
// face is not a code editor -- every column lands somewhere different from the
// one above it, and a caret drawn by arithmetic stops matching the glyphs.
// `content/fonts/Mono.ttf` is Cousine, already vendored inside ImGui's own
// sample assets under the SIL OFL 1.1, which is the licence Inter ships under
// and which `tools/repo/licensecheck.luau` already admits.
[[nodiscard]] std::filesystem::path codeFontFile();

// --- The ImGui half ---------------------------------------------------------
//
// Declared unconditionally and inert in a shipping build, the shape
// `DebugOverlay` established: the caller carries no #ifdef.

// Rewrites ImGui's style from the theme. Safe to call again -- it starts from a
// zeroed style rather than editing the one in place, so switching themes twice
// does not compound the scaling.
//
// Requires a live ImGui context. `scale` is already resolved.
void applyTheme(const Theme& theme, core::f32 scale);

// Loads Inter into the atlas at `themeMetrics().fontSize`, and answers false
// when the file is not there -- a build tree with no staged content, which is a
// normal state. The caller keeps ImGui's own font in that case and says so once
// in the log, because a shell that silently looks wrong is a shell somebody
// files a defect about.
[[nodiscard]] bool loadUiFont();

// The same for the code face. False leaves `codeFont()` null and the script
// editor draws in the UI face -- readable, wrongly spaced, and said out loud.
[[nodiscard]] bool loadCodeFont();

// What `loadCodeFont` produced, or null. **Forward-declared rather than
// included**: this header stays free of ImGui, and the two translation units
// that actually draw with the pointer include `imgui.h` themselves -- the same
// arrangement `platform::nativeWindow` uses to hand out an `SDL_Window*` without
// putting SDL in a header.
[[nodiscard]] ImFont* codeFont() noexcept;

// **Spaces to put at the head of a dock tab's label, so an icon can be drawn
// over them.**
//
// ImGui's tab bar takes a string and measures it; there is no hook for a
// picture, and there is no icon font in this build. So a tab that wants one
// reserves the room in its own label and the shell paints the icon into the gap
// afterwards -- which is why this is a width in SPACES rather than in pixels:
// the reservation has to be made in the units the tab bar measures.
//
// Computed from the current font rather than fixed, because the answer changes
// with the interface scale and a hard-coded three spaces is right at exactly
// one of them.
//
// **Put it before the `###`**, never after: `ImHashStr` restarts its hash at
// `###` (`imgui.cpp:2578`), so `"   Viewport###Viewport"` is the same window as
// `"Viewport"` to the dock builder, to a saved `layout.ini`, and to
// `SetWindowFocus`. Padding the id instead would silently orphan every
// arrangement anybody has saved.
[[nodiscard]] std::string tabIconPad();

} // namespace luaug::app
