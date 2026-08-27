// The terrain field: one signed-distance function, two encodings (ADR 0067).
//
// **What is worth testing here is the seam and the arithmetic**, because both
// are silent when wrong. A sampler that reads the wrong brick still returns a
// number; a mesher built on it produces terrain that is subtly in the wrong
// place, and no assertion anywhere fires.
#include "luaug/asset/terrain.h"

#include <algorithm>
#include <cmath>
#include <doctest/doctest.h>
#include <vector>

using namespace luaug;
using namespace luaug::asset;

namespace {

constexpr float kVoxel = 0.5f;

[[nodiscard]] TerrainField flatField(float height)
{
    TerrainField field(FieldSettings{.voxelSize = kVoxel});
    const std::vector<float> heights(TileArea, height);
    const std::vector<core::u8> materials(TileArea, core::u8{1});
    field.setTile(TileKey{0, 0}, heights, materials);
    return field;
}

} // namespace

TEST_CASE("a lattice point below the ground is inside it, and above it is not")
{
    const TerrainField field = flatField(4.0f);

    // `sd(p) = p.y - H(x, z)`, so the sign is the answer to "am I in the
    // ground", and the magnitude is the distance to it in metres.
    CHECK(static_cast<double>(field.sample(0, 0, 0).distance) == doctest::Approx(-4.0));
    CHECK(static_cast<double>(field.sample(0, 8, 0).distance) == doctest::Approx(0.0));
    CHECK(static_cast<double>(field.sample(0, 16, 0).distance) == doctest::Approx(4.0));

    // **Exactly zero on the surface, not near it.** This is the identity the
    // whole hybrid rests on: a Marching Cubes vertical edge between a sample
    // below and one above crosses at `y = H`, which is the height grid's own
    // vertex -- so the boundary between the two encodings is an equality rather
    // than a stitch. An epsilon here would be the design quietly not holding.
    CHECK(field.sample(0, 8, 0).distance == 0.0f);
}

TEST_CASE("a field with nothing in it is air rather than a hole")
{
    const TerrainField empty(FieldSettings{.voxelSize = kVoxel});

    // Positive everywhere, so a mesher walking it finds no sign change and emits
    // nothing -- which is the right picture of a cell nobody has sculpted. The
    // alternative, returning zero, would put a surface through the whole world.
    CHECK(empty.sample(0, 0, 0).distance > 0.0f);
    CHECK(empty.sample(-500, -500, -500).distance > 0.0f);
    CHECK(empty.tileCount() == 0);
    CHECK(empty.brickCount() == 0);
}

TEST_CASE("a brick answers where one covers the point, and the height layer everywhere else")
{
    TerrainField field = flatField(4.0f);

    // A brick of solid ground at the origin's brick, which on this lattice is
    // `{0, 0, 0}` covering lattice points 0..15 on each axis.
    const std::vector<core::u8> solid(BrickVolume, core::u8{0});
    const std::vector<core::u8> materials(BrickVolume, core::u8{7});
    field.setBrick(BrickKey{0, 0, 0}, solid, materials);

    // Inside the brick the brick answers -- and it says solid where the height
    // layer would have said "four metres of air".
    const FieldSample inBrick = field.sample(2, 14, 2);
    CHECK(inBrick.distance < 0.0f);
    CHECK(inBrick.material == 7);

    // One lattice point above the brick's top, the height layer answers again.
    const FieldSample aboveBrick = field.sample(2, 16, 2);
    CHECK(static_cast<double>(aboveBrick.distance) == doctest::Approx(4.0));
    CHECK(aboveBrick.material == 1);

    CHECK(field.isBricked(2, 2));
    CHECK_FALSE(field.isBricked(20, 2));
}

