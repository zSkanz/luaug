#include "luaug/asset/terrain_mesher.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <unordered_map>
#include <vector>

namespace luaug::asset {
namespace {

using core::i32;
using core::u32;
using core::u8;
using core::usize;
using core::Vec3;

// The eight corners of a lattice cell, indexed so that bit 0 is x, bit 1 is y
// and bit 2 is z. Written as a table rather than computed inline because every
// cell's edges index it, and one transposed literal here would be a surface
// subtly in the wrong place everywhere.
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

// One sampled lattice point.
struct Corner
{
    i32 x = 0;
    i32 y = 0;
    i32 z = 0;
    float distance = 0.0f;
    core::u8 material = 0;
};

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
    // has to be made consistently, because two cells disagreeing about a
    // shared corner is a crack.
    const auto solid = [](const Corner& corner) noexcept { return corner.distance <= 0.0f; };

    // **The lattice, sampled once.**
    //
    // Every cell wants its eight corners and every corner is shared by eight
    // cells, so sampling per cell asks the field for the same point eight times
    // -- 262,000 lookups for a 32-cubed region where 36,000 would do. Each
    // lookup is two binary searches and a pair of floor-divisions, and together
    // they were most of the ten milliseconds a tile cost to mesh.
    //
    // Read straight through, in x-fastest order, which is also the order the
    // walk below asks for them in.
    //
    // The gradient reaches one step OUTSIDE this grid at its faces, and that
    // falls back to the field -- a surface of the region's own edge rather than
    // a special case in the hot path.
    const usize gridX = static_cast<usize>(region.cellsX) + 1;
    const usize gridY = static_cast<usize>(region.cellsY) + 1;
    const usize gridZ = static_cast<usize>(region.cellsZ) + 1;
    std::vector<FieldSample> lattice(gridX * gridY * gridZ);
    for (usize iz = 0; iz < gridZ; ++iz) {
        for (usize iy = 0; iy < gridY; ++iy) {
            for (usize ix = 0; ix < gridX; ++ix) {
                lattice[(iz * gridY + iy) * gridX + ix] =
                    field.sample(region.minX + static_cast<i32>(ix) * static_cast<i32>(stride),
                                 region.minY + static_cast<i32>(iy) * static_cast<i32>(stride),
                                 region.minZ + static_cast<i32>(iz) * static_cast<i32>(stride));
            }
        }
    }

    // A lattice point's sample, from the grid when it is in it and from the
    // field when it is not.
    const auto sampleOf = [&](i32 x, i32 y, i32 z) noexcept -> FieldSample {
        const i32 dx = x - region.minX;
        const i32 dy = y - region.minY;
        const i32 dz = z - region.minZ;
        const auto step = static_cast<i32>(stride);
        if (dx < 0 || dy < 0 || dz < 0 || dx % step != 0 || dy % step != 0 || dz % step != 0) {
            return field.sample(x, y, z);
        }
        const auto ix = static_cast<usize>(dx / step);
        const auto iy = static_cast<usize>(dy / step);
        const auto iz = static_cast<usize>(dz / step);
        if (ix >= gridX || iy >= gridY || iz >= gridZ) {
            return field.sample(x, y, z);
        }
        return lattice[(iz * gridY + iy) * gridX + ix];
    };

    const auto sampleAt = [&](i32 x, i32 y, i32 z) noexcept {
        const FieldSample got = sampleOf(x, y, z);
        return Corner{x, y, z, got.distance, got.material};
    };

    // The gradient, by central differences, which is the surface normal. Read
    // from the FIELD rather than computed from the triangle: a triangle normal
    // is faceted and a field gradient is smooth, and the gradient is also what
    // decides the winding below.
    const auto gradientAt = [&](i32 x, i32 y, i32 z) noexcept {
        const auto step = static_cast<i32>(stride);
        const float dx = sampleOf(x + step, y, z).distance - sampleOf(x - step, y, z).distance;
        const float dy = sampleOf(x, y + step, z).distance - sampleOf(x, y - step, z).distance;
        const float dz = sampleOf(x, y, z + step).distance - sampleOf(x, y, z - step).distance;
        const Vec3 gradient{dx, dy, dz};
        const float length = std::sqrt(gradient.x * gradient.x + gradient.y * gradient.y + gradient.z * gradient.z);
        if (length < 1e-8f) {
            return Vec3{0.0f, 1.0f, 0.0f};
        }
        return Vec3{gradient.x / length, gradient.y / length, gradient.z / length};
    };

