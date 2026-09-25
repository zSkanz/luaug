// The code pane (ADR 0057).
//
// **Drawn by hand, and that is a decision with a reason rather than an
// appetite.** `InputTextMultiline` renders the buffer itself in one colour with
// no per-token hook, its multiline path has no length guard at all
// (`imgui_widgets.cpp:5512-5515` says a pathologically long line "would still
// crash"), and `ImGuiInputTextState` is a single object for the whole context
// (`imgui_internal.h:1273`) -- so N open documents could not each keep a caret.
// Vendoring somebody's ImGui text editor would be a new dependency under R5, and
// none of them know Luau while `Luau.Ast` is already linked.
//
// A separate translation unit from `debug_overlay.cpp` because that file is
// already 4,500 lines and this is another thousand of glyph arithmetic that has
// no business sharing its anonymous namespace.
//
// ## How it keeps the shell's shortcuts without changing them
//
// Every editor shortcut in `debug_overlay.cpp` is guarded by
// `!ImGui::IsAnyItemActive()`. The pane satisfies that guard honestly rather
// than working around it: it claims ImGui's active id and calls
// `SetActiveIdUsingAllKeyboardKeys`, which is exactly what `InputTextEx` does.
// While the caret is in code, Escape, Delete, F2, Ctrl+1 to Ctrl+4 and the world's
// Ctrl+C/V/X/D do not fire -- and Ctrl+S still saves, it just saves the script.
// Escape releases the pane rather than clearing the selection, so one press
// leaves the code and a second means what the shell says it means.
#include "luaug/app/script_editor_panel.h"

#include "luaug/app/language_service.h"
#include "luaug/app/script_editor.h"
#include "luaug/app/script_editor_settings.h"
#include "luaug/app/ui_theme.h"
#include "luaug/platform/file.h"
#include "luaug/platform/platform.h"
#include "luaug/scene/world.h"

#include "icon_ids.gen.h"

namespace luaug::app {

// Finds `<something>.luau:<digits>` -- the first one, which is the raise site.
//
// Hand-rolled rather than a regex, and that is the cheap answer here: this runs
// once per visible console line per frame, and `<regex>` costs more to construct
// than this costs to run.
std::optional<SourceLocation> parseSourceLocation(std::string_view text)
{
    constexpr std::string_view kSuffix = ".luau:";
    const std::size_t at = text.find(kSuffix);
    if (at == std::string_view::npos)
        return std::nullopt;

    std::size_t digits = at + kSuffix.size();
    core::u32 line = 0;
    std::size_t counted = 0;
    // Bounded, because a run of digits long enough to overflow is not a line
    // number and multiplying through it would wrap into one that looks real.
    for (; digits < text.size() && text[digits] >= '0' && text[digits] <= '9' && counted < 9; ++digits, ++counted)
        line = line * 10 + static_cast<core::u32>(text[digits] - '0');
    if (counted == 0 || line == 0)
        return std::nullopt;
    // **More digits than a line number has is a refusal, not a truncation.**
    // Stopping at nine and using what was read turns an absurd run into a
    // plausible line, and the click then scrolls somewhere arbitrary in a file
    // that is perfectly fine.
    if (digits < text.size() && text[digits] >= '0' && text[digits] <= '9')
        return std::nullopt;

    // Back to the start of the chunk name: everything up to a separator that
    // could not be part of one. A log line is `[level] [key] chunk.luau:12`, and
    // the chunk may itself hold slashes.
    std::size_t begin = at;
    while (begin > 0) {
        const char c = text[begin - 1];
        if (c == ' ' || c == '\t' || c == '\'' || c == '"' || c == '[' || c == ']' || c == '(' || c == ')')
            break;
        --begin;
    }

    SourceLocation found;
    found.chunk = std::string(text.substr(begin, at + 5 - begin)); // includes ".luau"
    found.line = line;
    return found;
}

} // namespace luaug::app

#if LUAUG_DEBUG_UI

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <imgui.h>
#include <imgui_internal.h>
#include <optional>
#include <string>
#include <utility>

namespace luaug::app {

namespace {

using core::f32;
using core::u32;

// --- Colour ------------------------------------------------------------------

[[nodiscard]] ImU32 col(core::Color3 c, float alpha = 1.0f) noexcept
{
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, alpha));
}

// **Every colour the pane draws with, through the person's settings**
// (`script_editor_settings.h`): what they chose, or the theme's.
[[nodiscard]] core::Color3 scol(ScriptColor which) noexcept
{
    return scriptEditorSettings().color(which, currentTheme());
}

[[nodiscard]] ImU32 ecol(ScriptColor which, float alpha = 1.0f) noexcept
{
    return col(scol(which), alpha);
}

// --- Keys ----------------------------------------------------------------------

// The ImGui key a settings file names, by ImGui's own name for it.
[[nodiscard]] ImGuiKey keyNamed(std::string_view name)
{
    for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key) {
        if (name == ImGui::GetKeyName(static_cast<ImGuiKey>(key)))
            return static_cast<ImGuiKey>(key);
    }
    return ImGuiKey_None;
}

// **Whether this command's chord was pressed this frame**, through the
// person's settings (`script_editor_settings.h`). The modifiers must match
// EXACTLY, so Ctrl+D and Ctrl+Shift+D are two chords -- and AltGr, which is
// Ctrl+Alt on Windows, cannot fire a Ctrl chord while somebody types a
// bracket on a Brazilian keyboard.
[[nodiscard]] bool pressed(ScriptAction action, bool repeat = false)
{
    const KeyChord chord = scriptEditorSettings().chord(action);
    if (chord.key.empty())
        return false;
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl != chord.ctrl || io.KeyShift != chord.shift || io.KeyAlt != chord.alt)
        return false;
    const ImGuiKey key = keyNamed(chord.key);
    return key != ImGuiKey_None && ImGui::IsKeyPressed(key, repeat);
}

// --- Metrics -----------------------------------------------------------------

struct PaneMetrics
{
    ImFont* font = nullptr;
    float size = 0.0f;
    float lineHeight = 0.0f;
    // **One number, because the face is monospace.** That is most of the reason
    // a monospace face is worth a second file: a column becomes multiplication,
    // and the caret, the selection rectangles, the current-line band and the hit
    // test all agree by construction rather than by four separate measurements
    // that must be kept in step.
    float advance = 0.0f;
    float gutter = 0.0f;
    // **Which line is on which row** once blocks are folded (see `FoldView`).
    // Null is every line on its own row. Everything that places a line goes
    // through `topOf` and everything that finds one through `lineOfRow`, so a
    // fold moves the caret, the selection, the marks and the clicks together.
    const FoldView* view = nullptr;
};

[[nodiscard]] u32 rowOf(const PaneMetrics& m, u32 line) noexcept
{
    return m.view != nullptr && line < m.view->lineRow.size() ? m.view->lineRow[line] : line;
}

[[nodiscard]] float topOf(const PaneMetrics& m, u32 line) noexcept
{
    return static_cast<float>(rowOf(m, line)) * m.lineHeight;
}

[[nodiscard]] bool hiddenLine(const PaneMetrics& m, u32 line) noexcept
{
    return m.view != nullptr && m.view->hidden(line);
}

[[nodiscard]] u32 lineOfRow(const PaneMetrics& m, u32 row) noexcept
{
    if (m.view == nullptr || m.view->rowLine.empty())
        return row;
    return m.view->rowLine[std::min<std::size_t>(row, m.view->rowLine.size() - 1)];
}

[[nodiscard]] PaneMetrics metricsFor(const ScriptDocument& document, float zoom)
{
    PaneMetrics m;
    const ImGuiStyle& style = ImGui::GetStyle();
    m.font = codeFont() != nullptr ? codeFont() : ImGui::GetFont();
    // **The size the CODE is drawn at**, which is the interface's own size times
    // whatever somebody has zoomed to. Rounded to a whole pixel: ImGui bakes a
    // face per size, and a fractional one bakes a new atlas on every notch of a
    // wheel that is still turning.
    m.size = std::round(ImGui::GetFontSize() * zoom);
    m.lineHeight = std::floor(m.size * 1.35f);
    ImFontBaked* baked = m.font->GetFontBaked(m.size);
    m.advance = baked != nullptr ? baked->GetCharAdvance('0') : m.size * 0.5f;
    if (m.advance <= 0.0f)
        m.advance = m.size * 0.5f;

    // Wide enough for the largest line number this document will ever show, so
    // the code does not shift sideways when the file passes a power of ten.
    int digits = 1;
    for (u32 count = document.lineCount(); count >= 10; count /= 10)
        ++digits;
    // The number, a breakpoint dot's worth of room on its left, and a gap of
    // two and a half cells before the code -- one was reported as the code
    // running into its own line numbers.
    m.gutter = m.advance * static_cast<float>(digits) + m.lineHeight + style.ItemSpacing.x + m.advance * 1.5f;
    return m;
}

// --- UTF-8 -------------------------------------------------------------------