TEST_CASE("a negative coordinate lands in the brick that contains it")
{
    // **Floor division, not truncation, and this is the case that catches it.**
    // `-1 / 16` is `0` in C++, so a truncating sampler puts lattice point -1 in
    // brick 0 -- and a world with its origin in the middle has negative
    // coordinates everywhere, which would put the left half of every cave one
    // brick to the right.
    TerrainField field(FieldSettings{.voxelSize = kVoxel});
    const std::vector<core::u8> solid(BrickVolume, core::u8{0});
    const std::vector<core::u8> materials(BrickVolume, core::u8{3});

    // Brick -1 covers lattice points -16..-1.
    field.setBrick(BrickKey{-1, -1, -1}, solid, materials);

    CHECK(field.sample(-1, -1, -1).material == 3);
    CHECK(field.sample(-16, -16, -16).material == 3);
    // And 0 is the NEXT brick along, which has nothing in it.
    CHECK(field.sample(0, 0, 0).material == 0);

    // The same for the height layer's tiles, which divide by a different edge.
    const std::vector<float> heights(TileArea, 2.0f);
    const std::vector<core::u8> tileMaterials(TileArea, core::u8{5});
    field.setTile(TileKey{-1, -1}, heights, tileMaterials);
    // Tile -1 covers -32..-1 on each axis, and y is far above the brick.
    CHECK(field.sample(-1, 100, -1).material == 5);
    CHECK(field.sample(-32, 100, -32).material == 5);
    CHECK(field.sample(0, 100, 0).material == 0);
}

TEST_CASE("a local offset inside a brick is the one the sample was taken at")
{
    // A brick whose every voxel carries its own index as a material, so reading
    // the wrong offset is visible rather than plausible. A transposed index --
    // x and z swapped, which is the commonest way to write this wrong -- passes
    // every symmetric test and fails this one.
    TerrainField field(FieldSettings{.voxelSize = kVoxel});
    std::vector<core::u8> distances(BrickVolume, core::u8{0});
    std::vector<core::u8> materials(BrickVolume);
    for (core::u32 y = 0; y < BrickEdge; ++y) {
        for (core::u32 z = 0; z < BrickEdge; ++z) {
            for (core::u32 x = 0; x < BrickEdge; ++x) {
                materials[(y * BrickEdge + z) * BrickEdge + x] = static_cast<core::u8>(x * 3 + z * 5 + y * 7);
            }
        }
    }
    field.setBrick(BrickKey{0, 0, 0}, distances, materials);

    CHECK(field.sample(1, 0, 0).material == 3);
    CHECK(field.sample(0, 0, 1).material == 5);
    CHECK(field.sample(0, 1, 0).material == 7);
    CHECK(field.sample(2, 3, 4).material == static_cast<core::u8>(2 * 3 + 4 * 5 + 3 * 7));
}

TEST_CASE("the distance quantisation round-trips near the surface and saturates far from it")
{
    // Near the surface is where a mesher reads, so that is where the precision
    // has to be. The tolerance is one quantisation step of the stated range.
    const float step = DistanceRange * kVoxel / 128.0f;
    for (const float metres : {-1.0f, -0.5f, 0.0f, 0.25f, 1.0f}) {
        const core::u8 quantised = quantiseDistance(metres, kVoxel);
        CHECK(std::abs(dequantiseDistance(quantised, kVoxel) - metres) <= step);
    }

    // Exactly on the surface is exactly the surface level, which is what makes a
    // sample able to sit ON the isosurface rather than approach it.
    CHECK(quantiseDistance(0.0f, kVoxel) == SurfaceLevel);

    // **Far from the surface saturates rather than wrapping**, and keeps its
    // sign -- which is the only thing a mesher asks of a distant sample.
    CHECK(quantiseDistance(-1000.0f, kVoxel) == 0);
    CHECK(quantiseDistance(1000.0f, kVoxel) == 255);
    CHECK(dequantiseDistance(0, kVoxel) < 0.0f);
    CHECK(dequantiseDistance(255, kVoxel) > 0.0f);
}

