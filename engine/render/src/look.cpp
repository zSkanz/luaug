// The look of a world, resolved (ADR 0096). See look.h.
#include "luaug/render/look.h"

#include "luaug/scene/components.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <cmath>

namespace luaug::render {
namespace {

using core::f32;
using core::InstanceId;

// Where exposure maps a frame's average, and so the brightness a contrast
// pivots about: pushing pixels away from the average is what contrast means,
// and after exposure the average IS this number (`tonemap.hlsl`'s key).
constexpr f32 kContrastPivot = 0.45f;

// Rec. 709's luminance weights, which saturation mixes towards.
constexpr f32 kLumaR = 0.2126f;
constexpr f32 kLumaG = 0.7152f;
constexpr f32 kLumaB = 0.0722f;

enum class Kind : core::u8
{
    None,
    Bloom,
    ColorCorrection,
    Blur,
    DepthOfField,
    SunRays,
    Atmosphere,
    Sky,
};

[[nodiscard]] Kind kindOf(const scene::World& world, InstanceId id) noexcept
{
    if (world.bloomEffects().find(id) != nullptr)
        return Kind::Bloom;
    if (world.colorCorrectionEffects().find(id) != nullptr)
        return Kind::ColorCorrection;
    if (world.blurEffects().find(id) != nullptr)
        return Kind::Blur;
    if (world.depthOfFieldEffects().find(id) != nullptr)
        return Kind::DepthOfField;
    if (world.sunRaysEffects().find(id) != nullptr)
        return Kind::SunRays;
    if (world.atmospheres().find(id) != nullptr)
        return Kind::Atmosphere;
    if (world.skies().find(id) != nullptr)
        return Kind::Sky;
    return Kind::None;
}

[[nodiscard]] bool enabled(const scene::World& world, InstanceId id) noexcept
{
    const scene::PostEffectComponent* effect = world.postEffects().find(id);
    return effect != nullptr && effect->enabled;
}

// An affine map of linear colour, as three rows of four.
struct Affine
{
    f32 m[3][4]{
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
    };
};

// `after` applied to the result of `before`.
[[nodiscard]] Affine compose(const Affine& after, const Affine& before) noexcept
{
    Affine out;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 4; ++column) {
            f32 sum = column == 3 ? after.m[row][3] : 0.0f;
            for (int k = 0; k < 3; ++k)
                sum += after.m[row][k] * before.m[k][column];
            out.m[row][column] = sum;
        }
    }
    return out;
}

// One `ColorCorrectionEffect`: its tint, then its saturation, then its
// contrast, then its brightness -- the order the class's documentation states.
[[nodiscard]] Affine gradeOf(const scene::ColorCorrectionEffectComponent& effect) noexcept
{
    Affine tint;
    tint.m[0][0] = effect.tintColor.r;
    tint.m[1][1] = effect.tintColor.g;
    tint.m[2][2] = effect.tintColor.b;

    // Towards or away from each pixel's own luminance: `s * c + (1 - s) * luma`.
    const f32 s = 1.0f + effect.saturation;
    Affine saturation;
    const f32 luma[3]{kLumaR, kLumaG, kLumaB};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column)
            saturation.m[row][column] = (row == column ? s : 0.0f) + (1.0f - s) * luma[column];
    }

    // Away from the pivot: `k * c + (1 - k) * pivot`.
    const f32 k = 1.0f + effect.contrast;
    Affine contrast;
    for (int row = 0; row < 3; ++row) {
        contrast.m[row][row] = k;
        contrast.m[row][3] = (1.0f - k) * kContrastPivot;
    }

    Affine brightness;
    for (int row = 0; row < 3; ++row)
        brightness.m[row][3] = effect.brightness;

    return compose(brightness, compose(contrast, compose(saturation, tint)));
}

// The state `resolveLook` and `lookStanding` walk the same way.
struct Walk
{
    bool bloomSeen = false;
    bool bloomChosen = false;
    bool depthOfFieldChosen = false;
    bool sunRaysChosen = false;
    bool atmosphereChosen = false;
    bool skyChosen = false;
};

