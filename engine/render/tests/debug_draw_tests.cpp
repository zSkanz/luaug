#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <span>

#include "luaug/render/debug_draw.h"

using namespace luaug::core;
using namespace luaug::render;

namespace
{

constexpr f32 kEpsilon = 1e-5f;
constexpr f32 kPi = 3.14159265358979323846f;

bool near(f32 a, f32 b) noexcept
{
    return std::fabs(a - b) <= kEpsilon;
}

bool near(Vec3 a, Vec3 b) noexcept
{
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}

// Widens core's Mat3 rotation into the Mat4 the debug-draw API takes. This used
// to spell the rotation out here because core had none; core::rotationZ now
// exists, and a second copy of the sign convention is exactly how the two drift
// apart. The name differs deliberately: `using namespace luaug::core` above
// makes an identically named local overload ambiguous rather than shadowing it.
Mat4 rotationZ4(f32 radians) noexcept
{
    const Mat3 rotation = rotationZ(radians);

    Mat4 result;
    for (int column = 0; column < 3; ++column)
        for (int row = 0; row < 3; ++row)
            result.m[column][row] = rotation.m[column][row];
    return result;
}

std::array<Vec3, 8> boxCorners(Vec3 center, Vec3 halfExtents)
{
    std::array<Vec3, 8> corners{};
    for (usize index = 0; index < corners.size(); ++index)
    {
        corners[index] = center
                         + Vec3{
                             (index & 1u) != 0u ? halfExtents.x : -halfExtents.x,
                             (index & 2u) != 0u ? halfExtents.y : -halfExtents.y,
                             (index & 4u) != 0u ? halfExtents.z : -halfExtents.z,
                         };
    }
    return corners;
}

// -1 when the points do not share a coordinate, otherwise the axis (0 = x,
// 1 = y, 2 = z) that every point holds fixed.
int constantAxis(std::span<const DebugVertex> points, Vec3 reference) noexcept
{
    for (int axis = 0; axis < 3; ++axis)
    {
        bool constant = true;
        for (const DebugVertex& vertex : points)
        {
            const f32 value = axis == 0 ? vertex.position.x : (axis == 1 ? vertex.position.y : vertex.position.z);
            const f32 expected = axis == 0 ? reference.x : (axis == 1 ? reference.y : reference.z);
            if (!near(value, expected))
            {
                constant = false;
                break;
            }
        }
        if (constant)
            return axis;
    }
    return -1;
}

} // namespace

TEST_CASE("a line is a vertex pair carrying the submitted colour")
{
    DebugDraw draw;
    CHECK(draw.empty());
    CHECK(draw.lineCount() == 0u);

    const DebugColor color = DebugColor::fromLinear(0.25f, 0.5f, 0.75f);
    draw.line({0.0f, 0.0f, 0.0f}, {1.0f, 2.0f, 3.0f}, color);

    REQUIRE(draw.vertices().size() == 2u);
    CHECK(draw.lineCount() == 1u);
    CHECK_FALSE(draw.empty());
    CHECK(draw.vertices()[0].position == Vec3{0.0f, 0.0f, 0.0f});
    CHECK(draw.vertices()[1].position == Vec3{1.0f, 2.0f, 3.0f});
    CHECK(draw.vertices()[0].color == color);
    CHECK(draw.vertices()[1].color == color);
}

