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
// While the caret is in code, Escape, Delete, F2, W/E/R and the world's
// Ctrl+C/V/X/D do not fire -- and Ctrl+S still saves, it just saves the script.
// Escape releases the pane rather than clearing the selection, so one press
// leaves the code and a second means what the shell says it means.
#include "luaug/app/script_editor_panel.h"

#include "luaug/app/script_editor.h"
#include "luaug/app/ui_theme.h"
#include "luaug/scene/world.h"

#if LUAUG_DEBUG_UI

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <imgui.h>
#include <imgui_internal.h>
#include <string>

namespace luaug::app {
namespace {

using core::f32;
using core::u32;

// --- Colour ------------------------------------------------------------------

[[nodiscard]] ImU32 col(core::Color3 c, float alpha = 1.0f) noexcept
{
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, alpha));
}

[[nodiscard]] ImU32 syntaxColor(const SyntaxPalette& s, const ThemePalette& p, TokenKind kind) noexcept
{
    switch (kind) {
    case TokenKind::Keyword:
        return col(s.keyword);
    case TokenKind::Identifier:
        return col(s.identifier);
    case TokenKind::Number:
        return col(s.number);
    case TokenKind::String:
        return col(s.string);
    case TokenKind::Comment:
        return col(s.comment);
    case TokenKind::Operator:
        return col(s.operatorToken);
    case TokenKind::Attribute:
        return col(s.attribute);
    case TokenKind::Error:
        return col(s.errorToken);
    case TokenKind::Text:
        break;
    }
    return col(p.text);
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
};

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
    // The number, a breakpoint dot's worth of room on its left, and a gap.
    m.gutter = m.advance * static_cast<float>(digits) + m.lineHeight + style.ItemSpacing.x;
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
}

// Vertical movement keeps the column somebody was aiming for, so passing through
// a short line and coming back lands where they left.
void moveVertically(OpenScript& tab, int delta, bool select)
{
    // The CELL, not the byte: moving down a line whose accents sit elsewhere
    // should keep the caret under the same glyph, not the same byte offset.
    const u32 wantedCell = tab.caret.desiredColumn;
    const auto line = static_cast<std::int64_t>(tab.caret.head.line) + delta;
    const auto clamped = static_cast<u32>(std::clamp<std::int64_t>(line, 0, tab.document.lineCount() - 1));
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
    eraseSelection(tab, out, index);
    tab.caret.head = tab.document.insert(tab.caret.head, text);
    tab.caret.anchor = tab.caret.head;
    tab.caret.desiredColumn = tab.caret.head.column;
    edited(out, index);
}

// --- Input -------------------------------------------------------------------

[[nodiscard]] Position hitTest(const ScriptDocument& document, const PaneMetrics& m, ImVec2 textOrigin, ImVec2 point)
{
    const float row = (point.y - textOrigin.y) / m.lineHeight;
    const auto line = static_cast<u32>(std::clamp(std::floor(row), 0.0f, static_cast<float>(document.lineCount() - 1)));
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

void handleTyping(OpenScript& tab, ScriptEditorCommands& out, std::size_t index)
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.InputQueueCharacters.Size == 0)
        return;

    std::string typed;
    for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
        const unsigned int c = io.InputQueueCharacters[i];
        // Control characters arrive as keys, not as text. Tab is handled below
        // because it means indent here rather than a character.
        if (c >= 0x20 && c != 0x7F)
            encodeUtf8(c, typed);
    }
    io.InputQueueCharacters.resize(0);
    if (!typed.empty())
        insertText(tab, out, index, typed);
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
    tab.completionReplace = request.replace;
    tab.completing = !tab.completions.empty();
    if (tab.completionIndex >= tab.completions.size())
        tab.completionIndex = 0;
}

