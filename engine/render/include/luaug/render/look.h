// The look of a world, resolved (ADR 0096).
//
// Atmosphere, a sky and the post effects are INSTANCES, and where one sits says
// whose it is: directly under `Lighting` it is the world's, directly under the
// current camera it is its viewer's, and anywhere else it does nothing. Several
// of one kind combine by rules the ADR writes down. Every one of those rules is
// applied here and nowhere else, into one POD block the renderer reads -- and the
// editor asks the same function why an instance does not count, so the marker
// it shows and the picture the renderer draws cannot disagree.
//
// **A world with none of these instances resolves to `RenderLook{}`**, and
// `RenderLook{}` is the picture the engine drew before any of them existed: the
// engine's own bloom, the linear fog, the analytic sky and nothing else. That
// is asserted rather than hoped (`look_tests.cpp`), because every golden in the
// repository rests on it.
#pragma once

#include "luaug/core/id.h"
#include "luaug/core/math.h"
#include "luaug/core/name_atom.h"
#include "luaug/core/types.h"

namespace luaug::scene {
class World;
}

namespace luaug::render {

// `Atmosphere`, as drawn. Meaningful only when `present`.
struct RenderAtmosphere
{
    bool present = false;
    core::f32 density = 0.35f;
    // Metres.
    core::f32 offset = 0.0f;
    core::Color3 color{196.0f / 255.0f, 210.0f / 255.0f, 230.0f / 255.0f};
    core::f32 decay = 0.1f;
    core::f32 glare = 0.0f;
    core::f32 haze = 0.0f;

    [[nodiscard]] bool operator==(const RenderAtmosphere&) const noexcept = default;
};

// The six faces of a skybox, in the order the properties are declared.
enum class SkyFace : core::u8
{
    Back,
    Down,
    Front,
    Left,
    Right,
    Up,
};
inline constexpr core::u32 kSkyFaceCount = 6;

// `Sky`, as drawn. Meaningful only when `present`; a sky whose six faces are all
// empty still governs the sun, the moon, the stars and the clouds over the
// engine's own gradient.
struct RenderSky
{
    bool present = false;
    core::NameAtom faces[kSkyFaceCount]{};
    // Degrees about X, Y and Z.
    core::Vec3 orientation{0.0f, 0.0f, 0.0f};
    core::NameAtom sunTexture;
    core::NameAtom moonTexture;
    // Degrees across.
    core::f32 sunAngularSize = 2.3f;
    core::f32 moonAngularSize = 2.0f;
    core::u32 starCount = 3000;
    bool celestialBodiesShown = true;
    core::f32 cloudCover = 0.0f;
    core::f32 cloudDensity = 0.5f;
    core::Color3 cloudColor{1.0f, 1.0f, 1.0f};

    // Whether any face names an image. None is the engine's own sky.
    [[nodiscard]] bool hasImages() const noexcept
    {
        for (const core::NameAtom face : faces) {
            if (face.valid())
                return true;
        }
        return false;
    }
    [[nodiscard]] bool operator==(const RenderSky&) const noexcept = default;
};

struct RenderLook
{
    // --- Bloom ---------------------------------------------------------------
    //
    // **Governed only when a `BloomEffect` counts.** With none, the engine's own
    // bloom applies exactly as it always has -- every scene and golden in the
    // repository has bloom and none has the instance. With one that counts, the
    // first ENABLED one supplies the numbers; with only disabled ones, bloom is
    // off, which is what a 2D game whose white sprites should not glow wants.
    bool bloomGoverned = false;
    bool bloomEnabled = true;
    // A multiple of the engine's own strength.
    core::f32 bloomIntensity = 1.0f;
    // 24 is the engine's own reach.
    core::f32 bloomSize = 24.0f;
    core::f32 bloomThreshold = 1.1f;