void encodeUtf8(unsigned int codepoint, std::string& out)
{
    if (codepoint < 0x80) {
        out.push_back(static_cast<char>(codepoint));
    }
    else if (codepoint < 0x800) {
        out.push_back(static_cast<char>(0xC0u | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    }
    else if (codepoint < 0x10000) {
        out.push_back(static_cast<char>(0xE0u | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    }
    else {
        out.push_back(static_cast<char>(0xF0u | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    }
}

// **Which pane a drag belongs to, if any.** A pane keeps ImGui's active id while
// the caret is in it, so without this it extended its selection on any drag
// anywhere -- clicking a tab at the bottom of the window selected everything
// between the caret and the tab. A drag belongs to the pane whose press started
// inside it, and to nothing else.
ImGuiID g_dragging = 0;

// **What a drag EXTENDS BY.** A double-click selects a word and then holds the
// button down, and `IsMouseDragging` with a zero threshold answers true for as
// long as it is held -- so the very next frame put the caret back under the
// pointer and the selection collapsed to whatever was left of the word. Reported
// as "double-clicking in the middle of `require` selects up to the `u`", which
// is exactly what that looks like.
//
// So a drag knows what it is extending by, and a word-wise drag grows a word at
// a time in either direction, which is what every editor does and what makes the
// gesture worth having.
bool g_dragByWord = false;
Range g_dragWord;
// **A Shift+Alt drag selects a column**: where it began, as a line and a
// CELL, since a column is what the eye sees and a byte offset is not.
bool g_dragColumn = false;
Position g_columnFrom;

// **Who holds the caret, and what counts as inside it.** A pane keeps ImGui's
// active id for as long as somebody is typing in it, and ImGui refuses to hover
// ANY other item while an item is active (`imgui.cpp`, `ItemHoverable`) -- so a
// click on the Explorer, the Viewport or another tab was swallowed whole, and
// only the second one did anything. Read by `releaseScriptPaneFocus`, which
// runs before the shell submits a single window.
// **How long the zoom readout has been up.** A number in the corner of the code
// is clutter once it has been read, and a zoom nobody can see the value of is a
// state somebody can get into and not out of -- so it appears on a change and
// leaves on its own.
//
// Seeded past the end so nothing shows on the first frame. `io.DeltaTime` and
// not a clock: this is interface timing, which R10 does not govern and which no
// hash ever sees.
float g_zoomShownFor = 1e9f;

ImGuiID g_paneActiveId = 0;
ImGuiID g_paneWindowId = 0;
ImRect g_paneBounds;
// **Where the completion list and its prose were drawn last frame**, which is
// inside the pane as far as a click is concerned: the list hangs below the
// caret and past the pane's edge, and a click on it that let go of the caret
// first could never pick a row.
ImRect g_popupBounds;

// **The colour being picked from a swatch in the code**, while its picker is
// open: which line, the literal as it was when the picker opened, and the value
// the picker holds -- drawn in the swatch as it changes, written into the text
// once when the picker is let go, so a drag is one undo step.
struct CodeColourEdit
{
    u32 line = 0;
    ColorLiteral literal;
    float value[3]{1.0f, 1.0f, 1.0f};
    bool open = false;
};
CodeColourEdit g_colourEdit;

// **Where a colour's swatch is drawn: just after its closing parenthesis**, and
// only while the pointer is on the call or the swatch (the owner: "it should
// appear only when the mouse is over a colour"). A swatch fixed in the margin
// was far from the colour it stood for.
[[nodiscard]] ImRect swatchRect(const ScriptDocument& document, const PaneMetrics& m, ImVec2 textOrigin,
                                const ColorLiteral& literal)
{
    const u32 line = literal.call.end.line;
    const float side = std::floor(m.lineHeight * 0.7f);
    const float left = textOrigin.x + static_cast<float>(document.cellOf(line, literal.call.end.column)) * m.advance +
                       m.advance * 0.4f;
    const float centreY = textOrigin.y + topOf(m, line) + 0.5f * m.lineHeight;
    return ImRect(ImVec2(left, centreY - side * 0.5f), ImVec2(left + side, centreY + side * 0.5f));
}

// The span of the call itself, for the hover that brings the swatch up.
[[nodiscard]] ImRect callRect(const ScriptDocument& document, const PaneMetrics& m, ImVec2 textOrigin,
                              const ColorLiteral& literal)
{
    const u32 line = literal.call.begin.line;
    const float top = textOrigin.y + topOf(m, line);
    return ImRect(
        ImVec2(textOrigin.x + static_cast<float>(document.cellOf(line, literal.call.begin.column)) * m.advance, top),
        ImVec2(textOrigin.x + static_cast<float>(document.cellOf(line, literal.call.end.column)) * m.advance,
               top + m.lineHeight));
}

// Defined with the find bar below, and declared here because a key binding needs
// it before the bar does.
void stepMatch(OpenScript& tab, bool forward);

// --- The caret ---------------------------------------------------------------

void placeCaret(OpenScript& tab, Position to, bool select)
{
    tab.caret.head = tab.document.clamp(to);
    if (!select)
        tab.caret.anchor = tab.caret.head;
    tab.caret.desiredColumn = tab.document.cellOf(tab.caret.head.line, tab.caret.head.column);
    // Moving by hand ends a typing run, so the next Ctrl+Z stops where somebody
    // moved rather than swallowing what came before.
    tab.document.breakUndoRun();
    // **And ends the word being completed.** A list left open after a click
    // somewhere else kept its range from where it was opened, and the next
    // Enter accepted into THAT range -- far down the file -- and took the
    // view with it (reported: "I press Enter and it jumps back down").
    tab.completing = false;
}

// Vertical movement keeps the column somebody was aiming for, so passing through
// a short line and coming back lands where they left.
void moveVertically(OpenScript& tab, int delta, bool select, const FoldView* view = nullptr)
{
    // The CELL, not the byte: moving down a line whose accents sit elsewhere
    // should keep the caret under the same glyph, not the same byte offset.
    const u32 wantedCell = tab.caret.desiredColumn;
    u32 clamped = 0;
    if (view != nullptr && !view->rowLine.empty() && tab.caret.head.line < view->lineRow.size()) {
        // **In rows**, so a folded block is one step over rather than a walk
        // into lines nobody can see.
        const auto row = static_cast<std::int64_t>(view->lineRow[tab.caret.head.line]) + delta;
        clamped = view->rowLine[static_cast<std::size_t>(
            std::clamp<std::int64_t>(row, 0, static_cast<std::int64_t>(view->rowLine.size()) - 1))];
    }
    else {
        const auto line = static_cast<std::int64_t>(tab.caret.head.line) + delta;
        clamped = static_cast<u32>(std::clamp<std::int64_t>(line, 0, tab.document.lineCount() - 1));
    }
    tab.caret.head = tab.document.clamp(Position{clamped, tab.document.columnOfCell(clamped, wantedCell)});
    if (!select)
        tab.caret.anchor = tab.caret.head;
    tab.document.breakUndoRun();
}

// Everything that changes text goes through here, so "the tab was edited" is
// recorded in exactly one place and cannot be forgotten by a new key.
void edited(ScriptEditorCommands& out, std::size_t index)
{
    if (std::find(out.edited.begin(), out.edited.end(), index) == out.edited.end())
        out.edited.push_back(index);
}

void eraseSelection(OpenScript& tab, ScriptEditorCommands& out, std::size_t index)
{
    if (!tab.caret.hasSelection())
        return;
    tab.caret.head = tab.document.erase(tab.caret.selection());
    tab.caret.anchor = tab.caret.head;
    tab.caret.desiredColumn = tab.caret.head.column;
    edited(out, index);
}

void insertText(OpenScript& tab, ScriptEditorCommands& out, std::size_t index, std::string_view text)
{
    // **Over a selection it is ONE step**: an erase and then an insert were two,
    // so typing or pasting over a selection took two Ctrl+Z to undo -- the
    // "some actions need two" that was reported.
    if (tab.caret.hasSelection())
        tab.caret.head = tab.document.replace(tab.caret.selection(), text);
    else
        tab.caret.head = tab.document.insert(tab.caret.head, text);
    tab.caret.anchor = tab.caret.head;
    tab.caret.desiredColumn = tab.caret.head.column;
    edited(out, index);
}

// --- Input -------------------------------------------------------------------

[[nodiscard]] Position hitTest(const ScriptDocument& document, const PaneMetrics& m, ImVec2 textOrigin, ImVec2 point)
{
    const float rows =
        m.view != nullptr ? static_cast<float>(m.view->rows()) : static_cast<float>(document.lineCount());
    const float row = (point.y - textOrigin.y) / m.lineHeight;
    const auto line = lineOfRow(m, static_cast<u32>(std::clamp(std::floor(row), 0.0f, rows - 1.0f)));
    // Rounded rather than floored, so clicking the right half of a glyph puts
    // the caret after it -- which is what every editor does and what makes a
    // click at the end of a line land at the end of the line.
    //
    // A pixel names a CELL, and a cell is a codepoint. Turning it back into a
    // byte column is what keeps a click on an accented word landing on the
    // letter under the pointer.
    const float cell = std::round((point.x - textOrigin.x) / m.advance);
    return document.clamp(Position{line, document.columnOfCell(line, static_cast<u32>(std::max(0.0f, cell)))});
}

[[nodiscard]] bool wordByte(char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           static_cast<unsigned char>(c) >= 0x80;
}

[[nodiscard]] char closerOf(char open) noexcept
{
    switch (open) {
    case '(':
        return ')';
    case '[':
        return ']';
    case '{':
        return '}';
    case '"':
    case '\'':
    case '`':
        return open;
    default:
        return '\0';
    }
}

// **Pairs close themselves**, the way every code editor does it: an opener
// types its closer after the caret, a closer typed where one already stands
// steps over it, and an opener typed over a selection wraps it. A quote beside
// a word is an apostrophe or a closing quote, and nothing is paired inside a
// string or a comment. Answers whether it handled the character.
bool typePaired(OpenScript& tab, ScriptEditorCommands& out, std::size_t index, char c)
{
    ScriptDocument& doc = tab.document;
    const std::string_view text = doc.line(tab.caret.head.line);
    const u32 column = tab.caret.head.column;
    const char next = column < text.size() ? text[column] : '\0';
    const char prev = column > 0 && column - 1 < text.size() ? text[column - 1] : '\0';
    const bool quote = c == '"' || c == '\'' || c == '`';

    if (!tab.caret.hasSelection() && (c == ')' || c == ']' || c == '}' || quote) && next == c) {
        placeCaret(tab, doc.nextColumn(tab.caret.head), false);
        // **Stepping over a closer ends the word being completed**, as typing
        // one would: the list left open took the next Enter as an accept and
        // replaced the argument just finished with a suggestion.
        tab.completing = false;
        return true;
    }
    const char close = closerOf(c);
    if (close == '\0')
        return false;

    if (tab.caret.hasSelection()) {
        const Range span = tab.caret.selection();
        std::string wrapped(1, c);
        wrapped += doc.textIn(span);
        wrapped.push_back(close);
        const Position end = doc.replace(span, wrapped);
        tab.caret.anchor = Position{span.begin.line, span.begin.column + 1};
        tab.caret.head = Position{end.line, end.column - 1};
        tab.caret.desiredColumn = tab.caret.head.column;
        edited(out, index);
        return true;
    }

    if (quote ? (wordByte(prev) || wordByte(next)) : wordByte(next))
        return false;
    for (const Token& token : doc.tokens(tab.caret.head.line)) {
        const bool inside = column > token.column && column < token.column + token.length;
        if (inside && (token.kind == TokenKind::String || token.kind == TokenKind::Comment))
            return false;
    }
    const Position after = doc.insert(tab.caret.head, std::string{c, close});
    tab.caret.head = Position{after.line, after.column - 1};
    tab.caret.anchor = tab.caret.head;
    tab.caret.desiredColumn = tab.caret.head.column;
    edited(out, index);
    return true;
}

// The characters that arrived this frame, taken once: with several carets the
// same text is typed at each, and the queue can only be drained one time.
[[nodiscard]] std::string takeTyped()
{
    ImGuiIO& io = ImGui::GetIO();
    std::string typed;
    for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
        const unsigned int c = io.InputQueueCharacters[i];
        // Control characters arrive as keys, not as text. Tab is handled below
        // because it means indent here rather than a character.
        if (c >= 0x20 && c != 0x7F)
            encodeUtf8(c, typed);
    }
    io.InputQueueCharacters.resize(0);
    return typed;
}

void typeText(OpenScript& tab, ScriptEditorCommands& out, std::size_t index, std::string_view typed)
{
    if (typed.size() == 1 && typePaired(tab, out, index, typed[0]))
        return;
    if (!typed.empty())
        insertText(tab, out, index, typed);
}

// --- The language service (ADR 0093) ------------------------------------------

// One for the editor, made the first time a tab asks, from the definitions the
// build stages beside the host. None when they are missing: completion then
// answers from the tree and the file alone, as it did before.
[[nodiscard]] LanguageService* languageService()
{
    static std::unique_ptr<LanguageService> service;
    static bool tried = false;
    if (!tried) {
        tried = true;
        std::string definitions;
        if (platform::readTextFile(platform::paths().contentDir / "runtime" / "types" / "engine.d.luau", definitions))
            service = std::make_unique<LanguageService>(std::move(definitions));
    }
    return service.get();
}

// The scripts a require can reach, from the top of `root`'s tree: a require
// names `game`, so the walk starts above a stage's workspace too.
//
// **With this tab's text from its BUFFER**, not from the world: the pane's
// writes to `Source` land through the inspector at the next frame's safe point,
// so the world is a keystroke behind -- and a signature asked about a call the
// checker could not yet see was never shown.
[[nodiscard]] LanguageTree languageTreeOf(const scene::World& world, core::InstanceId root, const OpenScript& tab)
{
    core::InstanceId top = root;
    while (world.alive(top) && world.parentOf(top).valid())
        top = world.parentOf(top);
    LanguageTree tree = captureLanguageTree(world, top);
    for (LanguageTree::Node& node : tree.nodes) {
        if (node.id == tab.instance && node.script)
            node.source = tab.document.text();
    }
    return tree;
}

// Recomputes what is on offer. **Called after an edit rather than on a key**, so
// that backspacing through a word narrows the list instead of dismissing it.
void refreshCompletions(OpenScript& tab, const scene::World* world, core::InstanceId root)
{
    if (world == nullptr) {
        tab.completing = false;
        return;
    }

    const CompletionRequest request = completionAt(tab.document, tab.caret.head);
    // Nothing to go on: no subject and fewer than two letters is every name in
    // the engine, which is a list nobody reads. A caret inside quotes is exempt:
    // `WaitForChild("` with nothing typed yet is a short list of real names,
    // which is the moment the list is worth the most.
    if (request.quoted == CompletionQuoted::No && request.subject.empty() && request.prefix.size() < 2) {
        tab.completing = false;
        return;
    }

    // `script` is THIS tab's instance, which is the one thing about the request
    // only the tab knows.
    const CompletionWorld tree{world, root, tab.instance};
    collectCompletions(tab.document, request, world->classes(), world->atoms(), tree, tab.completions);
    // **A word already whole closes the list**, longer names and all: `Part`
    // typed is `Part`, and `Part2D` waits for the `2`. The exact row itself
    // would offer nothing, and keeping any row up turns the next Enter into an
    // accept instead of a new line.
    tab.completionWhole = completesExactly(tab.completions, request.prefix);
    if (tab.completionWhole)
        tab.completions.clear();
    tab.completionReplace = request.replace;
    tab.completing = !tab.completions.empty();
    if (tab.completionIndex >= tab.completions.size())
        tab.completionIndex = 0;

    // **And the type checker, which answers a frame or two later** (see
    // `takeLanguageAnswers`). Not inside quotes: a string is the tree's.
    if (LanguageService* service = languageService(); service != nullptr && request.quoted == CompletionQuoted::No) {
        LanguageTree snapshot = languageTreeOf(*world, root, tab);
        tab.module = snapshot.pathOf(tab.instance);
        if (!tab.module.empty()) {
            tab.askedRevision = tab.document.revision();
            tab.askedAt = tab.caret.head;
            tab.completionPrefix = request.prefix;
            service->requestCompletion(std::move(snapshot), tab.module, tab.caret.head, tab.askedRevision);
        }
    }
}

// **The answers that have come back**, each given to the tab it was asked for
// when that tab's text and caret are still where they were -- an answer about
// a revision somebody has typed past is dropped, not shown.
void takeLanguageAnswers(ScriptEditor& editor)
{
    LanguageService* service = languageService();
    if (service == nullptr)
        return;
    const auto tabOf = [&editor](const std::string& module) -> OpenScript* {
        for (std::size_t index = 0; index < editor.count(); ++index) {
            OpenScript* tab = editor.at(index);
            if (tab != nullptr && !tab->module.empty() && tab->module == module)
                return tab;
        }
        return nullptr;
    };

    if (std::optional<LanguageService::CompletionAnswer> answer = service->takeCompletion()) {
        OpenScript* tab = tabOf(answer->module);
        if (tab != nullptr && answer->revision == tab->document.revision() && answer->revision == tab->askedRevision &&
            answer->at == tab->caret.head && !tab->justAccepted) {
            // A word the tree already knew was whole stays closed: the
            // checker's longer names are exactly what closing it withheld.
            if (tab->completionWhole)
                tab->completions.clear();
            else
                mergeCompletions(tab->completions, answer->completions.items, answer->completions.inType,
                                 tab->completionPrefix);
            tab->completing = !tab->completions.empty();
            if (tab->completionIndex >= tab->completions.size())
                tab->completionIndex = 0;
        }
    }
    if (std::optional<LanguageService::SignatureAnswer> answer = service->takeSignature()) {
        OpenScript* tab = tabOf(answer->module);
        if (tab != nullptr && answer->revision == tab->document.revision() && answer->at == tab->caret.head)
            tab->signature = std::move(answer->signature);
    }
    if (std::optional<LanguageService::CheckAnswer> answer = service->takeCheck()) {
        OpenScript* tab = tabOf(answer->module);
        if (tab != nullptr && answer->revision == tab->document.revision() &&
            tab->checkedRevision != answer->revision && !tab->document.diagnosticsStale()) {
            tab->document.appendDiagnostics(answer->check.diagnostics);
            tab->checkedRevision = answer->revision;
        }
    }
}

// **Asks for the signature of the call the caret is in**, whenever the text or
// the caret moved -- the service answers nothing when it is in no call.
void askSignature(OpenScript& tab, const scene::World* world, core::InstanceId root)
{
    LanguageService* service = languageService();
    if (service == nullptr || world == nullptr)
        return;
    if (tab.signatureRevision == tab.document.revision() && tab.signatureAt == tab.caret.head)
        return;
    tab.signatureRevision = tab.document.revision();
    tab.signatureAt = tab.caret.head;
    // Kept while the answer is on its way, if the caret is still on its line:
    // a box that blinks off at every keystroke is worse than one a letter late.
    if (tab.signature.has_value() && tab.caret.head.line != tab.askedAt.line)
        tab.signature.reset();
    LanguageTree snapshot = languageTreeOf(*world, root, tab);
    tab.module = snapshot.pathOf(tab.instance);
    if (tab.module.empty())
        return;
    service->requestSignature(std::move(snapshot), tab.module, tab.caret.head, tab.document.revision());
}

// **The signature of the call being typed**, above the caret's line: the
// function's parameters with the one the caret is on in the accent, and its
// doc under them.
void drawSignature(const OpenScript& tab, const PaneMetrics& m, ImVec2 textOrigin)
{
    if (!tab.signature.has_value() || tab.signature->label.empty())
        return;
    const SignatureHelp& help = *tab.signature;
    const ThemePalette& p = currentTheme().palette;
    ImDrawList* draw = ImGui::GetForegroundDrawList();

    const float caretX =
        textOrigin.x + static_cast<float>(tab.document.cellOf(tab.caret.head.line, tab.caret.head.column)) * m.advance;
    const float lineTop = textOrigin.y + topOf(m, tab.caret.head.line);
    const float padding = m.advance;
    const float labelWidth = m.font->CalcTextSizeA(m.size, FLT_MAX, 0.0f, help.label.c_str()).x;
    // The doc's first paragraph: the rest is for the Properties grid.
    const std::string doc = help.doc.substr(0, help.doc.find("\n\n"));
    const float wrap = std::max(labelWidth, m.advance * 60.0f);
    const ImVec2 docSize = doc.empty() ? ImVec2(0.0f, 0.0f) : m.font->CalcTextSizeA(m.size, FLT_MAX, wrap, doc.c_str());
    const float width = std::max(labelWidth, docSize.x) + padding * 2.0f;
    const float height = m.lineHeight + (doc.empty() ? 0.0f : docSize.y + m.lineHeight * 0.4f) + padding * 0.6f;

    // Above the line, or below it where there is no room above.
    const ImGuiViewport* viewport = ImGui::GetWindowViewport();
    float top = lineTop - height - 2.0f;
    if (top < viewport->Pos.y)
        top = lineTop + m.lineHeight + 2.0f;
    const float left =
        std::max(viewport->Pos.x, std::min(caretX - padding, viewport->Pos.x + viewport->Size.x - width));
    const ImVec2 min(left, top);
    const ImVec2 max(left + width, top + height);
    draw->AddRectFilled(ImVec2(min.x + 3.0f, min.y + 4.0f), ImVec2(max.x + 3.0f, max.y + 4.0f), IM_COL32(0, 0, 0, 70),
                        4.0f);
    draw->AddRectFilled(min, max, col(p.surfaceRaised), 4.0f);
    draw->AddRect(min, max, col(p.accent, 0.55f), 4.0f);

    // The label in three runs: before the active parameter, it, after it.
    const float y = min.y + padding * 0.3f;
    float x = min.x + padding;
    const auto run = [&](std::size_t from, std::size_t to, ImU32 colour) {
        if (to <= from)
            return;
        const char* begin = help.label.c_str() + from;
        const char* end = help.label.c_str() + to;
        draw->AddText(m.font, m.size, ImVec2(x, y), colour, begin, end);
        x += m.font->CalcTextSizeA(m.size, FLT_MAX, 0.0f, begin, end).x;
    };
    if (help.active < help.parameters.size()) {
        const auto [from, to] = help.parameters[help.active];
        run(0, from, col(p.text));
        run(from, to, col(p.accent));
        run(to, help.label.size(), col(p.text));
    }
    else {
        run(0, help.label.size(), col(p.text));
    }
    if (!doc.empty())
        draw->AddText(m.font, m.size, ImVec2(min.x + padding, y + m.lineHeight * 1.2f), col(p.textMuted), doc.c_str(),
                      nullptr, wrap);
}

void acceptCompletion(OpenScript& tab, ScriptEditorCommands& out, std::size_t index)
{
    if (!tab.completing || tab.completionIndex >= tab.completions.size())
        return;
    // Only where the list was opened: a caret that has left the word it was
    // completing accepts nothing, and the list is closed.
    if (!(tab.caret.head == tab.completionReplace.end) || tab.caret.hasSelection()) {
        tab.completing = false;
        return;
    }
    const std::string& label = tab.completions[tab.completionIndex].label;
    tab.caret.head = tab.document.replace(tab.completionReplace, label);
    tab.caret.anchor = tab.caret.head;
    tab.caret.desiredColumn = tab.document.cellOf(tab.caret.head.line, tab.caret.head.column);
    tab.completing = false;
    tab.justAccepted = true;
    edited(out, index);
}

// How wide a string is IN THE CODE FACE, in pixels.
//
// **Not `ImGui::CalcTextSize`**, which measures in whatever font is current --
// the interface face, Inter -- while everything below draws with `m.font`,
// Cousine. The two disagree by enough that a popup sized against Inter and
// filled with Cousine spills its text out of its own highlight, which is what a
// human saw and reported.
//
// Counting cells rather than asking the font is exact here for the same reason
// the rest of the pane multiplies: the face is monospace, so a codepoint is one
// advance and there is nothing to measure.
[[nodiscard]] float codeWidth(const PaneMetrics& m, std::string_view text)
{
    std::size_t cells = 0;
    for (const char byte : text) {
        if ((static_cast<unsigned char>(byte) & 0xC0u) != 0x80u)
            ++cells;
    }
    return static_cast<float>(cells) * m.advance;
}

// **The zoom, top-right, for as long as it has just changed.**
//
// In the pane's own corner rather than in a status bar, because it is about the
// text under it -- and it says the percentage rather than drawing a slider,
// since the whole job is telling somebody the number they need in order to type
// their way back to it.
void drawZoomReadout(const ScriptEditor& editor, const PaneMetrics& m)
{
    constexpr float Seconds = 1.6f;
    constexpr float Fade = 0.4f;
    if (g_zoomShownFor >= Seconds)
        return;
    g_zoomShownFor += ImGui::GetIO().DeltaTime;

    const float left = Seconds - g_zoomShownFor;
    const float alpha = std::min(1.0f, left / Fade);

    char label[32]{};
    (void)std::snprintf(label, sizeof(label), "%d%%   Ctrl+0", static_cast<int>(std::round(editor.zoom() * 100.0f)));

    const ThemePalette& p = currentTheme().palette;
    const ImVec2 size = ImGui::CalcTextSize(label);
    const ImVec2 pad(ImGui::GetStyle().FramePadding.x * 2.0f, ImGui::GetStyle().FramePadding.y);
    // The pane's visible corner, not the document's -- a scrolled window must
    // not put this off the top of the screen.
    const ImVec2 corner(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - size.x - pad.x * 2.0f -
                            ImGui::GetStyle().ScrollbarSize,
                        ImGui::GetWindowPos().y + pad.y);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(corner, ImVec2(corner.x + size.x + pad.x * 2.0f, corner.y + size.y + pad.y * 2.0f),
                        col(p.surfaceRaised, alpha));
    draw->AddRect(corner, ImVec2(corner.x + size.x + pad.x * 2.0f, corner.y + size.y + pad.y * 2.0f),
                  col(p.border, alpha));
    draw->AddText(ImVec2(corner.x + pad.x, corner.y + pad.y), col(p.text, alpha), label);
    (void)m;
}

// Where the completion list and its prose go, worked out once and used by the
// drawing and by the click that picks a row -- two copies of this arithmetic
// would be a row that highlights under the pointer and accepts its neighbour.
struct CompletionLayout
{
    ImVec2 min;
    ImVec2 max;
    std::size_t firstRow = 0;
    std::size_t rows = 0;
    bool hasDoc = false;
    ImVec2 docMin;
    ImVec2 docMax;
    float docWrap = 0.0f;
};

[[nodiscard]] CompletionLayout layoutCompletions(const OpenScript& tab, const PaneMetrics& m, ImVec2 textOrigin)
{
    CompletionLayout layout;
    const float x = textOrigin.x + static_cast<float>(tab.document.cellOf(tab.completionReplace.begin.line,
                                                                          tab.completionReplace.begin.column)) *
                                       m.advance;
    const float y = textOrigin.y + topOf(m, tab.caret.head.line) + m.lineHeight;

    layout.rows = std::min<std::size_t>(tab.completions.size(), kMaxCompletionRows);
    // The first row shown, so an index past the eighth scrolls the window rather
    // than walking off the bottom of it.
    layout.firstRow = tab.completionIndex >= layout.rows ? tab.completionIndex - layout.rows + 1 : 0;

    float width = 0.0f;
    for (const Completion& completion : tab.completions)
        width = std::max(width, codeWidth(m, completion.label) + codeWidth(m, completion.detail));
    // Two cells of margin on each side and two between the label and the
    // detail, so the longest row has air rather than exactly fitting.
    width += m.advance * 6.0f;

    // **Kept on screen, which the caret's own position does not guarantee.** A
    // completion at the right-hand edge of a wide window would otherwise draw
    // its list off the side of the display.
    const ImGuiViewport* viewport = ImGui::GetWindowViewport();
    const float rightLimit = viewport->Pos.x + viewport->Size.x - m.advance;
    layout.min = ImVec2(std::min(x, std::max(viewport->Pos.x, rightLimit - width)), y);
    layout.max = ImVec2(layout.min.x + width, y + static_cast<float>(layout.rows) * m.lineHeight);

    // **The prose is as tall as its words**, measured in the face it is drawn
    // in and wrapped at the box: a fixed two lines put the third line of every
    // class's description outside the box, which is what was reported.
    const Completion& current = tab.completions[std::min(tab.completionIndex, tab.completions.size() - 1)];
    if (!current.doc.empty()) {
        const float docWidth = std::max(width, m.advance * 52.0f);
        layout.docWrap = docWidth - m.advance * 4.0f;
        const ImVec2 text = m.font->CalcTextSizeA(m.size, FLT_MAX, layout.docWrap, current.doc.c_str());
        layout.hasDoc = true;
        layout.docMin = ImVec2(layout.min.x, layout.max.y + 2.0f);
        layout.docMax = ImVec2(layout.min.x + docWidth, layout.docMin.y + text.y + m.lineHeight * 0.6f);
    }
    return layout;
}

// The row under `point`, when it is on the list.
[[nodiscard]] std::optional<std::size_t> completionRowAt(const OpenScript& tab, const CompletionLayout& layout,
                                                         const PaneMetrics& m, ImVec2 point)
{
    if (point.x < layout.min.x || point.x >= layout.max.x || point.y < layout.min.y || point.y >= layout.max.y)
        return std::nullopt;
    const std::size_t at = layout.firstRow + static_cast<std::size_t>((point.y - layout.min.y) / m.lineHeight);
    if (at >= tab.completions.size())
        return std::nullopt;
    return at;
}

// The rows, under the caret. Drawn as a child of the code pane rather than as a
// popup, because an ImGui popup steals the keyboard and the pane needs to keep
// receiving the letters that narrow the list.
//
// **On the raised surface with an accent edge**, not the pane's own colour: the
// prose box was drawn in exactly the code's ground and read as part of the file.
void drawCompletions(OpenScript& tab, const PaneMetrics& m, ImVec2 textOrigin)
{
    g_popupBounds = ImRect();
    if (!tab.completing || tab.completions.empty())
        return;

    const ThemePalette& p = currentTheme().palette;
    const CompletionLayout layout = layoutCompletions(tab, m, textOrigin);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const float rounding = 4.0f;
    const ImVec2 shadow(3.0f, 4.0f);
    draw->AddRectFilled(ImVec2(layout.min.x + shadow.x, layout.min.y + shadow.y),
                        ImVec2(layout.max.x + shadow.x, layout.max.y + shadow.y), IM_COL32(0, 0, 0, 70), rounding);
    draw->AddRectFilled(layout.min, layout.max, col(p.surfaceRaised), rounding);
    draw->AddRect(layout.min, layout.max, col(p.accent, 0.55f), rounding);

    for (std::size_t row = 0; row < layout.rows; ++row) {
        const std::size_t at = layout.firstRow + row;
        if (at >= tab.completions.size())
            break;
        const Completion& completion = tab.completions[at];
        const float rowY = layout.min.y + static_cast<float>(row) * m.lineHeight;
        if (at == tab.completionIndex)
            draw->AddRectFilled(ImVec2(layout.min.x + 1.0f, rowY), ImVec2(layout.max.x - 1.0f, rowY + m.lineHeight),
                                col(p.accent, 0.30f));
        draw->AddText(m.font, m.size, ImVec2(layout.min.x + m.advance * 2.0f, rowY), col(p.text),
                      completion.label.c_str());
        const float detailX = layout.max.x - codeWidth(m, completion.detail) - m.advance * 2.0f;
        draw->AddText(m.font, m.size, ImVec2(detailX, rowY), col(p.textMuted), completion.detail.c_str());
    }
    g_popupBounds = ImRect(layout.min, layout.max);

    // The prose for the highlighted row, under the list. The whole reason this
    // is worth more than a list of names.
    if (layout.hasDoc) {
        const Completion& current = tab.completions[std::min(tab.completionIndex, tab.completions.size() - 1)];
        draw->AddRectFilled(ImVec2(layout.docMin.x + shadow.x, layout.docMin.y + shadow.y),
                            ImVec2(layout.docMax.x + shadow.x, layout.docMax.y + shadow.y), IM_COL32(0, 0, 0, 70),
                            rounding);
        draw->AddRectFilled(layout.docMin, layout.docMax, col(p.surfaceRaised), rounding);
        draw->AddRect(layout.docMin, layout.docMax, col(p.border), rounding);
        draw->AddText(m.font, m.size, ImVec2(layout.docMin.x + m.advance * 2.0f, layout.docMin.y + m.lineHeight * 0.3f),
                      col(p.text), current.doc.c_str(), nullptr, layout.docWrap);
        g_popupBounds.Add(ImRect(layout.docMin, layout.docMax));
    }
}

// Alt+Up and Alt+Down. The TEXT half is `ScriptDocument::moveLines`, which is
// where it can be tested; what is left here is the caret, and the caret is the
// half that makes the gesture repeatable -- a selection that does not travel
// with its own text can only be moved once.
void moveLines(OpenScript& tab, ScriptEditorCommands& out, std::size_t index, int delta)
{
    ScriptDocument& doc = tab.document;
    const Range span = tab.caret.selection();
    const u32 first = span.begin.line;
    // A selection that ends at column zero has not reached that line: dragging
    // down to the start of line 9 highlights through line 8, and moving 9 with
    // it would move a line nothing is pointing at.
    const u32 last = span.end.line > first && span.end.column == 0 ? span.end.line - 1 : span.end.line;

    if (!doc.moveLines(first, last, delta))
        return;

    const auto shift = [delta](Position at) {
        return Position{static_cast<u32>(static_cast<int>(at.line) + delta), at.column};
    };
    tab.caret.anchor = doc.clamp(shift(tab.caret.anchor));
    tab.caret.head = doc.clamp(shift(tab.caret.head));
    tab.caret.desiredColumn = tab.caret.head.column;
    // Forces the pane to scroll to wherever the caret landed, which is what
    // makes holding the chord walk a line off the bottom of the view and take
    // the view with it.
    tab.shownCaret = Position{~0u, 0};
    out.edited.push_back(index);
}

// --- The editing keys every code editor shares ------------------------------

[[nodiscard]] bool spaceByte(char c) noexcept
{
    return c == ' ' || c == '\t';
}

// A word to the left: over any blanks, then over a run of word bytes or of
// punctuation. At a line's start it is the end of the line above.
[[nodiscard]] Position wordLeft(const ScriptDocument& doc, Position at)
{
    if (at.column == 0)
        return at.line == 0 ? at : Position{at.line - 1, doc.lineLength(at.line - 1)};
    const std::string_view text = doc.line(at.line);
    u32 column = std::min<u32>(at.column, static_cast<u32>(text.size()));
    while (column > 0 && spaceByte(text[column - 1]))
        --column;
    if (column > 0 && wordByte(text[column - 1])) {
        while (column > 0 && wordByte(text[column - 1]))
            --column;
    }
    else {
        while (column > 0 && !wordByte(text[column - 1]) && !spaceByte(text[column - 1]))
            --column;
    }
    return doc.clamp(Position{at.line, column});
}

// The same, to the right.
[[nodiscard]] Position wordRight(const ScriptDocument& doc, Position at)
{
    const std::string_view text = doc.line(at.line);
    if (at.column >= text.size())
        return at.line + 1 < doc.lineCount() ? Position{at.line + 1, 0} : at;
    u32 column = at.column;
    while (column < text.size() && spaceByte(text[column]))
        ++column;
    if (column < text.size() && wordByte(text[column])) {
        while (column < text.size() && wordByte(text[column]))
            ++column;
    }
    else {
        while (column < text.size() && !wordByte(text[column]) && !spaceByte(text[column]))
            ++column;
    }
    return doc.clamp(Position{at.line, column});
}

// The lines a selection covers: one that ends at column zero has not reached
// that line, which is what a drag down to the start of line 9 means.
[[nodiscard]] std::pair<u32, u32> selectedLines(const OpenScript& tab)
{
    const Range span = tab.caret.selection();
    const u32 first = span.begin.line;
    const u32 last = span.end.line > first && span.end.column == 0 ? span.end.line - 1 : span.end.line;
    return {first, last};
}

// After a line edit: a selection covers the whole lines again, and a caret
// keeps its place in the text by moving with how much its line grew.
void afterLineEdit(OpenScript& tab, u32 first, u32 last, u32 lineBefore)
{
    ScriptDocument& doc = tab.document;
    if (tab.caret.hasSelection()) {
        tab.caret.anchor = Position{first, 0};
        tab.caret.head = doc.clamp(Position{last, doc.lineLength(last)});
    }
    else {
        const auto grown = static_cast<int>(doc.lineLength(tab.caret.head.line)) - static_cast<int>(lineBefore);
        const int moved = static_cast<int>(tab.caret.head.column) + grown;
        tab.caret.head = doc.clamp(Position{tab.caret.head.line, static_cast<u32>(std::max(0, moved))});
        tab.caret.anchor = tab.caret.head;
    }
    tab.caret.desiredColumn = tab.caret.head.column;
}

// The bracket that pairs with the one at (or just before) the caret, or
// nothing. Counted across lines, a few thousand bytes at most.
[[nodiscard]] std::optional<Position> matchingBracket(const ScriptDocument& doc, Position at)
{
    const auto openerOf = [](char c) -> char { return c == ')' ? '(' : c == ']' ? '[' : c == '}' ? '{' : '\0'; };
    const std::string_view here = doc.line(at.line);
    Position from = at;
    char c = at.column < here.size() ? here[at.column] : '\0';
    if (closerOf(c) == '\0' && openerOf(c) == '\0' && at.column > 0) {
        from = Position{at.line, at.column - 1};
        c = here[from.column];
    }
    const bool forward = c == '(' || c == '[' || c == '{';
    const char want = forward ? closerOf(c) : openerOf(c);
    if (want == '\0')
        return std::nullopt;
    int depth = 0;
    int budget = 200000;
    u32 line = from.line;
    auto column = static_cast<std::int64_t>(from.column);
    while (budget-- > 0) {
        const std::string_view text = doc.line(line);
        while (column >= 0 && column < static_cast<std::int64_t>(text.size())) {
            const char at2 = text[static_cast<std::size_t>(column)];
            if (at2 == c)
                ++depth;
            else if (at2 == want && --depth == 0)
                return Position{line, static_cast<u32>(column)};
            column += forward ? 1 : -1;
        }
        if (forward) {
            if (line + 1 >= doc.lineCount())
                return std::nullopt;
            ++line;
            column = 0;
        }
        else {
            if (line == 0)
                return std::nullopt;
            --line;
            column = static_cast<std::int64_t>(doc.lineLength(line)) - 1;
        }
    }
    return std::nullopt;
}

// **Which keys a pass answers.** With one caret, all of them. With several,
// the keys that move or edit AT a caret run once per caret (`PerCaret`), and
// the ones about the document as a whole -- save, find, undo, the line
// commands -- run once, at the primary (`Global`), and put the carets back to
// one where what they did would leave the others pointing at the wrong text.
enum class KeyScope : core::u8
{
    All,
    PerCaret,
    Global,
};

void handleCaretKeys(OpenScript& tab, ScriptEditorCommands& out, std::size_t index, const PaneMetrics& m,
                     float paneHeight);
template <typename Collapse>
void handleDocumentKeys(OpenScript& tab, ScriptEditorCommands& out, std::size_t index, bool multi,
                        const Collapse& single);

void handleKeys(OpenScript& tab, ScriptEditorCommands& out, std::size_t index, const PaneMetrics& m, float paneHeight,
                KeyScope scope)
{
    ImGuiIO& io = ImGui::GetIO();
    const bool perCaret = scope != KeyScope::Global;
    const bool global = scope != KeyScope::PerCaret;
    const bool multi = scope != KeyScope::All;
    // A document-wide command, run with several carets: the others go.
    const auto single = [&tab, multi]() {
        if (multi)
            tab.extraCarets.clear();
    };
    ScriptDocument& doc = tab.document;

    // **While the list is up it owns the keys that move through it.** Anything
    // else would make Enter insert a newline under a highlighted row, which is
    // the one thing nobody means by it.
    if (global && tab.completing) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            tab.completing = false;
            return;
        }
        // **A bare arrow moves through the list; an arrow with a modifier is a
        // command** (the owner: Alt+Up moved the highlight instead of the
        // line). The list steps aside for it, and the chord runs below.
        const ImGuiIO& keys = ImGui::GetIO();
        const bool bare = !keys.KeyCtrl && !keys.KeyAlt && !keys.KeyShift && !keys.KeySuper;
        const bool arrow = ImGui::IsKeyPressed(ImGuiKey_DownArrow, true) || ImGui::IsKeyPressed(ImGuiKey_UpArrow, true);
        if (arrow && !bare) {
            tab.completing = false;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
            tab.completionIndex = (tab.completionIndex + 1) % tab.completions.size();
            return;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
            tab.completionIndex = tab.completionIndex == 0 ? tab.completions.size() - 1 : tab.completionIndex - 1;
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
            ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
            acceptCompletion(tab, out, index);
            return;
        }
    }

    // **Alt+Shift+Up/Down copies the lines** above or below themselves, with
    // the caret on the copy the arrow points at.
    if (global && (pressed(ScriptAction::CopyLineUp, true) || pressed(ScriptAction::CopyLineDown, true))) {
        single();
        const auto [first, last] = selectedLines(tab);
        const bool down = pressed(ScriptAction::CopyLineDown, true);
        if (doc.duplicateLines(first, last)) {
            if (down) {
                const u32 span = last - first + 1;
                tab.caret.anchor = doc.clamp(Position{tab.caret.anchor.line + span, tab.caret.anchor.column});
                tab.caret.head = doc.clamp(Position{tab.caret.head.line + span, tab.caret.head.column});
            }
            tab.shownCaret = Position{~0u, 0};
            edited(out, index);
        }
        return;
    }

    // **Shift+Alt+A: a block comment** around the selection, or out of one.
    if (global && pressed(ScriptAction::BlockComment)) {
        single();
        if (!tab.caret.hasSelection()) {
            const u32 line = tab.caret.head.line;
            tab.caret.anchor = Position{line, doc.indentOf(line)};
            tab.caret.head = Position{line, doc.lineLength(line)};
        }
        const Range span = tab.caret.selection();
        const std::string text = doc.textIn(span);
        const bool wrapped = text.size() >= 6 && text.starts_with("--[[") && text.ends_with("]]");
        const std::string next = wrapped ? text.substr(4, text.size() - 6) : "--[[" + text + "]]";
        const Position end = doc.replace(span, next);
        tab.caret.anchor = span.begin;
        tab.caret.head = end;
        tab.caret.desiredColumn = end.column;
        edited(out, index);
        return;
    }

    // **Before the plain arrows**, which would otherwise move the caret as well
    // as the line. Alt and not Ctrl+Alt: AltGr is Ctrl+Alt, and a Brazilian
    // keyboard would move a line every time somebody typed a bracket.
    if (global) {
        if (pressed(ScriptAction::MoveLineUp, true)) {
            single();
            moveLines(tab, out, index, -1);
            return;
        }
        if (pressed(ScriptAction::MoveLineDown, true)) {
            single();
            moveLines(tab, out, index, 1);
            return;
        }
    }

    // Everything from here to the Tab key happens AT a caret. Alt with an
    // arrow was a line command above, and must not also move the caret.
    if (perCaret && !(io.KeyAlt && !io.KeyCtrl))
        handleCaretKeys(tab, out, index, m, paneHeight);
    if (!global)
        return;
    handleDocumentKeys(tab, out, index, multi, single);
}

void handleCaretKeys(OpenScript& tab, ScriptEditorCommands& out, std::size_t index, const PaneMetrics& m,
                     float paneHeight)
{
    ImGuiIO& io = ImGui::GetIO();
    const bool shift = io.KeyShift;
    const bool ctrl = io.KeyCtrl && !io.KeyAlt;
    ScriptDocument& doc = tab.document;

    // Ctrl moves a word at a time, and Shift with it selects the words.
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true))
        placeCaret(tab, ctrl ? wordLeft(doc, tab.caret.head) : doc.prevColumn(tab.caret.head), shift);
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true))
        placeCaret(tab, ctrl ? wordRight(doc, tab.caret.head) : doc.nextColumn(tab.caret.head), shift);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))
        moveVertically(tab, -1, shift, m.view);
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))
        moveVertically(tab, 1, shift, m.view);

    const auto page = static_cast<int>(std::max(1.0f, std::floor(paneHeight / m.lineHeight)) - 1.0f);
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp, true))
        moveVertically(tab, -page, shift, m.view);
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown, true))
        moveVertically(tab, page, shift, m.view);

    if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
        // The first non-blank column before column zero, which is the one
        // somebody pressing Home in indented code actually wants.
        const u32 indent = doc.indentOf(tab.caret.head.line);
        const u32 target = tab.caret.head.column == indent ? 0u : indent;
        placeCaret(tab, ctrl ? Position{0, 0} : Position{tab.caret.head.line, target}, shift);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_End, false)) {
        const u32 last = doc.lineCount() - 1;
        placeCaret(tab,
                   ctrl ? Position{last, doc.lineLength(last)}
                        : Position{tab.caret.head.line, doc.lineLength(tab.caret.head.line)},
                   shift);
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true)) {
        if (tab.caret.hasSelection())
            eraseSelection(tab, out, index);
        else if (!(tab.caret.head == Position{0, 0})) {
            // Ctrl+Backspace takes the word to the left.
            Position from = ctrl ? wordLeft(doc, tab.caret.head) : doc.prevColumn(tab.caret.head);
            Position to = tab.caret.head;
            // Between a pair the editor just closed -- `(|)` -- both go, or the
            // closer would be left behind by a backspace nobody meant for it.
            const std::string_view text = doc.line(to.line);
            if (!ctrl && from.line == to.line && to.column < text.size() && from.column < text.size() &&
                closerOf(text[from.column]) != '\0' && closerOf(text[from.column]) == text[to.column]) {
                to = doc.nextColumn(to);
            }
            tab.caret.head = doc.erase(Range{from, to});
            tab.caret.anchor = tab.caret.head;
            tab.caret.desiredColumn = tab.caret.head.column;
            edited(out, index);
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, true)) {
        if (tab.caret.hasSelection())
            eraseSelection(tab, out, index);
        else {
            // Ctrl+Delete takes the word to the right.
            const Position to = ctrl ? wordRight(doc, tab.caret.head) : doc.nextColumn(tab.caret.head);
            if (!(to == tab.caret.head)) {
                tab.caret.head = doc.erase(Range{tab.caret.head, to});
                tab.caret.anchor = tab.caret.head;
                edited(out, index);
            }
        }
    }

    const bool below = pressed(ScriptAction::InsertLineBelow, true);
    const bool above = pressed(ScriptAction::InsertLineAbove, true);
    const bool enter = !io.KeyCtrl && !io.KeyAlt &&
                       (ImGui::IsKeyPressed(ImGuiKey_Enter, true) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, true));
    if (below || above) {
        // **Ctrl+Enter opens a line below, Ctrl+Shift+Enter one above**,
        // wherever the caret is on this one, at this line's indent.
        const u32 line = tab.caret.head.line;
        const std::string indent(doc.line(line).substr(0, doc.indentOf(line)));
        doc.breakUndoRun();
        if (above) {
            (void)doc.insert(Position{line, 0}, indent + "\n");
            tab.caret.head = Position{line, static_cast<u32>(indent.size())};
        }
        else {
            (void)doc.insert(Position{line, doc.lineLength(line)}, "\n" + indent);
            tab.caret.head = Position{line + 1, static_cast<u32>(indent.size())};
        }
        tab.caret.anchor = tab.caret.head;
        tab.caret.desiredColumn = tab.caret.head.column;
        edited(out, index);
        return;
    }
    if (enter) {
        // The new line starts where the old one's text did. Losing the indent on
        // every Enter is the single most irritating thing a code editor can do.
        const u32 indent = doc.indentOf(tab.caret.head.line);
        const std::string lineIndent(doc.line(tab.caret.head.line).substr(0, indent));
        // **One step deeper after a line that opens a block, and its `end`
        // written below** when the document has none for it yet (the owner:
        // "the automatic end, as other editors do"). Only with the caret at
        // the end of the line: an Enter in the middle of one is splitting it.
        //
        // What follows the caret may be only the closers the pairing wrote --
        // `Connect(function(child)|)` -- and then they move below, after the
        // `end`, which is where the call they close now ends.
        const Position at = tab.caret.head;
        const std::string_view rest = doc.line(at.line).substr(at.column);
        const bool atEnd = rest.find_first_not_of(" \t") == std::string_view::npos;
        const bool closersOnly = !atEnd && rest.find_first_not_of(") \t") == std::string_view::npos;
        const ScriptDocument::BlockBreak block =
            tab.caret.hasSelection() || !(atEnd || closersOnly) ? ScriptDocument::BlockBreak{} : doc.blockBreakAt(at);
        if (block.opens && !block.closer.empty()) {
            // One replace, so one undo takes the whole of it back.
            doc.breakUndoRun();
            (void)doc.replace(Range{at, Position{at.line, doc.lineLength(at.line)}},
                              "\n" + lineIndent + "\t" + "\n" + lineIndent + block.closer);
            tab.caret.head = Position{at.line + 1, static_cast<u32>(lineIndent.size() + 1)};
            tab.caret.anchor = tab.caret.head;
            tab.caret.desiredColumn = tab.caret.head.column;
            tab.completing = false;
            edited(out, index);
            return;
        }
        std::string text = "\n";
        text.append(lineIndent);
        if (block.opens)
            text.append("\t");
        insertText(tab, out, index, text);
    }
    // **Tab over lines indents them, Shift+Tab outdents** -- over a selection
    // or, for Shift+Tab, the caret's own line. Tab alone is four spaces.
    if (ImGui::IsKeyPressed(ImGuiKey_Tab, true)) {
        const Range span = tab.caret.selection();
        if (shift || (tab.caret.hasSelection() && span.begin.line != span.end.line)) {
            const auto [first, last] = selectedLines(tab);
            const u32 before = doc.lineLength(tab.caret.head.line);
            if (doc.indentLines(first, last, shift)) {
                afterLineEdit(tab, first, last, before);
                edited(out, index);
            }
        }
        else {
            insertText(tab, out, index, "\t");
        }
    }
}