TEST_CASE("a wire box is twelve edges over the eight corners of the box")
{
    constexpr Vec3 center{1.0f, -2.0f, 3.0f};
    constexpr Vec3 halfExtents{0.5f, 1.0f, 2.0f};
    const DebugColor color = DebugColor::fromLinear(1.0f, 0.5f, 0.0f);

    DebugDraw draw;
    draw.wireBox(center, halfExtents, color);

    REQUIRE(draw.lineCount() == 12u);
    REQUIRE(draw.vertices().size() == 24u);

    // Every emitted vertex sits on a corner: each component is the centre plus
    // or minus the matching half extent, nothing in between.
    bool everyComponentIsExtreme = true;
    for (const DebugVertex& vertex : draw.vertices())
    {
        everyComponentIsExtreme = everyComponentIsExtreme
                                  && (near(vertex.position.x, center.x - halfExtents.x)
                                      || near(vertex.position.x, center.x + halfExtents.x))
                                  && (near(vertex.position.y, center.y - halfExtents.y)
                                      || near(vertex.position.y, center.y + halfExtents.y))
                                  && (near(vertex.position.z, center.z - halfExtents.z)
                                      || near(vertex.position.z, center.z + halfExtents.z));
        CHECK(vertex.color == color);
    }
    CHECK(everyComponentIsExtreme);

    // The assertion that catches a wrong edge set: a cube's eight corners each
    // meet exactly three edges, so each corner must appear exactly three times
    // and no vertex may be anything but a corner.
    const std::array<Vec3, 8> corners = boxCorners(center, halfExtents);
    std::array<int, 8> hits{};
    bool everyVertexIsACorner = true;

    for (const DebugVertex& vertex : draw.vertices())
    {
        int matches = 0;
        for (usize index = 0; index < corners.size(); ++index)
        {
            if (near(vertex.position, corners[index]))
            {
                hits[index] += 1;
                matches += 1;
            }
        }
        everyVertexIsACorner = everyVertexIsACorner && matches == 1;
    }

    CHECK(everyVertexIsACorner);
    for (const int count : hits)
        CHECK(count == 3);
}

TEST_CASE("wire box edges run along the axes and none is drawn twice")
{
    constexpr Vec3 center{0.0f, 0.0f, 0.0f};
    constexpr Vec3 halfExtents{1.0f, 2.0f, 3.0f};

    DebugDraw draw;
    draw.wireBox(center, halfExtents, DebugColor{});

    REQUIRE(draw.lineCount() == 12u);

    // A box edge joins two corners differing in exactly one coordinate. A
    // diagonal in the edge table would still produce twelve lines over eight
    // corners, so the count alone does not catch it.
    std::array<int, 3> edgesPerAxis{};
    bool everyEdgeIsAxisAligned = true;

    for (usize line = 0; line < draw.lineCount(); ++line)
    {
        const Vec3 from = draw.vertices()[line * 2].position;
        const Vec3 to = draw.vertices()[line * 2 + 1].position;

        const bool differsX = !near(from.x, to.x);
        const bool differsY = !near(from.y, to.y);
        const bool differsZ = !near(from.z, to.z);
        const int differing = static_cast<int>(differsX) + static_cast<int>(differsY) + static_cast<int>(differsZ);

        everyEdgeIsAxisAligned = everyEdgeIsAxisAligned && differing == 1;
        if (differing == 1)
            edgesPerAxis[differsX ? 0u : (differsY ? 1u : 2u)] += 1;
    }

    CHECK(everyEdgeIsAxisAligned);
    // Four edges parallel to each axis, which also rules out a repeated edge.
    CHECK(edgesPerAxis[0] == 4);
    CHECK(edgesPerAxis[1] == 4);
    CHECK(edgesPerAxis[2] == 4);
}

TEST_CASE("an oriented wire box is the axis-aligned one pushed through the transform")
{
    constexpr Vec3 halfExtents{1.0f, 2.0f, 0.5f};
    const Mat4 transform = translation({4.0f, -1.0f, 2.0f}) * rotationZ4(kPi * 0.25f);

    DebugDraw axisAligned;
    axisAligned.wireBox(Vec3{}, halfExtents, DebugColor{});

    DebugDraw oriented;
    oriented.wireBox(transform, halfExtents, DebugColor{});

    REQUIRE(oriented.vertices().size() == axisAligned.vertices().size());

    // Same topology, transformed positions -- there is one corner ordering and
    // one edge table, not two that could drift apart.
    bool matches = true;
    for (usize index = 0; index < oriented.vertices().size(); ++index)
    {
        const Vec3 expected = transformPoint(transform, axisAligned.vertices()[index].position);
        matches = matches && near(oriented.vertices()[index].position, expected);
    }
    CHECK(matches);

    // The rotation is real: an oriented box is no longer axis aligned.
    CHECK_FALSE(near(oriented.vertices()[0].position, axisAligned.vertices()[0].position));
}

