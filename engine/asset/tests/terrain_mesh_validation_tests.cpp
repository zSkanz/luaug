// **The terrain mesh, proved rather than looked at** (the owner's terrain
// report, 2026-09-23).
//
// A second opinion drawn from a video listed what the interior of the terrain
// looked like -- black regions, surfaces vanishing at some angles, stretched
// faces, possible cracks -- and asked for each hypothesis to be checked against
// the implementation rather than believed: inverted winding, degenerate or
// duplicated triangles, holes, seams between chunks, normals that disagree with
// their faces. Every one of those is a property of the triangles, so every one
// is asserted here, over the controlled cases the report named: flat ground, a
// slope, a hill, a ball added and a ball taken away, a tunnel, a cave, a
// vertical wall, an overhang and a ball across a chunk boundary.
//
// The surface of a field whose ground is all inside the meshed box is closed,
// so the whole of it is meshed region by region -- exactly as the renderer and
// the colliders cut it -- and the union is checked as one surface.
#include "luaug/asset/terrain_mesher.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <doctest/doctest.h>
#include <functional>
#include <map>
#include <string>
#include <tuple>
#include <vector>

using namespace luaug;
using namespace luaug::asset;

namespace {

[[nodiscard]] FieldSettings settingsOf()
{
    return FieldSettings{.voxelSize = 1.0f, .minHeight = -64.0f, .maxHeight = 64.0f};
}

// A slab of ground 40 m square and 16 m deep, top at zero, well inside the box
// the checks mesh -- so every surface it has, walls and bottom included, is
// closed.
[[nodiscard]] TerrainField slab()
{
    TerrainField field(settingsOf());
    (void)fillBlock(field, core::DVec3{0.0, -8.0, 0.0}, core::Vec3{40.0f, 16.0f, 40.0f}, 1);
    return field;
}

using Point = std::tuple<float, float, float>;

[[nodiscard]] Point snap(const core::Vec3& p)
{
    const auto at = [](float v) { return std::round(v * 1024.0f) / 1024.0f; };
    return {at(p.x), at(p.y), at(p.z)};
}

[[nodiscard]] core::Vec3 sub(const core::Vec3& a, const core::Vec3& b)
{
    return core::Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

struct Findings
{
    std::size_t triangles = 0;
    std::size_t degenerate = 0;
    std::size_t duplicated = 0;
    // Edges used by other than exactly two triangles: a hole (one) or a fin
    // (three or more).
    std::size_t openEdges = 0;
    std::size_t overusedEdges = 0;
    // Edges whose two triangles traverse them in the same direction, which is
    // what one triangle wound backwards against its neighbour looks like.
    std::size_t inconsistentEdges = 0;
    // Triangles whose winding faces away from the normals of their own
    // vertices, which is what "inside out" looks like to the shader.
    std::size_t facingAway = 0;
    std::size_t badNormals = 0;
    // The worst triangle's longest side over its height to that side: 1.15 for
    // an equilateral one, and large for a sliver.
    float worstAspect = 0.0f;
    // How many triangles are slivers by that measure (over 12).
    std::size_t slivers = 0;
};

// Meshes every region of the box [-64, 64) on x and z and [-96, 64) on y, 32
// lattice points a side, and checks the union. Below the world's floor on y,
// as the renderer's runs are: ground laid down to the floor has its bottom
// between the floor's voxel and the one under it.
[[nodiscard]] Findings check(const TerrainField& field)
{
    Findings found;
    std::map<std::tuple<Point, Point, Point>, int> seen;
    // Directed edge -> uses; the undirected count is the two directions summed.
    std::map<std::pair<Point, Point>, int> directed;

    for (core::i32 z = -64; z < 64; z += 32) {
        for (core::i32 y = -96; y < 64; y += 32) {
            for (core::i32 x = -64; x < 64; x += 32) {
                const MeshRegion region{
                    .minX = x, .minY = y, .minZ = z, .cellsX = 32, .cellsY = 32, .cellsZ = 32, .level = 0};
                const TerrainMesh meshed = meshField(field, region);
                const std::vector<Vertex>& vertices = meshed.mesh.vertices;
                const std::vector<core::u32>& indices = meshed.mesh.indices;
                for (const Vertex& vertex : vertices) {
                    if (std::abs(core::length(vertex.normal) - 1.0f) > 1e-3f)
                        ++found.badNormals;
                }
                for (std::size_t at = 0; at + 2 < indices.size(); at += 3) {
                    const Vertex& a = vertices[indices[at]];
                    const Vertex& b = vertices[indices[at + 1]];
                    const Vertex& c = vertices[indices[at + 2]];
                    ++found.triangles;

                    const core::Vec3 face = core::cross(sub(b.position, a.position), sub(c.position, a.position));
                    if (core::length(face) < 1e-6f) {
                        ++found.degenerate;
                    }
                    else {
                        const float ab = core::length(sub(b.position, a.position));
                        const float bc = core::length(sub(c.position, b.position));
                        const float ca = core::length(sub(a.position, c.position));
                        const float longest = std::max({ab, bc, ca});
                        const float aspect = longest * longest / core::length(face);
                        found.worstAspect = std::max(found.worstAspect, aspect);
                        if (aspect > 12.0f)
                            ++found.slivers;
                        const core::Vec3 average{a.normal.x + b.normal.x + c.normal.x,
                                                 a.normal.y + b.normal.y + c.normal.y,
                                                 a.normal.z + b.normal.z + c.normal.z};
                        if (core::dot(face, average) <= 0.0f)
                            ++found.facingAway;
                    }

                    std::array<Point, 3> corners{snap(a.position), snap(b.position), snap(c.position)};
                    for (int e = 0; e < 3; ++e)
                        directed[{corners[static_cast<std::size_t>(e)],
                                  corners[static_cast<std::size_t>((e + 1) % 3)]}] += 1;
                    std::sort(corners.begin(), corners.end());
                    if (++seen[{corners[0], corners[1], corners[2]}] == 2)
                        ++found.duplicated;
                }
            }
        }
    }

    for (const auto& [edge, uses] : directed) {
        if (edge.first > edge.second)
            continue;
        const auto reverse = directed.find({edge.second, edge.first});
        const int back = reverse == directed.end() ? 0 : reverse->second;
        const int total = uses + back;
        if (total < 2)
            ++found.openEdges;
        else if (total > 2)
            ++found.overusedEdges;
        else if (uses != 1)
            ++found.inconsistentEdges;
    }
    for (const auto& [edge, uses] : directed) {
        if (edge.first < edge.second)
            continue;
        if (directed.find({edge.second, edge.first}) == directed.end()) {
            // Only this direction exists, so the loop above never saw it.
            if (uses < 2)
                ++found.openEdges;
            else if (uses > 2)
                ++found.overusedEdges;
            else
                ++found.inconsistentEdges;
        }
    }
    return found;
}

// `maxAspect`: the worst triangle allowed, by `Findings::worstAspect`. Six for
// every ordinary shape -- quads are split along their shorter diagonal, which
// took the worst of these cases from 6 to 3.6 -- and looser only where a test
// says why.
void expectClean(const std::string& what, const TerrainField& field, float maxAspect = 6.0f)
{
    const Findings found = check(field);
    CAPTURE(what);
    CAPTURE(found.triangles);
    CAPTURE(found.worstAspect);
    REQUIRE(found.triangles > 100);
    CHECK(found.worstAspect < maxAspect);
    // A sliver is rare where it happens at all: at most one triangle in five
    // hundred, which is where the crease cases below land.
    CHECK(found.slivers * 500 <= found.triangles);
    CHECK(found.degenerate == 0);
    CHECK(found.duplicated == 0);
    CHECK(found.openEdges == 0);
    CHECK(found.overusedEdges == 0);
    CHECK(found.inconsistentEdges == 0);
    CHECK(found.facingAway == 0);
    CHECK(found.badNormals == 0);
}

} // namespace

TEST_CASE("flat ground, walls and a bottom make one closed surface, wound one way")
{
    expectClean("flat", slab());
}

TEST_CASE("a slope is a closed surface, wound one way")
{
    TerrainField field(settingsOf());
    constexpr core::u32 Columns = 40;
    std::vector<float> heights(Columns * Columns);
    for (core::u32 z = 0; z < Columns; ++z) {
        for (core::u32 x = 0; x < Columns; ++x)
            heights[z * Columns + x] = 0.6f * (static_cast<float>(x) - 20.0f);
    }
    (void)writeHeights(field, -20, -20, Columns, heights, 1);
    expectClean("slope", field);
}

TEST_CASE("a hill, a ball added and a ball taken away are closed surfaces, wound one way")
{
    TerrainField hill = slab();
    (void)growBall(hill, core::DVec3{0.0, 0.0, 0.0}, 10.0, 2.0f);
    (void)growBall(hill, core::DVec3{0.0, 1.0, 0.0}, 10.0, 2.0f);
    expectClean("hill", hill);

    TerrainField added = slab();
    (void)fillBall(added, core::DVec3{4.0, 0.0, -3.0}, 5.0, 1);
    expectClean("ball added", added);

    TerrainField taken = slab();
    (void)fillBall(taken, core::DVec3{-3.0, 0.0, 5.0}, 5.0, 0);
    expectClean("ball taken away", taken);
}

TEST_CASE("a tunnel and a cave are closed surfaces, wound one way, inside as well as out")
{
    TerrainField tunnel = slab();
    for (int step = -24; step <= 0; step += 2)
        (void)fillBall(tunnel, core::DVec3{static_cast<double>(step), -6.0, 1.0}, 3.0, 0);
    expectClean("tunnel", tunnel);

    TerrainField cave = slab();
    (void)fillBall(cave, core::DVec3{2.0, -8.0, -2.0}, 5.5, 0);
    expectClean("cave", cave);
}

TEST_CASE("a vertical wall and an overhang are closed surfaces, wound one way")
{
    TerrainField wall(settingsOf());
    (void)fillBlock(wall, core::DVec3{-10.0, -4.0, 0.0}, core::Vec3{20.0f, 24.0f, 30.0f}, 1);
    expectClean("wall", wall);

    TerrainField overhang = slab();
    (void)fillBall(overhang, core::DVec3{20.0, -1.0, 0.0}, 4.0, 1);
    expectClean("overhang", overhang);
}

TEST_CASE("a ball across a chunk boundary is one surface, with no seam")
{
    TerrainField field = slab();
    // Centred on the corner of four chunks, where x, y and z all cross one.
    (void)fillBall(field, core::DVec3{0.0, 0.0, 0.0}, 6.0, 0);
    // And a ball of ground floated inside that hole: a union of two shapes,
    // whose field is the larger of two ramps and so has a crease where they
    // meet. Surface nets puts a cell's vertex at the mean of its crossings, and
    // along a crease those crowd, so a couple of triangles in twelve thousand
    // come out thin (an aspect near 27). Closed, wound one way and shaded by
    // the field's gradient all the same.
    (void)fillBall(field, core::DVec3{0.37, -0.61, 0.43}, 3.0, 1);
    expectClean("chunk boundary", field, 30.0f);
}

TEST_CASE("a surface through the voxel centres themselves is closed and wound, if thin in places")
{
    // **The one shape surface nets cannot give sound triangles, stated.** A
    // ball centred ON a voxel centre with a whole radius puts its surface
    // through lattice samples, so the crossings of four neighbouring cells meet
    // within a hundredth of a voxel of one point and their vertices crowd it:
    // slivers of a few centimetres, at an aspect near 28. The topology is
    // unharmed -- closed, wound one way, no duplicate -- and the normals come
    // from the field's gradient rather than from these triangles, so they
    // shade as the surface around them does. Moving the vertices apart would
    // move the surface off where the field puts it, which the collider and the
    // raycast would then disagree with; this records the cost instead.
    TerrainField field = slab();
    (void)fillBall(field, core::DVec3{0.0, 0.0, 0.0}, 6.0, 0);
    (void)fillBall(field, core::DVec3{0.5, -0.5, 0.5}, 3.0, 1);
    expectClean("through the lattice", field, 30.0f);
}