template <typename Collapse>
void handleDocumentKeys(OpenScript& tab, ScriptEditorCommands& out, std::size_t index, bool multi,
                        const Collapse& single)
{
    ImGuiIO& io = ImGui::GetIO();
    const bool shift = io.KeyShift;
    ScriptDocument& doc = tab.document;

    if (pressed(ScriptAction::SelectAll)) {
        single();
        const u32 last = doc.lineCount() - 1;
        tab.caret.anchor = Position{0, 0};
        tab.caret.head = Position{last, doc.lineLength(last)};
    }
    // **Copy and cut with nothing selected take the whole line**, newline and
    // all, which is what every code editor does with them.
    if (!multi && pressed(ScriptAction::Copy)) {
        if (tab.caret.hasSelection())
            ImGui::SetClipboardText(doc.textIn(tab.caret.selection()).c_str());
        else
            ImGui::SetClipboardText((std::string(doc.line(tab.caret.head.line)) + "\n").c_str());
    }
    if (!multi && pressed(ScriptAction::Cut)) {
        if (tab.caret.hasSelection()) {
            ImGui::SetClipboardText(doc.textIn(tab.caret.selection()).c_str());
            eraseSelection(tab, out, index);
        }
        else {
            const u32 line = tab.caret.head.line;
            ImGui::SetClipboardText((std::string(doc.line(line)) + "\n").c_str());
            if (doc.deleteLines(line, line)) {
                tab.caret.head = doc.clamp(Position{line, 0});
                tab.caret.anchor = tab.caret.head;
                tab.caret.desiredColumn = 0;
                edited(out, index);
            }
        }
    }
    // **Ctrl+Shift+K deletes the lines** the caret or selection is on.
    if (pressed(ScriptAction::DeleteLine)) {
        single();
        const auto [first, last] = selectedLines(tab);
        if (doc.deleteLines(first, last)) {
            tab.caret.head = doc.clamp(Position{first, 0});
            tab.caret.anchor = tab.caret.head;
            tab.caret.desiredColumn = 0;
            edited(out, index);
        }
    }
    // **Ctrl+] and Ctrl+[ indent and outdent** the lines, selected or not.
    if (pressed(ScriptAction::Indent) || pressed(ScriptAction::Outdent)) {
        const bool outdent = pressed(ScriptAction::Outdent);
        single();
        const auto [first, last] = selectedLines(tab);
        const u32 before = doc.lineLength(tab.caret.head.line);
        if (doc.indentLines(first, last, outdent)) {
            afterLineEdit(tab, first, last, before);
            edited(out, index);
        }
    }
    // **Ctrl+L selects the line**, and again the next one.
    if (pressed(ScriptAction::SelectLine)) {
        single();
        const Range span = tab.caret.selection();
        const bool wholeLines = tab.caret.hasSelection() && span.begin.column == 0 && span.end.column == 0;
        const u32 from = wholeLines ? span.begin.line : tab.caret.head.line;
        const u32 through = wholeLines ? span.end.line : tab.caret.head.line + 1;
        tab.caret.anchor = Position{from, 0};
        tab.caret.head = through < doc.lineCount() ? Position{through, 0}
                                                   : Position{doc.lineCount() - 1, doc.lineLength(doc.lineCount() - 1)};
        tab.caret.desiredColumn = 0;
    }
    // **Ctrl+Shift+\ jumps to the bracket that pairs with this one.**
    if (pressed(ScriptAction::JumpToBracket)) {
        single();
        if (const std::optional<Position> pair = matchingBracket(doc, tab.caret.head); pair.has_value())
            placeCaret(tab, *pair, false);
    }
    if (!multi && pressed(ScriptAction::Paste)) {
        if (const char* text = ImGui::GetClipboardText(); text != nullptr && *text != '\0')
            insertText(tab, out, index, text);
    }
    // Ctrl+Shift+Z redoes as well as the redo chord: both are what hands
    // already know, and neither can mean anything else.
    const bool undo = pressed(ScriptAction::Undo);
    const bool redo =
        pressed(ScriptAction::Redo) || (io.KeyCtrl && shift && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_Z, false));
    if (undo || redo) {
        single();
        if (redo ? doc.redo(tab.caret.head) : doc.undo(tab.caret.head)) {
            tab.caret.anchor = tab.caret.head;
            edited(out, index);
        }
    }
    // **Ctrl+S still saves; it just saves the script.** The shell's own Ctrl+S
    // is suppressed while the pane is active, which is what makes one key mean
    // one thing wherever somebody presses it.
    if (pressed(ScriptAction::Save))
        out.save = index;
    // **Ctrl+W closes the tab** (the owner), through the same command its
    // cross sends, so a tab with unsaved work is handled one way.
    if (pressed(ScriptAction::CloseTab))
        out.close = index;

    // **Ctrl+/ comments the line, or every line the selection touches**, and
    // uncomments them when they all are.
    if (pressed(ScriptAction::ToggleComment) ||
        (io.KeyCtrl && !io.KeyAlt && !shift && ImGui::IsKeyPressed(ImGuiKey_KeypadDivide, false))) {
        single();
        const Range span = tab.caret.selection();
        const u32 first = span.begin.line;
        const u32 last = span.end.line > first && span.end.column == 0 ? span.end.line - 1 : span.end.line;
        const u32 before = doc.lineLength(tab.caret.head.line);
        if (doc.toggleComment(first, last)) {
            if (tab.caret.hasSelection()) {
                tab.caret.anchor = Position{first, 0};
                tab.caret.head = Position{last, doc.lineLength(last)};
            }
            else {
                const auto grown = static_cast<int>(doc.lineLength(tab.caret.head.line)) - static_cast<int>(before);
                const int moved = static_cast<int>(tab.caret.head.column) + grown;
                tab.caret.head = doc.clamp(Position{tab.caret.head.line, static_cast<u32>(std::max(0, moved))});
                tab.caret.anchor = tab.caret.head;
            }
            tab.caret.desiredColumn = tab.caret.head.column;
            edited(out, index);
        }
    }

    if (pressed(ScriptAction::Find)) {
        tab.findOpen = true;
        tab.focusFind = true;
        // Seeded from the selection, which is what somebody who highlighted a
        // word and pressed Ctrl+F is asking for.
        if (tab.caret.hasSelection() && tab.caret.selection().begin.line == tab.caret.selection().end.line)
            tab.findText = doc.textIn(tab.caret.selection());
    }
    if (pressed(ScriptAction::Replace)) {
        tab.findOpen = true;
        tab.replaceOpen = true;
        tab.focusFind = true;
    }
    if (pressed(ScriptAction::GoToLine))
        tab.findOpen = true;
}