// Decides one instance's standing, advancing `walk`. `underLighting` says which
// of the two parents it was found under.
[[nodiscard]] LookStanding judge(const scene::World& world, InstanceId id, Kind kind, bool underLighting,
                                 Walk& walk) noexcept
{
    switch (kind) {
    case Kind::None:
        return LookStanding::NotALook;
    case Kind::Atmosphere:
    case Kind::Sky: {
        // The world's air and the world's sky: never a viewer's.
        if (!underLighting)
            return LookStanding::WrongParent;
        bool& chosen = kind == Kind::Sky ? walk.skyChosen : walk.atmosphereChosen;
        if (chosen)
            return LookStanding::NotFirst;
        chosen = true;
        return LookStanding::Counts;
    }
    case Kind::Bloom:
        walk.bloomSeen = true;
        if (!enabled(world, id))
            return LookStanding::Disabled;
        if (walk.bloomChosen)
            return LookStanding::Outranked;
        walk.bloomChosen = true;
        return LookStanding::Counts;
    case Kind::DepthOfField:
    case Kind::SunRays: {
        if (!enabled(world, id))
            return LookStanding::Disabled;
        bool& chosen = kind == Kind::DepthOfField ? walk.depthOfFieldChosen : walk.sunRaysChosen;
        if (chosen)
            return LookStanding::Outranked;
        chosen = true;
        return LookStanding::Counts;
    }
    case Kind::ColorCorrection:
    case Kind::Blur:
        // Every enabled one applies.
        return enabled(world, id) ? LookStanding::Counts : LookStanding::Disabled;
    }
    return LookStanding::NotALook;
}

// `Lighting`'s children, then the camera's, each in document order -- calling
// `visit(id, kind, underLighting)` for every one of these classes. Destroyed
// instances waiting to be retired are not part of the world.
template <typename Visit>
void walkLook(const scene::World& world, InstanceId lightingHost, InstanceId camera, Visit&& visit)
{
    const auto children = [&](InstanceId parent, bool underLighting) {
        if (!world.alive(parent) || world.destroyed(parent))
            return;
        for (InstanceId child = world.firstChild(parent); child.valid(); child = world.nextSibling(child)) {
            if (world.destroyed(child))
                continue;
            const Kind kind = kindOf(world, child);
            if (kind != Kind::None)
                visit(child, kind, underLighting);
        }
    };
    children(lightingHost, true);
    if (camera != lightingHost)
        children(camera, false);
}

} // namespace