    // What each vertex is the surface of, parallel to `out.mesh.vertices`. Not a
    // member of `Vertex`: that struct is a GPU buffer layout and its size is
    // asserted, so a material byte in it would change every world shader.
    std::vector<u8> vertexMaterial;

    // Triangles by material, ordered so the sections come out the same way on
    // every machine.
    std::map<u8, std::vector<u32>> buckets;

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

        // **Bucketed by material rather than appended**, which is what makes a
        // painted hillside look painted: one section per material, each drawn
        // with its own colour. The triangle takes the majority of its three
        // vertices, and a three-way tie takes the lowest id -- an arbitrary rule
        // that has to be a rule, because a tie broken by iteration order would
        // put the visit order into the mesh (R10).
        const u8 ma = vertexMaterial[a];
        const u8 mb = vertexMaterial[second];
        const u8 mc = vertexMaterial[third];
        u8 material = ma;
        if (mb == mc && mb != ma) {
            material = mb;
        }
        else if (ma != mb && ma != mc && mb != mc) {
            material = std::min({ma, mb, mc});
        }

        std::vector<u32>& bucket = buckets[material];
        bucket.push_back(a);
        bucket.push_back(second);
        bucket.push_back(third);

        // **The collider is one surface and stays one.** What a body stands on
        // does not depend on what it is made of, and splitting it would build a
        // separate `TriangleMesh` per material for no gain.
        out.colliderIndices.push_back(a);
        out.colliderIndices.push_back(second);
        out.colliderIndices.push_back(third);
    };

    // **Surface nets: one vertex per cell the surface passes through, one quad
    // per lattice edge it crosses.**
    //
    // This replaced marching tetrahedra, and a person looking at a cave wall is
    // why. Six tetrahedra around each cube's main diagonal put that diagonal
    // into the surface: every curved wall came out as a zig-zag of long thin
    // triangles leaning the same way, and interpolated normals could soften the
    // shading but not the silhouette. A surface net places each vertex at the
    // average of the crossings on its cell's twelve edges -- the centre of the
    // little patch of surface inside the cell -- and joins the four cells
    // around every crossed edge with a quad. The result follows the field
    // without a preferred direction, and has roughly a third of the triangles.
    //
    // What it gives up is exact vertices on the lattice's edges, which marching
    // methods have: two regions meshed side by side do not share vertices along
    // their seam. Nothing here needs them to -- a cave region overlaps the
    // ground it replaces by a cell -- and the collider tolerates the overlap.
    const usize cellsX = region.cellsX;
    const usize cellsY = region.cellsY;
    const usize cellsZ = region.cellsZ;
    constexpr u32 NoVertex = 0xFFFFFFFFu;
    std::vector<u32> cellVertex(cellsX * cellsY * cellsZ, NoVertex);
    const auto cellIndex = [&](usize x, usize y, usize z) noexcept { return (z * cellsY + y) * cellsX + x; };
    const auto latticeSolid = [&](usize x, usize y, usize z) noexcept {
        return lattice[(z * gridY + y) * gridX + x].distance <= 0.0f;
    };

    // The twelve edges of a cell, as corner-index pairs (bit 0 x, bit 1 y,
    // bit 2 z).
    constexpr std::array<std::array<int, 2>, 12> CellEdges{{
        {0, 1},
        {2, 3},
        {4, 5},
        {6, 7}, // along x
        {0, 2},
        {1, 3},
        {4, 6},
        {5, 7}, // along y
        {0, 4},
        {1, 5},
        {2, 6},
        {3, 7}, // along z
    }};