    // --- Colour correction ----------------------------------------------------
    //
    // Every enabled `ColorCorrectionEffect`, composed in order into ONE affine
    // map of linear colour: `out = grade * (r, g, b, 1)`. Each effect's four
    // operations are affine, and so is their composition, so any number of them
    // costs the tonemap one 3x4 multiply. `graded` false is the identity.
    bool graded = false;
    core::f32 grade[3][4]{
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
    };

    // --- Blur -------------------------------------------------------------------
    //
    // Pixels of a 1080-line picture, every enabled `BlurEffect` combined by
    // their squares. Zero is no blur.
    core::f32 blurSize = 0.0f;

    // --- Depth of field ----------------------------------------------------------
    bool depthOfField = false;
    core::f32 focusDistance = 25.0f;
    core::f32 inFocusRadius = 10.0f;
    core::f32 nearIntensity = 0.5f;
    core::f32 farIntensity = 0.5f;

    // --- Sun rays -------------------------------------------------------------------
    bool sunRays = false;
    core::f32 sunRaysIntensity = 0.25f;
    core::f32 sunRaysSpread = 0.5f;

    RenderAtmosphere atmosphere;
    RenderSky sky;

    [[nodiscard]] bool operator==(const RenderLook&) const noexcept = default;
};

// **The air as numbers** (`Atmosphere`, ADR 0096): what `look_air.hlsl`
// integrates along every ray, derived from the instance's properties once and
// here, so the pass, the linear fog the blended surfaces approximate it with,
// and the tests that pin the documented distances all read one answer.
struct AirMedium
{
    // How much of the light it crosses the air hides per metre, at the height
    // of `Offset`: `Density` squared, times a fiftieth -- so the default hides
    // half of what stands about 280 metres away, and a density of 1 half of
    // what stands 35 metres away.
    core::f32 extinction = 0.0f;
    // How fast that falls per metre of height: `Decay` of 1 halves it every ten
    // metres, 0.1 every hundred, 0 never.
    core::f32 falloff = 0.0f;
    // The camera's height above `Offset`, in metres.
    core::f32 height = 0.0f;
    // How much thicker the air grows towards the horizon, as a multiple of
    // `extinction`.
    core::f32 haze = 0.0f;
};
[[nodiscard]] AirMedium airMediumOf(const RenderAtmosphere& atmosphere, core::f64 cameraHeight) noexcept;

// How much air a ray crosses from the camera: `reach` metres along a direction
// whose vertical component is `rise`. The CPU half of `airOpticalDepth` in
// `luaug_look.hlsli`; what survives is `exp(-depth)`.
[[nodiscard]] core::f32 airOpticalDepth(const AirMedium& air, core::f32 rise, core::f32 reach) noexcept;

// Why an instance of one of these classes changes nothing, or `Counts` when it
// does. The editor turns each into a sentence (R3); the renderer never needs to.
enum class LookStanding : core::u8
{
    // It takes part in the picture.
    Counts,
    // Not one of the classes this file is about.
    NotALook,
    // An effect whose `Enabled` is off. Not a mistake -- for a bloom it is how
    // bloom is turned off -- but the editor still says so.
    Disabled,
    // Not directly under `Lighting` or the current camera; or, for an
    // `Atmosphere` or a `Sky`, not directly under `Lighting`.
    WrongParent,
    // A second `Atmosphere` or `Sky` under `Lighting`: the first one is used.
    NotFirst,
    // A bloom, depth of field or sun rays that an earlier enabled one of its
    // kind outranks.
    Outranked,
};

// Resolves every instance under `lightingHost` and under `camera` into `out`,
// in document order -- `Lighting`'s children first, then the camera's. Either id
// may be invalid, which contributes nothing.
void resolveLook(const scene::World& world, core::InstanceId lightingHost, core::InstanceId camera, RenderLook& out);

// Whether `id` takes part, by exactly the rules `resolveLook` applies.
[[nodiscard]] LookStanding lookStanding(const scene::World& world, core::InstanceId id, core::InstanceId lightingHost,
                                        core::InstanceId camera);

} // namespace luaug::render
