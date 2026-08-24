// The shell's look, asserted without a window (ADR 0056).
//
// **The ImGui shell cannot render headlessly and SDL does not accept injected
// input**, which E1 recorded and every milestone since has repeated. So the
// picture needs a person and everything else belongs here -- and for a theme
// "everything else" turns out to be most of it: whether a colour is legible is
// arithmetic, not an opinion.
//
// The contrast cases are the ones worth reading. They are what stops the next
// person who nudges a hex digit from shipping a muted grey that is only muted on
// the monitor they happened to be using.
#include "luaug/app/ui_theme.h"
#include "luaug/platform/file.h"

#include <doctest/doctest.h>
#include <filesystem>
#include <string>
#include <system_error>

using namespace luaug;

namespace {

// Every colour a theme draws ON its own ground, with the ground it is drawn on.
struct Foreground
{
    const char* name;
    core::Color3 color;
    core::Color3 over;
};

void checkContrast(const app::Theme& theme)
{
    const app::ThemePalette& p = theme.palette;
    const Foreground foregrounds[]{
        {"text", p.text, p.background},
        {"textMuted", p.textMuted, p.background},
        {"accent", p.accent, p.background},
        {"warning", p.warning, p.background},
        {"danger", p.danger, p.background},
        {"success", p.success, p.background},
        // A field and a hovered row are grounds too, and a palette checked only
        // against the window background is a palette whose text goes grey the
        // moment somebody points at it.
        {"text over surface", p.text, p.surface},
        {"textMuted over surface", p.textMuted, p.surface},
        {"text over surfaceRaised", p.text, p.surfaceRaised},
        {"textMuted over surfaceRaised", p.textMuted, p.surfaceRaised},
        // The label of a primary button.
        {"onAccent", p.onAccent, p.accent},
    };

    for (const Foreground& foreground : foregrounds) {
        const core::f32 ratio = app::contrastRatio(foreground.color, foreground.over);
        INFO(std::string(theme.id) << " / " << std::string(foreground.name) << " = " << static_cast<double>(ratio)
                                   << ":1");
        CHECK(ratio >= app::kMinimumContrast);
    }
}

struct Scratch
{
    std::filesystem::path root;

    explicit Scratch(const std::string& name) : root(std::filesystem::temp_directory_path() / "luaug-theme" / name)
    {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        REQUIRE(platform::createDirectories(root));
    }

