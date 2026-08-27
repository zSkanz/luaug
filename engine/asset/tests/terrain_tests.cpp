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
