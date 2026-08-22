#include "luaug/app/project_config.h"

#include "luaug/core/toml.h"

#include <fstream>
#include <span>
#include <sstream>

namespace luaug::app {
namespace {

using core::f32;
using core::f64;
using core::i32;
using core::u32;
using render::GraphicsSettings;

[[nodiscard]] bool readFile(const std::filesystem::path& path, std::string& out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();
    return true;
}

// The middle layer: what the project file says, over the preset and under the
// command line. Every key is optional and an absent one leaves the preset's
// value, which is what makes `[graphics] quality = "low"` plus one override a
// two-line section rather than a full recital.
void applyFile(const core::TomlDocument& document, GraphicsSettings& settings)
{
    if (const std::optional<f64> value = document.number("graphics.render_scale"))
        settings.renderScale = static_cast<f32>(*value);
    if (const std::optional<f64> value = document.number("graphics.shadow_resolution"))
        settings.shadowTileResolution = static_cast<u32>(*value);
    if (const std::optional<f64> value = document.number("graphics.shadow_cascades"))
        settings.shadowCascades = static_cast<u32>(*value);
    if (const std::optional<f64> value = document.number("graphics.shadow_distance"))
        settings.shadowDistance = static_cast<f32>(*value);
    if (const std::optional<f64> value = document.number("graphics.light_budget"))
        settings.lightBudget = static_cast<u32>(*value);
    if (const std::optional<bool> value = document.boolean("graphics.bloom"))
        settings.bloom = *value;
    if (const std::optional<bool> value = document.boolean("graphics.ambient_occlusion"))
        settings.ambientOcclusion = *value;
    if (const std::optional<bool> value = document.boolean("graphics.anti_aliasing"))
        settings.antiAliasing = *value;
    if (const std::optional<bool> value = document.boolean("graphics.auto_exposure"))
        settings.autoExposure = *value;
}

void applyOverrides(const GraphicsOverrides& overrides, GraphicsSettings& settings)
{
    if (overrides.renderScale)
        settings.renderScale = *overrides.renderScale;
    if (overrides.shadowResolution)
        settings.shadowTileResolution = *overrides.shadowResolution;
    if (overrides.shadowCascades)
        settings.shadowCascades = *overrides.shadowCascades;
    if (overrides.shadowDistance)
        settings.shadowDistance = *overrides.shadowDistance;
    if (overrides.lightBudget)
        settings.lightBudget = *overrides.lightBudget;
    if (overrides.bloom)
        settings.bloom = *overrides.bloom;
    if (overrides.ambientOcclusion)
        settings.ambientOcclusion = *overrides.ambientOcclusion;
    if (overrides.antiAliasing)
        settings.antiAliasing = *overrides.antiAliasing;
    if (overrides.autoExposure)
        settings.autoExposure = *overrides.autoExposure;
}

} // namespace

render::GraphicsSettings resolveGraphics(const GraphicsOverrides& overrides)
{
    GraphicsSettings settings = render::settingsFor(overrides.quality.value_or(render::QualityLevel::High));
    applyOverrides(overrides, settings);
    return render::clampSettings(settings);
}

ProjectConfig loadProjectConfig(const std::filesystem::path& projectRoot, const GraphicsOverrides& overrides,
                                std::string* diagnostic)
{
    ProjectConfig config;

    std::string text;
    if (projectRoot.empty() || !readFile(projectRoot / "luaug.toml", text)) {
        config.graphics = resolveGraphics(overrides);
        return config;
    }

    core::TomlDocument document;
    const core::TomlDocument::ParseResult parsed = document.parse(text, (projectRoot / "luaug.toml").string());
    if (!parsed) {
        if (diagnostic != nullptr)
            *diagnostic = parsed.diagnostic;
        config.graphics = resolveGraphics(overrides);
        return config;
    }

    if (const std::optional<std::string_view> value = document.string("project.name"))
        config.name = *value;
    if (const std::optional<std::string_view> value = document.string("project.id"))
        config.id = *value;
    if (const std::optional<std::string_view> value = document.string("project.icon"))
        config.icon = *value;
    if (const std::optional<std::string_view> value = document.string("window.title"))
        config.windowTitle = *value;

    const std::span<const f64> size = document.numbers("window.size");
    if (size.size() == 2) {
        config.windowWidth = static_cast<i32>(size[0]);
        config.windowHeight = static_cast<i32>(size[1]);
    }

    // The preset the FILE names, so `quality = "low"` plus `bloom = true` is a
    // preset with one thing turned back on rather than a set of eleven numbers
    // the author has to remember.
    render::QualityLevel level = render::QualityLevel::High;
    if (const std::optional<std::string_view> named = document.string("graphics.quality")) {
        if (const std::optional<render::QualityLevel> parsedLevel = render::parseQuality(*named))
            level = *parsedLevel;
    }
    // And the command line's preset wins over the file's, because it is the
    // outer layer: `--quality=low` on a project that asks for ultra is somebody
    // saying "not on this machine".
    config.graphics = render::settingsFor(overrides.quality.value_or(level));

    applyFile(document, config.graphics);
    applyOverrides(overrides, config.graphics);
    config.graphics = render::clampSettings(config.graphics);
    return config;
}

} // namespace luaug::app
