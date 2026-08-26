// The three layers of `[graphics]` -- preset, file, command line -- and the one
// rule between them that is not obvious (D052).
//
// `project_config.cpp` had no test of its own until this defect: the flagship
// was the only project file in the repository, so the layering was exercised
// exactly once and always with the same answer. What that hid is what happens
// when a PLAYER contradicts the file, which is the case the layering exists for.

#include "luaug/app/project_config.h"
#include "luaug/core/toml_edit.h"
#include "luaug/platform/file.h"

#include <array>
#include <cmath>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <string>

using namespace luaug;

namespace {

// A project directory with one `luaug.toml` in it. Under the test binary's own
// temporary directory, and removed by the caller's scope guard below.
struct ProjectDir
{
    explicit ProjectDir(const std::string& contents)
    {
        path = std::filesystem::temp_directory_path() /
               ("luaug-project-config-" + std::to_string(std::hash<std::string>{}(contents)));
        std::filesystem::create_directories(path);
        std::ofstream file(path / "luaug.toml", std::ios::binary);
        file << contents;
    }

    ~ProjectDir()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }

    ProjectDir(const ProjectDir&) = delete;
    ProjectDir& operator=(const ProjectDir&) = delete;

    std::filesystem::path path;
};

// Compared with a tolerance in f32 rather than through `doctest::Approx`, whose
// double operand would promote every one of these and trip Clang's
// -Wdouble-promotion in the Tier-2 build.
[[nodiscard]] bool sameMetres(core::f32 left, core::f32 right) noexcept
{
    return std::fabs(left - right) < 1e-3f;
}

// What the flagship's file says, reduced to the part this is about: a level, and
// two shadow keys that refine it.
constexpr const char* kAuthoredHigh = "[project]\n"
                                      "name = \"Test World\"\n"
                                      "\n"
                                      "[graphics]\n"
                                      "quality = \"high\"\n"
                                      "shadow_resolution = 2048\n"
                                      "shadow_distance = 140.0\n";

} // namespace

TEST_CASE("a project file refines the level it names")
{
    const ProjectDir project(kAuthoredHigh);
    const app::ProjectConfig config = app::loadProjectConfig(project.path, {});

    CHECK(config.name == "Test World");
    CHECK(config.graphics.quality == render::QualityLevel::High);
    // The file's own two keys, over the preset's.
    CHECK(config.graphics.shadowTileResolution == 2048u);
    CHECK(sameMetres(config.graphics.shadowDistance, 140.0f));
    // And everything it did not mention is still the preset's.
    CHECK(config.graphics.shadowCascades == render::settingsFor(render::QualityLevel::High).shadowCascades);
}

TEST_CASE("a player's preset replaces the file's refinements, not just its level")
{
    const ProjectDir project(kAuthoredHigh);

    app::GraphicsOverrides overrides;
    overrides.quality = render::QualityLevel::Low;
    const app::ProjectConfig config = app::loadProjectConfig(project.path, overrides);

    // **The whole of D052's second half.** Before it, `--quality=low` took the
    // Low preset and then let the file put a 4096-pixel shadow atlas back on
    // top of it -- the single heaviest dial in the file, on the machine that
    // just asked for less. A preset the player names is the preset they get.
    const render::GraphicsSettings low = render::settingsFor(render::QualityLevel::Low);
    CHECK(config.graphics.quality == render::QualityLevel::Low);
    CHECK(config.graphics.shadowTileResolution == low.shadowTileResolution);
    CHECK(sameMetres(config.graphics.shadowDistance, low.shadowDistance));

    // The file's non-graphics half is untouched: it is not a performance dial.
    CHECK(config.name == "Test World");
}

TEST_CASE("a flag beats the file and the preset both")
{
    const ProjectDir project(kAuthoredHigh);

    app::GraphicsOverrides overrides;
    overrides.quality = render::QualityLevel::Low;
    overrides.shadowResolution = 1024;
    const app::ProjectConfig config = app::loadProjectConfig(project.path, overrides);

    // Typed by the same person as the preset, so it is not the file's
    // refinement coming back -- it is the outer layer being explicit.
    CHECK(config.graphics.shadowTileResolution == 1024u);
    CHECK(config.graphics.quality == render::QualityLevel::Low);
}