    ~Scratch()
    {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

} // namespace

TEST_CASE("every theme is complete and legible")
{
    REQUIRE(!app::themes().empty());

    for (const app::Theme& theme : app::themes()) {
        INFO("theme " << theme.id);
        CHECK(!theme.id.empty());
        CHECK(!theme.name.empty());
        // A theme is looked up by id and the ids are what `appearance.json`
        // stores, so two themes sharing one is one of them being unreachable.
        for (const app::Theme& other : app::themes()) {
            if (&other != &theme)
                CHECK(other.id != theme.id);
        }
        checkContrast(theme);
    }
}

TEST_CASE("every syntax token is legible on the code pane's own ground")
{
    // **The ground is `surface`, not `background`** (ADR 0057): a code pane is a
    // recessed area like a text field, and a palette measured against the window
    // would be measured against a colour the code is never drawn on.
    //
    // Eight tokens, two themes, and the same 4.5:1 the eleven interface tokens
    // are held to -- because a comment nobody can read is a comment nobody
    // writes, and "subtle" is the word people use for grey-on-grey right up
    // until somebody else opens the file.
    for (const app::Theme& theme : app::themes()) {
        const app::SyntaxPalette& syntax = theme.syntax;
        const app::ThemePalette& p = theme.palette;
        const struct
        {
            const char* name;
            core::Color3 color;
        } tokens[]{
            {"keyword", syntax.keyword},   {"identifier", syntax.identifier},
            {"number", syntax.number},     {"string", syntax.string},
            {"comment", syntax.comment},   {"operatorToken", syntax.operatorToken},
            {"attribute", syntax.attribute}, {"errorToken", syntax.errorToken},
        };

        for (const auto& token : tokens) {
            const core::f32 ratio = app::contrastRatio(token.color, p.surface);
            INFO(std::string(theme.id) << " / syntax." << std::string(token.name) << " = "
                                       << static_cast<double>(ratio) << ":1");
            CHECK(ratio >= app::kMinimumContrast);
        }
    }
}

TEST_CASE("the code face is a second file, and it is not the interface one")
{
    // Two faces, and the difference is the point: Inter is proportional and a
    // code editor set in a proportional face is not a code editor. Asserted so
    // that somebody staging one file and not the other finds out here.
    CHECK(app::uiFontFile().filename() == "Inter.ttf");
    CHECK(app::codeFontFile().filename() == "Mono.ttf");
    CHECK(app::uiFontFile() != app::codeFontFile());
    // Beside each other, so one staging rule covers both.
    CHECK(app::uiFontFile().parent_path() == app::codeFontFile().parent_path());
}

TEST_CASE("a theme's ground and its raised surface are distinguishable")
{
    // Not a contrast-ratio case: these are two surfaces rather than ink on a
    // surface, and WCAG's 3:1 for non-text is the wrong bar for a one-step
    // elevation. What is being asserted is only that they are not the SAME
    // colour -- a palette where a hovered row is the panel is a palette with no
    // hover at all, and that is a thing a typo produces.
    for (const app::Theme& theme : app::themes()) {
        INFO("theme " << theme.id);
        CHECK(theme.palette.surface != theme.palette.background);
        CHECK(theme.palette.surfaceRaised != theme.palette.background);
        CHECK(theme.palette.border != theme.palette.background);
    }
}

TEST_CASE("the shell is square")
{
    // The one visual claim this repository makes about its own shell, so it is
    // asserted rather than trusted to survive the next person who likes a
    // rounded button.
    CHECK(app::themeMetrics().rounding == 0.0f);
    // With no radius the border is what separates two panels, so it cannot be
    // zero at the same time.
    CHECK(app::themeMetrics().borderSize >= 1.0f);
}

TEST_CASE("contrast is symmetric and bounded")
{
    const core::Color3 white{1.0f, 1.0f, 1.0f};
    const core::Color3 black{0.0f, 0.0f, 0.0f};
    // 21:1 is the specification's maximum, and reproducing it is the cheapest
    // check that the transfer function is the real one rather than a gamma
    // approximation -- an approximation lands near 20 or near 22.
    CHECK(static_cast<double>(app::contrastRatio(white, black)) == doctest::Approx(21.0).epsilon(0.001));
    CHECK(static_cast<double>(app::contrastRatio(black, white)) == doctest::Approx(21.0).epsilon(0.001));
    CHECK(static_cast<double>(app::contrastRatio(white, white)) == doctest::Approx(1.0));
}

TEST_CASE("an unknown theme opens on the default rather than on nothing")
{
    CHECK(app::themeById("dark").id == "dark");
    CHECK(app::themeById("light").id == "light");
    // A file written by a build that carried a theme this one does not. Same
    // rule the scene format and `editor.json` follow: a newer file is a file
    // from next week, not a broken one.
    CHECK(app::themeById("solarized-flamingo").id == app::themes().front().id);
    CHECK(app::themeById("").id == app::themes().front().id);
}

TEST_CASE("the scale follows the display until somebody overrides it")
{
    // Zero is "ask the display", which is the default and the reason a person on
    // a 200% monitor does not have to find a setting before they can read the
    // menu bar.
    CHECK(static_cast<double>(app::resolveUiScale(0.0f, 1.5f)) == doctest::Approx(1.5));
    CHECK(static_cast<double>(app::resolveUiScale(0.0f, 1.0f)) == doctest::Approx(1.0));
    // A stored number wins over the display, because that is what choosing one
    // means.
    CHECK(static_cast<double>(app::resolveUiScale(1.25f, 2.0f)) == doctest::Approx(1.25));
    // A display with nothing to say does not get to multiply the shell by zero.
    CHECK(static_cast<double>(app::resolveUiScale(0.0f, 0.0f)) == doctest::Approx(1.0));
    CHECK(static_cast<double>(app::resolveUiScale(0.0f, -3.0f)) == doctest::Approx(1.0));
    // Hand-edited nonsense is clamped rather than obeyed: below the floor the
    // icons are unreadable and above the ceiling a dialog stops fitting a
    // 1080p screen.
    CHECK(static_cast<double>(app::resolveUiScale(0.1f, 1.0f)) ==
          doctest::Approx(static_cast<double>(app::kMinimumUiScale)));
    CHECK(static_cast<double>(app::resolveUiScale(99.0f, 1.0f)) ==
          doctest::Approx(static_cast<double>(app::kMaximumUiScale)));
}

TEST_CASE("the appearance survives a process")
{
    const Scratch scratch("roundtrip");
    const std::filesystem::path file = scratch.root / "appearance.json";

    CHECK(app::saveAppearance(file, app::Appearance{.themeId = "light", .scale = 1.25f}));

    const app::Appearance read = app::loadAppearance(file);
    CHECK(read.themeId == "light");
    CHECK(static_cast<double>(read.scale) == doctest::Approx(1.25));
}

TEST_CASE("a missing or broken appearance file is the default, not a failure")
{
    const Scratch scratch("missing");

    // Nothing there. The first launch is not an error.
    const app::Appearance absent = app::loadAppearance(scratch.root / "appearance.json");
    CHECK(absent.themeId == app::themes().front().id);
    CHECK(static_cast<double>(absent.scale) == doctest::Approx(0.0));

    // Nowhere to write, which is what a platform with no user directory answers.
    CHECK(!app::saveAppearance(std::filesystem::path{}, app::Appearance{}));
    const app::Appearance nowhere = app::loadAppearance(std::filesystem::path{});
    CHECK(nowhere.themeId == app::themes().front().id);

    // Somebody edited it by hand and broke it.
    const std::filesystem::path broken = scratch.root / "broken.json";
    REQUIRE(platform::writeTextFile(broken, "{ theme: nope"));
    CHECK(app::loadAppearance(broken).themeId == app::themes().front().id);

    // Valid JSON naming a theme that is not there, plus a scale that is not a
    // number. Each field falls back on its own -- a file half of which this
    // build understands is a file it reads half of.
    const std::filesystem::path partial = scratch.root / "partial.json";
    REQUIRE(platform::writeTextFile(partial, R"({"theme":"chartreuse","scale":"big"})"));
    const app::Appearance fallback = app::loadAppearance(partial);
    CHECK(fallback.themeId == app::themes().front().id);
    CHECK(static_cast<double>(fallback.scale) == doctest::Approx(0.0));
}
