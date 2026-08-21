#include "luaug/asset/primitives.h"

#include <cmath>
#include <numbers>

namespace luaug::asset {
namespace {

using core::u32;
using core::usize;

constexpr f32 kPi = std::numbers::pi_v<f32>;
constexpr f32 kTwoPi = 2.0f * kPi;

struct Builder
{
    Mesh mesh;

    u32 add(Vec3 position, Vec3 normal, Vec3 tangent, f32 handedness, f32 u, f32 v)
    {
        Vertex vertex;
        vertex.position = position;
        vertex.normal = normal;
        vertex.tangent[0] = tangent.x;
        vertex.tangent[1] = tangent.y;
        vertex.tangent[2] = tangent.z;
        vertex.tangent[3] = handedness;
        vertex.uv[0] = u;
        vertex.uv[1] = v;
        mesh.vertices.push_back(vertex);
        return static_cast<u32>(mesh.vertices.size() - 1);
    }

    // Counter-clockwise when seen from outside, which is what a `Back` cull mode
    // means and what every imported glTF already is.
    void triangle(u32 a, u32 b, u32 c)
    {
        mesh.indices.push_back(a);
        mesh.indices.push_back(b);
        mesh.indices.push_back(c);
    }

    void quad(u32 a, u32 b, u32 c, u32 d)
    {
        triangle(a, b, c);
        triangle(a, c, d);
    }

