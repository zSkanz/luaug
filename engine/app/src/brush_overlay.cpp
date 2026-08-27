#include "luaug/app/brush_overlay.h"

#include "luaug/render/debug_draw.h"

#include <algorithm>
#include <cmath>

namespace luaug::app {
namespace {

using core::DVec3;
using core::f32;
using core::usize;
using core::Vec3;

// How many segments the ring is drawn with. Thirty-two is smooth enough that a
// circle reads as a circle at any radius somebody sculpts with, and cheap enough
// that it is not worth scaling with screen size -- which would make the overlay
// a function of the camera and therefore something to reproduce rather than
// read.
constexpr int RingSegments = 32;

// Two unit vectors perpendicular to `normal` and to each other.
//
// **Picked from whichever axis the normal is LEAST aligned with**, which is the
// standard way and the one detail worth writing down: crossing with a fixed axis
// produces a zero-length vector exactly when the normal happens to be that axis,
// and a brush aimed straight down is the commonest case there is.
void basisFor(Vec3 normal, Vec3& outU, Vec3& outV) noexcept
{
    const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    const Vec3 unit =
        length < 1e-6f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{normal.x / length, normal.y / length, normal.z / length};

    const float ax = std::abs(unit.x);
    const float ay = std::abs(unit.y);
    const float az = std::abs(unit.z);
    const Vec3 away = (ax <= ay && ax <= az) ? Vec3{1.0f, 0.0f, 0.0f}
                      : (ay <= az)           ? Vec3{0.0f, 1.0f, 0.0f}
                                             : Vec3{0.0f, 0.0f, 1.0f};

    Vec3 u{unit.y * away.z - unit.z * away.y, unit.z * away.x - unit.x * away.z, unit.x * away.y - unit.y * away.x};
    const float ulen = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
    u = ulen < 1e-6f ? Vec3{1.0f, 0.0f, 0.0f} : Vec3{u.x / ulen, u.y / ulen, u.z / ulen};

    outU = u;
    outV = Vec3{unit.y * u.z - unit.z * u.y, unit.z * u.x - unit.x * u.z, unit.x * u.y - unit.y * u.x};
}

} // namespace

std::vector<DVec3> strokeStamps(DVec3 from, DVec3 to, double radius, double spacing)
{
    std::vector<DVec3> stamps;

    const double dx = to.x - from.x;
    const double dy = to.y - from.y;
    const double dz = to.z - from.z;
    const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);

    // The step, in metres. A spacing that cannot advance would be an unbounded
    // loop, so it falls back to one stamp where the pointer is.
    const double step = radius * spacing;
    if (!(step > 0.0) || !(distance > 0.0)) {
        stamps.push_back(to);
        return stamps;
    }

    // **Counted before it is walked**, so a pointer that jumped a kilometre
    // costs a comparison rather than a hundred thousand edits. Past the ceiling
    // the stroke is its endpoints and nothing between: a visible gap, which is
    // recoverable, rather than a frame that never ends.
    const auto count = static_cast<usize>(distance / step);
    if (count + 1 > MaxStrokeStamps) {
        stamps.push_back(from);
        stamps.push_back(to);
        return stamps;
    }

    // **From the far end backwards, so `to` is always stamped.** Walking forward
    // from `from` leaves the last stamp short of the pointer by up to one step,
    // and the place a person is looking at is the place the brush has to have
    // acted on.
    for (usize at = 0; at <= count; ++at) {
        const double along = distance - static_cast<double>(at) * step;
        const double t = along / distance;
        stamps.push_back(DVec3{from.x + dx * t, from.y + dy * t, from.z + dz * t});
    }
    std::reverse(stamps.begin(), stamps.end());
    return stamps;
}

void drawBrushRing(Vec3 centre, Vec3 normal, float radius, render::DebugDraw& debug)
{
    if (!(radius > 0.0f)) {
        return;
    }

    Vec3 u;
    Vec3 v;
    basisFor(normal, u, v);

    // A colour that is not any of the gizmo's three axes and not the selection
    // outline: a brush is a fourth thing and reading it as an axis handle would
    // invite dragging it.
    const render::DebugColor ring = render::DebugColor::fromLinear(0.95f, 0.75f, 0.25f, 1.0f);

    Vec3 previous{};
    for (int segment = 0; segment <= RingSegments; ++segment) {
        const auto angle = static_cast<float>(segment) * (6.283185307179586f / static_cast<float>(RingSegments));
        const float c = std::cos(angle) * radius;
        const float s = std::sin(angle) * radius;
        const Vec3 point{centre.x + u.x * c + v.x * s, centre.y + u.y * c + v.y * s, centre.z + u.z * c + v.z * s};
        if (segment > 0) {
            debug.line(previous, point, ring);
        }
        previous = point;
    }

    // The stalk, a quarter of the radius along the normal. Without it a ring on
    // a steep surface reads as a circle floating in the air -- and knowing which
    // way the brush faces is the difference between digging into a cliff and
    // digging past it.
    const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    if (length > 1e-6f) {
        const float stalk = radius * 0.25f;
        const Vec3 tip{centre.x + normal.x / length * stalk, centre.y + normal.y / length * stalk,
                       centre.z + normal.z / length * stalk};
        debug.line(centre, tip, ring);
    }
}

} // namespace luaug::app