// --- Multi-cursor editing ----------------------------------------------------
//
// The reference editor's gestures (its documentation's multi-cursor table),
// which are also every code editor's: Alt+click adds or removes a caret,
// Alt+drag adds one with a selection, Shift+Alt+drag selects a column,
// Ctrl+Alt+Up/Down add one above or below, Ctrl+D adds the next match of the
// selection, Shift+Alt+L every match, Shift+Alt+I splits a selection into its
// lines, Ctrl+U takes back the last one added, and Escape leaves only the
// primary.
//
// **One edit path, run once per caret.** Rather than teaching every key about
// several carets, a pass puts each caret in turn into `tab.caret`, runs the
// same single-caret code, and moves the carets it already visited by what the
// edit changed (`ScriptDocument::takeEditLog`). The last caret in the
// document goes first, so an edit never moves a caret still waiting its turn;
// and the whole pass is one undo group, so Ctrl+Z takes back one keystroke at
// every caret at once.

// Every caret with the primary first, which is how the pass hands them back.
[[nodiscard]] std::vector<Caret> allCarets(const OpenScript& tab)
{
    std::vector<Caret> all;
    all.reserve(tab.extraCarets.size() + 1);
    all.push_back(tab.caret);
    all.insert(all.end(), tab.extraCarets.begin(), tab.extraCarets.end());
    return all;
}

// **Carets that met become one.** Two carets typing at the same place would
// type everything twice; two selections that overlap would each replace the
// other's text. The primary wins a merge, so the view does not jump.
void mergeCarets(OpenScript& tab)
{
    if (tab.extraCarets.empty())
        return;
    std::vector<Caret> kept;
    for (const Caret& extra : tab.extraCarets) {
        const Range mine = extra.selection();
        const auto meets = [&mine](const Caret& other) {
            const Range theirs = other.selection();
            if (mine.empty() && theirs.empty())
                return mine.begin == theirs.begin;
            return mine.begin < theirs.end && theirs.begin < mine.end;
        };
        if (meets(tab.caret) || std::any_of(kept.begin(), kept.end(), meets))
            continue;
        kept.push_back(extra);
    }
    tab.extraCarets = std::move(kept);
}

// Runs `op(rank)` once at every caret, `rank` being the caret's place in the
// document from the top -- what a paste of one line per caret hands out by.
template <typename Op>
void forEachCaret(OpenScript& tab, const Op& op)
{
    std::vector<Caret> all = allCarets(tab);
    std::vector<std::size_t> order(all.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(),
              [&all](std::size_t a, std::size_t b) { return all[b].selection().begin < all[a].selection().begin; });

    ScriptDocument& doc = tab.document;
    doc.beginGroup();
    (void)doc.takeEditLog();
    for (std::size_t step = 0; step < order.size(); ++step) {
        const std::size_t at = order[step];
        tab.caret = all[at];
        op(order.size() - 1 - step);
        all[at] = tab.caret;
        const std::vector<ScriptDocument::EditSpan> log = doc.takeEditLog();
        // The carets already visited sit below this one, and are moved by
        // whatever it changed.
        for (std::size_t done = 0; done < step; ++done) {
            Caret& moved = all[order[done]];
            for (const ScriptDocument::EditSpan& edit : log) {
                moved.head = ScriptDocument::shifted(moved.head, edit);
                moved.anchor = ScriptDocument::shifted(moved.anchor, edit);
            }
            moved.head = doc.clamp(moved.head);
            moved.anchor = doc.clamp(moved.anchor);
        }
    }
    doc.endGroup();

    tab.caret = all.front();
    tab.extraCarets.assign(all.begin() + 1, all.end());
    mergeCarets(tab);
}

// Makes `added` the primary, the old primary one of the others: the view
// follows the caret just added, and Ctrl+U takes back exactly that one.
void addCaret(OpenScript& tab, Caret added)
{
    tab.extraCarets.push_back(tab.caret);
    tab.caret = added;
    tab.caret.desiredColumn = tab.document.cellOf(added.head.line, added.head.column);
    mergeCarets(tab);
}

// What Ctrl+D, Shift+Alt+L and the find box look for: the primary's
// selection, or -- when there is none -- the word under it, which the first
// press selects.
[[nodiscard]] std::optional<std::string> selectionNeedle(OpenScript& tab)
{
    if (tab.caret.hasSelection())
        return tab.document.textIn(tab.caret.selection());
    const Range word = tab.document.wordAt(tab.caret.head);
    if (word.empty())
        return std::nullopt;
    tab.caret.anchor = word.begin;
    tab.caret.head = word.end;
    tab.caret.desiredColumn = word.end.column;
    return std::nullopt;
}

// **The cursor commands.** Answers whether one of them took the keys this
// frame, so the ordinary handlers do not act on the same press.
bool handleCursorKeys(OpenScript& tab)
{
    ScriptDocument& doc = tab.document;
    const ScriptDocument::SearchOptions exact{.matchCase = true, .wholeWord = false};

    // Ctrl+D: the word under the caret, then the next place the selection
    // appears, as a caret of its own.
    if (pressed(ScriptAction::AddNextOccurrence)) {
        const std::optional<std::string> needle = selectionNeedle(tab);
        if (needle.has_value() && !needle->empty()) {
            const Range hit = doc.findNext(*needle, tab.caret.selection().end, exact);
            const bool taken =
                hit == tab.caret.selection() || std::any_of(tab.extraCarets.begin(), tab.extraCarets.end(),
                                                            [&hit](const Caret& c) { return c.selection() == hit; });
            if (!hit.empty() && !taken)
                addCaret(tab, Caret{.head = hit.end, .anchor = hit.begin});
        }
        return true;
    }
    // Shift+Alt+L: every place it appears.
    if (pressed(ScriptAction::SelectAllOccurrences)) {
        std::optional<std::string> needle = selectionNeedle(tab);
        if (!needle.has_value() && tab.caret.hasSelection())
            needle = doc.textIn(tab.caret.selection());
        if (needle.has_value() && !needle->empty()) {
            const Range primary = tab.caret.selection();
            tab.extraCarets.clear();
            for (const Range& hit : doc.findAll(*needle, exact)) {
                if (!(hit == primary))
                    tab.extraCarets.push_back(Caret{.head = hit.end, .anchor = hit.begin});
            }
        }
        return true;
    }
    // Shift+Alt+I: a caret at the end of every line the selection covers.
    if (pressed(ScriptAction::SplitIntoLines)) {
        const Range span = tab.caret.selection();
        if (span.begin.line != span.end.line) {
            tab.extraCarets.clear();
            for (u32 line = span.begin.line; line < span.end.line; ++line) {
                const Position end{line, doc.lineLength(line)};
                tab.extraCarets.push_back(Caret{.head = end, .anchor = end});
            }
            tab.caret = Caret{.head = span.end, .anchor = span.end};
        }
        return true;
    }
    // Ctrl+Alt+Up/Down: one more caret above the highest or below the lowest,
    // at the column the primary is aiming for.
    if (pressed(ScriptAction::AddCaretAbove, true) || pressed(ScriptAction::AddCaretBelow, true)) {
        const bool up = pressed(ScriptAction::AddCaretAbove, true);
        u32 edge = tab.caret.head.line;
        for (const Caret& c : tab.extraCarets)
            edge = up ? std::min(edge, c.head.line) : std::max(edge, c.head.line);
        if (up ? edge > 0 : edge + 1 < doc.lineCount()) {
            const u32 line = up ? edge - 1 : edge + 1;
            const Position at = doc.clamp(Position{line, doc.columnOfCell(line, tab.caret.desiredColumn)});
            const u32 wanted = tab.caret.desiredColumn;
            addCaret(tab, Caret{.head = at, .anchor = at});
            tab.caret.desiredColumn = wanted;
        }
        return true;
    }
    // Ctrl+U: take back the caret added last -- the primary -- and make the
    // one before it primary again.
    if (pressed(ScriptAction::RemoveLastCaret)) {
        if (!tab.extraCarets.empty()) {
            tab.caret = tab.extraCarets.back();
            tab.extraCarets.pop_back();
        }
        return true;
    }
    return false;
}