    void finish()
    {
        // One submesh over everything, material 0. A `Part`'s look is its own
        // properties, so the material index is a placeholder the renderer
        // replaces rather than something this file knows about.
        AABB bounds;
        bool first = true;
        for (const Vertex& vertex : mesh.vertices) {
            if (first) {
                bounds.min = vertex.position;
                bounds.max = vertex.position;
                first = false;
                continue;
            }
            bounds.min.x = std::fmin(bounds.min.x, vertex.position.x);
            bounds.min.y = std::fmin(bounds.min.y, vertex.position.y);
            bounds.min.z = std::fmin(bounds.min.z, vertex.position.z);
            bounds.max.x = std::fmax(bounds.max.x, vertex.position.x);
            bounds.max.y = std::fmax(bounds.max.y, vertex.position.y);
            bounds.max.z = std::fmax(bounds.max.z, vertex.position.z);
        }
        mesh.bounds = bounds;
        mesh.submeshes.push_back(Submesh{
            .firstIndex = 0,
            .indexCount = static_cast<u32>(mesh.indices.size()),
            .material = 0,
            .bounds = bounds,
        });
    }
};

// One face of a box, as four corners in counter-clockwise order seen from
// outside. Each face gets its own four vertices rather than sharing eight: a
// shared corner would have to average three perpendicular normals, and a cube
// with rounded-looking lighting is the classic symptom of exactly that.
void boxFace(Builder& out, Vec3 origin, Vec3 across, Vec3 up, Vec3 normal)
{
    const Vec3 tangent = core::normalize(across);
    const u32 a = out.add(origin, normal, tangent, 1.0f, 0.0f, 0.0f);
    const u32 b =
        out.add(Vec3{origin.x + across.x, origin.y + across.y, origin.z + across.z}, normal, tangent, 1.0f, 1.0f, 0.0f);
    const u32 c = out.add(Vec3{origin.x + across.x + up.x, origin.y + across.y + up.y, origin.z + across.z + up.z},
                          normal, tangent, 1.0f, 1.0f, 1.0f);
    const u32 d = out.add(Vec3{origin.x + up.x, origin.y + up.y, origin.z + up.z}, normal, tangent, 1.0f, 0.0f, 1.0f);
    out.quad(a, b, c, d);
}

Mesh makeBlock()
{
    Builder out;
    constexpr f32 h = 0.5f;
    // origin, across, up, normal -- per face, walked so that `across x up` is
    // the outward normal.
    boxFace(out, {-h, -h, h}, {2.0f * h, 0.0f, 0.0f}, {0.0f, 2.0f * h, 0.0f}, {0.0f, 0.0f, 1.0f});
    boxFace(out, {h, -h, -h}, {-2.0f * h, 0.0f, 0.0f}, {0.0f, 2.0f * h, 0.0f}, {0.0f, 0.0f, -1.0f});
    boxFace(out, {h, -h, h}, {0.0f, 0.0f, -2.0f * h}, {0.0f, 2.0f * h, 0.0f}, {1.0f, 0.0f, 0.0f});
    boxFace(out, {-h, -h, -h}, {0.0f, 0.0f, 2.0f * h}, {0.0f, 2.0f * h, 0.0f}, {-1.0f, 0.0f, 0.0f});
    boxFace(out, {-h, h, h}, {2.0f * h, 0.0f, 0.0f}, {0.0f, 0.0f, -2.0f * h}, {0.0f, 1.0f, 0.0f});
    boxFace(out, {-h, -h, -h}, {2.0f * h, 0.0f, 0.0f}, {0.0f, 0.0f, 2.0f * h}, {0.0f, -1.0f, 0.0f});
    out.finish();
    return std::move(out.mesh);
}

// A UV sphere of radius 0.5. The seam is duplicated -- the vertex at u = 0 and
// the one at u = 1 are the same point in space and different points in texture
// space -- because sharing it would wrap the whole texture backwards across one
// column of quads.
Mesh makeBall()
{
    Builder out;
    constexpr f32 radius = 0.5f;

    for (u32 ring = 0; ring <= kPrimitiveRings; ++ring) {
        const f32 v = static_cast<f32>(ring) / static_cast<f32>(kPrimitiveRings);
        const f32 phi = v * kPi;
        const f32 sinPhi = std::sin(phi);
        const f32 cosPhi = std::cos(phi);

        for (u32 segment = 0; segment <= kPrimitiveSegments; ++segment) {
            const f32 u = static_cast<f32>(segment) / static_cast<f32>(kPrimitiveSegments);
            const f32 theta = u * kTwoPi;
            const Vec3 normal{sinPhi * std::sin(theta), cosPhi, sinPhi * std::cos(theta)};
            // The direction u increases in, which is what a tangent is.
            const Vec3 tangent{std::cos(theta), 0.0f, -std::sin(theta)};
            out.add(Vec3{normal.x * radius, normal.y * radius, normal.z * radius}, normal, tangent, 1.0f, u, 1.0f - v);
        }
    }

    const u32 stride = kPrimitiveSegments + 1;
    for (u32 ring = 0; ring < kPrimitiveRings; ++ring) {
        for (u32 segment = 0; segment < kPrimitiveSegments; ++segment) {
            const u32 top = ring * stride + segment;
            const u32 bottom = top + stride;
            out.quad(bottom, bottom + 1, top + 1, top);
        }
    }
    out.finish();
    return std::move(out.mesh);
}

// The side wall of a body of revolution, from `bottomY` to `topY` at a constant
// radius. Returns nothing; the caller owns the caps.
void revolvedSide(Builder& out, f32 radius, f32 bottomY, f32 topY)
{
    const u32 first = static_cast<u32>(out.mesh.vertices.size());
    for (u32 segment = 0; segment <= kPrimitiveSegments; ++segment) {
        const f32 u = static_cast<f32>(segment) / static_cast<f32>(kPrimitiveSegments);
        const f32 theta = u * kTwoPi;
        const Vec3 normal{std::sin(theta), 0.0f, std::cos(theta)};
        const Vec3 tangent{std::cos(theta), 0.0f, -std::sin(theta)};
        out.add(Vec3{normal.x * radius, topY, normal.z * radius}, normal, tangent, 1.0f, u, 1.0f);
        out.add(Vec3{normal.x * radius, bottomY, normal.z * radius}, normal, tangent, 1.0f, u, 0.0f);
    }
    for (u32 segment = 0; segment < kPrimitiveSegments; ++segment) {
        // Lower-left, lower-right, upper-right, upper-left: counter-clockwise
        // seen from OUTSIDE, which is what a `Back` cull mode keeps.
        const u32 base = first + segment * 2;
        out.quad(base + 1, base + 3, base + 2, base);
    }
}

// A flat disc facing `up` (+1) or down (-1), as a fan around a centre vertex.
void disc(Builder& out, f32 radius, f32 y, f32 facing)
{
    const Vec3 normal{0.0f, facing, 0.0f};
    const Vec3 tangent{1.0f, 0.0f, 0.0f};
    const u32 centre = out.add(Vec3{0.0f, y, 0.0f}, normal, tangent, 1.0f, 0.5f, 0.5f);
    const u32 first = static_cast<u32>(out.mesh.vertices.size());
    for (u32 segment = 0; segment <= kPrimitiveSegments; ++segment) {
        const f32 theta = static_cast<f32>(segment) / static_cast<f32>(kPrimitiveSegments) * kTwoPi;
        const f32 x = std::sin(theta);
        const f32 z = std::cos(theta);
        out.add(Vec3{x * radius, y, z * radius}, normal, tangent, 1.0f, 0.5f + 0.5f * x, 0.5f + 0.5f * z);
    }
    for (u32 segment = 0; segment < kPrimitiveSegments; ++segment) {
        // Wound the other way for the downward disc, or the cull mode discards
        // the face you were meant to see.
        if (facing > 0.0f)
            out.triangle(centre, first + segment, first + segment + 1);
        else
            out.triangle(centre, first + segment + 1, first + segment);
    }
}

Mesh makeCylinder()
{
    Builder out;
    constexpr f32 radius = 0.5f;
    revolvedSide(out, radius, -0.5f, 0.5f);
    disc(out, radius, 0.5f, 1.0f);
    disc(out, radius, -0.5f, -1.0f);
    out.finish();
    return std::move(out.mesh);
}

// One hemispherical cap, centred at `centreY`, opening away from the body.
// `sign` is +1 for the top cap and -1 for the bottom.
void hemisphere(Builder& out, f32 radius, f32 centreY, f32 sign)
{
    const u32 first = static_cast<u32>(out.mesh.vertices.size());
    for (u32 ring = 0; ring <= kCapsuleCapRings; ++ring) {
        const f32 t = static_cast<f32>(ring) / static_cast<f32>(kCapsuleCapRings);
        const f32 phi = t * (kPi * 0.5f);
        const f32 sinPhi = std::sin(phi);
        const f32 cosPhi = std::cos(phi);
        for (u32 segment = 0; segment <= kPrimitiveSegments; ++segment) {
            const f32 u = static_cast<f32>(segment) / static_cast<f32>(kPrimitiveSegments);
            const f32 theta = u * kTwoPi;
            const Vec3 normal{cosPhi * std::sin(theta), sign * sinPhi, cosPhi * std::cos(theta)};
            const Vec3 tangent{std::cos(theta), 0.0f, -std::sin(theta)};
            out.add(Vec3{normal.x * radius, centreY + normal.y * radius, normal.z * radius}, normal, tangent, 1.0f, u,
                    sign > 0.0f ? 1.0f - t * 0.5f : t * 0.5f);
        }
    }

    const u32 stride = kPrimitiveSegments + 1;
    for (u32 ring = 0; ring < kCapsuleCapRings; ++ring) {
        for (u32 segment = 0; segment < kPrimitiveSegments; ++segment) {
            const u32 lower = first + ring * stride + segment;
            const u32 upper = lower + stride;
            if (sign > 0.0f)
                out.quad(lower + 1, upper + 1, upper, lower);
            else
                out.quad(upper, upper + 1, lower + 1, lower);
        }
    }
}

// Radius 0.5, cylinder section 1, total height 2 -- the character aspect. The
// header says why this one is not unit-extent like the rest.
Mesh makeCapsule()
{
    Builder out;
    constexpr f32 radius = 0.5f;
    constexpr f32 halfCylinder = 0.5f;
    revolvedSide(out, radius, -halfCylinder, halfCylinder);
    hemisphere(out, radius, halfCylinder, 1.0f);
    hemisphere(out, radius, -halfCylinder, -1.0f);
    out.finish();
    return std::move(out.mesh);
}

// A right-triangular prism: the full box with the +Y +Z edge removed, so the
// slope falls from the top at -Z to the bottom at +Z. Which edge is the high one
// is arbitrary and therefore written down: **the tall face is -Z**, so a wedge
// with no rotation is a ramp you walk UP as you walk towards -Z.
Mesh makeWedge()
{
    Builder out;
    constexpr f32 h = 0.5f;

    // Bottom and the tall back face are ordinary quads.
    boxFace(out, {-h, -h, -h}, {2.0f * h, 0.0f, 0.0f}, {0.0f, 0.0f, 2.0f * h}, {0.0f, -1.0f, 0.0f});
    boxFace(out, {h, -h, -h}, {-2.0f * h, 0.0f, 0.0f}, {0.0f, 2.0f * h, 0.0f}, {0.0f, 0.0f, -1.0f});

    // The slope. Its normal is the diagonal of the YZ square, normalized.
    const f32 diagonal = 1.0f / std::sqrt(2.0f);
    const Vec3 slopeNormal{0.0f, diagonal, diagonal};
    const Vec3 slopeTangent{1.0f, 0.0f, 0.0f};
    const u32 s0 = out.add(Vec3{-h, h, -h}, slopeNormal, slopeTangent, 1.0f, 0.0f, 1.0f);
    const u32 s1 = out.add(Vec3{h, h, -h}, slopeNormal, slopeTangent, 1.0f, 1.0f, 1.0f);
    const u32 s2 = out.add(Vec3{h, -h, h}, slopeNormal, slopeTangent, 1.0f, 1.0f, 0.0f);
    const u32 s3 = out.add(Vec3{-h, -h, h}, slopeNormal, slopeTangent, 1.0f, 0.0f, 0.0f);
    out.quad(s3, s2, s1, s0);

    // The two triangular sides.
    const Vec3 rightNormal{1.0f, 0.0f, 0.0f};
    const Vec3 rightTangent{0.0f, 0.0f, -1.0f};
    const u32 r0 = out.add(Vec3{h, -h, -h}, rightNormal, rightTangent, 1.0f, 0.0f, 0.0f);
    const u32 r1 = out.add(Vec3{h, -h, h}, rightNormal, rightTangent, 1.0f, 1.0f, 0.0f);
    const u32 r2 = out.add(Vec3{h, h, -h}, rightNormal, rightTangent, 1.0f, 0.0f, 1.0f);
    out.triangle(r0, r2, r1);

    const Vec3 leftNormal{-1.0f, 0.0f, 0.0f};
    const Vec3 leftTangent{0.0f, 0.0f, 1.0f};
    const u32 l0 = out.add(Vec3{-h, -h, -h}, leftNormal, leftTangent, 1.0f, 0.0f, 0.0f);
    const u32 l1 = out.add(Vec3{-h, h, -h}, leftNormal, leftTangent, 1.0f, 0.0f, 1.0f);
    const u32 l2 = out.add(Vec3{-h, -h, h}, leftNormal, leftTangent, 1.0f, 1.0f, 0.0f);
    out.triangle(l0, l2, l1);

    out.finish();
    return std::move(out.mesh);
}

} // namespace

Mesh makePrimitive(PrimitiveShape shape)
{
    switch (shape) {
    case PrimitiveShape::Block:
        return makeBlock();
    case PrimitiveShape::Ball:
        return makeBall();
    case PrimitiveShape::Cylinder:
        return makeCylinder();
    case PrimitiveShape::Capsule:
        return makeCapsule();
    case PrimitiveShape::Wedge:
        return makeWedge();
    case PrimitiveShape::Count:
        break;
    }
    // An unknown shape draws as a block rather than as nothing. A `Part` whose
    // `Shape` came from a future enum item should be visible and wrong, not
    // invisible and correct.
    return makeBlock();
}

} // namespace luaug::asset
