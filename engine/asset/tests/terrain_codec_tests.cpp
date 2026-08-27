// Saving the ground (F1).
//
// **A sculpted world is somebody's afternoon**, and until this existed a scene
// save wrote every instance in the world and none of the terrain.
#include "luaug/asset/terrain.h"

#include <doctest/doctest.h>

using namespace luaug;
using namespace luaug::asset;

namespace {

[[nodiscard]] TerrainField sculpted()
{
    TerrainField field(FieldSettings{.voxelSize = 0.5f, .minHeight = -32.0f, .maxHeight = 32.0f});
    // Ground that reaches the floor -- height tiles -- and a cave through it,
    // which is what puts voxel bricks in the field. Both encodings, so the codec
    // is asserted against the shape the format actually has to carry.
    fillBlock(field, core::DVec3{0.0, -20.0, 0.0}, core::Vec3{48.0f, 40.0f, 48.0f}, 1);
    fillBall(field, core::DVec3{0.0, -3.0, 0.0}, 4.0, 0);
    fillBall(field, core::DVec3{6.0, 1.0, 3.0}, 5.0, 2);
    return field;
}

} // namespace

TEST_CASE("a field survives a round trip byte for byte")
{
    const TerrainField before = sculpted();
    REQUIRE(before.tileCount() > 0);
    REQUIRE(before.brickCount() > 0);

    const std::vector<core::u8> bytes = encodeTerrain(before);
    const std::optional<TerrainField> after = decodeTerrain(bytes);
    REQUIRE(after.has_value());

    // The digest is over every tile, every brick and their keys, so this is the
    // whole field in one comparison.
    CHECK(after->digest() == before.digest());
    CHECK(after->tileCount() == before.tileCount());
    CHECK(after->brickCount() == before.brickCount());
    CHECK(after->settings().voxelSize == before.settings().voxelSize);
    CHECK(after->settings().minHeight == before.settings().minHeight);
    CHECK(after->settings().maxHeight == before.settings().maxHeight);

    // And it samples the same, which is the claim a digest cannot make on its
    // own: two fields could hash alike and be read differently.
    for (core::i32 x = -20; x <= 20; x += 7) {
        for (core::i32 y = -30; y <= 10; y += 11) {
            CAPTURE(x);
            CAPTURE(y);
            CHECK(after->sample(x, y, 0).distance == before.sample(x, y, 0).distance);
            CHECK(after->sample(x, y, 0).material == before.sample(x, y, 0).material);
        }
    }
}

TEST_CASE("the same field always encodes to the same bytes")
{
    // R10, and what makes a scene file diffable: the keys come out in the
    // field's own sorted order, so two identical worlds produce identical text.
    CHECK(encodeTerrain(sculpted()) == encodeTerrain(sculpted()));
}

TEST_CASE("an empty field round-trips to an empty field")
{
    const TerrainField empty(FieldSettings{.voxelSize = 0.5f, .minHeight = -32.0f, .maxHeight = 32.0f});
    const std::optional<TerrainField> back = decodeTerrain(encodeTerrain(empty));
    REQUIRE(back.has_value());
    CHECK(back->tileCount() == 0);
    CHECK(back->brickCount() == 0);
    CHECK(back->digest() == empty.digest());
}

TEST_CASE("the run coder actually pays for itself on ground")
{
    // Not a threshold on a benchmark -- an assertion about the data. A tile of
    // flat ground is one height repeated a thousand times and one material
    // repeated a thousand times, and a coder that did not collapse that would
    // be a coder worth removing.
    TerrainField flat(FieldSettings{.voxelSize = 0.5f, .minHeight = -32.0f, .maxHeight = 32.0f});
    fillBlock(flat, core::DVec3{0.0, -20.0, 0.0}, core::Vec3{64.0f, 40.0f, 64.0f}, 1);
    REQUIRE(flat.tileCount() >= 4);

    const core::usize plain = flat.tileCount() * (TileArea * 5) + flat.brickCount() * (BrickVolume * 2);
    const core::usize coded = encodeTerrain(flat).size();
    CHECK(coded * 8 < plain);
}

TEST_CASE("bytes that are not a terrain are refused rather than guessed at")
{
    // A world that loads and is silently wrong is worse than one that says it
    // cannot load.
    CHECK_FALSE(decodeTerrain(std::vector<core::u8>{}).has_value());
    CHECK_FALSE(decodeTerrain(std::vector<core::u8>{1, 2, 3, 4}).has_value());

    std::vector<core::u8> wrongVersion = encodeTerrain(sculpted());
    REQUIRE(wrongVersion.size() > 8);
    wrongVersion[4] = 99;
    CHECK_FALSE(decodeTerrain(wrongVersion).has_value());

    // Truncated: the header promises more than the body carries.
    std::vector<core::u8> cut = encodeTerrain(sculpted());
    cut.resize(cut.size() / 2);
    CHECK_FALSE(decodeTerrain(cut).has_value());
}
