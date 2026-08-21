// The five unit solids (M6).
//
// These cases check the things a screenshot cannot: that every triangle faces
// outward, that no normal is a zero vector, and that the extents are what the
// renderer's scaling assumes. A primitive whose winding was backwards renders as
// a hole in the world with a back cull, and looks perfectly fine with the cull
// off -- which is how that bug survives a visual check.
#include "luaug/asset/primitives.h"

#include <cmath>
#include <doctest/doctest.h>

using luaug::asset::kPrimitiveSegments;
using luaug::asset::makePrimitive;
using luaug::asset::Mesh;
using luaug::asset::PrimitiveShape;
using luaug::asset::Vertex;
using luaug::core::f32;
using luaug::core::Vec3;

namespace {

constexpr f32 kEpsilon = 1e-4f;

// The signed volume of the tetrahedron each triangle makes with the origin. For
// a closed surface with outward-facing, counter-clockwise winding this sums to
// the enclosed volume and is POSITIVE; flip the winding and every one of them
// changes sign.
[[nodiscard]] f32 signedVolume(const Mesh& mesh)
{
    f32 total = 0.0f;
    for (std::size_t index = 0; index + 2 < mesh.indices.size(); index += 3) {
        const Vec3 a = mesh.vertices[mesh.indices[index]].position;
        const Vec3 b = mesh.vertices[mesh.indices[index + 1]].position;
        const Vec3 c = mesh.vertices[mesh.indices[index + 2]].position;
        const Vec3 cross{b.y * c.z - b.z * c.y, b.z * c.x - b.x * c.z, b.x * c.y - b.y * c.x};
        total += (a.x * cross.x + a.y * cross.y + a.z * cross.z) / 6.0f;
    }
    return total;
}

} // namespace

TEST_CASE("every primitive is a closed, outward-wound solid with a real tangent frame")
{
    for (int raw = 0; raw < static_cast<int>(PrimitiveShape::Count); ++raw) {
        const auto shape = static_cast<PrimitiveShape>(raw);
        CAPTURE(raw);
        const Mesh mesh = makePrimitive(shape);

        REQUIRE_FALSE(mesh.vertices.empty());
        REQUIRE_FALSE(mesh.indices.empty());
        CHECK(mesh.indices.size() % 3 == 0);
        REQUIRE(mesh.submeshes.size() == 1);
        CHECK(mesh.submeshes[0].indexCount == mesh.indices.size());

        // Positive, which is what says the winding is outward. The number itself
        // is not asserted here -- the per-shape cases below do that.
        CHECK(signedVolume(mesh) > 0.0f);

        for (const Vertex& vertex : mesh.vertices) {
            const f32 normalLength = std::sqrt(vertex.normal.x * vertex.normal.x + vertex.normal.y * vertex.normal.y +
                                               vertex.normal.z * vertex.normal.z);
            CHECK(std::fabs(normalLength - 1.0f) <= kEpsilon);

            // A zeroed tangent is what an importer leaves when a file has none,
            // and `tangentFrame` has a fallback for it. A generated primitive
            // has no excuse to take that path -- it would shade differently
            // from an imported mesh for no reason a person could see.
            const f32 tangentLength =
                std::sqrt(vertex.tangent[0] * vertex.tangent[0] + vertex.tangent[1] * vertex.tangent[1] +
                          vertex.tangent[2] * vertex.tangent[2]);
            CHECK(tangentLength > 0.5f);
            CHECK(static_cast<double>(std::fabs(vertex.tangent[3])) == doctest::Approx(1.0));

            // The tangent has to be perpendicular to the normal or the frame is
            // sheared, which shows up as normal-mapped lighting that swims.
            const f32 dot = vertex.tangent[0] * vertex.normal.x + vertex.tangent[1] * vertex.normal.y +
                            vertex.tangent[2] * vertex.normal.z;
            CHECK(std::fabs(dot) <= kEpsilon);

            for (const f32 uv : vertex.uv) {
                CHECK(uv >= -kEpsilon);
                CHECK(uv <= 1.0f + kEpsilon);
            }
        }
    }
}

TEST_CASE("four of the five span the unit box, and the capsule says why it does not")
{
    for (int raw = 0; raw < static_cast<int>(PrimitiveShape::Count); ++raw) {
        const auto shape = static_cast<PrimitiveShape>(raw);
        CAPTURE(raw);
        const Mesh mesh = makePrimitive(shape);

        CHECK(static_cast<double>(mesh.bounds.min.x) == doctest::Approx(-0.5));
        CHECK(static_cast<double>(mesh.bounds.max.x) == doctest::Approx(0.5));
        CHECK(static_cast<double>(mesh.bounds.min.z) == doctest::Approx(-0.5));
        CHECK(static_cast<double>(mesh.bounds.max.z) == doctest::Approx(0.5));

        // The capsule is built at the CHARACTER aspect -- radius 0.5, cylinder
        // section 1, total height 2 -- so that `Size.y == 2 * Size.x` is exact
        // rather than an ellipsoid. Everything else is unit-extent.
        const auto expected = shape == PrimitiveShape::Capsule ? 1.0 : 0.5;
        CHECK(static_cast<double>(mesh.bounds.min.y) == doctest::Approx(-expected));
        CHECK(static_cast<double>(mesh.bounds.max.y) == doctest::Approx(expected));
    }
}

TEST_CASE("the block is exactly a unit cube and the wedge is exactly half of one")
{
    // Written as volumes because that is what catches a face wound backwards or
    // a slope that cuts the wrong corner -- both of which leave the bounds
    // unchanged.
    CHECK(static_cast<double>(signedVolume(makePrimitive(PrimitiveShape::Block))) == doctest::Approx(1.0));
    CHECK(static_cast<double>(signedVolume(makePrimitive(PrimitiveShape::Wedge))) == doctest::Approx(0.5));
}

TEST_CASE("the ball and the cylinder approach their true volumes from below")
{
    // A tessellated solid is INSCRIBED in the shape it approximates, so it is
    // always slightly smaller -- and a version that came out larger would mean
    // the vertices were being pushed outward, which is a radius bug.
    const f32 ball = signedVolume(makePrimitive(PrimitiveShape::Ball));
    const f32 trueBall = 4.0f / 3.0f * 3.14159265f * 0.125f;
    CHECK(ball < trueBall);
    CHECK(ball > trueBall * 0.97f);

    const f32 cylinder = signedVolume(makePrimitive(PrimitiveShape::Cylinder));
    const f32 trueCylinder = 3.14159265f * 0.25f;
    CHECK(cylinder < trueCylinder);
    CHECK(cylinder > trueCylinder * 0.97f);
}

TEST_CASE("the tessellation is the number the goldens were recorded against")
{
    // Not a smoke test: every capture golden recorded after M6 is baked against
    // these counts, and changing one re-records all of them. The assertion is
    // here so that the change is a decision rather than a surprise.
    CHECK(kPrimitiveSegments == 24);
    CHECK(makePrimitive(PrimitiveShape::Block).indices.size() == 36);
    CHECK(makePrimitive(PrimitiveShape::Ball).indices.size() == 24 * 12 * 6);
}

TEST_CASE("an out-of-range shape draws as a block rather than as nothing")
{
    // A `Part` whose `Shape` came from an enum item this build does not know
    // should be visible and wrong, not invisible and correct.
    const Mesh fallback = makePrimitive(static_cast<PrimitiveShape>(99));
    CHECK(fallback.indices.size() == 36);
}