void resolveLook(const scene::World& world, InstanceId lightingHost, InstanceId camera, RenderLook& out)
{
    out = RenderLook{};
    Walk walk;
    Affine grade;
    f32 blurSquared = 0.0f;
    walkLook(world, lightingHost, camera, [&](InstanceId id, Kind kind, bool underLighting) {
        if (judge(world, id, kind, underLighting, walk) != LookStanding::Counts)
            return;
        switch (kind) {
        case Kind::Bloom:
            if (const auto* bloom = world.bloomEffects().find(id)) {
                out.bloomIntensity = bloom->intensity;
                out.bloomSize = bloom->size;
                out.bloomThreshold = bloom->threshold;
            }
            break;
        case Kind::ColorCorrection:
            if (const auto* correction = world.colorCorrectionEffects().find(id)) {
                grade = compose(gradeOf(*correction), grade);
                out.graded = true;
            }
            break;
        case Kind::Blur:
            if (const auto* blur = world.blurEffects().find(id))
                blurSquared += blur->size * blur->size;
            break;
        case Kind::DepthOfField:
            if (const auto* focus = world.depthOfFieldEffects().find(id)) {
                out.depthOfField = true;
                out.focusDistance = focus->focusDistance;
                out.inFocusRadius = focus->inFocusRadius;
                out.nearIntensity = focus->nearIntensity;
                out.farIntensity = focus->farIntensity;
            }
            break;
        case Kind::SunRays:
            if (const auto* rays = world.sunRaysEffects().find(id)) {
                out.sunRays = true;
                out.sunRaysIntensity = rays->intensity;
                out.sunRaysSpread = rays->spread;
            }
            break;
        case Kind::Atmosphere:
            if (const auto* air = world.atmospheres().find(id)) {
                out.atmosphere.present = true;
                out.atmosphere.density = air->density;
                out.atmosphere.offset = air->offset;
                out.atmosphere.color = air->color;
                out.atmosphere.decay = air->decay;
                out.atmosphere.glare = air->glare;
                out.atmosphere.haze = air->haze;
            }
            break;
        case Kind::Sky:
            if (const auto* sky = world.skies().find(id)) {
                out.sky.present = true;
                out.sky.faces[static_cast<int>(SkyFace::Back)] = sky->skyboxBack;
                out.sky.faces[static_cast<int>(SkyFace::Down)] = sky->skyboxDown;
                out.sky.faces[static_cast<int>(SkyFace::Front)] = sky->skyboxFront;
                out.sky.faces[static_cast<int>(SkyFace::Left)] = sky->skyboxLeft;
                out.sky.faces[static_cast<int>(SkyFace::Right)] = sky->skyboxRight;
                out.sky.faces[static_cast<int>(SkyFace::Up)] = sky->skyboxUp;
                out.sky.orientation = sky->skyboxOrientation;
                out.sky.sunTexture = sky->sunTexture;
                out.sky.moonTexture = sky->moonTexture;
                out.sky.sunAngularSize = sky->sunAngularSize;
                out.sky.moonAngularSize = sky->moonAngularSize;
                out.sky.starCount = static_cast<core::u32>(sky->starCount);
                out.sky.celestialBodiesShown = sky->celestialBodiesShown;
                out.sky.cloudCover = sky->cloudCover;
                out.sky.cloudDensity = sky->cloudDensity;
                out.sky.cloudColor = sky->cloudColor;
            }
            break;
        case Kind::None:
            break;
        }
    });

    // A bloom instance anywhere it counts governs bloom, enabled or not.
    out.bloomGoverned = walk.bloomSeen;
    out.bloomEnabled = !walk.bloomSeen || walk.bloomChosen;
    if (out.graded) {
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 4; ++column)
                out.grade[row][column] = grade.m[row][column];
        }
    }
    out.blurSize = std::sqrt(blurSquared);
}

AirMedium airMediumOf(const RenderAtmosphere& atmosphere, core::f64 cameraHeight) noexcept
{
    AirMedium air;
    air.extinction = atmosphere.density * atmosphere.density * 0.02f;
    air.falloff = 0.69314718f * atmosphere.decay / 10.0f;
    air.height = static_cast<f32>(cameraHeight - static_cast<core::f64>(atmosphere.offset));
    air.haze = atmosphere.haze;
    return air;
}

f32 airOpticalDepth(const AirMedium& air, f32 rise, f32 reach) noexcept
{
    const auto clamped = [](f32 value) { return std::clamp(value, -60.0f, 60.0f); };
    const f32 atCamera = air.extinction * std::exp(clamped(-air.falloff * air.height));
    const f32 k = clamped(air.falloff * rise * reach);
    const f32 along = std::abs(k) > 1e-4f ? (1.0f - std::exp(-k)) / k : 1.0f - 0.5f * k;
    const f32 horizon = 1.0f - std::abs(rise);
    const f32 squared = horizon * horizon;
    const f32 haze = air.haze * air.extinction * squared * squared * squared * squared;
    return atCamera * reach * along + haze * reach;
}

LookStanding lookStanding(const scene::World& world, InstanceId id, InstanceId lightingHost, InstanceId camera)
{
    const Kind kind = kindOf(world, id);
    if (kind == Kind::None)
        return LookStanding::NotALook;
    const InstanceId parent = world.parentOf(id);
    if (!parent.valid() || (parent != lightingHost && parent != camera))
        return LookStanding::WrongParent;

    // Replayed from the start rather than judged alone, because "the first
    // enabled one" is a fact about everything before it.
    Walk walk;
    LookStanding standing = LookStanding::NotALook;
    walkLook(world, lightingHost, camera, [&](InstanceId visited, Kind visitedKind, bool underLighting) {
        const LookStanding judged = judge(world, visited, visitedKind, underLighting, walk);
        if (visited == id)
            standing = judged;
    });
    return standing;
}

} // namespace luaug::render