TEST_CASE("a wire sphere is three closed rings on the sphere")
{
    constexpr Vec3 center{2.0f, 3.0f, -1.0f};
    constexpr f32 radius = 4.0f;
    constexpr u32 segments = 8;

    DebugDraw draw;
    draw.wireSphere(center, radius, DebugColor{}, segments);

    REQUIRE(draw.lineCount() == 3 * segments);

    bool everyVertexIsOnTheSphere = true;
    for (const DebugVertex& vertex : draw.vertices())
        everyVertexIsOnTheSphere = everyVertexIsOnTheSphere && near(length(vertex.position - center), radius);
    CHECK(everyVertexIsOnTheSphere);

    // Per ring: the segments form one connected polyline and the last one
    // returns exactly to the first vertex. Exactly, not nearly -- the
    // implementation reuses the stored first point instead of evaluating the
    // angle at tau, because a hairline gap in a debug ring reads as a bug in
    // whatever is being debugged.
    constexpr usize verticesPerRing = 2 * segments;
    std::array<int, 3> constantAxes{};

    for (usize ring = 0; ring < 3; ++ring)
    {
        const std::span<const DebugVertex> vertices = draw.vertices().subspan(ring * verticesPerRing, verticesPerRing);

        bool connected = true;
        for (usize segment = 0; segment + 1 < segments; ++segment)
            connected = connected && vertices[segment * 2 + 1].position == vertices[segment * 2 + 2].position;

        CHECK(connected);
        CHECK(vertices[verticesPerRing - 1].position == vertices[0].position);

        constantAxes[ring] = constantAxis(vertices, center);
    }

    // Axis aligned, and the three rings cover all three planes rather than
    // stacking two in the same one.
    CHECK(constantAxes[0] + constantAxes[1] + constantAxes[2] == 0 + 1 + 2);
    CHECK(constantAxes[0] != constantAxes[1]);
    CHECK(constantAxes[1] != constantAxes[2]);
    CHECK(constantAxes[0] != constantAxes[2]);
}

TEST_CASE("a wire sphere honours the segment count and refuses to divide by zero")
{
    DebugDraw draw;

    draw.wireSphere(Vec3{}, 1.0f, DebugColor{});
    CHECK(draw.lineCount() == 3u * 24u); // the documented default

    draw.clear();
    draw.wireSphere(Vec3{}, 1.0f, DebugColor{}, 3);
    CHECK(draw.lineCount() == 9u);

    draw.clear();
    draw.wireSphere(Vec3{}, 1.0f, DebugColor{}, 0);
    CHECK(draw.empty());
}

TEST_CASE("axes start at the transform's origin and follow its own axes")
{
    constexpr Vec3 position{5.0f, 6.0f, 7.0f};
    constexpr f32 axisLength = 2.0f;

    DebugDraw draw;
    draw.axes(translation(position), axisLength);

    REQUIRE(draw.lineCount() == 3u);

    CHECK(near(draw.vertices()[0].position, position));
    CHECK(near(draw.vertices()[2].position, position));
    CHECK(near(draw.vertices()[4].position, position));

    CHECK(near(draw.vertices()[1].position, position + Vec3{axisLength, 0.0f, 0.0f}));
    CHECK(near(draw.vertices()[3].position, position + Vec3{0.0f, axisLength, 0.0f}));
    CHECK(near(draw.vertices()[5].position, position + Vec3{0.0f, 0.0f, axisLength}));

    // X red, Y green, Z blue: the whole point of the triad is reading the
    // orientation off the colours.
    CHECK(draw.vertices()[0].color == DebugColor::fromLinear(1.0f, 0.0f, 0.0f));
    CHECK(draw.vertices()[2].color == DebugColor::fromLinear(0.0f, 1.0f, 0.0f));
    CHECK(draw.vertices()[4].color == DebugColor::fromLinear(0.0f, 0.0f, 1.0f));
}

