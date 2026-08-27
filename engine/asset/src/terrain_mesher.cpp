#include "luaug/asset/terrain_mesher.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace luaug::asset {
namespace {

using core::i32;
using core::u32;
using core::usize;
using core::Vec3;

// The eight corners of a lattice cell, indexed so that bit 0 is x, bit 1 is y
// and bit 2 is z. Written as a table rather than computed inline because every
// tetrahedron below indexes it, and one transposed literal here would be a
// surface subtly in the wrong place everywhere.
constexpr std::array<std::array<i32, 3>, 8> CornerOffsets{{
    {0, 0, 0}, // 0
    {1, 0, 0}, // 1
    {0, 1, 0}, // 2
    {1, 1, 0}, // 3
    {0, 0, 1}, // 4
    {1, 0, 1}, // 5
    {0, 1, 1}, // 6
    {1, 1, 1}, // 7
}};

// **Six tetrahedra sharing the cube's main diagonal 0-7.**
//
// The decomposition is the standard one and the property that matters is that
// every neighbouring cell uses the SAME diagonal -- so two cells sharing a face
// split that face along the same line, and their triangles meet exactly. A
// decomposition that alternated by parity would be watertight inside a cell and
// cracked between two.
constexpr std::array<std::array<int, 4>, 6> Tetrahedra{{
    {0, 7, 1, 3},
    {0, 7, 3, 2},
    {0, 7, 2, 6},
    {0, 7, 6, 4},
    {0, 7, 4, 5},
    {0, 7, 5, 1},
}};

// The six edges of a tetrahedron, as index pairs into its four corners.
constexpr std::array<std::array<int, 2>, 6> TetraEdges{{
    {0, 1},
    {0, 2},
    {0, 3},
    {1, 2},
    {1, 3},
    {2, 3},
}};

// One sampled lattice point.
struct Corner
{
    i32 x = 0;
    i32 y = 0;
    i32 z = 0;
    float distance = 0.0f;
    core::u8 material = 0;
};

// A vertex's identity: the two lattice points whose edge it sits on, in sorted
// order. **Sorted so that the two tetrahedra sharing an edge agree**, which is
// what makes the surface indexed and watertight rather than a soup of
// coincident vertices.
struct EdgeKey
{
    i32 ax = 0;
    i32 ay = 0;
    i32 az = 0;
    i32 bx = 0;
    i32 by = 0;
    i32 bz = 0;

    [[nodiscard]] constexpr auto operator<=>(const EdgeKey&) const noexcept = default;
};

[[nodiscard]] EdgeKey edgeKeyOf(const Corner& a, const Corner& b) noexcept
{
    const bool aFirst = std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z);
    const Corner& first = aFirst ? a : b;
    const Corner& second = aFirst ? b : a;
    return EdgeKey{first.x, first.y, first.z, second.x, second.y, second.z};
}

// Where along the edge the surface crosses.
//
// **Guarded against a zero denominator**, which happens when both endpoints sit
// exactly on the surface -- and that is not a pathological case here, it is what
// `sd(p) = p.y - H` produces whenever the ground passes exactly through a
// lattice plane. Half is the honest answer when the field says the whole edge is
// the surface.
[[nodiscard]] float crossingAt(float a, float b) noexcept
{
    const float delta = b - a;
    if (std::abs(delta) < 1e-12f) {
        return 0.5f;
    }
    return std::clamp(-a / delta, 0.0f, 1.0f);
}

} // namespace