// **Copy, cut and paste with several carets.** Copy joins what each caret
// holds, top to bottom, one per line -- whole lines where a caret holds
// nothing. A paste of exactly as many lines as there are carets hands one line
// to each, which is what makes copy-at-three-carets, paste-at-three-carets do
// the obvious thing; any other paste goes in whole at every caret.
void handleMultiClipboard(OpenScript& tab, ScriptEditorCommands& out, std::size_t index)
{
    const bool copy = pressed(ScriptAction::Copy);
    const bool cut = pressed(ScriptAction::Cut);
    const bool paste = pressed(ScriptAction::Paste);
    if (!copy && !cut && !paste)
        return;
    ScriptDocument& doc = tab.document;

    if (copy || cut) {
        std::vector<Caret> all = allCarets(tab);
        std::sort(all.begin(), all.end(),
                  [](const Caret& a, const Caret& b) { return a.selection().begin < b.selection().begin; });
        const bool anySelection = std::any_of(all.begin(), all.end(), [](const Caret& c) { return c.hasSelection(); });
        std::string joined;
        for (std::size_t i = 0; i < all.size(); ++i) {
            if (i > 0)
                joined.push_back('\n');
            joined += anySelection ? doc.textIn(all[i].selection()) : std::string(doc.line(all[i].head.line));
        }
        ImGui::SetClipboardText(joined.c_str());
        if (cut && anySelection)
            forEachCaret(tab, [&](std::size_t) { eraseSelection(tab, out, index); });
        return;
    }

    const char* clip = ImGui::GetClipboardText();
    if (clip == nullptr || *clip == '\0')
        return;
    const std::string text(clip);
    std::vector<std::string> pieces;
    for (std::size_t start = 0;;) {
        const std::size_t newline = text.find('\n', start);
        std::string piece = text.substr(start, newline == std::string::npos ? std::string::npos : newline - start);
        if (!piece.empty() && piece.back() == '\r')
            piece.pop_back();
        pieces.push_back(std::move(piece));
        if (newline == std::string::npos)
            break;
        start = newline + 1;
    }
    const bool spread = pieces.size() == tab.extraCarets.size() + 1;
    forEachCaret(tab, [&](std::size_t rank) { insertText(tab, out, index, spread ? pieces[rank] : text); });
}

// --- Drawing -----------------------------------------------------------------

// The outermost block that opens on `line`, if one does -- what its arrow
// folds.
[[nodiscard]] const ScriptDocument::FoldRange* foldAt(const OpenScript& tab, u32 line) noexcept
{
    const auto found =
        std::lower_bound(tab.foldRanges.begin(), tab.foldRanges.end(), line,
                         [](const ScriptDocument::FoldRange& range, u32 wanted) { return range.first < wanted; });
    return found != tab.foldRanges.end() && found->first == line ? &*found : nullptr;
}

[[nodiscard]] bool isFolded(const OpenScript& tab, u32 line) noexcept
{
    return std::find(tab.folded.begin(), tab.folded.end(), line) != tab.folded.end();
}

// **Where the fold arrow is**: the gap between the number and the code, which
// is two and a half cells wide (see `metricsFor`). A click there folds; the
// rest of the gutter still arms a breakpoint.
[[nodiscard]] float foldZoneLeft(const PaneMetrics& m) noexcept
{
    return m.gutter - m.advance * 2.4f;
}

void drawGutter(const OpenScript& tab, const ScriptEditor& editor, const DebugView& debug, const PaneMetrics& m,
                ImDrawList* draw, ImVec2 origin, u32 line, bool current)
{
    const ThemePalette& p = currentTheme().palette;
    const float y = origin.y + topOf(m, line);

    // **Where execution is stopped**, drawn as a band rather than a marker in
    // the margin: a person looking for it is looking at the code, not at the
    // numbers. Luau reports lines from one and a document counts them from
    // zero, which is the whole of the conversion here.
    if (debug.parked && debug.chunk == tab.chunk && debug.line == line + 1) {
        draw->AddRectFilled(ImVec2(origin.x, y), ImVec2(origin.x + m.gutter, y + m.lineHeight), col(p.warning, 0.35f));
    }

    char number[16]{};
    (void)std::snprintf(number, sizeof(number), "%u", line + 1);
    // Measured in the code face it is drawn in, and ending two and a half
    // cells short of the code (see `metricsFor`).
    const float width = codeWidth(m, number);
    draw->AddText(m.font, m.size, ImVec2(origin.x + m.gutter - m.advance * 2.5f - width, y),
                  current ? ecol(ScriptColor::Text) : ecol(ScriptColor::LineNumber), number);

    // **The fold arrow** (the owner: "a button to open and close a block"):
    // pointing down on a block that is open, right on one that is folded --
    // and the folded one louder, because it is hiding something.
    if (foldAt(tab, line) != nullptr) {
        const bool folded = isFolded(tab, line);
        const float size = std::min(m.lineHeight * 0.28f, m.advance * 0.7f);
        const ImVec2 centre(origin.x + foldZoneLeft(m) + m.advance * 1.1f, y + m.lineHeight * 0.5f);
        const ImU32 colour = folded ? ecol(ScriptColor::Text) : ecol(ScriptColor::LineNumber, 0.8f);
        if (folded)
            draw->AddTriangleFilled(ImVec2(centre.x - size * 0.6f, centre.y - size),
                                    ImVec2(centre.x + size * 0.8f, centre.y),
                                    ImVec2(centre.x - size * 0.6f, centre.y + size), colour);
        else
            draw->AddTriangleFilled(ImVec2(centre.x - size, centre.y - size * 0.6f),
                                    ImVec2(centre.x + size, centre.y - size * 0.6f),
                                    ImVec2(centre.x, centre.y + size * 0.8f), colour);
    }

    if (editor.hasBreakpoint(tab.chunk, line)) {
        // A filled dot on the left of the number, at the line's own height so it
        // scales with the interface rather than being a fixed number of pixels.
        const float radius = m.lineHeight * 0.22f;
        draw->AddCircleFilled(ImVec2(origin.x + radius * 2.0f, y + m.lineHeight * 0.5f), radius, col(p.danger));
    }
}

void drawLine(const OpenScript& tab, const PaneMetrics& m, ImDrawList* draw, ImVec2 textOrigin, u32 line,
              float paneWidth)
{
    const std::string_view text = tab.document.line(line);
    if (text.empty())
        return;

    const float y = textOrigin.y + topOf(m, line);
    // **Only the columns that can be seen.** A minified line of a hundred
    // thousand bytes is one `AddText` of a hundred thousand glyphs otherwise,
    // and the clip rectangle would throw away the work after it was done.
    const auto lastVisibleCell = static_cast<u32>(paneWidth / m.advance) + 2u;

    // `from` and `to` are BYTE columns; where a run is DRAWN is its CELL. The
    // two differ the moment a line holds anything outside ASCII, and treating
    // them as one is what put a space after every accented letter.
    //
    // A tab is not drawn: it is the gap to its stop, so a run is drawn a piece
    // at a time between tabs, each piece at its own cell.
    const auto run = [&](u32 from, u32 to, ImU32 colour) {
        while (from < to) {
            while (from < to && text[from] == '\t')
                ++from;
            u32 end = from;
            while (end < to && text[end] != '\t')
                ++end;
            if (end > from) {
                const u32 cell = tab.document.cellOf(line, from);
                if (cell >= lastVisibleCell)
                    return;
                draw->AddText(m.font, m.size, ImVec2(textOrigin.x + static_cast<float>(cell) * m.advance, y), colour,
                              text.data() + from, text.data() + end);
            }
            from = end;
        }
    };

    // **Indentation guides**, in the whitespace colour: a thin line at each
    // four-space step of the line's own indent, which is what makes a long
    // block's shape readable at a glance.
    const u32 indent = tab.document.cellOf(line, tab.document.indentOf(line));
    for (u32 step = kTabWidth; step <= indent && step <= lastVisibleCell; step += kTabWidth) {
        const float x = textOrigin.x + static_cast<float>(step - kTabWidth) * m.advance + 1.0f;
        draw->AddLine(ImVec2(x, y), ImVec2(x, y + m.lineHeight), ecol(ScriptColor::Whitespace), 1.0f);
    }

    static std::vector<StyledRun> styled;
    styleLine(text, tab.document.tokens(line), styled);
    const ImU32 plain = ecol(ScriptColor::Text);
    u32 column = 0;
    for (const StyledRun& piece : styled) {
        // The gaps between runs are whitespace the lexer did not name, drawn in
        // the pane's own foreground so a tab or a stray byte is still visible.
        run(column, piece.column, plain);
        run(piece.column, piece.column + piece.length, ecol(piece.color));
        column = std::max(column, piece.column + piece.length);
    }
    run(column, static_cast<u32>(text.size()), plain);
}

void drawSelection(const OpenScript& tab, const Caret& caret, const PaneMetrics& m, ImDrawList* draw, ImVec2 textOrigin,
                   u32 first, u32 last)
{
    if (!caret.hasSelection())
        return;

    const Range span = caret.selection();
    const ImU32 colour = ecol(ScriptColor::Selection);
    for (u32 line = std::max(first, span.begin.line); line <= std::min(last, span.end.line); ++line) {
        if (hiddenLine(m, line))
            continue;
        const u32 from = tab.document.cellOf(line, line == span.begin.line ? span.begin.column : 0u);
        // A line in the middle of a selection is highlighted one cell past its
        // end, so a multi-line selection reads as covering the newline it holds.
        const u32 to =
            line == span.end.line ? tab.document.cellOf(line, span.end.column) : tab.document.cellCount(line) + 1u;
        const float y = textOrigin.y + topOf(m, line);
        draw->AddRectFilled(ImVec2(textOrigin.x + static_cast<float>(from) * m.advance, y),
                            ImVec2(textOrigin.x + static_cast<float>(to) * m.advance, y + m.lineHeight), colour);
    }
}

// **How long a line has to sit still before what is wrong with it is shown.**
//
// The owner: "it shows the error on the line before I have finished writing
// it". A half-typed `createM` IS an unknown global, and saying so while the
// hand is still moving is noise; both reference editors analyse on a pause
// rather than per keystroke. Other lines are shown at once -- an edit here can
// break something there, and that is worth knowing now.
constexpr double kDiagnosticSettleSeconds = 1.2;

//
// **And a syntax error anywhere below it** (the owner, with an open `(` still
// being typed and "Expected ')' ... got 'end'" shown on the line after): the
// parser reports a half-typed line where it gives up, which is later in the
// file, so the line alone was not enough to hold back.
[[nodiscard]] bool settling(const OpenScript& tab, const Diagnostic& diagnostic) noexcept
{
    if (ImGui::GetTime() - tab.lastEditTime >= kDiagnosticSettleSeconds)
        return false;
    return diagnostic.at.line == tab.lastEditLine || (diagnostic.syntax && diagnostic.at.line > tab.lastEditLine);
}

void drawDiagnostics(const OpenScript& tab, const PaneMetrics& m, ImDrawList* draw, ImVec2 textOrigin, u32 first,
                     u32 last)
{
    const ImU32 colour = ecol(ScriptColor::ErrorUnderline);
    const ImU32 warned = ecol(ScriptColor::WarningUnderline, 0.75f);
    const std::span<const Diagnostic> all = tab.document.diagnostics();

    // **One message per line, the worst first.** The list is the parser's,
    // then the tree lint's, then the type checker's, so one line can appear in
    // it more than once and far apart -- and each speaker drew at the same
    // place, which is the overlap that was reported. Collected per line, the
    // line says its most serious problem and how many more it has; the
    // tooltip over each underline still says each one.
    struct Spoken
    {
        u32 line = 0;
        const Diagnostic* worst = nullptr;
        u32 count = 0;
    };
    std::vector<Spoken> spoken;

    for (const Diagnostic& diagnostic : all) {
        if (diagnostic.at.line < first || diagnostic.at.line > last || settling(tab, diagnostic))
            continue;
        // **The same mark twice is one mark**: the tree lint and the type
        // checker both name an unknown global, at the same name.
        const bool repeated = std::any_of(all.data(), &diagnostic, [&diagnostic](const Diagnostic& earlier) {
            return earlier.at == diagnostic.at && earlier.length == diagnostic.length;
        });
        if (repeated || hiddenLine(m, diagnostic.at.line))
            continue;

        const float y = textOrigin.y + topOf(m, diagnostic.at.line) + m.lineHeight - 2.0f;
        const float x0 = textOrigin.x +
                         static_cast<float>(tab.document.cellOf(diagnostic.at.line, diagnostic.at.column)) * m.advance;
        // **The extent the diagnostic knows about**, which for a lint is the
        // name it is talking about. A parse error reports where the parser gave
        // up rather than how much is wrong, so it says nothing and gets the
        // rest of the line.
        const float x1 =
            diagnostic.length > 0
                ? textOrigin.x + static_cast<float>(tab.document.cellOf(diagnostic.at.line,
                                                                        diagnostic.at.column + diagnostic.length)) *
                                     m.advance
                : std::max(x0 + m.advance,
                           textOrigin.x + static_cast<float>(tab.document.cellCount(diagnostic.at.line)) * m.advance);
        // **A warning is not a quieter error.** An error means this will not
        // compile; a warning means it will and probably should not have to.
        // Drawing them the same colour is how a panel teaches somebody to
        // ignore both.
        const ImU32 mark = diagnostic.severity == Severity::Warning ? warned : colour;
        // A straight underline rather than a wave: at one physical pixel a wave
        // is a dotted line that reads as a rendering fault.
        draw->AddLine(ImVec2(x0, y), ImVec2(x1, y), mark, 1.0f);

        const auto found = std::find_if(spoken.begin(), spoken.end(),
                                        [&diagnostic](const Spoken& s) { return s.line == diagnostic.at.line; });
        if (found == spoken.end()) {
            spoken.push_back(Spoken{diagnostic.at.line, &diagnostic, 1});
        }
        else {
            ++found->count;
            if (diagnostic.severity == Severity::Error && found->worst->severity == Severity::Warning)
                found->worst = &diagnostic;
        }

        // And the whole message under the pointer, wrapped, for one too long
        // to fit beside the code.
        const float lineTop = textOrigin.y + topOf(m, diagnostic.at.line);
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        if (ImGui::IsWindowHovered() && mouse.x >= x0 && mouse.x <= x1 && mouse.y >= lineTop &&
            mouse.y <= lineTop + m.lineHeight) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
            ImGui::TextUnformatted(diagnostic.message.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

    // **What is wrong, at the end of the line** (the owner's report: "it
    // underlines in red and does not say why"): three cells past the code,
    // quieter than the code.
    for (const Spoken& line : spoken) {
        const float lineTop = textOrigin.y + topOf(m, line.line);
        const float after = textOrigin.x + static_cast<float>(tab.document.cellCount(line.line) + 3u) * m.advance;
        std::string text = line.worst->message;
        if (line.count > 1)
            text += "  (+" + std::to_string(line.count - 1) + " more)";
        draw->AddText(m.font, m.size, ImVec2(after, lineTop),
                      ecol(line.worst->severity == Severity::Warning ? ScriptColor::WarningUnderline
                                                                     : ScriptColor::ErrorUnderline,
                           0.85f),
                      text.c_str());
    }
}

// --- Find, replace and go to ------------------------------------------------

// Copies a `std::string` into an ImGui text field and back, which is the shape
// every other dialog in this shell already uses: ImGui wants a buffer and the
// model wants a string, and the conversion belongs at the one place they meet.
bool stringField(const char* label, const char* hint, std::string& value, float width)
{
    char buffer[256]{};
    const std::size_t count = std::min(value.size(), sizeof(buffer) - 1);
    std::memcpy(buffer, value.data(), count);

    ImGui::SetNextItemWidth(width);
    const bool changed = ImGui::InputTextWithHint(label, hint, buffer, sizeof(buffer));
    if (changed)
        value.assign(buffer);
    return changed;
}

[[nodiscard]] ScriptDocument::SearchOptions searchOptionsOf(const OpenScript& tab)
{
    return ScriptDocument::SearchOptions{.matchCase = tab.matchCase, .wholeWord = tab.wholeWord, .regex = tab.regex};
}

void stepMatch(OpenScript& tab, bool forward)
{
    if (tab.findText.empty())
        return;
    const ScriptDocument::SearchOptions options = searchOptionsOf(tab);
    // Stepping from the END of the last match going forward and from its START
    // going back, so pressing Enter twice does not land on the same hit.
    const Position from = forward ? tab.caret.selection().end : tab.caret.selection().begin;
    const Range hit = forward ? tab.document.findNext(tab.findText, from, options)
                              : tab.document.findPrevious(tab.findText, from, options);
    if (hit.empty())
        return;
    tab.lastMatch = hit;
    tab.caret.anchor = hit.begin;
    tab.caret.head = hit.end;
    tab.caret.desiredColumn = hit.end.column;
}

// What the find matches, recomputed when the text, the query or the options
// change rather than every frame: the highlights, the count and "3 of 12" all
// read it.
void refreshMatches(OpenScript& tab)
{
    const ScriptDocument::SearchOptions options = searchOptionsOf(tab);
    const std::string key = tab.findText + (options.matchCase ? "\x01" : "\x02") +
                            (options.wholeWord ? "\x01" : "\x02") + (options.regex ? "\x01" : "\x02");
    if (tab.matchesRevision == tab.document.revision() && tab.matchesKey == key)
        return;
    tab.matchesRevision = tab.document.revision();
    tab.matchesKey = key;
    tab.matches = tab.findOpen ? tab.document.findAll(tab.findText, options) : std::vector<Range>{};
}

// The match the selection IS, when it is one: what "3 of 12" counts from and
// what Replace replaces.
[[nodiscard]] std::optional<std::size_t> currentMatch(const OpenScript& tab)
{
    if (!tab.caret.hasSelection())
        return std::nullopt;
    const Range selected = tab.caret.selection();
    for (std::size_t index = 0; index < tab.matches.size(); ++index) {
        if (tab.matches[index] == selected)
            return index;
    }
    return std::nullopt;
}

// A toggle drawn as a small labelled button, lit when on.
bool findToggle(const char* label, const char* tip, bool& value)
{
    const ThemePalette& p = currentTheme().palette;
    if (value)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(p.accent.r, p.accent.g, p.accent.b, 0.45f));
    const bool pressed = ImGui::SmallButton(label);
    if (value)
        ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tip);
    if (pressed)
        value = !value;
    return pressed;
}

