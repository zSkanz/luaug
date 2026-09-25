// Where the sun is (roadmap M4; api-design.md §2.1, `Lighting`).
//
// A pure function of `ClockTime` and `GeographicLatitude` and nothing else. No
// wall clock, no accumulated state, no drift: the same clock time always gives
// the same direction, in a replay exactly as in the run it replays. That is R10
// applied to the one piece of the renderer a script can steer, and it is why
// `SunDirection` is a derived read-only property rather than stored state that
// something advances.
#pragma once

#include "luaug/core/id.h"
#include "luaug/core/math.h"

#include <optional>

namespace luaug::scene {
class World;
}

namespace luaug::render {

// **Where a light shines from** (api-design.md §2.2, ADR 0095): the `BasePart`
// it hangs off, then -- under an `Attachment` -- the attachment's offset from
// that part, then the light's own `CFrame`. A light with no part above it
// stands on its own: `part` is invalid, `partFrame` the identity, and the
// offset is its own `CFrame` in the world. The renderer interpolates the part
// and applies the offset after; the editor's gizmo reads the same answer, so
// its cone is where the light is.
struct LightAnchor
{
    core::InstanceId part;
    core::CFrameD partFrame;
    core::CFrameD offset;
};
[[nodiscard]] std::optional<LightAnchor> lightAnchorOf(const scene::World& world, core::InstanceId light) noexcept;

// The unit vector pointing **from the world towards the sun** -- so at noon on
// the equator it is straight up, and shading dots it against a surface normal
// without negating first.
//
// Axes are the engine's: +X east, +Y up, and north is **-Z**, which follows
// from the camera looking down -Z (core/math.h). At noon in the northern
// hemisphere the sun is therefore towards +Z, which is south, as it should be.
//
// `clockTime` is hours in [0, 24) and wraps if it is not; `latitudeDegrees` is
// [-90, 90]. Solar declination is zero -- v1 has a day cycle and no seasons --
// so the sun rises due east and sets due west whatever the latitude. That is a
// modelling choice rather than an approximation error, and it is the one thing
// here that a later milestone might want to widen; adding a day-of-year
// parameter is the shape it would take.
[[nodiscard]] core::Vec3 sunDirection(core::f32 clockTime, core::f32 latitudeDegrees) noexcept;

} // namespace luaug::render