TEST_CASE("an edit clones what it touches and leaves the rest shared")
{
    // **This is what makes undo affordable.** `UndoStack` snapshots the whole
    // world by value and keeps 64 of them; a tile is a `shared_ptr<const T>`, so
    // a snapshot copies pointers. An edit that mutated in place would corrupt
    // every snapshot at once, and one that deep-copied would make a snapshot
    // cost megabytes.
    TerrainField field = flatField(4.0f);
    const std::vector<float> other(TileArea, 9.0f);
    const std::vector<core::u8> materials(TileArea, core::u8{2});
    field.setTile(TileKey{1, 0}, other, materials);

    const HeightTile* untouched = field.findTile(TileKey{1, 0});
    REQUIRE(untouched != nullptr);

    // Replace a DIFFERENT tile.
    const std::vector<float> raised(TileArea, 6.0f);
    field.setTile(TileKey{0, 0}, raised, materials);

    // The one nobody edited is the same object, not an equal copy.
    CHECK(field.findTile(TileKey{1, 0}) == untouched);
    // And the edited one took.
    CHECK(static_cast<double>(field.sample(0, 0, 0).distance) == doctest::Approx(-6.0));
}

TEST_CASE("a digest is a function of the contents and the keys, and nothing else")
{
    const TerrainField a = flatField(4.0f);
    const TerrainField b = flatField(4.0f);

    // Same contents built twice: the same digest. A digest that depended on an
    // address or an insertion order would fail here.
    CHECK(a.digest() == b.digest());

    // Different heights: different digest.
    CHECK(flatField(5.0f).digest() != a.digest());

    // **Same tile at a different key: different digest.** A digest over contents
    // alone could not tell two fields apart that hold the same ground in
    // different places, which is a world hash that misses a move.
    TerrainField moved(FieldSettings{.voxelSize = kVoxel});
    const std::vector<float> heights(TileArea, 4.0f);
    const std::vector<core::u8> materials(TileArea, core::u8{1});
    moved.setTile(TileKey{3, 7}, heights, materials);
    CHECK(moved.digest() != a.digest());

    // An empty field has a stable digest of its own rather than a zero that
    // collides with "nobody computed one".
    const TerrainField empty(FieldSettings{.voxelSize = kVoxel});
    CHECK(empty.digest() == TerrainField(FieldSettings{.voxelSize = kVoxel}).digest());
}

TEST_CASE("keys come back sorted, whatever order they went in")
{
    // **R10.** The mesher and the serializer both walk these, and a walk that
    // depended on insertion order would make a saved cell -- and the world hash
    // over it -- a fact about how somebody sculpted rather than about what they
    // sculpted.
    TerrainField field(FieldSettings{.voxelSize = kVoxel});
    const std::vector<float> heights(TileArea, 1.0f);
    const std::vector<core::u8> materials(TileArea, core::u8{1});
    for (const TileKey key : {TileKey{5, 1}, TileKey{-2, 4}, TileKey{5, -3}, TileKey{0, 0}}) {
        field.setTile(key, heights, materials);
    }

    const std::vector<TileKey> keys = field.tileKeys();
    REQUIRE(keys.size() == 4);
    CHECK(std::is_sorted(keys.begin(), keys.end()));
    CHECK(keys.front() == TileKey{-2, 4});

    TerrainField reversed(FieldSettings{.voxelSize = kVoxel});
    for (const TileKey key : {TileKey{0, 0}, TileKey{5, -3}, TileKey{-2, 4}, TileKey{5, 1}}) {
        reversed.setTile(key, heights, materials);
    }
    CHECK(reversed.tileKeys() == keys);
    // And therefore the same digest, which is the property that matters.
    CHECK(reversed.digest() == field.digest());
}