void acceptCompletion(OpenScript& tab, ScriptEditorCommands& out, std::size_t index)
{
    if (!tab.completing || tab.completionIndex >= tab.completions.size())
        return;
    const std::string& label = tab.completions[tab.completionIndex].label;
    tab.caret.head = tab.document.replace(tab.completionReplace, label);
    tab.caret.anchor = tab.caret.head;
    tab.caret.desiredColumn = tab.document.cellOf(tab.caret.head.line, tab.caret.head.column);
    tab.completing = false;
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

// The rows, under the caret. Drawn as a child of the code pane rather than as a
// popup, because an ImGui popup steals the keyboard and the pane needs to keep
// receiving the letters that narrow the list.
void drawCompletions(OpenScript& tab, const PaneMetrics& m, ImVec2 textOrigin)
{
    if (!tab.completing || tab.completions.empty())
        return;

    const ThemePalette& p = currentTheme().palette;
    const float x = textOrigin.x + static_cast<float>(tab.document.cellOf(tab.completionReplace.begin.line,
                                                                          tab.completionReplace.begin.column)) *
                                       m.advance;
    const float y = textOrigin.y + static_cast<float>(tab.caret.head.line + 1) * m.lineHeight;

    const std::size_t rows = std::min<std::size_t>(tab.completions.size(), kMaxCompletionRows);
    // The first row shown, so an index past the eighth scrolls the window rather
    // than walking off the bottom of it.
    const std::size_t firstRow = tab.completionIndex >= rows ? tab.completionIndex - rows + 1 : 0;

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

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const ImVec2 min(std::min(x, std::max(viewport->Pos.x, rightLimit - width)), y);
    const ImVec2 max(min.x + width, y + static_cast<float>(rows) * m.lineHeight);
    draw->AddRectFilled(min, max, col(p.surfaceRaised));
    draw->AddRect(min, max, col(p.border));

    for (std::size_t row = 0; row < rows; ++row) {
        const std::size_t at = firstRow + row;
        if (at >= tab.completions.size())
            break;
        const Completion& completion = tab.completions[at];
        const float rowY = y + static_cast<float>(row) * m.lineHeight;
        if (at == tab.completionIndex)
            draw->AddRectFilled(ImVec2(min.x, rowY), ImVec2(max.x, rowY + m.lineHeight), col(p.accent, 0.30f));
        draw->AddText(m.font, m.size, ImVec2(min.x + m.advance * 2.0f, rowY), col(p.text), completion.label.c_str());
        const float detailX = max.x - codeWidth(m, completion.detail) - m.advance * 2.0f;
        draw->AddText(m.font, m.size, ImVec2(detailX, rowY), col(p.textMuted), completion.detail.c_str());
    }

    // The prose for the highlighted row, under the list. The whole reason this
    // is worth more than a list of names.
    const Completion& current = tab.completions[tab.completionIndex];
    if (!current.doc.empty()) {
        const float docY = max.y;
        const float docWidth = std::max(width, m.advance * 52.0f);
        const ImVec2 docMin(min.x, docY);
        const ImVec2 docMax(min.x + docWidth, docY + m.lineHeight * 2.0f);
        draw->AddRectFilled(docMin, docMax, col(p.surface));
        draw->AddRect(docMin, docMax, col(p.border));
        draw->AddText(m.font, m.size, ImVec2(docMin.x + m.advance * 2.0f, docY), col(p.textMuted), current.doc.c_str(),
                      nullptr, docWidth - m.advance * 4.0f);
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

void handleKeys(OpenScript& tab, ScriptEditorCommands& out, std::size_t index, const PaneMetrics& m, float paneHeight)
{
    ImGuiIO& io = ImGui::GetIO();
    const bool shift = io.KeyShift;
    // **AltGr is Ctrl+Alt on Windows, and it is how a Brazilian, German or
    // Polish keyboard types half its punctuation.** Reading it as a Ctrl chord
    // makes AltGr+something silently mean Select All, Paste, Undo or Save --
    // characters that refuse to type and edits nobody asked for. A real Ctrl
    // shortcut never has Alt held, so this one condition is the whole fix.
    const bool ctrl = io.KeyCtrl && !io.KeyAlt;
    ScriptDocument& doc = tab.document;

    // **While the list is up it owns the keys that move through it.** Anything
    // else would make Enter insert a newline under a highlighted row, which is
    // the one thing nobody means by it.
    if (tab.completing) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            tab.completing = false;
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
            tab.completionIndex = (tab.completionIndex + 1) % tab.completions.size();
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
            tab.completionIndex = tab.completionIndex == 0 ? tab.completions.size() - 1 : tab.completionIndex - 1;
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
            ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
            acceptCompletion(tab, out, index);
            return;
        }
    }

    // **Before the plain arrows**, which would otherwise move the caret as well
    // as the line. Alt and not Ctrl+Alt: AltGr is Ctrl+Alt, and a Brazilian
    // keyboard would move a line every time somebody typed a bracket.
    if (io.KeyAlt && !io.KeyCtrl && !shift) {
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
            moveLines(tab, out, index, -1);
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
            moveLines(tab, out, index, 1);
            return;
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true))
        placeCaret(tab, doc.prevColumn(tab.caret.head), shift);
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true))
        placeCaret(tab, doc.nextColumn(tab.caret.head), shift);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))
        moveVertically(tab, -1, shift);
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))
        moveVertically(tab, 1, shift);

    const auto page = static_cast<int>(std::max(1.0f, std::floor(paneHeight / m.lineHeight)) - 1.0f);
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp, true))
        moveVertically(tab, -page, shift);
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown, true))
        moveVertically(tab, page, shift);

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
            const Position from = doc.prevColumn(tab.caret.head);
            tab.caret.head = doc.erase(Range{from, tab.caret.head});
            tab.caret.anchor = tab.caret.head;
            tab.caret.desiredColumn = tab.caret.head.column;
            edited(out, index);
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, true)) {
        if (tab.caret.hasSelection())
            eraseSelection(tab, out, index);
        else {
            const Position to = doc.nextColumn(tab.caret.head);
            if (!(to == tab.caret.head)) {
                tab.caret.head = doc.erase(Range{tab.caret.head, to});
                tab.caret.anchor = tab.caret.head;
                edited(out, index);
            }
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Enter, true) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, true)) {
        // The new line starts where the old one's text did. Losing the indent on
        // every Enter is the single most irritating thing a code editor can do.
        const u32 indent = doc.indentOf(tab.caret.head.line);
        std::string text = "\n";
        text.append(std::string(doc.line(tab.caret.head.line).substr(0, indent)));
        insertText(tab, out, index, text);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Tab, true))
        insertText(tab, out, index, "    ");

    if (!ctrl)
        return;

    if (ImGui::IsKeyPressed(ImGuiKey_A, false)) {
        const u32 last = doc.lineCount() - 1;
        tab.caret.anchor = Position{0, 0};
        tab.caret.head = Position{last, doc.lineLength(last)};
    }
    if (ImGui::IsKeyPressed(ImGuiKey_C, false) && tab.caret.hasSelection())
        ImGui::SetClipboardText(doc.textIn(tab.caret.selection()).c_str());
    if (ImGui::IsKeyPressed(ImGuiKey_X, false) && tab.caret.hasSelection()) {
        ImGui::SetClipboardText(doc.textIn(tab.caret.selection()).c_str());
        eraseSelection(tab, out, index);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_V, false)) {
        if (const char* text = ImGui::GetClipboardText(); text != nullptr && *text != '\0')
            insertText(tab, out, index, text);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        const bool ok = shift ? doc.redo(tab.caret.head) : doc.undo(tab.caret.head);
        if (ok) {
            tab.caret.anchor = tab.caret.head;
            edited(out, index);
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Y, false) && doc.redo(tab.caret.head)) {
        tab.caret.anchor = tab.caret.head;
        edited(out, index);
    }
    // **Ctrl+S still saves; it just saves the script.** The shell's own Ctrl+S
    // is suppressed while the pane is active, which is what makes one key mean
    // one thing wherever somebody presses it.
    if (ImGui::IsKeyPressed(ImGuiKey_S, false))
        out.save = index;

    if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
        tab.findOpen = true;
        // Seeded from the selection, which is what somebody who highlighted a
        // word and pressed Ctrl+F is asking for.
        if (tab.caret.hasSelection() && tab.caret.selection().begin.line == tab.caret.selection().end.line)
            tab.findText = doc.textIn(tab.caret.selection());
    }
    if (ImGui::IsKeyPressed(ImGuiKey_H, false)) {
        tab.findOpen = true;
        tab.replaceOpen = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_G, false))
        tab.findOpen = true;
}

