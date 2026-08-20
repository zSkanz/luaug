#include "luaug/render/debug_draw.h"

#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace luaug::render {
namespace {

// Corners are indexed by sign bits: bit 0 is X, bit 1 is Y, bit 2 is Z, and a
// set bit means +halfExtent while a clear bit means -halfExtent. So corner 0 is
// the all-negative one and corner 7 the all-positive one.
constexpr usize kBoxCornerCount = 8;
constexpr usize kBoxEdgeCount = 12;

// The twelve edges are exactly the corner pairs whose indices differ in a
// single bit -- four running along each axis. Written out rather than derived
// at runtime so the shape is reviewable: an edge set that is wrong by one pair
// draws a box that looks almost right.
constexpr usize kBoxEdges[kBoxEdgeCount][2] = {
    {0, 1}, {2, 3}, {4, 5}, {6, 7}, // along X (differ in bit 0)
    {0, 2}, {1, 3}, {4, 6}, {5, 7}, // along Y (differ in bit 1)
    {0, 4}, {1, 5}, {2, 6}, {3, 7}, // along Z (differ in bit 2)
};

constexpr DebugColor kAxisColorX = DebugColor::fromLinear(1.0f, 0.0f, 0.0f);
constexpr DebugColor kAxisColorY = DebugColor::fromLinear(0.0f, 1.0f, 0.0f);
constexpr DebugColor kAxisColorZ = DebugColor::fromLinear(0.0f, 0.0f, 1.0f);

constexpr f32 kTau = 2.0f * std::numbers::pi_v<f32>;

// `std::vector::reserve` grows to exactly what is asked for, so reserving
// `size() + n` before every shape would reallocate once per shape and make a
// frame's submission quadratic. Doubling keeps the geometric growth the vector
// would have had if it were left to push_back alone. After the first frame this
// is a no-op, because clear() keeps the capacity.
void reserveAdditional(std::vector<DebugVertex>& vertices, usize additional)
{
    const usize required = vertices.size() + additional;
    const usize capacity = vertices.capacity();
    if (required <= capacity)
        return;
    vertices.reserve(required > capacity * 2 ? required : capacity * 2);
}

[[nodiscard]] constexpr Vec3 cornerOffset(usize index, Vec3 halfExtents) noexcept
{
    return {
        (index & 1u) != 0u ? halfExtents.x : -halfExtents.x,
        (index & 2u) != 0u ? halfExtents.y : -halfExtents.y,
        (index & 4u) != 0u ? halfExtents.z : -halfExtents.z,
    };
}

void appendBoxEdges(std::vector<DebugVertex>& vertices, const std::array<Vec3, kBoxCornerCount>& corners,
                    DebugColor color)
{
    reserveAdditional(vertices, 2 * kBoxEdgeCount);
    for (const auto& edge : kBoxEdges) {
        vertices.push_back({corners[edge[0]], color});
        vertices.push_back({corners[edge[1]], color});
    }
}

} // namespace

void DebugDraw::clear() noexcept
{
    // Capacity is kept deliberately: this runs at the top of every frame, and a
    // shrink here would trade one allocation per frame for nothing.
    vertices_.clear();
}

void DebugDraw::rebaseTo(core::DVec3 origin) noexcept
{
    if (origin.x == 0.0 && origin.y == 0.0 && origin.z == 0.0)
        return;

    const Vec3 offset{
        static_cast<f32>(origin.x),
        static_cast<f32>(origin.y),
        static_cast<f32>(origin.z),
    };
    for (DebugVertex& vertex : vertices_)
        vertex.position = vertex.position - offset;
}

void DebugDraw::line(Vec3 from, Vec3 to, DebugColor color)
{
    vertices_.push_back({from, color});
    vertices_.push_back({to, color});
}

void DebugDraw::wireBox(Vec3 center, Vec3 halfExtents, DebugColor color)
{
    std::array<Vec3, kBoxCornerCount> corners{};
    for (usize index = 0; index < kBoxCornerCount; ++index)
        corners[index] = center + cornerOffset(index, halfExtents);

    appendBoxEdges(vertices_, corners, color);
}

void DebugDraw::wireBox(const Mat4& transform, Vec3 halfExtents, DebugColor color)
{
    // The extents are in the transform's own space, so an oriented box is the
    // axis-aligned one with its corners pushed through the matrix -- same eight
    // corners, same twelve edges, no second topology to keep in step.
    std::array<Vec3, kBoxCornerCount> corners{};
    for (usize index = 0; index < kBoxCornerCount; ++index)
        corners[index] = core::transformPoint(transform, cornerOffset(index, halfExtents));

    appendBoxEdges(vertices_, corners, color);
}

void DebugDraw::wireSphere(Vec3 center, f32 radius, DebugColor color, u32 segments)
{
    // Zero segments would divide by zero and fill the buffer with NaN, which
    // reaches the GPU as a garbled frame rather than as an error.
    if (segments == 0)
        return;

    reserveAdditional(vertices_, 6 * static_cast<usize>(segments));

    const f32 step = kTau / static_cast<f32>(segments);

    const auto ring = [&](Vec3 axisU, Vec3 axisV) {
        const auto pointAt = [&](u32 index) -> Vec3 {
            const f32 angle = step * static_cast<f32>(index);
            return center + axisU * (std::cos(angle) * radius) + axisV * (std::sin(angle) * radius);
        };

        // The closing edge reuses the stored first point rather than evaluating
        // the angle at tau: cos(tau) is not bit-identical to cos(0) in f32, and
        // the difference shows up as a hairline gap in the ring.
        const Vec3 first = pointAt(0);
        Vec3 previous = first;
        for (u32 index = 1; index <= segments; ++index) {
            const Vec3 current = index == segments ? first : pointAt(index);
            vertices_.push_back({previous, color});
            vertices_.push_back({current, color});
            previous = current;
        }
    };

    ring({1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    ring({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f});
    ring({0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f});
}

void DebugDraw::axes(const Mat4& transform, f32 length)
{
    reserveAdditional(vertices_, 6);

    const Vec3 origin = core::transformPoint(transform, Vec3{});

    // The directions come out of the transform rather than being world axes,
    // because a triad that always points the same way diagnoses nothing. They
    // are normalized so that `length` means a length: a scaled transform would
    // otherwise draw a triad whose size says more about the scale than about
    // the orientation it exists to show. A degenerate axis normalizes to zero
    // and simply does not appear, which is itself the diagnosis.
    const auto axis = [&](Vec3 direction, DebugColor color) {
        const Vec3 tip = origin + core::normalize(core::transformDirection(transform, direction)) * length;
        vertices_.push_back({origin, color});
        vertices_.push_back({tip, color});
    };

    axis({1.0f, 0.0f, 0.0f}, kAxisColorX);
    axis({0.0f, 1.0f, 0.0f}, kAxisColorY);
    axis({0.0f, 0.0f, 1.0f}, kAxisColorZ);
}

} // namespace luaug::render