TEST_CASE("removing a brick gives the column back to the height layer")
{
    TerrainField field = flatField(4.0f);
    const std::vector<core::u8> solid(BrickVolume, core::u8{0});
    const std::vector<core::u8> materials(BrickVolume, core::u8{7});
    field.setBrick(BrickKey{0, 0, 0}, solid, materials);
    REQUIRE(field.isBricked(2, 2));
    REQUIRE(field.sample(2, 14, 2).distance < 0.0f);

    field.removeBrick(BrickKey{0, 0, 0});

    CHECK_FALSE(field.isBricked(2, 2));
    CHECK(field.brickCount() == 0);
    // The height layer answers again, and says what it always said.
    CHECK(static_cast<double>(field.sample(2, 14, 2).distance) == doctest::Approx(3.0));
}

// --- The raycast (F1 B4) ----------------------------------------------------

TEST_CASE("a ray fired down at the ground hits it, at the height the ground is")
{
    // **No physics involved, and that is the point rather than an
    // optimisation.** `PhysicsSync::mirror` runs only when the world is paused
    // AND the collision wireframe is on, so an editor sitting in edit mode with
    // that view closed holds no bodies at all -- and a brush asking physics
    // where the ground was would find nothing to hit.
    const TerrainField field = flatField(4.0f);

    const auto hit = raycastField(field, core::DVec3{0.5, 20.0, 0.5}, core::Vec3{0.0f, -1.0f, 0.0f}, 100.0);
    REQUIRE(hit.has_value());
    CHECK(hit->position.y == doctest::Approx(4.0).epsilon(0.01));
    CHECK(hit->distance == doctest::Approx(16.0).epsilon(0.01));
    // Facing up, because the field's gradient does -- the same normal the mesher
    // gives that surface, so a decal placed here sits flush with the triangle.
    CHECK(static_cast<double>(hit->normal.y) > 0.9);
}

TEST_CASE("a ray fired at the sky misses rather than marching for ever")
{
    const TerrainField field = flatField(4.0f);
    CHECK_FALSE(raycastField(field, core::DVec3{0.5, 20.0, 0.5}, core::Vec3{0.0f, 1.0f, 0.0f}, 100.0).has_value());

    // And so does one that runs out of budget before it arrives.
    CHECK_FALSE(raycastField(field, core::DVec3{0.5, 200.0, 0.5}, core::Vec3{0.0f, -1.0f, 0.0f}, 10.0).has_value());
}

TEST_CASE("a ray that starts underground hits at once rather than refusing")
{
    // The case that produces this is a brush dragged into a hillside, or a
    // camera inside terrain. Both want the surface they are already past.
    const TerrainField field = flatField(4.0f);
    const auto hit = raycastField(field, core::DVec3{0.5, 1.0, 0.5}, core::Vec3{0.0f, -1.0f, 0.0f}, 100.0);
    REQUIRE(hit.has_value());
    CHECK(hit->distance == doctest::Approx(0.0));
}

TEST_CASE("a degenerate ray is refused rather than dividing by zero")
{
    const TerrainField field = flatField(4.0f);
    CHECK_FALSE(raycastField(field, core::DVec3{0.0, 20.0, 0.0}, core::Vec3{0.0f, 0.0f, 0.0f}, 100.0).has_value());
    CHECK_FALSE(raycastField(field, core::DVec3{0.0, 20.0, 0.0}, core::Vec3{0.0f, -1.0f, 0.0f}, 0.0).has_value());
}

