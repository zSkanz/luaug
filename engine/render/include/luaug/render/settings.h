// The graphics settings family (roadmap M8; ADR 0038 §3, ADR 0044).
//
// Every number in here was `constexpr` until M8. They are **engine** settings
// and not `Lighting` properties, and the distinction is the whole design:
// `Lighting` describes the world and travels with the scene, while these
// describe the machine the scene is being shown on. A game that shipped a
// 4096-texel shadow map as scene state would be deciding how a stranger's
// laptop spends its frame.
//
// Three sources, each overriding the one before: a quality preset, the
// project's `[graphics]` table, and the host's own flags. What is deliberately
// NOT a source is a running script -- see ADR 0044 for why, and for what would
// have to be true for that to change.
//
// **Nothing here reaches the simulation.** A world hashes identically at every
// quality level, which is asserted rather than asserted-in-prose: the M8 gate
// runs the determinism replay twice at two settings.
#pragma once

#include "luaug/core/types.h"

#include <optional>
#include <string_view>

namespace luaug::render {

using core::f32;
using core::u32;

enum class QualityLevel : core::u8
{
    Low,
    Medium,
    High,
    Ultra,
};

struct GraphicsSettings
{
    // Which preset these started from. Carried so the overlay and the log can
    // say "High, with two overrides" rather than reciting eleven numbers.
    QualityLevel quality = QualityLevel::High;

    // The fraction of the output resolution the world is rendered at. The post
    // chain and the UI are unaffected: the final resolve upscales the world
    // image into the target, and the 2D pass draws at full resolution on top of
    // it, which is the whole reason a render scale is worth having at all.
    f32 renderScale = 1.0f;

    // One cascade's tile, in texels. The atlas is always two tiles by two, so
    // this squares: 512 costs 4 MiB, 1024 costs 16, 2048 costs 64.
    u32 shadowTileResolution = 1024;

    // How many cascades the sun casts into, 0 through 4. Zero is "no sun
    // shadow", which is a real setting on a weak machine and not a bug: the
    // shadow pass then submits nothing and `shadowRadius` reports zero, so
    // extraction stops keeping off-screen casters as well.
    //
    // Fewer than four does not shrink the atlas -- `shadowTileResolution` is
    // the dial for memory. What it removes is submission: every caster is drawn
    // once per cascade it touches.
    u32 shadowCascades = 4;

    // How far from the camera the sun casts, in metres.
    f32 shadowDistance = 120.0f;

    // How many lights one frame may carry into the cluster assignment. Beyond
    // it, the lights nearest the extraction order's front win -- deterministic,
    // because extraction order is (R10).
    u32 lightBudget = 256;

    bool bloom = true;
    bool ambientOcclusion = true;
    bool antiAliasing = true;

    // False holds the exposure at the calibration key instead of metering the
    // frame. The scene still tonemaps and `ExposureCompensation` still applies;
    // what stops is the three-pass reduction and the frame-to-frame adaptation.
    bool autoExposure = true;
};

// The named set every preset is. `High` is exactly what the engine shipped
// through M7.5, which is a requirement rather than a coincidence: every golden
// in the repository was recorded against it, so a preset that changed the
// default would have re-recorded all of them and hidden whatever else moved.
[[nodiscard]] GraphicsSettings settingsFor(QualityLevel quality) noexcept;

// Clamps every field into the range the renderer can honour, so a hand-edited
// project file cannot ask for a 32-texel shadow map or a render scale of zero.
// Returns the corrected copy rather than mutating, because the caller wants to
// report what it changed.
[[nodiscard]] GraphicsSettings clampSettings(GraphicsSettings settings) noexcept;

[[nodiscard]] std::optional<QualityLevel> parseQuality(std::string_view name) noexcept;
[[nodiscard]] std::string_view qualityName(QualityLevel quality) noexcept;

} // namespace luaug::render