TEST_CASE("a project with no file is the defaults plus the command line")
{
    app::GraphicsOverrides overrides;
    overrides.shadowDistance = 60.0f;

    const app::ProjectConfig config =
        app::loadProjectConfig(std::filesystem::temp_directory_path() / "luaug-absent", overrides);

    CHECK(config.graphics.quality == render::QualityLevel::High);
    CHECK(sameMetres(config.graphics.shadowDistance, 60.0f));
    CHECK(config.name.empty());
}

// --- Writing a setting back (S5.7) -------------------------------------------

TEST_CASE("a setting written back is read back, and the file keeps its comments")
{
    // The round trip the Settings dialog is: read, change one thing, write, and
    // the next `loadProjectConfig` sees it. Everything else in the file -- the
    // paragraph at the top especially -- has to survive, because every project
    // file in this repository has one and a dialog that ate it would eat it the
    // first time anybody moved a slider.
    const ProjectDir project(R"(# What this project is, and why these settings are what they are.

[project]
name = "Before"

[graphics]
quality = "high"   # authored against, not demanded
)");

    std::string diagnostic;
    REQUIRE_MESSAGE(
        app::writeProjectSetting(project.path, "project.name", luaug::core::tomlString("After"), &diagnostic),
        diagnostic);

    const app::ProjectConfig config = app::loadProjectConfig(project.path, app::GraphicsOverrides{});
    CHECK(config.name == "After");
    CHECK(config.graphics.quality == render::QualityLevel::High);

    std::string text;
    REQUIRE(luaug::platform::readTextFile(project.path / "luaug.toml", text));
    CHECK(text.find("# What this project is") != std::string::npos);
    CHECK(text.find("# authored against, not demanded") != std::string::npos);
}

TEST_CASE("a project with no file yet gets one")
{
    // Otherwise the Settings dialog works on some projects and silently does
    // nothing on the rest -- and "the rest" is every project before somebody
    // first names its window.
    const std::filesystem::path fresh = std::filesystem::temp_directory_path() / "luaug-project-config-fresh";
    std::error_code ignored;
    std::filesystem::remove_all(fresh, ignored);
    std::filesystem::create_directories(fresh);

    std::string diagnostic;
    REQUIRE_MESSAGE(app::writeProjectSetting(fresh, "window.title", luaug::core::tomlString("A New Game"), &diagnostic),
                    diagnostic);

    const app::ProjectConfig config = app::loadProjectConfig(fresh, app::GraphicsOverrides{});
    CHECK(config.windowTitle == "A New Game");
    std::filesystem::remove_all(fresh, ignored);
}

TEST_CASE("a write that would leave the file unreadable is refused before it happens")
{
    // The check that stops a Settings dialog turning a working project into one
    // the engine will not open. The edit is textual, so a value that is not a
    // TOML literal produces a file the reader refuses -- and writing it anyway
    // would be a dialog whose whole job is to be safe to poke at doing the one
    // unsafe thing.
    const ProjectDir project(R"([project]
name = "Intact"
)");

    std::string diagnostic;
    // Not run through `tomlString`, which is exactly the mistake this guards.
    CHECK_FALSE(app::writeProjectSetting(project.path, "project.name", "not a literal", &diagnostic));
    CHECK_FALSE(diagnostic.empty());

    const app::ProjectConfig config = app::loadProjectConfig(project.path, app::GraphicsOverrides{});
    CHECK(config.name == "Intact");
}

TEST_CASE("the window size round-trips as two integers")
{
    const ProjectDir project(R"([window]
size = [1280, 720]
)");

    const std::array<luaug::core::f64, 2> wanted{1920.0, 1080.0};
    std::string diagnostic;
    REQUIRE_MESSAGE(
        app::writeProjectSetting(project.path, "window.size", luaug::core::tomlNumberArray(wanted), &diagnostic),
        diagnostic);

    const app::ProjectConfig config = app::loadProjectConfig(project.path, app::GraphicsOverrides{});
    CHECK(config.windowWidth == 1920);
    CHECK(config.windowHeight == 1080);

    // As integers, so a save is not a diff on a value nobody changed.
    std::string text;
    REQUIRE(luaug::platform::readTextFile(project.path / "luaug.toml", text));
    CHECK(text.find("[1920, 1080]") != std::string::npos);
}
