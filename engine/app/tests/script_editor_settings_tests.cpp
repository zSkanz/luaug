// The script editor's colours and keys (the owner: "every colour the other
// editor lets you change, and the shortcuts, are the user's preferences").
#include "luaug/app/script_editor_settings.h"
#include "luaug/app/ui_theme.h"

#include <doctest/doctest.h>
#include <filesystem>
#include <set>
#include <string>

using namespace luaug::app;

TEST_CASE("every script colour and command has a name, and no two share one")
{
    std::set<std::string_view> colours;
    for (const ScriptColorInfo& info : scriptColorInfo()) {
        CHECK_FALSE(info.id.empty());
        CHECK_FALSE(info.label.empty());
        CHECK(colours.insert(info.id).second);
    }
    std::set<std::string_view> actions;
    for (const ScriptActionInfo& info : scriptActionInfo()) {
        CHECK(actions.insert(info.id).second);
        // Every default chord is one the parser reads back.
        CHECK(parseChord(info.chord).has_value());
    }
}

TEST_CASE("no two commands share a default chord")
{
    const ScriptEditorSettings defaults;
    for (std::size_t index = 0; index < kScriptActionCount; ++index) {
        const auto action = static_cast<ScriptAction>(index);
        CHECK_FALSE(defaults.conflictOf(action, defaults.chord(action)).has_value());
    }
}

TEST_CASE("every default code colour is readable on the pane, in every theme")
{
    for (const Theme& theme : themes()) {
        const luaug::core::Color3 ground = defaultScriptColor(ScriptColor::Background, theme);
        for (std::size_t index = 0; index <= static_cast<std::size_t>(ScriptColor::BrokenToken); ++index) {
            const auto colour = static_cast<ScriptColor>(index);
            INFO(theme.id, " ", scriptColorInfo()[index].id);
            CHECK(contrastRatio(defaultScriptColor(colour, theme), ground) >= kMinimumContrast);
        }
    }
}

TEST_CASE("a chord reads and writes in the same spelling, and asks for its modifiers exactly")
{
    const std::optional<KeyChord> chord = parseChord("ctrl+SHIFT+K");
    REQUIRE(chord.has_value());
    CHECK(chord->ctrl);
    CHECK(chord->shift);
    CHECK_FALSE(chord->alt);
    CHECK(chord->key == "K");
    CHECK(formatChord(*chord) == "Ctrl+Shift+K");
    CHECK(formatChord(*parseChord("Alt+Shift+UpArrow")) == "Shift+Alt+UpArrow");
    CHECK_FALSE(parseChord("").has_value());
    CHECK_FALSE(parseChord("Ctrl+").has_value());
    CHECK_FALSE(parseChord("Hyper+K").has_value());
    CHECK_FALSE(*parseChord("Ctrl+D") == *parseChord("Ctrl+Shift+D"));
}

TEST_CASE("the settings file keeps only what somebody changed, and reads it back")
{
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / "luaug-script-editor-settings-test" / "script-editor.json";
    std::error_code ec;
    std::filesystem::remove(file, ec);

    ScriptEditorSettings chosen;
    chosen.colors[static_cast<std::size_t>(ScriptColor::Comment)] = luaug::core::Color3{1.0f, 0.0f, 0.0f};
    chosen.keys[static_cast<std::size_t>(ScriptAction::DeleteLine)] = parseChord("Ctrl+Shift+D");
    chosen.keys[static_cast<std::size_t>(ScriptAction::ResetZoom)] = KeyChord{};
    REQUIRE(saveScriptEditorSettings(file, chosen));

    const ScriptEditorSettings read = loadScriptEditorSettings(file);
    const Theme& theme = themes().front();
    const luaug::core::Color3 comment = read.color(ScriptColor::Comment, theme);
    CHECK(static_cast<double>(comment.r) == doctest::Approx(1.0));
    CHECK(static_cast<double>(comment.g) == doctest::Approx(0.0));
    // Nobody chose the keyword colour, so it is still the theme's.
    CHECK_FALSE(read.colors[static_cast<std::size_t>(ScriptColor::Keyword)].has_value());
    CHECK(formatChord(read.chord(ScriptAction::DeleteLine)) == "Ctrl+Shift+D");
    // Unbound on purpose stays unbound.
    CHECK(read.chord(ScriptAction::ResetZoom).key.empty());
    CHECK(formatChord(read.chord(ScriptAction::Save)) == "Ctrl+S");

    std::filesystem::remove_all(file.parent_path(), ec);
}

TEST_CASE("a missing settings file is the defaults")
{
    const ScriptEditorSettings read = loadScriptEditorSettings(std::filesystem::path("no/such/dir/script-editor.json"));
    CHECK(formatChord(read.chord(ScriptAction::AddNextOccurrence)) == "Ctrl+D");
    CHECK_FALSE(read.colors[0].has_value());
}
