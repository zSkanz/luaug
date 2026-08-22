// The three layers of `[graphics]` -- preset, file, command line -- and the one
// rule between them that is not obvious (D052).
//
// `project_config.cpp` had no test of its own until this defect: the flagship
// was the only project file in the repository, so the layering was exercised
// exactly once and always with the same answer. What that hid is what happens
// when a PLAYER contradicts the file, which is the case the layering exists for.

#include "luaug/app/project_config.h"

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
