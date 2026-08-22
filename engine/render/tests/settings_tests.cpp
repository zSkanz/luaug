#include "luaug/render/clusters.h"
#include "luaug/render/settings.h"
#include "luaug/render/shadow.h"

#include <doctest/doctest.h>

using namespace luaug::render;

TEST_CASE("High is exactly what the engine shipped before there were presets")
{
    // **The load-bearing case in this file.** Every golden in the repository was
    // recorded against the constants M7.5 shipped, so a `High` preset that
    // differed from them by one field would have re-recorded all of them at
    // once -- and a re-recorded golden hides whatever else moved with it (M7.5
    // Finding 4).
    const GraphicsSettings high = settingsFor(QualityLevel::High);

    CHECK(high.renderScale == doctest::Approx(1.0f));
    CHECK(high.shadowTileResolution == kShadowTileResolution);
    CHECK(high.shadowCascades == kShadowCascadeCount);
    CHECK(high.shadowDistance == doctest::Approx(kShadowDistance));
    CHECK(high.lightBudget == kMaxClusteredLights);
    CHECK(high.bloom);
    CHECK(high.ambientOcclusion);
    CHECK(high.antiAliasing);
    CHECK(high.autoExposure);

    // And it is the default, so a caller that never sets anything gets it.
    CHECK(GraphicsSettings{}.shadowTileResolution == high.shadowTileResolution);
    CHECK(GraphicsSettings{}.quality == QualityLevel::High);
}

TEST_CASE("the presets are ordered on every axis they move")
{
    const GraphicsSettings low = settingsFor(QualityLevel::Low);
    const GraphicsSettings medium = settingsFor(QualityLevel::Medium);
    const GraphicsSettings high = settingsFor(QualityLevel::High);
    const GraphicsSettings ultra = settingsFor(QualityLevel::Ultra);

    // A preset family whose members are not ordered is a menu rather than a
    // scale, and a slider over it would move quality sideways.
    CHECK(low.renderScale <= medium.renderScale);
    CHECK(low.shadowTileResolution <= medium.shadowTileResolution);
    CHECK(medium.shadowTileResolution <= high.shadowTileResolution);
    CHECK(high.shadowTileResolution <= ultra.shadowTileResolution);
    CHECK(low.shadowCascades <= medium.shadowCascades);
    CHECK(medium.shadowCascades <= high.shadowCascades);
    CHECK(low.shadowDistance <= medium.shadowDistance);
    CHECK(high.shadowDistance <= ultra.shadowDistance);
    CHECK(low.lightBudget <= medium.lightBudget);
    CHECK(medium.lightBudget <= high.lightBudget);

    // Every preset carries its own name, so the overlay and the log can report
    // where a configuration started rather than reciting its fields.
    CHECK(low.quality == QualityLevel::Low);
    CHECK(ultra.quality == QualityLevel::Ultra);
}

TEST_CASE("a hand-edited project file cannot ask for something the renderer cannot do")
{
    GraphicsSettings absurd;
    absurd.renderScale = 0.0f;
    absurd.shadowTileResolution = 32;
    absurd.shadowCascades = 99;
    absurd.shadowDistance = 0.0f;
    absurd.lightBudget = 1000000;

    const GraphicsSettings clamped = clampSettings(absurd);
    CHECK(clamped.renderScale >= 0.5f);
    CHECK(clamped.shadowTileResolution >= 256u);
    CHECK(clamped.shadowCascades <= kShadowCascadeCount);
    CHECK(clamped.shadowDistance >= 10.0f);
    CHECK(clamped.lightBudget <= kMaxClusteredLights);

    GraphicsSettings tooLarge;
    tooLarge.renderScale = 4.0f;
    tooLarge.shadowTileResolution = 8192;
    // 2048 rather than 4096, because the atlas is two tiles across and 4096 is
    // the 2D size the weakest conforming device is required to support.
    CHECK(clampSettings(tooLarge).renderScale == doctest::Approx(1.0f));
    CHECK(clampSettings(tooLarge).shadowTileResolution == 2048u);

    // Rounded DOWN to a power of two rather than refused: a project file asking
    // for 1500 texels means "about this much", and a build that refused to start
    // over it would be pedantry.
    GraphicsSettings odd;
    odd.shadowTileResolution = 1500;
    CHECK(clampSettings(odd).shadowTileResolution == 1024u);

    // Zero cascades survives clamping, because it is a real setting: the sun
    // casts no shadow, the pass submits nothing, and extraction stops keeping
    // off-screen casters.
    GraphicsSettings noShadows;
    noShadows.shadowCascades = 0;
    CHECK(clampSettings(noShadows).shadowCascades == 0u);
}

TEST_CASE("quality names round-trip, and an unknown one is not a silent default")
{
    for (const QualityLevel level :
         {QualityLevel::Low, QualityLevel::Medium, QualityLevel::High, QualityLevel::Ultra}) {
        const std::optional<QualityLevel> parsedBack = parseQuality(qualityName(level));
        REQUIRE(parsedBack.has_value());
        CHECK(*parsedBack == level);
    }

    // Nothing rather than `High`: a typo on the command line is a usage error
    // the caller reports, not a preset silently chosen for somebody.
    CHECK_FALSE(parseQuality("ultra-high").has_value());
    CHECK_FALSE(parseQuality("HIGH").has_value());
    CHECK_FALSE(parseQuality("").has_value());
}
