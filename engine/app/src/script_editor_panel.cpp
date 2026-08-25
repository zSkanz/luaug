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

#if LUAUG_DEBUG_UI

#include <algorithm>
#include <cmath>
#include <cstdio>
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

[[nodiscard]] PaneMetrics metricsFor(const ScriptDocument& document)
{
    PaneMetrics m;
    const ImGuiStyle& style = ImGui::GetStyle();
    m.font = codeFont() != nullptr ? codeFont() : ImGui::GetFont();
    m.size = style.FontSizeBase;
    m.lineHeight = std::floor(ImGui::GetFontSize() * 1.35f);
    ImFontBaked* baked = m.font->GetFontBaked(ImGui::GetFontSize());
    m.advance = baked != nullptr ? baked->GetCharAdvance('0') : ImGui::GetFontSize() * 0.5f;
    if (m.advance <= 0.0f)
        m.advance = ImGui::GetFontSize() * 0.5f;

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

// --- The caret ---------------------------------------------------------------

void placeCaret(OpenScript& tab, Position to, bool select)
{
    tab.caret.head = tab.document.clamp(to);
    if (!select)
        tab.caret.anchor = tab.caret.head;
    tab.caret.desiredColumn = tab.caret.head.column;
    // Moving by hand ends a typing run, so the next Ctrl+Z stops where somebody
    // moved rather than swallowing what came before.
    tab.document.breakUndoRun();
}

// Vertical movement keeps the column somebody was aiming for, so passing through
// a short line and coming back lands where they left.
void moveVertically(OpenScript& tab, int delta, bool select)
{
    const u32 wanted = tab.caret.desiredColumn;
    const auto line = static_cast<std::int64_t>(tab.caret.head.line) + delta;
    const auto clamped = static_cast<u32>(std::clamp<std::int64_t>(line, 0, tab.document.lineCount() - 1));
    tab.caret.head = tab.document.clamp(Position{clamped, wanted});
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
    const float column = std::round((point.x - textOrigin.x) / m.advance);
    return document.clamp(Position{line, static_cast<u32>(std::max(0.0f, column))});
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

void handleKeys(OpenScript& tab, ScriptEditorCommands& out, std::size_t index, const PaneMetrics& m, float paneHeight)
{
    ImGuiIO& io = ImGui::GetIO();
    const bool shift = io.KeyShift;
    const bool ctrl = io.KeyCtrl;
    ScriptDocument& doc = tab.document;

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
}

// --- Drawing -----------------------------------------------------------------

void drawGutter(const OpenScript& tab, const ScriptEditor& editor, const PaneMetrics& m, ImDrawList* draw,
                ImVec2 origin, u32 line, bool current)
{
    const ThemePalette& p = currentTheme().palette;
    const float y = origin.y + static_cast<float>(line) * m.lineHeight;

    char number[16]{};
    (void)std::snprintf(number, sizeof(number), "%u", line + 1);
    const float width = ImGui::CalcTextSize(number).x;
    draw->AddText(m.font, ImGui::GetFontSize(), ImVec2(origin.x + m.gutter - m.advance - width, y),
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
    const auto lastVisible = static_cast<u32>(paneWidth / m.advance) + 2u;

    const auto run = [&](u32 from, u32 to, ImU32 colour) {
        if (to <= from || from >= lastVisible)
            return;
        to = std::min(to, lastVisible);
        draw->AddText(m.font, ImGui::GetFontSize(), ImVec2(textOrigin.x + static_cast<float>(from) * m.advance, y),
                      colour, text.data() + from, text.data() + to);
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
        const u32 from = line == span.begin.line ? span.begin.column : 0u;
        const u32 to = line == span.end.line ? span.end.column : tab.document.lineLength(line) + 1u;
        const float y = textOrigin.y + static_cast<float>(line) * m.lineHeight;
        draw->AddRectFilled(ImVec2(textOrigin.x + static_cast<float>(from) * m.advance, y),
                            ImVec2(textOrigin.x + static_cast<float>(to) * m.advance, y + m.lineHeight), colour);
    }
}

void drawDiagnostics(const OpenScript& tab, const PaneMetrics& m, ImDrawList* draw, ImVec2 textOrigin, u32 first,
                     u32 last)
{
    const ImU32 colour = col(currentTheme().palette.danger);
    for (const Diagnostic& diagnostic : tab.document.diagnostics()) {
        if (diagnostic.at.line < first || diagnostic.at.line > last)
            continue;
        const float y = textOrigin.y + static_cast<float>(diagnostic.at.line + 1) * m.lineHeight - 2.0f;
        const float x0 = textOrigin.x + static_cast<float>(diagnostic.at.column) * m.advance;
        const float x1 = std::max(
            x0 + m.advance, textOrigin.x + static_cast<float>(tab.document.lineLength(diagnostic.at.line)) * m.advance);
        // A straight underline rather than a wave: at one physical pixel a wave
        // is a dotted line that reads as a rendering fault.
        draw->AddLine(ImVec2(x0, y), ImVec2(x1, y), colour, 1.0f);
    }
}

void drawPane(OpenScript& tab, ScriptEditor& editor, ScriptEditorCommands& out, std::size_t index)
{
    const ThemePalette& p = currentTheme().palette;
    const PaneMetrics m = metricsFor(tab.document);

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
    if (!ImGui::ItemAdd(bounds, id)) {
        ImGui::EndChild();
        return;
    }

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const bool hovered = ImGui::ItemHoverable(bounds, id, 0);
    bool active = ImGui::GetActiveID() == id;

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ImGui::SetActiveID(id, window);
        ImGui::SetFocusID(id, window);
        ImGui::FocusWindow(window);
        active = true;
        const Position at = hitTest(tab.document, m, textOrigin, ImGui::GetIO().MousePos);
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            const Range word = tab.document.wordAt(at);
            tab.caret.anchor = word.begin;
            tab.caret.head = word.end;
        }
        else {
            placeCaret(tab, at, ImGui::GetIO().KeyShift);
        }
    }
    if (active && ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
        tab.caret.head = hitTest(tab.document, m, textOrigin, ImGui::GetIO().MousePos);

    if (active) {
        // What makes the shell's `!IsAnyItemActive()` guards do the right thing
        // without any of them being edited.
        ImGui::SetActiveIdUsingAllKeyboardKeys();

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

        handleTyping(tab, out, index);
        handleKeys(tab, out, index, m, paneHeight);
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

    drawSelection(tab, m, draw, textOrigin, first, last);
    for (u32 line = first; line <= last && line < lineCount; ++line) {
        drawGutter(tab, editor, m, draw, ImVec2(origin.x + ImGui::GetScrollX(), origin.y), line,
                   line == tab.caret.head.line);
        drawLine(tab, m, draw, textOrigin, line, paneWidth + ImGui::GetScrollX());
    }
    drawDiagnostics(tab, m, draw, textOrigin, first, last);

    if (active) {
        // Blinking on ImGui's own clock, so every caret in the application
        // blinks together.
        const float phase = std::fmod(static_cast<float>(ImGui::GetTime()), 1.06f);
        if (phase < 0.7f) {
            const float x = textOrigin.x + static_cast<float>(tab.caret.head.column) * m.advance;
            draw->AddLine(ImVec2(x, caretY), ImVec2(x, caretY + m.lineHeight), col(p.accent), 1.5f);
        }
    }

    // A gutter click arms or disarms a breakpoint. Recorded rather than acted
    // on: the debugger has to be told, and it lives a frame away.
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        ImGui::GetIO().MousePos.x < origin.x + ImGui::GetScrollX() + m.gutter) {
        out.toggleBreakpointLine = hitTest(tab.document, m, textOrigin, ImGui::GetIO().MousePos).line;
    }

    ImGui::EndChild();
}

} // namespace

void drawScriptEditor(ScriptEditor& editor, core::u32 dockNode, ScriptEditorCommands& out)
{
    for (std::size_t index = 0; index < editor.count(); ++index) {
        OpenScript* tab = editor.at(index);
        if (tab == nullptr)
            continue;

        // `Title###id` so the label can change -- a rename, a dirty marker --
        // while the id ImGui docks by stays the same. Keyed on the instance,
        // which is what the tab IS.
        char name[192]{};
        (void)std::snprintf(name, sizeof(name), "%s###script-%u", tab->title.c_str(), tab->instance.index);

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
            drawPane(*tab, editor, out, index);
        }
        ImGui::End();

        if (!open)
            out.close = index;
    }
}

} // namespace luaug::app

#else

namespace luaug::app {

// ADR 0011: a shipping build has no ImGui, so the pane has no body. The
// signature stays so the frame loop calls it without an #ifdef.
void drawScriptEditor(ScriptEditor&, core::u32, ScriptEditorCommands&)
{}

} // namespace luaug::app

#endif