// --- Drawing -----------------------------------------------------------------

void drawGutter(const OpenScript& tab, const ScriptEditor& editor, const DebugView& debug, const PaneMetrics& m,
                ImDrawList* draw, ImVec2 origin, u32 line, bool current)
{
    const ThemePalette& p = currentTheme().palette;
    const float y = origin.y + static_cast<float>(line) * m.lineHeight;

    // **Where execution is stopped**, drawn as a band rather than a marker in
    // the margin: a person looking for it is looking at the code, not at the
    // numbers. Luau reports lines from one and a document counts them from
    // zero, which is the whole of the conversion here.
    if (debug.parked && debug.chunk == tab.chunk && debug.line == line + 1) {
        draw->AddRectFilled(ImVec2(origin.x, y), ImVec2(origin.x + m.gutter, y + m.lineHeight), col(p.warning, 0.35f));
    }

    char number[16]{};
    (void)std::snprintf(number, sizeof(number), "%u", line + 1);
    const float width = ImGui::CalcTextSize(number).x;
    draw->AddText(m.font, m.size, ImVec2(origin.x + m.gutter - m.advance - width, y),
                  col(current ? p.text : p.textMuted), number);

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
    const Theme& theme = currentTheme();
    const std::string_view text = tab.document.line(line);
    if (text.empty())
        return;

    const float y = textOrigin.y + static_cast<float>(line) * m.lineHeight;
    // **Only the columns that can be seen.** A minified line of a hundred
    // thousand bytes is one `AddText` of a hundred thousand glyphs otherwise,
    // and the clip rectangle would throw away the work after it was done.
    const auto lastVisibleCell = static_cast<u32>(paneWidth / m.advance) + 2u;

    // `from` and `to` are BYTE columns; where a run is DRAWN is its CELL. The
    // two differ the moment a line holds anything outside ASCII, and treating
    // them as one is what put a space after every accented letter.
    const auto run = [&](u32 from, u32 to, ImU32 colour) {
        if (to <= from)
            return;
        const u32 cell = tab.document.cellOf(line, from);
        if (cell >= lastVisibleCell)
            return;
        draw->AddText(m.font, m.size, ImVec2(textOrigin.x + static_cast<float>(cell) * m.advance, y), colour,
                      text.data() + from, text.data() + to);
    };

    const ImU32 plain = col(theme.palette.text);
    u32 column = 0;
    for (const Token& token : tab.document.tokens(line)) {
        // The gaps between runs are whitespace the lexer did not name, drawn in
        // the pane's own foreground so a tab or a stray byte is still visible.
        run(column, token.column, plain);
        run(token.column, token.column + token.length, syntaxColor(theme.syntax, theme.palette, token.kind));
        column = std::max(column, token.column + token.length);
    }
    run(column, static_cast<u32>(text.size()), plain);
}