    for (usize cellZ = 0; cellZ < cellsZ; ++cellZ) {
        for (usize cellY = 0; cellY < cellsY; ++cellY) {
            for (usize cellX = 0; cellX < cellsX; ++cellX) {
                const i32 baseX = region.minX + static_cast<i32>(cellX) * stride;
                const i32 baseY = region.minY + static_cast<i32>(cellY) * stride;
                const i32 baseZ = region.minZ + static_cast<i32>(cellZ) * stride;

                std::array<Corner, 8> corners{};
                int inside = 0;
                for (int at = 0; at < 8; ++at) {
                    corners[static_cast<usize>(at)] =
                        sampleAt(baseX + CornerOffsets[static_cast<usize>(at)][0] * stride,
                                 baseY + CornerOffsets[static_cast<usize>(at)][1] * stride,
                                 baseZ + CornerOffsets[static_cast<usize>(at)][2] * stride);
                    if (solid(corners[static_cast<usize>(at)]))
                        inside |= 1 << at;
                }
                if (inside == 0 || inside == 0xFF)
                    continue; // Wholly solid or wholly air: no surface here.

                // The vertex: the mean of the crossings, and the gradient
                // interpolated to each crossing, summed.
                float sumX = 0.0f;
                float sumY = 0.0f;
                float sumZ = 0.0f;
                Vec3 normalSum{0.0f, 0.0f, 0.0f};
                int crossings = 0;
                for (const std::array<int, 2>& edge : CellEdges) {
                    const Corner& a = corners[static_cast<usize>(edge[0])];
                    const Corner& b = corners[static_cast<usize>(edge[1])];
                    if (solid(a) == solid(b))
                        continue;
                    const float t = crossingAt(a.distance, b.distance);
                    sumX += static_cast<float>(a.x) + (static_cast<float>(b.x) - static_cast<float>(a.x)) * t;
                    sumY += static_cast<float>(a.y) + (static_cast<float>(b.y) - static_cast<float>(a.y)) * t;
                    sumZ += static_cast<float>(a.z) + (static_cast<float>(b.z) - static_cast<float>(a.z)) * t;
                    const Vec3 ga = gradientAt(a.x, a.y, a.z);
                    const Vec3 gb = gradientAt(b.x, b.y, b.z);
                    normalSum.x += ga.x + (gb.x - ga.x) * t;
                    normalSum.y += ga.y + (gb.y - ga.y) * t;
                    normalSum.z += ga.z + (gb.z - ga.z) * t;
                    ++crossings;
                }
                const float inverse = 1.0f / static_cast<float>(crossings);
                const Vec3 position{sumX * inverse * voxel, sumY * inverse * voxel, sumZ * inverse * voxel};
                const float normalLength =
                    std::sqrt(normalSum.x * normalSum.x + normalSum.y * normalSum.y + normalSum.z * normalSum.z);
                const Vec3 normal = normalLength < 1e-8f ? Vec3{0.0f, 1.0f, 0.0f}
                                                         : Vec3{normalSum.x / normalLength, normalSum.y / normalLength,
                                                                normalSum.z / normalLength};

                // **What the cell is made of: the commonest material among its
                // SOLID corners**, lowest id on a tie. The air corners' material
                // is whatever the ground there used to be, and a hole's colour on
                // the wall around it is the mistake that rule avoids.
                std::array<u8, 8> seen{};
                std::array<int, 8> votes{};
                int kinds = 0;
                for (int at = 0; at < 8; ++at) {
                    if ((inside & (1 << at)) == 0)
                        continue;
                    const u8 material = corners[static_cast<usize>(at)].material;
                    int slot = 0;
                    while (slot < kinds && seen[static_cast<usize>(slot)] != material)
                        ++slot;
                    if (slot == kinds) {
                        seen[static_cast<usize>(kinds)] = material;
                        ++kinds;
                    }
                    ++votes[static_cast<usize>(slot)];
                }
                u8 material = seen[0];
                int best = votes[0];
                for (int slot = 1; slot < kinds; ++slot) {
                    const u8 candidate = seen[static_cast<usize>(slot)];
                    const int count = votes[static_cast<usize>(slot)];
                    if (count > best || (count == best && candidate < material)) {
                        material = candidate;
                        best = count;
                    }
                }

                Vertex vertex;
                vertex.position = position;
                vertex.normal = normal;
                // **Triplanar UVs are the terrain's business and not this
                // function's.** What goes here is the world position on the two
                // axes the normal is least aligned with, which is the same answer
                // for the same point however it was reached.
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

                cellVertex[cellIndex(cellX, cellY, cellZ)] = static_cast<u32>(out.mesh.vertices.size());
                out.mesh.vertices.push_back(vertex);
                out.colliderPoints.push_back(position);
                vertexMaterial.push_back(material);
            }
        }
    }

