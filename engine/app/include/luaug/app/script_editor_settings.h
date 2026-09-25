// What a person chose about the script editor: every colour it draws with, and
// the keys its commands answer to.
//
// **The reference editor lets both be changed, so this one does too** (the
// owner: "every colour the other editor lets you change, and the shortcuts,
// are the user's preferences"). The colour list is its Script Editor list --
// `function`, `local`, `nil`, `self`, `TODO`, bool, bracket, built-in
// function, comment, current line, debugger lines, error, function name,
// keyword, Luau keyword, matching word, method, number, operator, property,
// string, type, warning, whitespace -- plus the ground, the selection, the
// line numbers and the caret, which it spreads over other settings. Hint and
// information are left out: this editor draws no diagnostic of either kind,
// and a colour that changes nothing is a setting that looks broken.
//
// Per USER, beside `appearance.json` and for its reason: how code looks and
// which hand reaches for which key is about a person, not a project.
//
// No ImGui here, so the list, the defaults and the file are testable; a key is
// named the way ImGui names it (`ImGui::GetKeyName`), and the panel turns the
// name back into a key.
#pragma once

#include "luaug/core/math.h"
#include "luaug/core/types.h"

#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace luaug::app {

struct Theme;

// Every colour, in the order the Preferences page lists them.
enum class ScriptColor : core::u8
{
    // --- The code ------------------------------------------------------------
    Text,
    Keyword,
    LuauKeyword,
    FunctionKeyword,
    LocalKeyword,
    Nil,
    Bool,
    Self,
    Number,
    String,
    Comment,
    Todo,
    Operator,
    Bracket,
    BuiltinFunction,
    FunctionName,
    Method,
    Property,
    Type,
    Attribute,
    BrokenToken,
    // --- The pane ------------------------------------------------------------
    Background,
    Selection,
    CurrentLine,
    MatchingWord,
    LineNumber,
    // The indentation guides: the one thing whitespace draws here.
    Whitespace,
    Caret,
    ErrorUnderline,
    WarningUnderline,
    DebuggerCurrentLine,
    DebuggerErrorLine,

    Count,
};

inline constexpr std::size_t kScriptColorCount = static_cast<std::size_t>(ScriptColor::Count);

struct ScriptColorInfo
{
    // What the file stores. Renaming one resets that colour for everybody, so
    // it is an identifier, not a label.
    std::string_view id;
    // What the Preferences page says.
    std::string_view label;
};

[[nodiscard]] std::span<const ScriptColorInfo, kScriptColorCount> scriptColorInfo() noexcept;

// What `color` is when nobody changed it: the theme's, so switching theme
// moves every colour nobody chose by hand.
[[nodiscard]] core::Color3 defaultScriptColor(ScriptColor color, const Theme& theme) noexcept;

// Every command whose key can be changed, in the order the page lists them.
// Typing, the arrows, Home, End, Backspace and Delete are not here: they are
// what the keys ARE, in every editor, and rebinding them is how an editor
// stops being usable.
enum class ScriptAction : core::u8
{
    Save,
    Find,
    Replace,
    GoToLine,
    SelectAll,
    Undo,
    Redo,
    Cut,
    Copy,
    Paste,
    ToggleComment,
    BlockComment,
    DeleteLine,
    SelectLine,
    Indent,
    Outdent,
    JumpToBracket,
    MoveLineUp,
    MoveLineDown,
    CopyLineUp,
    CopyLineDown,
    InsertLineBelow,
    InsertLineAbove,
    AddNextOccurrence,
    SelectAllOccurrences,
    AddCaretAbove,
    AddCaretBelow,
    RemoveLastCaret,
    SplitIntoLines,
    TriggerCompletion,
    ResetZoom,
    // Closes the tab, as its cross does -- unsaved work asks first there too.
    CloseTab,

    Count,
};

inline constexpr std::size_t kScriptActionCount = static_cast<std::size_t>(ScriptAction::Count);

struct ScriptActionInfo
{
    std::string_view id;
    std::string_view label;
    // `Ctrl+Shift+K`: modifiers in that order, then the key's ImGui name.
    std::string_view chord;
};

[[nodiscard]] std::span<const ScriptActionInfo, kScriptActionCount> scriptActionInfo() noexcept;

// A key and the modifiers held with it -- EXACTLY those: `Ctrl+D` is not
// pressed by Ctrl+Shift+D, or two commands would answer one chord.
struct KeyChord
{
    std::string key;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;

    [[nodiscard]] bool operator==(const KeyChord&) const = default;
};

// `Ctrl+Shift+K` both ways. Nothing when the text names no key; modifier
// names are read in any order and any case.
[[nodiscard]] std::optional<KeyChord> parseChord(std::string_view text);
[[nodiscard]] std::string formatChord(const KeyChord& chord);

struct ScriptEditorSettings
{
    // A colour somebody chose, or nothing for the theme's.
    std::array<std::optional<core::Color3>, kScriptColorCount> colors{};
    // A chord somebody chose, or nothing for the default. An empty chord
    // (`KeyChord{}`) is a command somebody unbound on purpose.
    std::array<std::optional<KeyChord>, kScriptActionCount> keys{};

    [[nodiscard]] core::Color3 color(ScriptColor which, const Theme& theme) const noexcept;
    [[nodiscard]] KeyChord chord(ScriptAction which) const;
    // The other command already on `chord`, if any -- the page says so rather
    // than letting one chord silently mean two things.
    [[nodiscard]] std::optional<ScriptAction> conflictOf(ScriptAction which, const KeyChord& chord) const;
};

// The one the running editor reads. Main thread only, like the rest of the
// shell.
[[nodiscard]] ScriptEditorSettings& scriptEditorSettings() noexcept;

// `<userDir>/script-editor.json`, or empty where there is no user directory.
[[nodiscard]] std::filesystem::path scriptEditorSettingsFile();
// A missing or unreadable file is the defaults, and an entry this build does
// not know is skipped: a file from another build is not a broken one.
[[nodiscard]] ScriptEditorSettings loadScriptEditorSettings(const std::filesystem::path& file);
// Only what differs from the defaults is written, so a later default reaches
// everybody who never changed that one.
[[nodiscard]] bool saveScriptEditorSettings(const std::filesystem::path& file, const ScriptEditorSettings& settings);

} // namespace luaug::app