TEST_CASE("a ray finds a cave's roof before its floor")
{
    // The property that makes the representation worth its cost: a ray entering
    // from above meets the top of the cavity first, which a height field could
    // not express at all.
    TerrainField field(FieldSettings{.voxelSize = 0.5f});
    const std::vector<float> heights(TileArea, 20.0f);
    const std::vector<core::u8> tileMaterials(TileArea, core::u8{1});
    field.setTile(TileKey{0, 0}, heights, tileMaterials);

    // Air in the middle of the brick, solid around it.
    std::vector<core::u8> distances(BrickVolume);
    const std::vector<core::u8> brickMaterials(BrickVolume, core::u8{9});
    for (core::u32 y = 0; y < BrickEdge; ++y) {
        for (core::u32 z = 0; z < BrickEdge; ++z) {
            for (core::u32 x = 0; x < BrickEdge; ++x) {
                const bool hollow = y >= 4 && y < 12;
                distances[(y * BrickEdge + z) * BrickEdge + x] = quantiseDistance(hollow ? 1.0f : -1.0f, 0.5f);
            }
        }
    }
    field.setBrick(BrickKey{0, 0, 0}, distances, brickMaterials);

    // Fired upward from inside the hollow: it should meet the roof at y = 12
    // lattice, which is 6 metres.
    const auto roof = raycastField(field, core::DVec3{4.0, 4.0, 4.0}, core::Vec3{0.0f, 1.0f, 0.0f}, 20.0);
    REQUIRE(roof.has_value());
    CHECK(roof->position.y == doctest::Approx(6.0).epsilon(0.2));
    CHECK(roof->material == 9);
}

// --- Editing the field (F1) --------------------------------------------------

TEST_CASE("adding a ball to empty air makes ground you can stand on")
{
    TerrainField field(FieldSettings{.voxelSize = 0.5f});
    const EditReport report = fillBall(field, core::DVec3{0.0, 0.0, 0.0}, 3.0, 1);

    CHECK(report.touched > 0);
    // The ball's middle is solid and a point well outside it is not.
    CHECK(field.sample(0, 0, 0).distance <= 0.0f);
    CHECK(field.sample(0, 20, 0).distance > 0.0f);
}

TEST_CASE("digging into a hillside from the side makes a cave, and the cave is bricked")
{
    // **The promotion rule, which is the whole hybrid in one test.** A ball
    // removed from INSIDE solid ground leaves air with ground above it, so the
    // column stops being a height function and has to carry voxels.
    TerrainField field(FieldSettings{.voxelSize = 0.5f});
    const std::vector<float> heights(TileArea, 20.0f);
    const std::vector<core::u8> materials(TileArea, core::u8{1});
    field.setTile(TileKey{0, 0}, heights, materials);

    REQUIRE_FALSE(field.isBricked(8, 8));
    const core::usize bricksBefore = field.brickCount();

    // Ten metres down, well below the surface at twenty.
    const EditReport report = fillBall(field, core::DVec3{4.0, 10.0, 4.0}, 2.0, 0);

    CHECK(report.promoted > 0);
    CHECK(field.brickCount() > bricksBefore);
    CHECK(field.isBricked(8, 8));

    // And the cave is air with ground above AND below it, which is what a height
    // function cannot express.
    CHECK(field.sample(8, 20, 8).distance > 0.0f);
    CHECK(field.sample(8, 30, 8).distance <= 0.0f);
    CHECK(field.sample(8, 10, 8).distance <= 0.0f);
}

TEST_CASE("lowering the ground from above stays in the cheap encoding")
{
    // The common edit, and the one that must NOT promote: a ball taken out of
    // the top of a hill leaves one surface, lower down. A design that bricked
    // this would pay voxel prices for ordinary sculpting.
    TerrainField field(FieldSettings{.voxelSize = 0.5f});
    const std::vector<float> heights(TileArea, 10.0f);
    const std::vector<core::u8> materials(TileArea, core::u8{1});
    field.setTile(TileKey{0, 0}, heights, materials);

    const EditReport report = fillBall(field, core::DVec3{4.0, 11.0, 4.0}, 2.5, 0);

    CHECK(report.promoted == 0);
    CHECK(field.brickCount() == 0);
    CHECK_FALSE(field.isBricked(8, 8));

    // And the ground is lower than it was.
    const std::optional<float> after = heightAt(field, 4.0, 4.0);
    REQUIRE(after.has_value());
    CHECK(*after < 10.0f);
}

