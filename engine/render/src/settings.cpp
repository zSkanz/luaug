#include "luaug/render/settings.h"

#include "luaug/render/clusters.h"
#include "luaug/render/shadow.h"

#include <algorithm>

namespace luaug::render {

GraphicsSettings settingsFor(QualityLevel quality) noexcept
{
    GraphicsSettings settings;
    settings.quality = quality;

    switch (quality) {
    case QualityLevel::Low:
        // Everything that costs fragments, turned down or off. Render scale is
        // the first dial rather than the last because it is the only one that
        // reduces EVERY per-pixel pass at once, and a machine that needs this
        // preset is a machine that is fragment-bound.
        settings.renderScale = 0.75f;
        settings.shadowTileResolution = 512;
        settings.shadowCascades = 2;
        settings.shadowDistance = 70.0f;
        settings.lightBudget = 32;
        settings.bloom = false;
        settings.ambientOcclusion = false;
        settings.antiAliasing = true;
        settings.autoExposure = true;
        break;

    case QualityLevel::Medium:
        settings.renderScale = 1.0f;
        settings.shadowTileResolution = 1024;
        settings.shadowCascades = 3;
        settings.shadowDistance = 100.0f;
        settings.lightBudget = 96;
        settings.bloom = true;
        settings.ambientOcclusion = false;
        settings.antiAliasing = true;
        settings.autoExposure = true;
        break;

    case QualityLevel::High:
        // The M7.5 defaults, to the value. See the header for why that is a
        // requirement and not an accident.
        settings.renderScale = 1.0f;
        settings.shadowTileResolution = kShadowTileResolution;
        settings.shadowCascades = kShadowCascadeCount;
        settings.shadowDistance = kShadowDistance;
        settings.lightBudget = kMaxClusteredLights;
        settings.bloom = true;
        settings.ambientOcclusion = true;
        settings.antiAliasing = true;
        settings.autoExposure = true;
        break;

    case QualityLevel::Ultra:
        // The one preset that spends MORE than the engine's default. Its whole
        // content is shadow quality, because that is where this renderer's
        // remaining headroom visibly goes: four times the atlas, spent on
        // DENSITY rather than on range.
        //
        // **160 metres rather than 220, and D052 is why.** A cascade's texel is
        // its box divided by its tile, and the far cascade's box is set by the
        // frustum's cross-section at the shadow distance -- so pushing the
        // distance out costs resolution in proportion. At 220 metres the far
        // cascade measured 0.32 m per texel against High's 0.35: four times the
        // atlas bought nine per cent, and a preset called Ultra was, for
        // anything past thirty metres, exactly as blocky as the one below it.
        // At 160 it measures 0.23 -- half again finer than High, and still a
        // third further out.
        settings.renderScale = 1.0f;
        settings.shadowTileResolution = 2048;
        settings.shadowCascades = 4;
        settings.shadowDistance = 160.0f;
        settings.lightBudget = kMaxClusteredLights;
        settings.bloom = true;
        settings.ambientOcclusion = true;
        settings.antiAliasing = true;
        settings.autoExposure = true;
        break;
    }

    return settings;
}

GraphicsSettings clampSettings(GraphicsSettings settings) noexcept
{
    // A floor of a half rather than a quarter: below that the world image is
    // upscaled by more than two and the UI drawn crisply on top of it makes the
    // difference impossible to ignore.
    settings.renderScale = std::clamp(settings.renderScale, 0.5f, 1.0f);

    // Powers of two between 256 and 4096, and 4096 is the ceiling because it is
    // the 2D texture size the weakest conforming device is required to support
    // -- an atlas is two tiles across, so 2048 is the largest tile that fits it.
    settings.shadowTileResolution = std::clamp(settings.shadowTileResolution, 256u, 2048u);
    u32 rounded = 256;
    while (rounded * 2 <= settings.shadowTileResolution)
        rounded *= 2;
    settings.shadowTileResolution = rounded;

    settings.shadowCascades = std::min(settings.shadowCascades, kShadowCascadeCount);
    settings.shadowDistance = std::clamp(settings.shadowDistance, 10.0f, 1000.0f);
    settings.lightBudget = std::min(settings.lightBudget, kMaxClusteredLights);
    return settings;
}

std::optional<QualityLevel> parseQuality(std::string_view name) noexcept
{
    if (name == "low")
        return QualityLevel::Low;
    if (name == "medium")
        return QualityLevel::Medium;
    if (name == "high")
        return QualityLevel::High;
    if (name == "ultra")
        return QualityLevel::Ultra;
    return std::nullopt;
}

std::string_view qualityName(QualityLevel quality) noexcept
{
    switch (quality) {
    case QualityLevel::Low:
        return "low";
    case QualityLevel::Medium:
        return "medium";
    case QualityLevel::High:
        return "high";
    case QualityLevel::Ultra:
        return "ultra";
    }
    return "high";
}

} // namespace luaug::render
