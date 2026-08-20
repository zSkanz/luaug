#include "luaug/render/lighting.h"

#include <cmath>

namespace luaug::render {
namespace {

using core::f32;

constexpr f32 kPi = 3.14159265358979323846f;
constexpr f32 kDegreesToRadians = kPi / 180.0f;
// One hour of rotation. Noon is hour angle zero, so the sun is highest there
// and the expression below needs no offset table.
constexpr f32 kRadiansPerHour = kPi / 12.0f;

} // namespace

core::Vec3 sunDirection(f32 clockTime, f32 latitudeDegrees) noexcept
{
    f32 hours = std::fmod(clockTime, 24.0f);
    if (hours < 0.0f)
        hours += 24.0f;

    const f32 hourAngle = (hours - 12.0f) * kRadiansPerHour;
    const f32 latitude = latitudeDegrees * kDegreesToRadians;

    const f32 cosHour = std::cos(hourAngle);

    // The standard horizontal-coordinate conversion with declination zero,
    // rewritten into engine axes. East and up fall straight out; north is -Z,
    // so the north component is negated on its way into z.
    //
    // Unit by construction: sin^2(H) + cos^2(H) * (cos^2(lat) + sin^2(lat))
    // is one, so nothing here needs normalizing -- which also means no
    // near-zero length to guard against.
    return core::Vec3{
        -std::sin(hourAngle),
        std::cos(latitude) * cosHour,
        std::sin(latitude) * cosHour,
    };
}

} // namespace luaug::render