// **The find box, at the pane's top right, over the code** -- the owner: "it
// should be like the other editors: find, replace, and the rest". Two rows: the
// query with its options, count and steps, and -- opened by the arrow or by
// Ctrl+H -- the replacement with Replace and Replace All. Enter finds the next,
// Shift+Enter the previous, Escape closes it and gives the code the keys back.
bool scriptAction(const ScriptActionButton& button, std::string_view id, const char* label, bool compact = false)
{
    return button ? button(id, label, compact) : ImGui::Button(label);
}

void drawFindBox(OpenScript& tab, ScriptEditorCommands& out, std::size_t index, ImVec2 paneMin, float paneWidth,
                 const ScriptActionButton& actionButton)
{
    if (!tab.findOpen)
        return;
    refreshMatches(tab);

    const ThemePalette& p = currentTheme().palette;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float rowHeight = ImGui::GetFrameHeight();
    const float width = std::min(paneWidth - 24.0f, ImGui::GetFontSize() * 34.0f);
    const float height = rowHeight * (tab.replaceOpen ? 2.0f : 1.0f) +
                         style.ItemSpacing.y * (tab.replaceOpen ? 1.0f : 0.0f) + style.WindowPadding.y * 2.0f;
    ImGui::SetCursorScreenPos(ImVec2(paneMin.x + paneWidth - width - 18.0f, paneMin.y + 6.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(p.surfaceRaised.r, p.surfaceRaised.g, p.surfaceRaised.b, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(p.accent.r, p.accent.g, p.accent.b, 0.55f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
    if (ImGui::BeginChild("##find-box", ImVec2(width, height),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar)) {
        const ScriptDocument::SearchOptions options = searchOptionsOf(tab);
        const bool valid = tab.findText.empty() || ScriptDocument::searchable(tab.findText, options);

        // The chevron that opens the replace row.
        if (ImGui::ArrowButton("##replace-toggle", tab.replaceOpen ? ImGuiDir_Down : ImGuiDir_Right))
            tab.replaceOpen = !tab.replaceOpen;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("replace  (Ctrl+H)");
        ImGui::SameLine();

        // **Sized to leave everything after the field its room, measured item
        // by item** (reported: the close button hung outside the box). The
        // three toggles are buttons with padding on both sides, the count is
        // as wide as its longest text, and the two steps and the close are
        // square -- seven items after the field, so seven gaps.
        const float toggles = ImGui::CalcTextSize("Aa").x + ImGui::CalcTextSize("ab").x + ImGui::CalcTextSize(".*").x +
                              style.FramePadding.x * 6.0f;
        const float count = std::max(ImGui::CalcTextSize("999 of 999").x, ImGui::CalcTextSize("No results").x);
        const float trailing = toggles + count + rowHeight * 3.0f + style.ItemSpacing.x * 7.0f + 2.0f;
        const float field = std::max(90.0f, ImGui::GetContentRegionAvail().x - trailing);
        if (!valid)
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(p.danger.r, p.danger.g, p.danger.b, 0.35f));
        if (tab.focusFind) {
            ImGui::SetKeyboardFocusHere();
            tab.focusFind = false;
        }
        const bool entered = stringField("##find-text", "find", tab.findText, field);
        const bool findActive = ImGui::IsItemActive();
        if (!valid) {
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("not a valid regular expression");
        }
        refreshMatches(tab);
        if (entered)
            stepMatch(tab, !ImGui::GetIO().KeyShift);
        ImGui::SameLine();
        (void)findToggle("Aa", "match case", tab.matchCase);
        ImGui::SameLine();
        (void)findToggle("ab", "whole word", tab.wholeWord);
        ImGui::SameLine();
        (void)findToggle(".*", "regular expression", tab.regex);
        ImGui::SameLine();

        // "3 of 12", or how many when the caret is on none of them.
        const std::optional<std::size_t> current = currentMatch(tab);
        if (tab.findText.empty())
            ImGui::TextDisabled("        ");
        else if (tab.matches.empty())
            ImGui::TextColored(ImVec4(p.danger.r, p.danger.g, p.danger.b, 1.0f), "No results");
        else if (current.has_value())
            ImGui::Text("%zu of %zu", *current + 1, tab.matches.size());
        else
            ImGui::TextDisabled("%zu found", tab.matches.size());
        ImGui::SameLine();
        ImGui::BeginDisabled(tab.matches.empty());
        if (ImGui::ArrowButton("##find-previous", ImGuiDir_Up))
            stepMatch(tab, false);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("previous  (Shift+Enter)");
        ImGui::SameLine();
        if (ImGui::ArrowButton("##find-next", ImGuiDir_Down))
            stepMatch(tab, true);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("next  (Enter)");
        ImGui::EndDisabled();
        ImGui::SameLine();
        // Square, the size of the two steps beside it, with the cross drawn
        // in it: an icon at the text's size read as a speck at the edge of
        // the box (reported).
        bool close = ImGui::Button("##find-close", ImVec2(rowHeight, rowHeight));
        {
            const ImVec2 lo = ImGui::GetItemRectMin();
            const ImVec2 hi = ImGui::GetItemRectMax();
            const float inset = rowHeight * 0.32f;
            const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddLine(ImVec2(lo.x + inset, lo.y + inset), ImVec2(hi.x - inset, hi.y - inset), ink, 1.5f);
            draw->AddLine(ImVec2(hi.x - inset, lo.y + inset), ImVec2(lo.x + inset, hi.y - inset), ink, 1.5f);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("close  (Escape)");

        if (tab.replaceOpen) {
            ImGui::Dummy(ImVec2(ImGui::GetFrameHeight(), rowHeight));
            ImGui::SameLine();
            (void)stringField("##replace-text", "replace", tab.replaceText, field);
            const bool replaceActive = ImGui::IsItemActive();
            ImGui::SameLine();
            ImGui::BeginDisabled(tab.matches.empty());
            // **Replace takes the match the caret is on**, then moves to the
            // next -- the first press only selects one, when the caret is on
            // none, so nothing is replaced that was never shown.
            if (scriptAction(actionButton, icons::ActionReplace, "Replace")) {
                if (const std::optional<std::size_t> on = currentMatch(tab); on.has_value()) {
                    const Range done =
                        tab.document.replaceMatch(tab.matches[*on], tab.findText, tab.replaceText, options);
                    tab.caret.anchor = tab.caret.head = done.end;
                    edited(out, index);
                    refreshMatches(tab);
                }
                stepMatch(tab, true);
            }
            ImGui::SameLine();
            if (scriptAction(actionButton, icons::ActionReplaceAll, "Replace all")) {
                if (tab.document.replaceAll(tab.findText, tab.replaceText, options) > 0) {
                    tab.caret = Caret{};
                    edited(out, index);
                    refreshMatches(tab);
                }
            }
            ImGui::EndDisabled();
            if (replaceActive && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                close = true;
        }
        if (findActive && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            close = true;

        if (close) {
            tab.findOpen = false;
            tab.replaceOpen = false;
            tab.matches.clear();
            tab.matchesKey.clear();
            tab.claimCaret = true;
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

// Every match on the visible lines, under the text; the one the caret is on
// in the accent, the rest in the warning's colour.
void drawMatches(const OpenScript& tab, const PaneMetrics& m, ImDrawList* draw, ImVec2 textOrigin, u32 first, u32 last)
{
    if (!tab.findOpen || tab.matches.empty())
        return;
    const ThemePalette& p = currentTheme().palette;
    const Range selected = tab.caret.selection();
    for (const Range& match : tab.matches) {
        if (match.begin.line < first || match.begin.line > last || hiddenLine(m, match.begin.line))
            continue;
        const float y = textOrigin.y + topOf(m, match.begin.line);
        const float x0 =
            textOrigin.x + static_cast<float>(tab.document.cellOf(match.begin.line, match.begin.column)) * m.advance;
        const float x1 =
            textOrigin.x + static_cast<float>(tab.document.cellOf(match.end.line, match.end.column)) * m.advance;
        const bool current = match == selected;
        draw->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + m.lineHeight),
                            current ? col(p.accent, 0.45f) : ecol(ScriptColor::MatchingWord), 2.0f);
        if (current)
            draw->AddRect(ImVec2(x0, y), ImVec2(x1, y + m.lineHeight), col(p.accent, 0.9f), 2.0f);
    }
}

// How far the pane scrolls sideways: the widest line, measured again only
// when the text changed (see `OpenScript::widestCells`).
[[nodiscard]] u32 widestCells(OpenScript& tab)
{
    if (tab.widestRevision != tab.document.revision()) {
        u32 widest = 0;
        for (u32 line = 0; line < tab.document.lineCount(); ++line)
            widest = std::max(widest, tab.document.cellCount(line));
        tab.widestCells = widest;
        tab.widestRevision = tab.document.revision();
    }
    return tab.widestCells;
}

// **The minimap**, in the reference editor's proportions: a line is three
// units tall and a character one wide, drawn as a block in its own colour,
// up to this many columns -- the shape of the code rather than its letters.
constexpr float kMinimapColumns = 100.0f;
constexpr float kMinimapLineStep = 3.0f;
constexpr float kMinimapBlock = 2.0f;
// The strip down its right edge that marks the WHOLE file's problems, which
// the map itself stops showing once the file is taller than the pane.
constexpr float kMinimapRuler = 5.0f;
// Below this the pane is for the code alone.
constexpr float kMinimapShownFrom = 480.0f;

// Drawn over the right of the code pane, and it moves the pane's scroll: a
// click jumps there, the slider drags, and a click in the ruler goes to that
// share of the file. The wheel over it scrolls the code, which the pane
// already does because the map is inside it.
void drawMinimap(OpenScript& tab, const PaneMetrics& m, ImDrawList* draw, const ImRect& rect, float unit, float scroll,
                 float scrollMax, float viewHeight, bool overMap)
{
    const u32 lineCount = tab.document.lineCount();
    if (lineCount == 0)
        return;
    // **In rows, as the pane scrolls**: a folded block is one row here too,
    // or the slider would drift from the code it stands for.
    const u32 rowCount = m.view != nullptr ? std::max(1u, m.view->rows()) : lineCount;
    const float lineStep = kMinimapLineStep * unit;
    const float block = kMinimapBlock * unit;
    const float ruler = kMinimapRuler * unit;
    const ImRect map(rect.Min, ImVec2(rect.Max.x - ruler, rect.Max.y));
    const MinimapView view =
        minimapView(rowCount, m.lineHeight, lineStep, rect.GetHeight(), viewHeight, scroll, scrollMax);

    const ImGuiIO& io = ImGui::GetIO();
    if (overMap && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const float y = io.MousePos.y - rect.Min.y;
        if (io.MousePos.x >= map.Max.x) {
            // The ruler is the whole file, top to bottom.
            const float row = std::floor(y / rect.GetHeight() * static_cast<float>(rowCount));
            ImGui::SetScrollY(std::clamp(row * m.lineHeight - viewHeight * 0.5f, 0.0f, scrollMax));
        }
        else {
            float from = scroll;
            if (y < view.sliderTop || y >= view.sliderTop + view.sliderHeight) {
                from = minimapJump(view, y, m.lineHeight, lineStep, viewHeight, scrollMax);
                ImGui::SetScrollY(from);
            }
            tab.mapDragging = true;
            tab.mapGrabScroll = from;
            tab.mapGrabY = io.MousePos.y;
        }
    }
    if (tab.mapDragging) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            tab.mapDragging = false;
        else if (io.MousePos.y != tab.mapGrabY)
            ImGui::SetScrollY(
                std::clamp(tab.mapGrabScroll + (io.MousePos.y - tab.mapGrabY) * view.dragRatio, 0.0f, scrollMax));
    }
    if (overMap || tab.mapDragging)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);

    // Its own ground, over the code that scrolled under it.
    draw->AddRectFilled(rect.Min, rect.Max, ecol(ScriptColor::Background));
    draw->AddLine(rect.Min, ImVec2(rect.Min.x, rect.Max.y), ecol(ScriptColor::LineNumber, 0.2f));

    const auto top = [&](u32 line) { return rect.Min.y + static_cast<float>(rowOf(m, line)) * lineStep - view.offset; };
    const auto shown = [&](u32 line) {
        const u32 row = rowOf(m, line);
        return !hiddenLine(m, line) && row >= view.first && row <= view.last;
    };
    const auto xOf = [&](u32 cell) {
        return map.Min.x + unit + std::min(static_cast<float>(cell), kMinimapColumns) * unit;
    };

    if (shown(tab.caret.head.line)) {
        const float y = top(tab.caret.head.line);
        draw->AddRectFilled(ImVec2(map.Min.x, y), ImVec2(map.Max.x, y + lineStep), ecol(ScriptColor::CurrentLine));
    }

    // **The problems, under the code**, where the pane underlines them.
    const std::span<const Diagnostic> all = tab.document.diagnostics();
    for (const Diagnostic& diagnostic : all) {
        const u32 line = diagnostic.at.line;
        if (line >= lineCount || !shown(line) || settling(tab, diagnostic))
            continue;
        const u32 from = tab.document.cellOf(line, diagnostic.at.column);
        const u32 to = diagnostic.length > 0 ? tab.document.cellOf(line, diagnostic.at.column + diagnostic.length)
                                             : tab.document.cellCount(line);
        const float y = top(line);
        draw->AddRectFilled(ImVec2(xOf(from), y), ImVec2(std::max(xOf(to), xOf(from) + 2.0f * unit), y + lineStep),
                            diagnostic.severity == Severity::Error ? ecol(ScriptColor::ErrorUnderline, 0.55f)
                                                                   : ecol(ScriptColor::WarningUnderline, 0.45f));
    }

    // The code: each run of non-blank characters a block in its token's colour.
    static std::vector<StyledRun> styled;
    for (u32 row = view.first; row <= view.last; ++row) {
        const u32 line = lineOfRow(m, row);
        if (line >= lineCount)
            break;
        const std::string_view text = tab.document.line(line);
        if (text.empty())
            continue;
        const float y = top(line);
        const auto blocks = [&](u32 from, u32 to, ImU32 colour) {
            while (from < to) {
                while (from < to && (text[from] == ' ' || text[from] == '\t'))
                    ++from;
                u32 end = from;
                while (end < to && text[end] != ' ' && text[end] != '\t')
                    ++end;
                if (end > from) {
                    const u32 cell = tab.document.cellOf(line, from);
                    if (static_cast<float>(cell) >= kMinimapColumns)
                        return;
                    draw->AddRectFilled(ImVec2(xOf(cell), y), ImVec2(xOf(tab.document.cellOf(line, end)), y + block),
                                        colour);
                }
                from = end;
            }
        };
        styleLine(text, tab.document.tokens(line), styled);
        const ImU32 plain = ecol(ScriptColor::Text, 0.7f);
        u32 column = 0;
        for (const StyledRun& piece : styled) {
            blocks(column, piece.column, plain);
            blocks(piece.column, piece.column + piece.length, ecol(piece.color, 0.8f));
            column = std::max(column, piece.column + piece.length);
        }
        blocks(column, static_cast<u32>(text.size()), plain);
    }

    // The slider -- what the pane shows -- when the pointer is over the map,
    // as the reference editor shows it.
    if (overMap || tab.mapDragging) {
        draw->AddRectFilled(
            ImVec2(map.Min.x, rect.Min.y + view.sliderTop),
            ImVec2(map.Max.x, rect.Min.y + view.sliderTop + view.sliderHeight),
            ImGui::GetColorU32(tab.mapDragging ? ImGuiCol_ScrollbarGrabActive : ImGuiCol_ScrollbarGrabHovered, 0.35f));
    }

    // **The ruler: the whole file's problems at their share of its height**,
    // warnings first so an error on the same line is the one seen.
    draw->AddRectFilled(ImVec2(map.Max.x, rect.Min.y), rect.Max, ecol(ScriptColor::LineNumber, 0.08f));
    const float perLine = rect.GetHeight() / static_cast<float>(rowCount);
    const auto tick = [&](u32 line, ImU32 colour, float height) {
        const float y = rect.Min.y + static_cast<float>(rowOf(m, line)) * perLine;
        draw->AddRectFilled(ImVec2(map.Max.x + unit, y), ImVec2(rect.Max.x, y + std::max(height, perLine)), colour);
    };
    for (const Severity severity : {Severity::Warning, Severity::Error}) {
        for (const Diagnostic& diagnostic : all) {
            if (diagnostic.severity != severity || diagnostic.at.line >= lineCount || settling(tab, diagnostic))
                continue;
            tick(diagnostic.at.line,
                 severity == Severity::Error ? ecol(ScriptColor::ErrorUnderline) : ecol(ScriptColor::WarningUnderline),
                 2.0f * unit);
        }
    }
    if (tab.errorLine.has_value() && *tab.errorLine < lineCount)
        tick(*tab.errorLine, ecol(ScriptColor::ErrorUnderline), 3.0f * unit);
    tick(tab.caret.head.line, ecol(ScriptColor::Caret, 0.7f), unit);
}

// **Folds follow their lines through an edit**, and the ranges are worked
// out again when the text changed. Lines inserted or removed above a fold move
// it by as many; a fold whose block is gone is dropped. And a caret that lands
// inside a folded block -- typed into, searched to, jumped to -- opens it,
// because a caret nobody can see is a caret nobody can use.
void refreshFolds(OpenScript& tab)
{
    const core::u64 revision = tab.document.revision();
    if (tab.foldRevision != revision) {
        const u32 count = tab.document.lineCount();
        if (tab.foldRevision != ~0ull && !tab.folded.empty() && count != tab.foldLineCount) {
            const long long delta = static_cast<long long>(count) - static_cast<long long>(tab.foldLineCount);
            const long long pivot = static_cast<long long>(tab.caret.head.line) - std::max(0LL, delta);
            for (u32& first : tab.folded) {
                if (static_cast<long long>(first) > pivot)
                    first = static_cast<u32>(std::max(0LL, static_cast<long long>(first) + delta));
            }
        }
        tab.foldRanges = tab.document.foldRanges();
        std::erase_if(tab.folded, [&tab](u32 first) {
            return std::none_of(tab.foldRanges.begin(), tab.foldRanges.end(),
                                [first](const ScriptDocument::FoldRange& range) { return range.first == first; });
        });
        tab.foldRevision = revision;
        tab.foldLineCount = count;
    }
    const u32 caretLine = tab.caret.head.line;
    std::erase_if(tab.folded, [&tab, caretLine](u32 first) {
        const ScriptDocument::FoldRange* range = foldAt(tab, first);
        return range != nullptr && caretLine > range->first && caretLine < range->last;
    });
}

void drawPane(OpenScript& tab, ScriptEditor& editor, const DebugView& debug, const scene::World* world,
              core::InstanceId root, ScriptEditorCommands& out, std::size_t index,
              const ScriptActionButton& actionButton)
{
    const ThemePalette& p = currentTheme().palette;
    PaneMetrics m = metricsFor(tab.document, editor.zoom());
    refreshFolds(tab);
    const FoldView view = foldView(tab.document.lineCount(), tab.foldRanges, tab.folded);
    m.view = &view;

    // A tab whose indentation was made tabs on opening hands the text to the
    // instance now, the way an edit would (see `OpenScript::convertedIndent`).
    if (tab.convertedIndent) {
        tab.convertedIndent = false;
        edited(out, index);
    }

    // **Parsed when the text is at rest**, which is one frame after the last
    // edit: per keystroke would re-parse a file per character, and a timer would
    // put a clock in a panel.
    if (tab.document.diagnosticsStale() && tab.document.revision() == tab.idleRevision) {
        tab.document.refreshDiagnostics();
        // **The half that needs the tree** (decision 10). `refreshDiagnostics`
        // parses TEXT and knows nothing about a world; dot access to a live
        // child can only be told from a plain table field by resolving the
        // path, so it is a second pass in the same breath and lands in the same
        // list.
        if (world != nullptr) {
            std::vector<Diagnostic> reached;
            lintInstanceAccess(tab.document, world->classes(), world->atoms(),
                               CompletionWorld{world, root, tab.instance}, reached);
            tab.document.appendDiagnostics(reached);
            // And the type checker's, which land when it answers (ADR 0093).
            if (LanguageService* service = languageService(); service != nullptr) {
                LanguageTree snapshot = languageTreeOf(*world, root, tab);
                tab.module = snapshot.pathOf(tab.instance);
                if (!tab.module.empty())
                    service->requestCheck(std::move(snapshot), tab.module, tab.document.revision());
            }
        }
    }
    tab.idleRevision = tab.document.revision();

    const core::Color3 ground = scol(ScriptColor::Background);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(ground.r, ground.g, ground.b, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    // The pane's rectangle, for the find box drawn over its top right.
    const ImVec2 paneTopLeft = ImGui::GetCursorScreenPos();
    const float paneOuterWidth = ImGui::GetContentRegionAvail().x;
    const bool open = ImGui::BeginChild("##code", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders,
                                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if (!open) {
        ImGui::EndChild();
        return;
    }

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 textOrigin(origin.x + m.gutter, origin.y);
    const float paneWidth = ImGui::GetContentRegionAvail().x;
    const float paneHeight = ImGui::GetWindowHeight();
    const auto lineCount = tab.document.lineCount();

    // **The minimap's place**: the right of what the pane shows, beside the
    // scrollbar and not under it, and fixed while the code scrolls under it.
    const ImRect inner = ImGui::GetCurrentWindow()->InnerRect;
    const float unit = std::max(1.0f, ImGui::GetFontSize() / 15.0f);
    const float mapWidth =
        inner.GetWidth() >= kMinimapShownFrom * unit ? (kMinimapColumns + 2.0f + kMinimapRuler) * unit : 0.0f;
    const ImRect mapRect(ImVec2(inner.Max.x - mapWidth, inner.Min.y), inner.Max);
    const bool overMap = mapWidth > 0.0f && ImGui::IsWindowHovered() && mapRect.Contains(ImGui::GetIO().MousePos);
    // What the code has of the pane once the map has its share.
    const float textWidth = paneWidth - mapWidth;

    // The extent, which is also the click target. An `InvisibleButton` would
    // reset the active id on release; this claims it and keeps it, which is what
    // a caret needs and what makes `IsAnyItemActive()` true for the shell's own
    // guards.
    //
    // **As wide as the widest line**, plus the map's width so the end of that
    // line can come out from under it.
    const ImVec2 extent(
        std::max(paneWidth, m.gutter + static_cast<float>(widestCells(tab) + 4u) * m.advance + mapWidth),
        static_cast<float>(view.rows()) * m.lineHeight + m.lineHeight);
    const ImGuiID id = ImGui::GetID("##surface");
    const ImRect bounds(origin, ImVec2(origin.x + extent.x, origin.y + extent.y));
    ImGui::ItemSize(extent);
    // **`ItemAdd` answers false when the rectangle is clipped, and returning
    // there would drop the active id** -- ImGui keeps an item active only while
    // something re-submits its id each frame. The pane would lose the caret
    // mid-scroll and the shell's own shortcuts would start firing on the keys
    // being typed into it. Submitted either way; only the drawing is skipped.
    const bool visible = ImGui::ItemAdd(bounds, id);

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    // Over the map the pointer is the map's, not the text's.
    const bool hovered = ImGui::ItemHoverable(bounds, id, 0) && !overMap;
    bool active = ImGui::GetActiveID() == id;

    // A tab just opened or focused takes the caret (see `OpenScript::claimCaret`).
    if (tab.claimCaret) {
        tab.claimCaret = false;
        ImGui::SetActiveID(id, window);
        ImGui::SetFocusID(id, window);
        ImGui::FocusWindow(window);
        active = true;
    }

    // **A row of the list is a target of its own**, answered before the text:
    // the list lies over the code, and a click on a row is a choice, not a
    // caret move. The pointer moving over a row highlights it.
    bool popupClick = false;
    if (active && tab.completing && !tab.completions.empty()) {
        const CompletionLayout layout = layoutCompletions(tab, m, textOrigin);
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        if (const std::optional<std::size_t> row = completionRowAt(tab, layout, m, mouse); row.has_value()) {
            if (ImGui::GetIO().MouseDelta.x != 0.0f || ImGui::GetIO().MouseDelta.y != 0.0f)
                tab.completionIndex = *row;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                tab.completionIndex = *row;
                acceptCompletion(tab, out, index);
                popupClick = true;
            }
        }
        else if (layout.hasDoc && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                 ImRect(layout.docMin, layout.docMax).Contains(mouse)) {
            popupClick = true;
        }
    }

    // The gutter is a different target from the text: clicking a line number
    // arms a breakpoint and must not move the caret, which is what every editor
    // does and what stops a breakpoint from throwing away a selection.
    const bool overGutter = hovered && ImGui::GetIO().MousePos.x < origin.x + ImGui::GetScrollX() + m.gutter;

    // **The colour under the pointer** (see `swatchRect`): found before the
    // text takes the click, because a click on its swatch is a choice of
    // colour and not a caret move.
    std::optional<ColorLiteral> hoveredColour;
    bool swatchClicked = false;
    if (hovered && !overGutter) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const u32 line = hitTest(tab.document, m, textOrigin, mouse).line;
        if (const std::optional<ColorLiteral> literal = findColorLiteral(tab.document.line(line), line);
            literal.has_value()) {
            const bool onSwatch = swatchRect(tab.document, m, textOrigin, *literal).Contains(mouse);
            if (onSwatch || callRect(tab.document, m, textOrigin, *literal).Contains(mouse))
                hoveredColour = literal;
            if (onSwatch && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                g_colourEdit =
                    CodeColourEdit{line, *literal, {literal->color.r, literal->color.g, literal->color.b}, true};
                ImGui::OpenPopup("##code-colour");
                swatchClicked = true;
            }
        }
    }

    if (!popupClick && !swatchClicked && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ImGui::SetActiveID(id, window);
        ImGui::SetFocusID(id, window);
        ImGui::FocusWindow(window);
        active = true;
        // A click in the text is a move, and a move ends the completion
        // (see `placeCaret`) -- the double-click and Alt paths below do not
        // go through it.
        tab.completing = false;
        if (!overGutter) {
            g_dragging = id;
            g_dragColumn = false;
            const Position at = hitTest(tab.document, m, textOrigin, ImGui::GetIO().MousePos);
            const ImGuiIO& io = ImGui::GetIO();
            const bool alt = io.KeyAlt && !io.KeyCtrl;
            if (alt && io.KeyShift) {
                // Shift+Alt+drag: a column, one caret per line it crosses.
                tab.extraCarets.clear();
                placeCaret(tab, at, false);
                g_dragColumn = true;
                g_columnFrom = Position{at.line, tab.document.cellOf(at.line, at.column)};
                g_dragByWord = false;
            }
            else if (alt) {
                // Alt+click: a caret here, or -- on one already here -- none.
                const auto same = [&at](const Caret& c) { return !c.hasSelection() && c.head == at; };
                if (const auto found = std::find_if(tab.extraCarets.begin(), tab.extraCarets.end(), same);
                    found != tab.extraCarets.end()) {
                    tab.extraCarets.erase(found);
                    g_dragging = 0;
                }
                else if (same(tab.caret) && !tab.extraCarets.empty()) {
                    tab.caret = tab.extraCarets.back();
                    tab.extraCarets.pop_back();
                    g_dragging = 0;
                }
                else {
                    addCaret(tab, Caret{.head = at, .anchor = at});
                }
                g_dragByWord = false;
            }
            else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                tab.extraCarets.clear();
                const Range word = tab.document.wordAt(at);
                tab.caret.anchor = word.begin;
                tab.caret.head = word.end;
                g_dragByWord = true;
                g_dragWord = word;
            }
            else {
                tab.extraCarets.clear();
                placeCaret(tab, at, ImGui::GetIO().KeyShift);
                g_dragByWord = false;
            }
        }
    }
    // **Cleared whenever the button is not down, rather than on the release
    // event.** Anything that eats a frame -- a modal, a window the compositor
    // stopped delivering to, a long load -- can hide the release, and a pane
    // that missed it drags forever, extending its selection at whatever the
    // pointer touches next. Asking about the state cannot miss an edge.
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && g_dragging == id) {
        g_dragging = 0;
        g_dragByWord = false;
        g_dragColumn = false;
        mergeCarets(tab);
    }
    if (g_dragging == id && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        const Position at = hitTest(tab.document, m, textOrigin, ImGui::GetIO().MousePos);
        if (g_dragColumn) {
            // The rectangle between where the drag began and the pointer, in
            // cells: a caret per line, each selecting that line's share.
            const u32 fromCell = g_columnFrom.column;
            const u32 toCell = tab.document.cellOf(at.line, at.column);
            const float pointerCell =
                std::max(0.0f, std::round((ImGui::GetIO().MousePos.x - textOrigin.x) / m.advance));
            const u32 wantCell = std::max(toCell, static_cast<u32>(pointerCell));
            const u32 top = std::min(g_columnFrom.line, at.line);
            const u32 bottom = std::max(g_columnFrom.line, at.line);
            std::vector<Caret> rows;
            for (u32 line = top; line <= bottom; ++line) {
                const Position anchor = tab.document.clamp(Position{line, tab.document.columnOfCell(line, fromCell)});
                const Position head = tab.document.clamp(Position{line, tab.document.columnOfCell(line, wantCell)});
                // A line too short to reach the column is left out, as a
                // column selection does everywhere.
                if (tab.document.cellCount(line) < std::min(fromCell, wantCell) && line != g_columnFrom.line)
                    continue;
                rows.push_back(Caret{.head = head, .anchor = anchor, .desiredColumn = wantCell});
            }
            if (!rows.empty()) {
                const bool downward = at.line >= g_columnFrom.line;
                tab.caret = downward ? rows.back() : rows.front();
                if (downward)
                    rows.pop_back();
                else
                    rows.erase(rows.begin());
                tab.extraCarets = std::move(rows);
            }
        }
        else if (!g_dragByWord) {
            tab.caret.head = at;
        }
        else if (at < g_dragWord.begin) {
            // Backwards: the far end of the word that was double-clicked stays
            // put, and the near end runs to the START of whatever is under the
            // pointer. A selection that ended mid-word would be the same defect
            // the double-click had.
            tab.caret.anchor = g_dragWord.end;
            tab.caret.head = tab.document.wordAt(at).begin;
        }
        else {
            tab.caret.anchor = g_dragWord.begin;
            const Range word = tab.document.wordAt(at);
            // `wordAt` answers an empty range on a byte that is not part of a
            // word -- a space, a bracket -- and there the pointer's own place is
            // the honest end.
            tab.caret.head = at < word.end ? word.end : at;
        }
    }

    // **Recorded for `releaseScriptPaneFocus`**, which runs before anything else
    // in the frame and needs to know whose active id this is and what rectangle
    // counts as inside it. Ids rather than pointers: a window can be destroyed
    // between two frames and a stale `ImGuiWindow*` would be read.
    if (active) {
        g_paneActiveId = id;
        g_paneWindowId = window->ID;
        g_paneBounds = bounds;
    }
    else if (g_paneActiveId == id) {
        g_paneActiveId = 0;
    }

    // **Ctrl and the wheel, which is what every editor does.** Read from the
    // hover rather than from the focus, because a wheel belongs to whatever is
    // under the pointer -- and the shell's own guards never see it, since the
    // pane is not an ImGui input item.
    //
    // The window must be told not to scroll as well, which is what `SetKeyOwner`
    // on the modifier does: without it a zoom also scrolls the code out from
    // under itself.
    if (hovered && ImGui::GetIO().KeyCtrl) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            // A notch is a tenth, which is coarse enough to get somewhere and
            // fine enough to stop where you meant to.
            if (editor.setZoom(editor.zoom() + (wheel > 0.0f ? 0.1f : -0.1f)))
                g_zoomShownFor = 0.0f;
            ImGui::SetKeyOwner(ImGuiMod_Ctrl, id);
            ImGui::GetIO().MouseWheel = 0.0f;
        }
    }

    if (active) {
        // **Ctrl+0 is the way back**, and the readout in the corner is what
        // tells somebody there is one.
        if (pressed(ScriptAction::ResetZoom)) {
            if (editor.setZoom(1.0f))
                g_zoomShownFor = 0.0f;
        }

        // What makes the shell's `!IsAnyItemActive()` guards do the right thing
        // without any of them being edited.
        ImGui::SetActiveIdUsingAllKeyboardKeys();

        // **And this is what lets the FIRST click on anything else land.**
        //
        // ImGui refuses to hover any item while another one is active
        // (`imgui.cpp`, `ItemHoverable`). For most widgets that is invisible,
        // because most widgets are active only while a button is held. A caret
        // is active for as long as somebody is typing, so with the pane holding
        // the active id the Explorer's rows, the Viewport and the other tabs
        // were never hovered at all.
        //
        // **And the cost is not one frame, it is one CLICK**, which is why
        // releasing the active id when a click arrives was not enough on its
        // own. The Explorer's rows are submitted with
        // `ImGuiSelectableFlags_AllowOverlap`, and an overlap-allowing item is
        // hoverable only if it was ALREADY the hovered id on the previous frame
        // (`ItemHoverable` again) -- so a row that was blocked from hovering
        // while the caret lived here cannot be pressed on the frame the block
        // is lifted either. The first click established the hover and the
        // second one did the work, which is exactly what was reported.
        //
        // Declaring the overlap says the true thing: this widget is active and
        // another may take that from it. Hover goes on being resolved
        // underneath, so the row is already hovered when the click arrives.
        ImGui::GetCurrentContext()->ActiveIdAllowOverlap = true;

        // **Asking the platform for characters, which is not automatic.** SDL3
        // does not deliver text until something calls `SDL_StartTextInput`, and
        // the only thing that makes ImGui call it is a widget setting
        // `PlatformImeData.WantTextInput` -- which `InputTextEx` does at
        // `imgui_widgets.cpp:5700-5709` and which a hand-drawn pane therefore
        // has to do for itself. Without it every arrow key works, the caret
        // moves, and not one character ever arrives.
        //
        // `InputPos` is where an IME puts its candidate window, so it is the
        // caret rather than the corner: somebody composing Japanese gets the
        // list under what they are typing.
        ImGuiPlatformImeData& ime = ImGui::GetCurrentContext()->PlatformImeData;
        ime.WantVisible = true;
        ime.WantTextInput = true;
        ime.InputPos = ImVec2(textOrigin.x + static_cast<float>(tab.caret.head.column) * m.advance,
                              textOrigin.y + topOf(m, tab.caret.head.line));
        ime.InputLineHeight = m.lineHeight;
        ime.ViewportId = window->Viewport->ID;

        const core::u64 before = tab.document.revision();
        const std::string typed = takeTyped();
        if (!handleCursorKeys(tab)) {
            if (tab.extraCarets.empty()) {
                typeText(tab, out, index, typed);
                handleKeys(tab, out, index, m, paneHeight, KeyScope::All);
            }
            else {
                // No list with several carets: an accept is a choice made at
                // one place, and would be typed at every other.
                tab.completing = false;
                handleMultiClipboard(tab, out, index);
                forEachCaret(tab, [&](std::size_t) {
                    typeText(tab, out, index, typed);
                    handleKeys(tab, out, index, m, paneHeight, KeyScope::PerCaret);
                });
                handleKeys(tab, out, index, m, paneHeight, KeyScope::Global);
            }
        }
        if (tab.document.revision() != before) {
            tab.lastEditTime = ImGui::GetTime();
            tab.lastEditLine = tab.caret.head.line;
            tab.errorLine.reset();
        }

        // **Offered after the text moved, not on a key.** Backspacing through a
        // word then narrows the list instead of dismissing it, and typing a `.`
        // opens it without anybody asking.
        const bool ctrlSpace = pressed(ScriptAction::TriggerCompletion);
        // Not for the edit an accept just made (see `OpenScript::justAccepted`).
        const bool accepted = std::exchange(tab.justAccepted, false);
        // **An edit made by a chord is a command, not typing** (the owner:
        // Alt+Up moved the line and the list came back over it). AltGr arrives
        // as Ctrl and Alt together and types characters, so it still counts.
        const ImGuiIO& held = ImGui::GetIO();
        const bool chord = held.KeyCtrl != held.KeyAlt;
        if (((tab.document.revision() != before && !accepted && !chord) || ctrlSpace) && tab.extraCarets.empty())
            refreshCompletions(tab, world, root);
        // **Escape lets go of the pane rather than clearing the selection.** One
        // press to leave the code, and the second means what the shell says.
        //
        // With several carets the first press leaves only the primary, which is
        // what the reference editor's Escape does.
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !tab.extraCarets.empty()) {
            tab.extraCarets.clear();
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            ImGui::ClearActiveID();
            active = false;
        }
        else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !hovered && !popupClick && !overMap) {
            ImGui::ClearActiveID();
            active = false;
        }
    }
    // **No list without a caret**: a pane somebody has clicked away from is not
    // being typed in, and a list hanging over it is a list nobody can use.
    if (!active)
        tab.completing = false;

    // Which lines can be seen: the same arithmetic `ImGuiListClipper` does, done
    // by hand because the pane already owns its extent and its scroll.
    const float scroll = ImGui::GetScrollY();
    // In ROWS, then the lines they show: a folded block is one row.
    const u32 rowCount = std::max(1u, view.rows());
    const auto firstRow = std::min(rowCount - 1, static_cast<u32>(std::max(0.0f, std::floor(scroll / m.lineHeight))));
    const auto lastRow = std::min(rowCount - 1, firstRow + static_cast<u32>(paneHeight / m.lineHeight) + 1u);
    const u32 first = lineOfRow(m, firstRow);
    const u32 last = lineOfRow(m, lastRow);

    ImDrawList* draw = ImGui::GetWindowDrawList();

    if (!visible) {
        ImGui::EndChild();
        return;
    }

    // The gutter's own ground, so the line numbers do not swim over the code
    // when it is scrolled sideways.
    draw->AddRectFilled(ImVec2(origin.x + ImGui::GetScrollX(), origin.y + scroll),
                        ImVec2(origin.x + ImGui::GetScrollX() + m.gutter, origin.y + scroll + paneHeight),
                        col(p.background));

    // The line the caret is on, marked quietly. Loudly enough to find and not so
    // loudly that it competes with a selection.
    const float caretY = textOrigin.y + topOf(m, tab.caret.head.line);
    if (active) {
        draw->AddRectFilled(
            ImVec2(origin.x + m.gutter, caretY),
            ImVec2(origin.x + ImGui::GetScrollX() + std::max(paneWidth, extent.x), caretY + m.lineHeight),
            ecol(ScriptColor::CurrentLine));
    }

    if (debug.parked && debug.chunk == tab.chunk && debug.line > 0) {
        const float stopY = textOrigin.y + topOf(m, debug.line - 1);
        draw->AddRectFilled(
            ImVec2(origin.x + m.gutter, stopY),
            ImVec2(origin.x + ImGui::GetScrollX() + std::max(paneWidth, extent.x), stopY + m.lineHeight),
            ecol(ScriptColor::DebuggerCurrentLine));
    }
    // **The line an error in the console pointed at**, until somebody edits
    // the script: where it went wrong, marked the way a stopped debugger marks
    // where it is.
    if (tab.errorLine.has_value() && *tab.errorLine < lineCount) {
        const float errorY = textOrigin.y + topOf(m, *tab.errorLine);
        draw->AddRectFilled(
            ImVec2(origin.x + m.gutter, errorY),
            ImVec2(origin.x + ImGui::GetScrollX() + std::max(paneWidth, extent.x), errorY + m.lineHeight),
            ecol(ScriptColor::DebuggerErrorLine));
    }

    drawMatches(tab, m, draw, textOrigin, first, last);
    drawSelection(tab, tab.caret, m, draw, textOrigin, first, last);
    for (const Caret& extra : tab.extraCarets)
        drawSelection(tab, extra, m, draw, textOrigin, first, last);
    for (u32 row = firstRow; row <= lastRow; ++row) {
        const u32 line = lineOfRow(m, row);
        if (line >= lineCount)
            break;
        drawGutter(tab, editor, debug, m, draw, ImVec2(origin.x + ImGui::GetScrollX(), origin.y), line,
                   line == tab.caret.head.line);
        drawLine(tab, m, draw, textOrigin, line, paneWidth + ImGui::GetScrollX());
        // **What a fold hides is marked where it hides it**: a quiet pill
        // after the line's code, which is the reference editor's "...".
        if (isFolded(tab, line)) {
            const float x = textOrigin.x + static_cast<float>(tab.document.cellCount(line) + 1u) * m.advance;
            const float y = textOrigin.y + topOf(m, line);
            draw->AddRectFilled(ImVec2(x, y + m.lineHeight * 0.15f),
                                ImVec2(x + m.advance * 3.0f, y + m.lineHeight * 0.85f),
                                ecol(ScriptColor::Selection, 0.7f), m.lineHeight * 0.2f);
            draw->AddText(m.font, m.size, ImVec2(x + m.advance * 0.5f, y - m.lineHeight * 0.12f),
                          ecol(ScriptColor::LineNumber), "...");
        }
    }
    drawDiagnostics(tab, m, draw, textOrigin, first, last);

    if (active) {
        // Blinking on ImGui's own clock, so every caret in the application
        // blinks together.
        const float phase = std::fmod(static_cast<float>(ImGui::GetTime()), 1.06f);
        if (phase < 0.7f) {
            const auto bar = [&](const Caret& caret) {
                const float x = textOrigin.x +
                                static_cast<float>(tab.document.cellOf(caret.head.line, caret.head.column)) * m.advance;
                const float y = textOrigin.y + topOf(m, caret.head.line);
                draw->AddLine(ImVec2(x, y), ImVec2(x, y + m.lineHeight), ecol(ScriptColor::Caret), 1.5f);
            };
            bar(tab.caret);
            for (const Caret& extra : tab.extraCarets)
                bar(extra);
        }
    }

    if (mapWidth > 0.0f)
        drawMinimap(tab, m, draw, mapRect, unit, scroll, ImGui::GetScrollMaxY(), inner.GetHeight(), overMap);

    // **The view follows the caret, and only when the caret moved.** Ctrl+End in
    // a long file otherwise put the caret at the bottom of a document still
    // showing its first page.
    if (!(tab.caret.head == tab.shownCaret)) {
        tab.shownCaret = tab.caret.head;
        const float caretTop = topOf(m, tab.caret.head.line);
        if (caretTop < scroll)
            ImGui::SetScrollY(caretTop);
        else if (caretTop + m.lineHeight > scroll + paneHeight)
            ImGui::SetScrollY(caretTop + m.lineHeight - paneHeight);

        const float caretX =
            static_cast<float>(tab.document.cellOf(tab.caret.head.line, tab.caret.head.column)) * m.advance;
        const float scrollX = ImGui::GetScrollX();
        if (caretX < scrollX)
            ImGui::SetScrollX(std::max(0.0f, caretX - m.advance * 4.0f));
        else if (caretX + m.gutter + m.advance > scrollX + textWidth)
            ImGui::SetScrollX(caretX + m.gutter + m.advance - textWidth);
    }

    drawZoomReadout(editor, m);
    askSignature(tab, world, root);
    drawSignature(tab, m, textOrigin);
    drawCompletions(tab, m, textOrigin);

    // **The swatch of the colour under the pointer**, or of the one whose
    // picker is open, drawn over the code after its call.
    const auto drawSwatch = [&](const ColorLiteral& literal, core::Color3 shown) {
        const ImRect swatch = swatchRect(tab.document, m, textOrigin, literal);
        const ImRect ring(ImVec2(swatch.Min.x - 2.0f, swatch.Min.y - 2.0f),
                          ImVec2(swatch.Max.x + 2.0f, swatch.Max.y + 2.0f));
        draw->AddRectFilled(ring.Min, ring.Max, col(p.surfaceRaised), 3.0f);
        draw->AddRectFilled(swatch.Min, swatch.Max, col(shown), 2.0f);
        draw->AddRect(ring.Min, ring.Max, col(p.border), 3.0f);
    };
    if (g_colourEdit.open && g_colourEdit.line < tab.document.lineCount()) {
        if (const std::optional<ColorLiteral> literal =
                findColorLiteral(tab.document.line(g_colourEdit.line), g_colourEdit.line);
            literal.has_value())
            drawSwatch(*literal, core::Color3{g_colourEdit.value[0], g_colourEdit.value[1], g_colourEdit.value[2]});
    }
    else if (hoveredColour.has_value()) {
        drawSwatch(*hoveredColour, hoveredColour->color);
    }

    if (ImGui::BeginPopup("##code-colour")) {
        (void)ImGui::ColorPicker3("##picker", g_colourEdit.value,
                                  ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_DisplayRGB);
        // Written once, when the picker is let go: a drag is one undo step.
        if (ImGui::IsItemDeactivatedAfterEdit() && g_colourEdit.line < tab.document.lineCount()) {
            const core::Color3 picked{g_colourEdit.value[0], g_colourEdit.value[1], g_colourEdit.value[2]};
            // Found again rather than trusted: the line may have been typed in
            // since the picker opened.
            if (const std::optional<ColorLiteral> literal =
                    findColorLiteral(tab.document.line(g_colourEdit.line), g_colourEdit.line);
                literal.has_value()) {
                tab.document.breakUndoRun();
                (void)tab.document.replace(literal->args, formatColorLiteral(literal->kind, picked));
                edited(out, index);
            }
        }
        ImGui::EndPopup();
    }
    else {
        g_colourEdit.open = false;
    }

    // A gutter click arms or disarms a breakpoint. Recorded rather than acted
    // on: the debugger has to be told, and it lives a frame away.
    if (overGutter && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const u32 clicked = hitTest(tab.document, m, textOrigin, ImGui::GetIO().MousePos).line;
        const bool onArrow = ImGui::GetIO().MousePos.x >= origin.x + ImGui::GetScrollX() + foldZoneLeft(m);
        if (onArrow && foldAt(tab, clicked) != nullptr) {
            if (const auto at = std::find(tab.folded.begin(), tab.folded.end(), clicked); at != tab.folded.end())
                tab.folded.erase(at);
            else
                tab.folded.push_back(clicked);
        }
        else {
            out.toggleBreakpointLine = clicked;
        }
    }

    ImGui::EndChild();
    drawFindBox(tab, out, index, paneTopLeft, paneOuterWidth, actionButton);
}

} // namespace