TEST_CASE("a block is a box rather than a rounded lump")
{
    // `blockDepth` takes the MINIMUM over the three axes, and taking the maximum
    // is the mistake that rounds every corner off. A point near a corner is the
    // one that tells them apart.
    TerrainField field(FieldSettings{.voxelSize = 0.5f});
    fillBlock(field, core::DVec3{0.0, 0.0, 0.0}, core::Vec3{8.0f, 8.0f, 8.0f}, 1);

    // The middle, a face, and a corner are all inside a box.
    CHECK(field.sample(0, 0, 0).distance <= 0.0f);
    CHECK(field.sample(6, 0, 0).distance <= 0.0f);
    CHECK(field.sample(6, 6, 6).distance <= 0.0f);
    // And just outside the corner is not.
    CHECK(field.sample(10, 10, 10).distance > 0.0f);
}

TEST_CASE("heightAt answers about the height layer and admits when it cannot")
{
    TerrainField field(FieldSettings{.voxelSize = 0.5f});
    CHECK_FALSE(heightAt(field, 0.0, 0.0).has_value());

    const std::vector<float> heights(TileArea, 7.5f);
    const std::vector<core::u8> materials(TileArea, core::u8{1});
    field.setTile(TileKey{0, 0}, heights, materials);

    const std::optional<float> found = heightAt(field, 2.0, 2.0);
    REQUIRE(found.has_value());
    CHECK(static_cast<double>(*found) == doctest::Approx(7.5).epsilon(0.001));

    // Outside any tile is still nothing rather than zero, because zero is a
    // legitimate height and "no ground" is not a height at all.
    CHECK_FALSE(heightAt(field, 1000.0, 1000.0).has_value());
}

TEST_CASE("an edit is deterministic: the same brush on the same field twice agrees")
{
    // **R10.** Every column is examined before any is written and the columns are
    // visited in a fixed order, precisely so a promotion does not depend on the
    // order voxels happened to be reached -- and a promotion decides which
    // encoding a column is in, which is hashed.
    const auto sculpted = [] {
        TerrainField field(FieldSettings{.voxelSize = 0.5f});
        const std::vector<float> heights(TileArea, 12.0f);
        const std::vector<core::u8> materials(TileArea, core::u8{1});
        field.setTile(TileKey{0, 0}, heights, materials);
        fillBall(field, core::DVec3{4.0, 6.0, 4.0}, 2.0, 0);
        fillBall(field, core::DVec3{6.0, 13.0, 4.0}, 1.5, 2);
        return field;
    };

    CHECK(sculpted().digest() == sculpted().digest());
}

TEST_CASE("compact gives a column back only when its bricks carry nothing")
{
    // **Nothing does this automatically**, which is the decision: the
    // representation is part of the world's state, so a field that recompacted
    // itself would be a world that changed when nobody touched it.
    TerrainField field(FieldSettings{.voxelSize = 0.5f});
    const std::vector<float> heights(TileArea, 20.0f);
    const std::vector<core::u8> materials(TileArea, core::u8{1});
    field.setTile(TileKey{0, 0}, heights, materials);

    fillBall(field, core::DVec3{4.0, 10.0, 4.0}, 2.0, 0);
    const core::usize withCave = field.brickCount();
    REQUIRE(withCave > 0);

    // **Nothing is redundant, because the edit no longer writes redundant
    // bricks.** The first version of this test expected that and got three of
    // four bricks reclaimed -- the examined column runs a brick above and below
    // the brush, so promoting used to allocate levels that only repeated the
    // height layer. `writeBricks` skips creating those now, and `compact` is
    // left as the verb for reclaiming what LATER edits made redundant rather
    // than as a required step after every dig.
    CHECK(compact(field) == 0);
    CHECK(field.brickCount() == withCave);

    // And the cave survives, which is the half a reclaim count cannot say.
    CHECK(field.sample(8, 20, 8).distance > 0.0f);
    CHECK(field.sample(8, 30, 8).distance <= 0.0f);
}
