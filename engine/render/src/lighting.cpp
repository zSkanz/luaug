#include "luaug/render/lighting.h"

#include "luaug/scene/components.h"
#include "luaug/scene/world.h"

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

std::optional<LightAnchor> lightAnchorOf(const scene::World& world, core::InstanceId light) noexcept
{
    // The nearest attachment on the way up is the offset; the first part above
    // it is what the offset is from. A `Bone` lights from its REST offset: its
    // animated pose is resolved by a tick, and following it is left for when a
    // light on a moving joint is asked for.
    core::CFrameD own;
    if (const scene::PointLightComponent* point = world.pointLights().find(light); point != nullptr)
        own = point->cframe;
    else if (const scene::SpotLightComponent* spot = world.spotLights().find(light); spot != nullptr)
        own = spot->cframe;
    else
        return std::nullopt;

    core::CFrameD offset;
    bool viaAttachment = false;
    for (core::InstanceId cursor = world.parentOf(light); cursor.valid(); cursor = world.parentOf(cursor)) {
        if (const scene::PartComponent* part = world.parts().find(cursor); part != nullptr)
            return LightAnchor{cursor, part->cframe, offset * own};
        if (const scene::AttachmentComponent* attachment = world.attachments().find(cursor);
            attachment != nullptr && !viaAttachment) {
            viaAttachment = true;
            offset = attachment->cframe;
        }
    }
    // Nothing holds it: it shines from its own place (ADR 0095).
    return LightAnchor{core::InstanceId{}, core::CFrameD{}, own};
}

} // namespace luaug::render