void drawSelection(const OpenScript& tab, const PaneMetrics& m, ImDrawList* draw, ImVec2 textOrigin, u32 first,
                   u32 last)
{
    if (!tab.caret.hasSelection())
        return;

    const Range span = tab.caret.selection();
    const ImU32 colour = col(currentTheme().palette.accent, 0.30f);
    for (u32 line = std::max(first, span.begin.line); line <= std::min(last, span.end.line); ++line) {
        const u32 from = tab.document.cellOf(line, line == span.begin.line ? span.begin.column : 0u);
        // A line in the middle of a selection is highlighted one cell past its
        // end, so a multi-line selection reads as covering the newline it holds.
        const u32 to =
            line == span.end.line ? tab.document.cellOf(line, span.end.column) : tab.document.cellCount(line) + 1u;
        const float y = textOrigin.y + static_cast<float>(line) * m.lineHeight;
        draw->AddRectFilled(ImVec2(textOrigin.x + static_cast<float>(from) * m.advance, y),
                            ImVec2(textOrigin.x + static_cast<float>(to) * m.advance, y + m.lineHeight), colour);
    }
}

void drawDiagnostics(const OpenScript& tab, const PaneMetrics& m, ImDrawList* draw, ImVec2 textOrigin, u32 first,
                     u32 last)
{
    const ImU32 colour = col(currentTheme().palette.danger);
    const ImU32 warned = col(currentTheme().palette.warning, 0.75f);
    for (const Diagnostic& diagnostic : tab.document.diagnostics()) {
        if (diagnostic.at.line < first || diagnostic.at.line > last)
            continue;
        const float y = textOrigin.y + static_cast<float>(diagnostic.at.line + 1) * m.lineHeight - 2.0f;
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

void stepMatch(OpenScript& tab, bool forward)
{
    if (tab.findText.empty())
        return;
    const ScriptDocument::SearchOptions options{.matchCase = tab.matchCase, .wholeWord = tab.wholeWord};
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

void drawFindBar(OpenScript& tab, ScriptEditorCommands& out, std::size_t index)
{
    if (!tab.findOpen)
        return;

    const ThemePalette& p = currentTheme().palette;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(p.surfaceRaised.r, p.surfaceRaised.g, p.surfaceRaised.b, 1.0f));
    const float rows = tab.replaceOpen ? 2.0f : 1.0f;
    if (ImGui::BeginChild("##find", ImVec2(0.0f, ImGui::GetFrameHeightWithSpacing() * rows + 8.0f),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding)) {
        const float field = std::max(140.0f, ImGui::GetContentRegionAvail().x * 0.35f);

        if (stringField("##find-text", "find", tab.findText, field))
            stepMatch(tab, true);
        ImGui::SameLine();
        if (ImGui::Button("<"))
            stepMatch(tab, false);
        ImGui::SameLine();
        if (ImGui::Button(">"))
            stepMatch(tab, true);
        ImGui::SameLine();
        ImGui::Checkbox("Aa", &tab.matchCase);
        ImGui::SameLine();
        ImGui::Checkbox("Word", &tab.wholeWord);
        ImGui::SameLine();
        // The count, which is the one number a person actually reads off a find
        // bar -- "is it there at all" before "where".
        const core::u32 total =
            tab.document.countMatches(tab.findText, {.matchCase = tab.matchCase, .wholeWord = tab.wholeWord});
        ImGui::TextDisabled("%u match%s", total, total == 1 ? "" : "es");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Close").x -
                        ImGui::GetStyle().FramePadding.x * 2.0f);
        if (ImGui::SmallButton("Close")) {
            tab.findOpen = false;
            tab.replaceOpen = false;
        }

        if (tab.replaceOpen) {
            (void)stringField("##replace-text", "replace with", tab.replaceText, field);
            ImGui::SameLine();
            ImGui::BeginDisabled(tab.findText.empty());
            if (ImGui::Button("Replace all")) {
                const core::u32 replaced = tab.document.replaceAll(
                    tab.findText, tab.replaceText, {.matchCase = tab.matchCase, .wholeWord = tab.wholeWord});
                if (replaced > 0) {
                    tab.caret = Caret{};
                    edited(out, index);
                }
            }
            ImGui::EndDisabled();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void drawPane(OpenScript& tab, ScriptEditor& editor, const DebugView& debug, const scene::World* world,
              core::InstanceId root, ScriptEditorCommands& out, std::size_t index)
{
    const ThemePalette& p = currentTheme().palette;
    const PaneMetrics m = metricsFor(tab.document, editor.zoom());

    drawFindBar(tab, out, index);

    // **Parsed when the text is at rest**, which is one frame after the last
    // edit: per keystroke would re-parse a file per character, and a timer would
    // put a clock in a panel.
    if (tab.document.diagnosticsStale() && tab.document.revision() == tab.idleRevision)
        tab.document.refreshDiagnostics();
    tab.idleRevision = tab.document.revision();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(p.surface.r, p.surface.g, p.surface.b, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
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

    // The extent, which is also the click target. An `InvisibleButton` would
    // reset the active id on release; this claims it and keeps it, which is what
    // a caret needs and what makes `IsAnyItemActive()` true for the shell's own
    // guards.
    const ImVec2 extent(std::max(paneWidth, m.gutter + 200.0f * m.advance),
                        static_cast<float>(lineCount) * m.lineHeight + m.lineHeight);
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
    const bool hovered = ImGui::ItemHoverable(bounds, id, 0);
    bool active = ImGui::GetActiveID() == id;

    // The gutter is a different target from the text: clicking a line number
    // arms a breakpoint and must not move the caret, which is what every editor
    // does and what stops a breakpoint from throwing away a selection.
    const bool overGutter = hovered && ImGui::GetIO().MousePos.x < origin.x + ImGui::GetScrollX() + m.gutter;

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ImGui::SetActiveID(id, window);
        ImGui::SetFocusID(id, window);
        ImGui::FocusWindow(window);
        active = true;
        if (!overGutter) {
            g_dragging = id;
            const Position at = hitTest(tab.document, m, textOrigin, ImGui::GetIO().MousePos);
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                const Range word = tab.document.wordAt(at);
                tab.caret.anchor = word.begin;
                tab.caret.head = word.end;
                g_dragByWord = true;
                g_dragWord = word;
            }
            else {
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
    }
    if (g_dragging == id && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        const Position at = hitTest(tab.document, m, textOrigin, ImGui::GetIO().MousePos);
        if (!g_dragByWord) {
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
        if (ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt && ImGui::IsKeyPressed(ImGuiKey_0, false)) {
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
                              textOrigin.y + static_cast<float>(tab.caret.head.line) * m.lineHeight);
        ime.InputLineHeight = m.lineHeight;
        ime.ViewportId = window->Viewport->ID;

        const core::u64 before = tab.document.revision();
        handleTyping(tab, out, index);
        handleKeys(tab, out, index, m, paneHeight);

        // **Offered after the text moved, not on a key.** Backspacing through a
        // word then narrows the list instead of dismissing it, and typing a `.`
        // opens it without anybody asking.
        const bool ctrlSpace =
            ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt && ImGui::IsKeyPressed(ImGuiKey_Space, false);
        if (tab.document.revision() != before || ctrlSpace)
            refreshCompletions(tab, world, root);
        // **Escape lets go of the pane rather than clearing the selection.** One
        // press to leave the code, and the second means what the shell says.
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            ImGui::ClearActiveID();
            active = false;
        }
        else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !hovered) {
            ImGui::ClearActiveID();
            active = false;
        }
    }

    // Which lines can be seen: the same arithmetic `ImGuiListClipper` does, done
    // by hand because the pane already owns its extent and its scroll.
    const float scroll = ImGui::GetScrollY();
    const auto first = static_cast<u32>(std::max(0.0f, std::floor(scroll / m.lineHeight)));
    const auto last = std::min(lineCount - 1, first + static_cast<u32>(paneHeight / m.lineHeight) + 1u);

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
    const float caretY = textOrigin.y + static_cast<float>(tab.caret.head.line) * m.lineHeight;
    if (active) {
        draw->AddRectFilled(
            ImVec2(origin.x + m.gutter, caretY),
            ImVec2(origin.x + ImGui::GetScrollX() + std::max(paneWidth, extent.x), caretY + m.lineHeight),
            col(p.surfaceRaised, 0.45f));
    }

    if (debug.parked && debug.chunk == tab.chunk && debug.line > 0) {
        const float stopY = textOrigin.y + static_cast<float>(debug.line - 1) * m.lineHeight;
        draw->AddRectFilled(
            ImVec2(origin.x + m.gutter, stopY),
            ImVec2(origin.x + ImGui::GetScrollX() + std::max(paneWidth, extent.x), stopY + m.lineHeight),
            col(p.warning, 0.22f));
    }

    drawSelection(tab, m, draw, textOrigin, first, last);
    for (u32 line = first; line <= last && line < lineCount; ++line) {
        drawGutter(tab, editor, debug, m, draw, ImVec2(origin.x + ImGui::GetScrollX(), origin.y), line,
                   line == tab.caret.head.line);
        drawLine(tab, m, draw, textOrigin, line, paneWidth + ImGui::GetScrollX());
    }
    drawDiagnostics(tab, m, draw, textOrigin, first, last);

    if (active) {
        // Blinking on ImGui's own clock, so every caret in the application
        // blinks together.
        const float phase = std::fmod(static_cast<float>(ImGui::GetTime()), 1.06f);
        if (phase < 0.7f) {
            const float x =
                textOrigin.x +
                static_cast<float>(tab.document.cellOf(tab.caret.head.line, tab.caret.head.column)) * m.advance;
            draw->AddLine(ImVec2(x, caretY), ImVec2(x, caretY + m.lineHeight), col(p.accent), 1.5f);
        }
    }

    // **The view follows the caret, and only when the caret moved.** Ctrl+End in
    // a long file otherwise put the caret at the bottom of a document still
    // showing its first page.
    if (!(tab.caret.head == tab.shownCaret)) {
        tab.shownCaret = tab.caret.head;
        const float caretTop = static_cast<float>(tab.caret.head.line) * m.lineHeight;
        if (caretTop < scroll)
            ImGui::SetScrollY(caretTop);
        else if (caretTop + m.lineHeight > scroll + paneHeight)
            ImGui::SetScrollY(caretTop + m.lineHeight - paneHeight);

        const float caretX =
            static_cast<float>(tab.document.cellOf(tab.caret.head.line, tab.caret.head.column)) * m.advance;
        const float scrollX = ImGui::GetScrollX();
        if (caretX < scrollX)
            ImGui::SetScrollX(std::max(0.0f, caretX - m.advance * 4.0f));
        else if (caretX + m.gutter + m.advance > scrollX + paneWidth)
            ImGui::SetScrollX(caretX + m.gutter + m.advance - paneWidth);
    }

    drawZoomReadout(editor, m);
    drawCompletions(tab, m, textOrigin);

    // A gutter click arms or disarms a breakpoint. Recorded rather than acted
    // on: the debugger has to be told, and it lives a frame away.
    if (overGutter && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        out.toggleBreakpointLine = hitTest(tab.document, m, textOrigin, ImGui::GetIO().MousePos).line;

    ImGui::EndChild();
}

} // namespace

void drawDebugPanel(ScriptEditor& editor, DebugView& debug, ScriptEditorCommands& out, bool& open)
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

    if (!ImGui::Begin("Debug", &open)) {
        ImGui::End();
        return;
    }

    const ThemePalette& p = currentTheme().palette;

    // The transport. Disabled rather than hidden when nothing is stopped, so
    // the buttons stay where a hand already expects them -- the same rule the
    // File menu follows for Save.
    ImGui::BeginDisabled(!debug.parked);
    if (ImGui::Button("Continue"))
        out.step = DebugStep::Continue;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("let the script run on (F5)");
    ImGui::SameLine();
    if (ImGui::Button("Over"))
        out.step = DebugStep::Over;
    ImGui::SameLine();
    if (ImGui::Button("Into"))
        out.step = DebugStep::Into;
    ImGui::SameLine();
    if (ImGui::Button("Out"))
        out.step = DebugStep::Out;
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (debug.parked)
        ImGui::TextColored(ImVec4(p.warning.r, p.warning.g, p.warning.b, 1.0f), "stopped in %s at line %u",
                           debug.chunk.c_str(), debug.line);
    else
        ImGui::TextDisabled("running");

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
    if (inside)
        return;

    ImGui::ClearActiveID();
    g_paneActiveId = 0;
}

void drawScriptEditor(ScriptEditor& editor, core::u32 dockNode, DebugView& debug, const scene::World* world,
                      core::InstanceId root, ScriptEditorCommands& out)
{
    const std::optional<std::size_t> focus = editor.takeFocusRequest();

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
        char name[224]{};
        (void)std::snprintf(name, sizeof(name), "%s%s###script-%u", tabIconPad().c_str(), tab->title.c_str(),
                            tab->instance.index);

        // Beside the Viewport on first appearance, and wherever somebody moved
        // it afterwards -- `FirstUseEver` is what lets the saved layout win.
        if (dockNode != 0)
            ImGui::SetNextWindowDockID(static_cast<ImGuiID>(dockNode), ImGuiCond_FirstUseEver);

        bool open = true;
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        if (tab->dirty())
            flags |= ImGuiWindowFlags_UnsavedDocument;

        if (ImGui::Begin(name, &open, flags)) {
            if (ImGui::IsWindowAppearing() || ImGui::IsWindowFocused())
                editor.setActive(index);
            drawPane(*tab, editor, debug, world, root, out, index);
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
                      ScriptEditorCommands&)
{}

void releaseScriptPaneFocus()
{}

void drawDebugPanel(ScriptEditor&, DebugView&, ScriptEditorCommands&, bool&)
{}

} // namespace luaug::app

#endif