TerrainMesh meshField(const TerrainField& field, const MeshRegion& region)
{
    TerrainMesh out;
    const float voxel = field.settings().voxelSize;
    const auto stride = static_cast<i32>(std::max(region.stride, 1u));

    // The surface is INSIDE where distance is negative, so a corner is "solid"
    // when its distance is below zero. Zero counts as solid, which puts a sample
    // sitting exactly on the surface on the inside -- an arbitrary choice that
    // has to be made consistently, because two tetrahedra disagreeing about a
    // shared corner is a crack.
    const auto solid = [](const Corner& corner) noexcept { return corner.distance <= 0.0f; };

    // Vertex identity, so an edge shared by several tetrahedra produces one
    // vertex and one index. A `std::map` rather than a hash map for R10: this
    // walk decides the order vertices are emitted in, and therefore the mesh's
    // bytes.
    std::map<EdgeKey, u32> emitted;

    const auto sampleAt = [&](i32 x, i32 y, i32 z) noexcept {
        const FieldSample got = field.sample(x, y, z);
        return Corner{x, y, z, got.distance, got.material};
    };

    // The gradient, by central differences, which is the surface normal. Read
    // from the FIELD rather than computed from the triangle: a triangle normal
    // is faceted and a field gradient is smooth, and the gradient is also what
    // decides the winding below.
    const auto gradientAt = [&](i32 x, i32 y, i32 z) noexcept {
        const float dx = field.sample(x + stride, y, z).distance - field.sample(x - stride, y, z).distance;
        const float dy = field.sample(x, y + stride, z).distance - field.sample(x, y - stride, z).distance;
        const float dz = field.sample(x, y, z + stride).distance - field.sample(x, y, z - stride).distance;
        const Vec3 gradient{dx, dy, dz};
        const float length = std::sqrt(gradient.x * gradient.x + gradient.y * gradient.y + gradient.z * gradient.z);
        if (length < 1e-8f) {
            return Vec3{0.0f, 1.0f, 0.0f};
        }
        return Vec3{gradient.x / length, gradient.y / length, gradient.z / length};
    };

    const auto vertexOn = [&](const Corner& a, const Corner& b) -> u32 {
        const EdgeKey key = edgeKeyOf(a, b);
        if (const auto at = emitted.find(key); at != emitted.end()) {
            return at->second;
        }

        const float t = crossingAt(a.distance, b.distance);
        const auto lerp = [t](float from, float to) { return from + (to - from) * t; };
        const Vec3 position{lerp(static_cast<float>(a.x), static_cast<float>(b.x)) * voxel,
                            lerp(static_cast<float>(a.y), static_cast<float>(b.y)) * voxel,
                            lerp(static_cast<float>(a.z), static_cast<float>(b.z)) * voxel};

        // The gradient at whichever end the crossing is nearer, which is cheaper
        // than interpolating two gradients and indistinguishable at this
        // resolution.
        const Corner& nearer = t < 0.5f ? a : b;
        const Vec3 normal = gradientAt(nearer.x, nearer.y, nearer.z);

        Vertex vertex;
        vertex.position = position;
        vertex.normal = normal;
        // **Triplanar UVs are the terrain's business and not this function's.**
        // A world-space UV is what a terrain shader wants -- a texture that does
        // not swim when the ground is sculpted -- and picking the plane is a
        // shading decision. What goes here is the world position on the two axes
        // the normal is least aligned with, which is the same answer for the
        // same point however it was reached.
        const float ax = std::abs(normal.x);
        const float ay = std::abs(normal.y);
        const float az = std::abs(normal.z);
        if (ay >= ax && ay >= az) {
            vertex.uv[0] = position.x;
            vertex.uv[1] = position.z;
        }
        else if (ax >= az) {
            vertex.uv[0] = position.z;
            vertex.uv[1] = position.y;
        }
        else {
            vertex.uv[0] = position.x;
            vertex.uv[1] = position.y;
        }

        const auto index = static_cast<u32>(out.mesh.vertices.size());
        out.mesh.vertices.push_back(vertex);
        out.colliderPoints.push_back(position);
        emitted.emplace(key, index);
        return index;
    };

    // **Winding is derived from the field, not from a case table.** The triangle
    // is emitted, its geometric normal computed, and the order reversed when it
    // disagrees with the gradient -- so "which way does this face" stops being
    // something a table can have backwards. That class of bug already cost this
    // milestone a failing test in the physics seam, where a quad wound the wrong
    // way was a floor that a cube fell through.
    const auto emitTriangle = [&](u32 a, u32 b, u32 c) {
        if (a == b || b == c || a == c) {
            return; // Degenerate: two crossings landed on the same point.
        }
        const Vec3& pa = out.mesh.vertices[a].position;
        const Vec3& pb = out.mesh.vertices[b].position;
        const Vec3& pc = out.mesh.vertices[c].position;
        const Vec3 ab{pb.x - pa.x, pb.y - pa.y, pb.z - pa.z};
        const Vec3 ac{pc.x - pa.x, pc.y - pa.y, pc.z - pa.z};
        const Vec3 face{ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x};

        const Vec3& reference = out.mesh.vertices[a].normal;
        const float agreement = face.x * reference.x + face.y * reference.y + face.z * reference.z;
        const u32 second = agreement < 0.0f ? c : b;
        const u32 third = agreement < 0.0f ? b : c;

        out.mesh.indices.push_back(a);
        out.mesh.indices.push_back(second);
        out.mesh.indices.push_back(third);
        out.colliderIndices.push_back(a);
        out.colliderIndices.push_back(second);
        out.colliderIndices.push_back(third);
    };

    for (u32 cellZ = 0; cellZ < region.cellsZ; ++cellZ) {
        for (u32 cellY = 0; cellY < region.cellsY; ++cellY) {
            for (u32 cellX = 0; cellX < region.cellsX; ++cellX) {
                const i32 baseX = region.minX + static_cast<i32>(cellX) * stride;
                const i32 baseY = region.minY + static_cast<i32>(cellY) * stride;
                const i32 baseZ = region.minZ + static_cast<i32>(cellZ) * stride;

                std::array<Corner, 8> corners{};
                for (int at = 0; at < 8; ++at) {
                    corners[static_cast<usize>(at)] =
                        sampleAt(baseX + CornerOffsets[static_cast<usize>(at)][0] * stride,
                                 baseY + CornerOffsets[static_cast<usize>(at)][1] * stride,
                                 baseZ + CornerOffsets[static_cast<usize>(at)][2] * stride);
                }

                for (const std::array<int, 4>& tetra : Tetrahedra) {
                    std::array<const Corner*, 4> points{};
                    int inside = 0;
                    for (int at = 0; at < 4; ++at) {
                        points[static_cast<usize>(at)] = &corners[static_cast<usize>(tetra[static_cast<usize>(at)])];
                        if (solid(*points[static_cast<usize>(at)])) {
                            inside |= 1 << at;
                        }
                    }
                    if (inside == 0 || inside == 0b1111) {
                        continue; // Wholly solid or wholly air: no surface here.
                    }

                    // The crossings, in the fixed edge order above. A tetrahedron
                    // with a 1-3 split has three of them and a 2-2 split has four,
                    // and there is no other possibility -- which is the whole
                    // reason a tetrahedron needs no table.
                    std::array<u32, 6> crossing{};
                    std::array<bool, 6> crosses{};
                    int found = 0;
                    for (int at = 0; at < 6; ++at) {
                        const Corner& a = *points[static_cast<usize>(TetraEdges[static_cast<usize>(at)][0])];
                        const Corner& b = *points[static_cast<usize>(TetraEdges[static_cast<usize>(at)][1])];
                        if (solid(a) != solid(b)) {
                            crossing[static_cast<usize>(at)] = vertexOn(a, b);
                            crosses[static_cast<usize>(at)] = true;
                            ++found;
                        }
                    }

                    if (found == 3) {
                        std::array<u32, 3> triangle{};
                        int taken = 0;
                        for (int at = 0; at < 6 && taken < 3; ++at) {
                            if (crosses[static_cast<usize>(at)]) {
                                triangle[static_cast<usize>(taken++)] = crossing[static_cast<usize>(at)];
                            }
                        }
                        emitTriangle(triangle[0], triangle[1], triangle[2]);
                    }
                    else if (found == 4) {
                        // **A quad, and its two triangles have to share a
                        // diagonal that exists.** The four crossings are not in a
                        // cycle in edge order, so they are ordered here by which
                        // corner they touch: the two edges leaving the first
                        // inside corner, then the two leaving the second. That
                        // makes 0-1 and 2-3 opposite sides of the quad, so
                        // `(0,1,3)` and `(0,3,2)` are its two halves.
                        std::array<int, 2> insideCorners{-1, -1};
                        std::array<int, 2> outsideCorners{-1, -1};
                        int insideCount = 0;
                        int outsideCount = 0;
                        for (int at = 0; at < 4; ++at) {
                            if ((inside & (1 << at)) != 0) {
                                insideCorners[static_cast<usize>(insideCount++)] = at;
                            }
                            else {
                                outsideCorners[static_cast<usize>(outsideCount++)] = at;
                            }
                        }

                        const auto crossingBetween = [&](int cornerA, int cornerB) -> u32 {
                            for (int at = 0; at < 6; ++at) {
                                const int first = TetraEdges[static_cast<usize>(at)][0];
                                const int second = TetraEdges[static_cast<usize>(at)][1];
                                if ((first == cornerA && second == cornerB) ||
                                    (first == cornerB && second == cornerA)) {
                                    return crossing[static_cast<usize>(at)];
                                }
                            }
                            return 0;
                        };

                        const u32 q0 = crossingBetween(insideCorners[0], outsideCorners[0]);
                        const u32 q1 = crossingBetween(insideCorners[0], outsideCorners[1]);
                        const u32 q2 = crossingBetween(insideCorners[1], outsideCorners[0]);
                        const u32 q3 = crossingBetween(insideCorners[1], outsideCorners[1]);

                        emitTriangle(q0, q1, q3);
                        emitTriangle(q0, q3, q2);
                    }
                }
            }
        }
    }

    // The bounds every consumer of an `asset::Mesh` expects to be filled.
    if (!out.mesh.vertices.empty()) {
        Vec3 min = out.mesh.vertices.front().position;
        Vec3 max = min;
        for (const Vertex& vertex : out.mesh.vertices) {
            min.x = std::min(min.x, vertex.position.x);
            min.y = std::min(min.y, vertex.position.y);
            min.z = std::min(min.z, vertex.position.z);
            max.x = std::max(max.x, vertex.position.x);
            max.y = std::max(max.y, vertex.position.y);
            max.z = std::max(max.z, vertex.position.z);
        }
        out.mesh.bounds = core::AABB{min, max};
    }

    // One section covering everything. A per-material split is what a terrain
    // wants eventually and it is not what makes the surface correct, so it is
    // named as absent rather than half-built.
    if (!out.mesh.indices.empty()) {
        Submesh section;
        section.firstIndex = 0;
        section.indexCount = static_cast<u32>(out.mesh.indices.size());
        out.mesh.submeshes.push_back(section);
    }

    return out;
}

} // namespace luaug::asset