void drawDebugPanel(ScriptEditor& editor, DebugView& debug, ScriptEditorCommands& out, bool& open,
                    const ScriptActionButton& actionButton)
{
    // **Beside the Console, even in a layout written before this panel
    // existed.** `buildDefaultLayout` docks it for a fresh arrangement, but a
    // saved `.ini` has no entry for a window that did not exist when it was
    // written -- so the panel appeared floating over the Explorer. Asking the
    // Console where it lives puts this in the same node without throwing away
    // an arrangement somebody chose.
    if (const ImGuiWindow* console = ImGui::FindWindowByName("Console"); console != nullptr && console->DockId != 0) {
        ImGui::SetNextWindowDockID(console->DockId, ImGuiCond_FirstUseEver);
    }

    if (!ImGui::Begin((tabIconPad() + "Debug###Debug").c_str(), &open)) {
        ImGui::End();
        return;
    }

    const ThemePalette& p = currentTheme().palette;

    // The transport. Disabled rather than hidden when nothing is stopped, so
    // the buttons stay where a hand already expects them -- the same rule the
    // File menu follows for Save.
    const auto nextControl = [](const char* label) {
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f +
                                                   ImGui::CalcTextSize(tabIconPad().c_str()).x)
            ImGui::NewLine();
    };
    ImGui::BeginDisabled(!debug.parked);
    if (scriptAction(actionButton, icons::ActionPlay, "Continue"))
        out.step = DebugStep::Continue;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("let the script run on (F5)");
    nextControl("Over");
    if (scriptAction(actionButton, icons::ActionStepOver, "Over"))
        out.step = DebugStep::Over;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Step over: execute the current line without entering calls (F10)");
    nextControl("Into");
    if (scriptAction(actionButton, icons::ActionStepInto, "Into"))
        out.step = DebugStep::Into;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Step into: enter the next function call (F11)");
    nextControl("Out");
    if (scriptAction(actionButton, icons::ActionStepOut, "Out"))
        out.step = DebugStep::Out;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Step out: finish the current function (Shift+F11)");
    ImGui::EndDisabled();

    if (debug.parked)
        ImGui::TextColored(ImVec4(p.warning.r, p.warning.g, p.warning.b, 1.0f), "stopped in %s at line %u",
                           debug.chunk.c_str(), debug.line);
    else {
        nextControl("running");
        ImGui::TextDisabled("running");
    }

    // The keys every debugger uses, so hands already know them. Read HERE and
    // not in the code pane: the pane owns the keyboard while the caret is in it,
    // and pressing F5 should not require clicking away from the code being
    // looked at.
    if (debug.parked) {
        if (ImGui::IsKeyPressed(ImGuiKey_F5, false))
            out.step = DebugStep::Continue;
        if (ImGui::IsKeyPressed(ImGuiKey_F10, false))
            out.step = DebugStep::Over;
        if (ImGui::IsKeyPressed(ImGuiKey_F11, false))
            out.step = ImGui::GetIO().KeyShift ? DebugStep::Out : DebugStep::Into;
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Breakpoints");
    if (editor.breakpoints().empty()) {
        ImGui::TextDisabled("Click a line number to stop there.");
    }
    else {
        for (const Breakpoint& bp : editor.breakpoints()) {
            // **Hollow when it is bound to nothing**, which is a breakpoint set
            // before the world ran or one on a line with no code. Saying so is
            // the difference between "not armed yet" and "armed and never
            // fires", which look identical from the outside.
            if (bp.boundLine == 0)
                ImGui::TextDisabled("%s:%u  (not bound)", bp.chunk.c_str(), bp.line + 1);
            else if (bp.boundLine != bp.line + 1)
                ImGui::Text("%s:%u  (stops at %u)", bp.chunk.c_str(), bp.line + 1, bp.boundLine);
            else
                ImGui::Text("%s:%u", bp.chunk.c_str(), bp.line + 1);
        }
    }

    if (!debug.parked) {
        ImGui::End();
        return;
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Call stack");
    for (std::size_t index = 0; index < debug.frames.size(); ++index) {
        const DebugFrameView& frame = debug.frames[index];
        char label[256]{};
        (void)std::snprintf(label, sizeof(label), "%s  %s:%u##frame-%zu", frame.function.c_str(), frame.chunk.c_str(),
                            frame.line, index);
        if (ImGui::Selectable(label, index == debug.selectedFrame))
            debug.selectedFrame = index;
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Variables");
    if (debug.selectedFrame < debug.frames.size()) {
        const DebugFrameView& frame = debug.frames[debug.selectedFrame];
        if (ImGui::BeginTable("##vars", 3,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("name");
            ImGui::TableSetupColumn("type");
            ImGui::TableSetupColumn("value");
            ImGui::TableHeadersRow();
            const auto row = [](const DebugValueView& value) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(value.name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("%s", value.type.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(value.preview.c_str());
            };
            for (const DebugValueView& value : frame.locals)
                row(value);
            for (const DebugValueView& value : frame.upvalues)
                row(value);
            ImGui::EndTable();
        }
        if (frame.locals.empty() && frame.upvalues.empty())
            ImGui::TextDisabled("Nothing named in this frame.");
    }

    ImGui::End();
}

void releaseScriptPaneFocus()
{
    ImGuiContext& g = *ImGui::GetCurrentContext();
    if (g_paneActiveId == 0 || g.ActiveId != g_paneActiveId)
        return;
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        return;

    const bool inside = g.HoveredWindow != nullptr && g.HoveredWindow->ID == g_paneWindowId &&
                        g_paneBounds.Contains(ImGui::GetIO().MousePos);
    // The completion list hangs past the pane, and a click on it is the pane's.
    if (inside || g_popupBounds.Contains(ImGui::GetIO().MousePos))
        return;

    ImGui::ClearActiveID();
    g_paneActiveId = 0;
}

void drawScriptEditor(ScriptEditor& editor, core::u32 dockNode, DebugView& debug, const scene::World* world,
                      core::InstanceId root, ScriptEditorCommands& out, const ScriptActionButton& actionButton)
{
    const std::optional<std::size_t> focus = editor.takeFocusRequest();
    takeLanguageAnswers(editor);

    for (std::size_t index = 0; index < editor.count(); ++index) {
        OpenScript* tab = editor.at(index);
        if (tab == nullptr)
            continue;

        // `Title###id` so the label can change -- a rename, a dirty marker --
        // while the id ImGui docks by stays the same. Keyed on the instance,
        // which is what the tab IS.
        //
        // The leading spaces are the room the shell paints this script's class
        // icon into, and they are before the `###` for the reason `tabIconPad`
        // gives: everything that identifies a window reads from the far side of
        // it.
        //
        // **Unsaved, it ends in a gap the shell paints a dot into**, for the
        // same reason the leading gap exists: a tab takes a string.
        char name[224]{};
        (void)std::snprintf(name, sizeof(name), "%s%s%s###script-%u", tabIconPad().c_str(), tab->title.c_str(),
                            tab->dirty() ? tabIconPad().c_str() : "", tab->instance.index);

        // Beside the Viewport on first appearance, and wherever somebody moved
        // it afterwards -- `FirstUseEver` is what lets the saved layout win.
        if (dockNode != 0)
            ImGui::SetNextWindowDockID(static_cast<ImGuiID>(dockNode), ImGuiCond_FirstUseEver);

        bool open = true;
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

        if (focus.has_value() && *focus == index)
            tab->claimCaret = true;
        if (ImGui::Begin(name, &open, flags)) {
            if (ImGui::IsWindowAppearing() || ImGui::IsWindowFocused())
                editor.setActive(index);
            drawPane(*tab, editor, debug, world, root, out, index, actionButton);
        }
        ImGui::End();

        // **After `End`, because focusing a window ImGui has not seen this frame
        // does nothing** -- the same rule `drawEditorShell` already follows for
        // the panel it opens on. A tab that is docked behind the Viewport comes
        // to the front; one that is a floating window is raised.
        if (focus.has_value() && *focus == index)
            ImGui::SetWindowFocus(name);

        if (!open)
            out.close = index;
    }
}

} // namespace luaug::app

#else

namespace luaug::app {

// ADR 0011: a shipping build has no ImGui, so the pane has no body. The
// signature stays so the frame loop calls it without an #ifdef.
void drawScriptEditor(ScriptEditor&, core::u32, DebugView&, const scene::World*, core::InstanceId,
                      ScriptEditorCommands&, const ScriptActionButton&)
{}

void releaseScriptPaneFocus()
{}

void drawDebugPanel(ScriptEditor&, DebugView&, ScriptEditorCommands&, bool&, const ScriptActionButton&)
{}

} // namespace luaug::app

#endif