    // The quads. A lattice edge the surface crosses is shared by four cells,
    // and their four vertices are the quad around it -- emitted only where all
    // four cells are inside the region. `emitTriangle` derives each triangle's
    // winding from the field, so the order around the edge does not matter.
    const auto quad = [&](usize x0, usize y0, usize z0, usize x1, usize y1, usize z1, usize x2, usize y2, usize z2,
                          usize x3, usize y3, usize z3) {
        const u32 a = cellVertex[cellIndex(x0, y0, z0)];
        const u32 b = cellVertex[cellIndex(x1, y1, z1)];
        const u32 c = cellVertex[cellIndex(x2, y2, z2)];
        const u32 d = cellVertex[cellIndex(x3, y3, z3)];
        if (a == NoVertex || b == NoVertex || c == NoVertex || d == NoVertex)
            return;
        emitTriangle(a, b, c);
        emitTriangle(a, c, d);
    };
    for (usize z = 0; z < gridZ; ++z) {
        for (usize y = 0; y < gridY; ++y) {
            for (usize x = 0; x < gridX; ++x) {
                const bool here = latticeSolid(x, y, z);
                // Along x: the cells around it are y-1..y by z-1..z.
                if (x < cellsX && y >= 1 && y < cellsY && z >= 1 && z < cellsZ && here != latticeSolid(x + 1, y, z))
                    quad(x, y - 1, z - 1, x, y, z - 1, x, y, z, x, y - 1, z);
                // Along y: x-1..x by z-1..z.
                if (y < cellsY && x >= 1 && x < cellsX && z >= 1 && z < cellsZ && here != latticeSolid(x, y + 1, z))
                    quad(x - 1, y, z - 1, x, y, z - 1, x, y, z, x - 1, y, z);
                // Along z: x-1..x by y-1..y.
                if (z < cellsZ && x >= 1 && x < cellsX && y >= 1 && y < cellsY && here != latticeSolid(x, y, z + 1))
                    quad(x - 1, y - 1, z, x, y - 1, z, x, y, z, x - 1, y, z);
            }
        }
    }