TEST_CASE("rotating the transform rotates the axes it draws")
{
    // Quarter turn about Z: the local X axis points along world +Y and the
    // local Y axis along world -X. Hardcoded world axes would pass every other
    // assertion in this file and fail this one.
    DebugDraw draw;
    draw.axes(rotationZ4(kPi * 0.5f), 1.0f);

    REQUIRE(draw.lineCount() == 3u);

    CHECK(near(draw.vertices()[1].position, Vec3{0.0f, 1.0f, 0.0f}));
    CHECK(near(draw.vertices()[3].position, Vec3{-1.0f, 0.0f, 0.0f}));
    CHECK(near(draw.vertices()[5].position, Vec3{0.0f, 0.0f, 1.0f}));
}

TEST_CASE("a scaled transform draws a triad of the requested length")
{
    // The directions are normalized, so `length` is a length. Scaling the
    // transform up would otherwise draw a triad whose size reports the scale
    // instead of the orientation it exists to show.
    DebugDraw draw;
    draw.axes(scaling({10.0f, 10.0f, 10.0f}), 3.0f);

    REQUIRE(draw.lineCount() == 3u);

    for (usize line = 0; line < draw.lineCount(); ++line)
    {
        const Vec3 from = draw.vertices()[line * 2].position;
        const Vec3 to = draw.vertices()[line * 2 + 1].position;
        CHECK(near(length(to - from), 3.0f));
    }
}

TEST_CASE("clear empties the buffer without giving up its capacity")
{
    DebugDraw draw;
    for (int index = 0; index < 16; ++index)
        draw.wireBox(Vec3{}, {1.0f, 1.0f, 1.0f}, DebugColor{});

    REQUIRE_FALSE(draw.empty());
    const DebugVertex* buffer = draw.vertices().data();

    draw.clear();
    CHECK(draw.empty());
    CHECK(draw.lineCount() == 0u);
    CHECK(draw.vertices().empty());

    // Capacity is not observable through the span, but its consequence is:
    // refilling with the same geometry must not reallocate even once, because
    // this runs on every frame. Sampled at every step rather than only at the
    // end -- a released buffer has to grow back from nothing, and an allocator
    // can hand the same address back by the time the refill is over.
    bool sameAllocation = draw.vertices().data() == buffer;
    for (int index = 0; index < 16; ++index)
    {
        draw.wireBox(Vec3{}, {1.0f, 1.0f, 1.0f}, DebugColor{});
        sameAllocation = sameAllocation && draw.vertices().data() == buffer;
    }

    CHECK(sameAllocation);
}

TEST_CASE("a colour packs into one byte per channel")
{
    // The GPU reads these bytes directly, so the packing order is a contract
    // with shaders/src/debug_line.hlsl, not an implementation detail.
    const auto channel = [](DebugColor color, u32 index) { return (color.rgba >> (index * 8)) & 0xFFu; };

    const DebugColor color = DebugColor::fromLinear(1.0f, 0.0f, 1.0f, 0.0f);
    CHECK(channel(color, 0) == 255u); // red
    CHECK(channel(color, 1) == 0u);   // green
    CHECK(channel(color, 2) == 255u); // blue
    CHECK(channel(color, 3) == 0u);   // alpha

    CHECK(DebugColor::fromLinear(0.0f, 0.0f, 0.0f, 0.0f).rgba == 0u);
    CHECK(DebugColor::fromLinear(1.0f, 1.0f, 1.0f, 1.0f).rgba == 0xFFFFFFFFu);

    // Alpha defaults to opaque, and every channel round-trips through the
    // quantization it was given.
    const DebugColor half = DebugColor::fromLinear(0.5f, 0.25f, 0.75f);
    CHECK(channel(half, 0) == 128u);
    CHECK(channel(half, 1) == 64u);
    CHECK(channel(half, 2) == 191u);
    CHECK(channel(half, 3) == 255u);

    // Out-of-range input clamps rather than wrapping, which would turn a too
    // bright colour into a dark one.
    const DebugColor clamped = DebugColor::fromLinear(2.0f, -1.0f, 1.5f, -0.5f);
    CHECK(channel(clamped, 0) == 255u);
    CHECK(channel(clamped, 1) == 0u);
    CHECK(channel(clamped, 2) == 255u);
    CHECK(channel(clamped, 3) == 0u);
}
