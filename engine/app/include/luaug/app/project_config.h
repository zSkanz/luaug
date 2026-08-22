// What `luaug.toml` tells the ENGINE (api-design.md §4, roadmap M8).
//
// The project file has been the CLI's since M3 -- `tools/cli/project.luau`
// reads it with the TOML subset in `tools/cli/toml.luau`. M8 is the first
// milestone where the host needs it too, and the reason is packaging: a game
// built with `luaug build` runs `luaug-host` directly, with no Lute anywhere
// near it, so a window title and a graphics preset that only the CLI could read
// would be settings that stop applying the moment the game ships.
//
// Only the keys the host acts on are read here. Everything else in the file --
// `[dev] port`, `[assets]`, `[build]` -- belongs to tools that run before the
// engine does, and reading it here would be duplicating a decision.
#pragma once

#include "luaug/core/types.h"
#include "luaug/render/settings.h"

#include <filesystem>
#include <optional>
#include <string>

namespace luaug::app {

// A graphics field the command line named explicitly. The distinction matters
// because the project file is a DEFAULT and a flag is an override: without it,
// a host that filled `GraphicsSettings` from its own defaults could not tell
// "the user asked for scale 1.0" from "nobody said anything".
struct GraphicsOverrides
{
    std::optional<render::QualityLevel> quality;
    std::optional<core::f32> renderScale;
    std::optional<core::u32> shadowResolution;
    std::optional<core::u32> shadowCascades;
    std::optional<core::f32> shadowDistance;
    std::optional<core::u32> lightBudget;
    std::optional<bool> bloom;
    std::optional<bool> ambientOcclusion;
    std::optional<bool> antiAliasing;
    std::optional<bool> autoExposure;
};

struct ProjectConfig
{
    // `[project] name`, or empty. The window falls back to the engine's own
    // titled window when a project does not name itself.
    std::string name;

    // `[project] id` -- reverse-DNS, and on Windows the identity the shell
    // groups taskbar buttons and pinned shortcuts by. Two games built with this
    // engine that shared one id would share one taskbar button.
    std::string id;

    // `[window] title`, or empty. **The game's own string, not the engine's**:
    // it is passed through rather than translated, which is the split
    // `log()`/`logText()` already draws and which `WindowDesc` reserved a
    // passthrough for at M1.
    std::string windowTitle;

    // `[window] size`, or zero when the file does not say.
    core::i32 windowWidth = 0;
    core::i32 windowHeight = 0;

    // `[project] icon` -- a project-relative path to a PNG or `.ico`. Read here
    // so the dev host can wear the game's face; `luaug build` reads the same key
    // to embed it in the packaged artifact.
    std::string icon;

    // `[project] scene` -- the scene a RUN of this project starts with, as a
    // content-relative path (`scenes/main.scene.json`).
    //
    // Declared rather than found by convention. A fixed filename the engine
    // looks for is a rule nobody can see in a project that has three scenes,
    // and "which one starts" is a decision a project makes -- the same decision
    // the first entry of Unity's build settings is. Empty means the project
    // starts with whatever its scripts build, which is every example before
    // `06-scene`.
    std::string scene;

    render::GraphicsSettings graphics;
};

// Reads `<projectRoot>/luaug.toml`, resolves the graphics family through its
// three layers -- preset, then the file, then the overrides -- and clamps the
// result.
//
// **A project with no file is not an error**, and neither is a project whose
// file says nothing about graphics: both mean "the defaults, plus whatever the
// command line said". A file that fails to PARSE is a different thing and is
// reported: a config format that quietly ignores what it cannot read is how a
// setting ends up not applying.
//
// `diagnostic` is filled on a parse failure and is developer-facing (R3 exempt,
// like every other config diagnostic in the engine).
[[nodiscard]] ProjectConfig loadProjectConfig(const std::filesystem::path& projectRoot,
                                              const GraphicsOverrides& overrides, std::string* diagnostic = nullptr);

// The same resolution without a file, for a bare script or a project that has
// none.
[[nodiscard]] render::GraphicsSettings resolveGraphics(const GraphicsOverrides& overrides);

} // namespace luaug::app