    // **Skirts, hung from every edge the region's own side cuts.**
    //
    // A boundary edge is one exactly one triangle uses; in a surface cut by a
    // box, those are the edges on the box's faces. Only the four SIDE faces
    // matter -- a skirt on the top or bottom face would hang into the surface
    // it belongs to -- so an edge counts only when both its ends lie on the same
    // side plane.
    //
    // Emitted both windings, because which side of a skirt faces the camera
    // depends on which neighbour is the coarse one, and a skirt culled from the
    // side that matters is no skirt.
    // The buckets and not `out.mesh.indices`: the index buffer is assembled
    // from them further down, so at this point it is still empty.
    if (region.skirt > 0.0f && !buckets.empty()) {
        const auto step = static_cast<float>(stride) * voxel;
        const float sideMinX = static_cast<float>(region.minX) * voxel;
        const float sideMinZ = static_cast<float>(region.minZ) * voxel;
        const float sideMaxX = sideMinX + static_cast<float>(region.cellsX) * step;
        const float sideMaxZ = sideMinZ + static_cast<float>(region.cellsZ) * step;
        const float tolerance = voxel * 1e-3f;

        // Which side plane a point lies on, or -1.
        const auto sideOf = [&](const Vec3& p) noexcept -> int {
            if (std::abs(p.x - sideMinX) < tolerance)
                return 0;
            if (std::abs(p.x - sideMaxX) < tolerance)
                return 1;
            if (std::abs(p.z - sideMinZ) < tolerance)
                return 2;
            if (std::abs(p.z - sideMaxZ) < tolerance)
                return 3;
            return -1;
        };

        // Edge use counts, keyed by the ordered index pair. Only read by key
        // and then walked in the ORDER THE TRIANGLES WERE EMITTED below, so the
        // container's own order never reaches the mesh.
        std::unordered_map<core::u64, u32> uses;
        const auto edgeKey = [](u32 a, u32 b) noexcept {
            const u32 lo = std::min(a, b);
            const u32 hi = std::max(a, b);
            return (static_cast<core::u64>(lo) << 32) | hi;
        };
        for (const auto& entry : buckets) {
            const std::vector<u32>& list = entry.second;
            for (usize at = 0; at + 2 < list.size(); at += 3) {
                ++uses[edgeKey(list[at], list[at + 1])];
                ++uses[edgeKey(list[at + 1], list[at + 2])];
                ++uses[edgeKey(list[at + 2], list[at])];
            }
        }

        // The lowered copy of a vertex, made once per vertex.
        std::unordered_map<u32, u32> lowered;
        const auto lowerOf = [&](u32 index) {
            if (const auto at = lowered.find(index); at != lowered.end())
                return at->second;
            // **Against the surface normal, not straight down.** Straight down
            // is the textbook skirt and it only covers the crack a HEIGHT FIELD
            // has, which is a vertical gap under a horizontal edge. A volume
            // has cliffs, and the crack between two levels on a cliff face is a
            // horizontal gap that a skirt hanging downward runs parallel to and
            // never fills -- the first render with levels on showed exactly that,
            // a sliver of sky down the side of the example's plateau. Hung
            // against the normal, the same strip goes down under flat ground and
            // into the rock behind a cliff.
            Vertex copy = out.mesh.vertices[index];
            copy.position.x -= copy.normal.x * region.skirt;
            copy.position.y -= copy.normal.y * region.skirt;
            copy.position.z -= copy.normal.z * region.skirt;
            const auto made = static_cast<u32>(out.mesh.vertices.size());
            out.mesh.vertices.push_back(copy);
            vertexMaterial.push_back(vertexMaterial[index]);
            lowered.emplace(index, made);
            return made;
        };

        for (auto& entry : buckets) {
            std::vector<u32>& list = entry.second;
            const usize triangles = list.size();
            for (usize at = 0; at + 2 < triangles; at += 3) {
                const u32 corners[3] = {list[at], list[at + 1], list[at + 2]};
                for (int edge = 0; edge < 3; ++edge) {
                    const u32 a = corners[edge];
                    const u32 b = corners[(edge + 1) % 3];
                    if (uses[edgeKey(a, b)] != 1)
                        continue;
                    const int side = sideOf(out.mesh.vertices[a].position);
                    if (side < 0 || side != sideOf(out.mesh.vertices[b].position))
                        continue;
                    const u32 la = lowerOf(a);
                    const u32 lb = lowerOf(b);
                    list.insert(list.end(), {a, b, lb, a, lb, la});
                    list.insert(list.end(), {a, lb, b, a, la, lb});
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

    // **One section per material, in id order.** A `std::map` rather than an
    // unordered one for the reason everything in this module is ordered: the
    // section order reaches a GPU buffer and a world hash, and an allocator's
    // iteration order is not a fact about the world (R10).
    //
    // The index buffer is assembled here rather than as it goes, because a
    // section is a contiguous run and triangles arrive interleaved -- a cell
    // straddling grass and rock emits both within three lines of each other.
    for (const auto& entry : buckets) {
        if (entry.second.empty()) {
            continue;
        }
        Submesh section;
        section.firstIndex = static_cast<u32>(out.mesh.indices.size());
        section.indexCount = static_cast<u32>(entry.second.size());
        out.mesh.indices.insert(out.mesh.indices.end(), entry.second.begin(), entry.second.end());
        out.mesh.submeshes.push_back(section);
        out.sectionMaterials.push_back(entry.first);
    }

    return out;
}

} // namespace luaug::asset
