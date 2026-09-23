// The terrain: one grid of voxels, each a material and an occupancy (ADR 0082).
//
// **What is worth testing here is where the surface lands and what a brush
// leaves alone**, because both are silent when wrong. A brush that writes a
// voxel outside its box still returns a count; a surface a quarter of a voxel
// off still draws. The owner's world was full of both before this grid existed
// (D162, D163), so each of those has a test here in the new terms.
#include "luaug/asset/terrain.h"
#include "luaug/asset/terrain_palette.h"

#include <algorithm>
#include <cmath>
#include <doctest/doctest.h>
#include <optional>
#include <string>
#include <vector>

using namespace luaug;
using namespace luaug::asset;

namespace {

constexpr float kVoxel = 1.0f;

[[nodiscard]] FieldSettings settingsOf(float voxel = kVoxel)
{
    return FieldSettings{.voxelSize = voxel, .minHeight = -64.0f, .maxHeight = 64.0f};
}

// Flat ground over x and z in [-32, 32), `height` metres high.
[[nodiscard]] TerrainField flatField(float height, float voxel = kVoxel)
{
    TerrainField field(settingsOf(voxel));
    (void)fillFlat(field, core::DVec3{0.0, 0.0, 0.0}, 64.0f, height, 1);
    return field;
}

[[nodiscard]] double top(const TerrainField& field, double x, double z)
{
    const std::optional<float> height = heightAt(field, x, z);
    REQUIRE(height.has_value());
    return static_cast<double>(*height);
}

[[nodiscard]] bool solidAt(const TerrainField& field, double x, double y, double z)
{
    return sampleField(field, core::DVec3{x, y, z}).distance < 0.0f;
}

// Every voxel in a box, for comparing what a brush left alone.
[[nodiscard]] std::vector<Voxel> voxelsIn(const TerrainField& field, int minX, int minY, int minZ, int maxX, int maxY,
                                          int maxZ)
{
    std::vector<Voxel> out;
    for (int z = minZ; z <= maxZ; ++z) {
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x)
                out.push_back(field.voxel(x, y, z));
        }
    }
    return out;
}

} // namespace

// --- Storage ---------------------------------------------------------------------

TEST_CASE("a voxel is air until something is written, and air keeps no material")
{
    TerrainField field(settingsOf());
    CHECK(field.voxel(3, 4, 5) == Voxel{});
    // Empty with a material is still air: the digest is over bytes, and two
    // equal worlds must hash equal.
    CHECK_FALSE(field.setVoxel(3, 4, 5, Voxel{0, 3}));
    CHECK(field.empty());
    CHECK(field.setVoxel(3, 4, 5, Voxel{200, 3}));
    CHECK(field.voxel(3, 4, 5) == Voxel{200, 3});
    CHECK(field.setVoxel(3, 4, 5, Voxel{0, 3}));
    CHECK(field.empty());
}

TEST_CASE("negative coordinates land in the chunk to their left")
{
    TerrainField field(settingsOf());
    (void)field.setVoxel(-1, -1, -1, Voxel{FullOccupancy, 1});
    CHECK(field.findChunk(ChunkKey{-1, -1, -1}) != nullptr);
    CHECK(field.findChunk(ChunkKey{0, 0, 0}) == nullptr);
    CHECK(field.voxel(-1, -1, -1).occupancy == FullOccupancy);
    CHECK(field.voxel(0, 0, 0).occupancy == 0);
    CHECK(chunkOf(-33, 31, 32) == ChunkKey{-2, 0, 1});
}

TEST_CASE("a column of chunks is one run of the sorted list, lowest first")
{
    TerrainField field(settingsOf());
    (void)field.setVoxel(0, 70, 0, Voxel{FullOccupancy, 1});
    (void)field.setVoxel(0, -5, 0, Voxel{FullOccupancy, 1});
    (void)field.setVoxel(40, 0, 0, Voxel{FullOccupancy, 1});
    (void)field.setVoxel(0, 0, 40, Voxel{FullOccupancy, 1});
    const std::span<const TerrainField::Entry> column = field.column(0, 0);
    REQUIRE(column.size() == 2);
    CHECK(column[0].first == ChunkKey{0, -1, 0});
    CHECK(column[1].first == ChunkKey{0, 2, 0});
}

TEST_CASE("a chunk holding one value is stored as that value")
{
    TerrainField field(settingsOf());
    {
        FieldWriter writer(field);
        for (int z = 0; z < 32; ++z) {
            for (int y = 0; y < 32; ++y) {
                for (int x = 0; x < 32; ++x)
                    (void)writer.set(x, y, z, Voxel{FullOccupancy, 3});
            }
        }
    }
    const TerrainChunk* chunk = field.findChunk(ChunkKey{0, 0, 0});
    REQUIRE(chunk != nullptr);
    CHECK(chunk->uniform());
    CHECK(chunk->value() == Voxel{FullOccupancy, 3});
    CHECK(chunk->bytes() < 512);
}

TEST_CASE("flat ground is mostly rows of one value, and small")
{
    const TerrainField field = flatField(2.3f);
    const TerrainChunk* surface = field.findChunk(ChunkKey{0, 0, 0});
    REQUIRE(surface != nullptr);
    CHECK_FALSE(surface->uniform());
    // The ramp is four voxels across, so a handful of layers of 32 rows are
    // partial and every other row is one value.
    CHECK(surface->denseRows() <= 6 * 32);
    CHECK(surface->bytes() < 16 * 1024);
    // Everything under it is whole chunks of ground.
    const TerrainChunk* under = field.findChunk(ChunkKey{0, -1, 0});
    REQUIRE(under != nullptr);
    CHECK(under->uniform());
    CHECK(under->value().occupancy == FullOccupancy);
}

TEST_CASE("flat ground over empty columns is one column shared, and the same ground as laid column by column")
{
    // **The owner's 5 km plain froze the editor**: laid column by column it
    // was thirty seconds and a quarter of a gigabyte. Every whole empty column
    // comes out the same, so it is laid once and shared.
    TerrainField shared(settingsOf());
    (void)fillFlat(shared, core::DVec3{3.0, 0.0, -5.0}, 200.0f, 1.3f, 2);

    TerrainField laid(settingsOf());
    const core::i32 first = shared.voxelIndex(3.0 - 100.0);
    const core::i32 firstZ = shared.voxelIndex(-5.0 - 100.0);
    const core::u32 columns = static_cast<core::u32>(shared.voxelIndex(3.0 + 100.0) - first);
    const core::u32 rows = static_cast<core::u32>(shared.voxelIndex(-5.0 + 100.0) - firstZ);
    const std::vector<float> heights(static_cast<std::size_t>(columns) * rows, 1.3f);
    (void)writeHeights(laid, first, firstZ, columns, heights, 2);
    CHECK(shared.digest() == laid.digest());

    // Two whole columns hold one chunk between them...
    const auto surfaceOf = [&](core::i32 x, core::i32 z) -> const TerrainChunk* {
        for (const TerrainField::Entry& entry : shared.column(x, z)) {
            if (entry.first.y == 0)
                return entry.second.get();
        }
        return nullptr;
    };
    REQUIRE(surfaceOf(0, 0) != nullptr);
    CHECK(surfaceOf(0, 0) == surfaceOf(1, 1));
    // ...until one is dug, which leaves the other as it was.
    const core::u64 before = surfaceOf(1, 1)->digest();
    (void)fillBall(shared, core::DVec3{16.0, 1.0, 16.0}, 3.0, 0);
    CHECK(surfaceOf(0, 0) != surfaceOf(1, 1));
    CHECK(surfaceOf(1, 1)->digest() == before);
    CHECK_FALSE(solidAt(shared, 16.5, 0.5, 16.5));
    CHECK(solidAt(shared, 48.5, 0.5, 48.5));
}

TEST_CASE("equal voxels hash equal, however they were written")
{
    TerrainField once = flatField(0.0f);
    (void)fillBall(once, core::DVec3{3.0, 0.0, 2.0}, 5.0, 2);

    TerrainField twice = flatField(0.0f);
    (void)fillBall(twice, core::DVec3{3.0, 0.0, 2.0}, 5.0, 2);
    (void)fillBall(twice, core::DVec3{3.0, 0.0, 2.0}, 5.0, 2);
    CHECK(once.digest() == twice.digest());

    // Voxel by voxel into a fresh field: the chunks come out canonical too.
    TerrainField copied(settingsOf());
    {
        FieldWriter writer(copied);
        for (const TerrainField::Entry& entry : once.chunks()) {
            for (int z = 0; z < 32; ++z) {
                for (int y = 0; y < 32; ++y) {
                    for (int x = 0; x < 32; ++x) {
                        const auto u = static_cast<core::u32>(x);
                        const auto v = static_cast<core::u32>(y);
                        const auto w = static_cast<core::u32>(z);
                        (void)writer.set(entry.first.x * 32 + x, entry.first.y * 32 + y, entry.first.z * 32 + z,
                                         entry.second->get(u, v, w));
                    }
                }
            }
        }
    }
    CHECK(copied.digest() == once.digest());

    (void)fillBall(twice, core::DVec3{3.0, 0.0, 2.0}, 1.0, 0);
    CHECK(once.digest() != twice.digest());
}

TEST_CASE("a snapshot taken before an edit does not see it")
{
    TerrainField field = flatField(0.0f);
    const TerrainField snapshot = field;
    const core::u64 before = snapshot.digest();
    CHECK(fillBall(field, core::DVec3{0.0, 0.0, 0.0}, 4.0, 0).touched > 0);
    CHECK(snapshot.digest() == before);
    CHECK(solidAt(snapshot, 0.5, -2.0, 0.5));
    CHECK_FALSE(solidAt(field, 0.5, -2.0, 0.5));
}

TEST_CASE("a mip is the mean occupancy of what is under it, and the fullest material")
{
    TerrainChunk chunk;
    // Half of one 2x2x2 block full of material 4, the rest empty.
    for (core::u32 y = 0; y < 2; ++y) {
        for (core::u32 x = 0; x < 2; ++x)
            (void)chunk.set(x, y, 0, Voxel{FullOccupancy, 4});
    }
    chunk.normalize();
    const Voxel half = chunk.mip(1, 0, 0, 0);
    CHECK(half.material == 4);
    CHECK(std::abs(static_cast<int>(half.occupancy) - 128) <= 1);
    CHECK(chunk.mip(1, 1, 0, 0) == Voxel{});
    // The whole chunk is one value at the top: a sixty-fourth of a sixty-fourth.
    CHECK(chunk.mip(5, 0, 0, 0).occupancy == 0);
}

TEST_CASE("fields share what they load, and drop what they are told to")
{
    const TerrainField a = flatField(0.0f);
    TerrainField b(settingsOf());
    (void)fillBall(b, core::DVec3{100.0, 0.0, 0.0}, 3.0, 2);
    const core::usize own = b.chunkCount();
    b.shareFrom(a);
    CHECK(b.chunkCount() == own + a.chunkCount());
    // Shared, not copied: the very object.
    CHECK(b.findChunk(ChunkKey{0, 0, 0}) == a.findChunk(ChunkKey{0, 0, 0}));
    const std::vector<ChunkKey> keys = a.chunkKeys();
    b.removeAll(keys);
    CHECK(b.chunkCount() == own);
}

// --- Where the surface lands -------------------------------------------------------

TEST_CASE("flat ground's surface is where it was put, between the voxel centres")
{
    const TerrainField field = flatField(2.3f);
    CHECK(top(field, 0.5, 0.5) == doctest::Approx(2.3).epsilon(0.005));
    CHECK(top(field, -7.25, 12.8) == doctest::Approx(2.3).epsilon(0.005));
    CHECK(solidAt(field, 0.5, 2.2, 0.5));
    CHECK_FALSE(solidAt(field, 0.5, 2.4, 0.5));
}

TEST_CASE("a ball lands where the brush put it, on every axis")
{
    TerrainField field(settingsOf());
    CHECK(fillBall(field, core::DVec3{0.0, 0.0, 0.0}, 6.0, 3).touched > 0);
    for (const core::DVec3 direction : {core::DVec3{1.0, 0.0, 0.0}, core::DVec3{0.0, 1.0, 0.0},
                                        core::DVec3{0.0, 0.0, -1.0}, core::DVec3{0.577, 0.577, 0.577}}) {
        CHECK(solidAt(field, direction.x * 5.8, direction.y * 5.8, direction.z * 5.8));
        CHECK_FALSE(solidAt(field, direction.x * 6.2, direction.y * 6.2, direction.z * 6.2));
    }
    CHECK(sampleField(field, core::DVec3{0.0, 0.0, 0.0}).material == 3);
}

TEST_CASE("a box and a cylinder land where they were put")
{
    TerrainField field(settingsOf());
    (void)fillBlock(field, core::DVec3{0.0, 0.0, 0.0}, core::Vec3{8.0f, 4.0f, 6.0f}, 2);
    // Against each face, away from the corners, which the interpolation
    // between voxel centres rounds by a fraction of a voxel.
    CHECK(solidAt(field, 3.8, 0.5, 0.5));
    CHECK(solidAt(field, 0.5, 1.8, 0.5));
    CHECK(solidAt(field, 0.5, 0.5, 2.8));
    CHECK_FALSE(solidAt(field, 4.2, 0.0, 0.0));
    CHECK_FALSE(solidAt(field, 0.0, 2.2, 0.0));

    (void)fillCylinder(field, core::DVec3{20.0, 0.0, 0.0}, 10.0, 3.0, 5);
    CHECK(solidAt(field, 22.8, 0.0, 0.0));
    CHECK(solidAt(field, 20.0, 4.8, 0.0));
    CHECK_FALSE(solidAt(field, 23.2, 0.0, 0.0));
    CHECK_FALSE(solidAt(field, 20.0, 5.2, 0.0));
}

TEST_CASE("adding never takes ground away, and removing never adds it")
{
    TerrainField field = flatField(0.0f);
    // A ball of sand half in the grass: the grass under the surface stays grass,
    // and the half above it is sand.
    (void)fillBall(field, core::DVec3{0.0, 0.0, 0.0}, 4.0, 2);
    CHECK(sampleField(field, core::DVec3{0.5, -3.5, 0.5}).material == 1);
    CHECK(sampleField(field, core::DVec3{0.5, 2.5, 0.5}).material == 2);
    CHECK(solidAt(field, 0.5, 3.8, 0.5));

    // Removing a ball in the air changes nothing.
    CHECK(fillBall(field, core::DVec3{0.0, 30.0, 0.0}, 4.0, 0).touched == 0);
}

// --- What a brush leaves alone (D163) ---------------------------------------------

TEST_CASE("no brush writes a voxel outside its reach")
{
    // A hill beside a plain, the shape the owner's world had when a dig beside
    // a hill cut its peak off and a dig on it deleted the plain below.
    TerrainField field = flatField(0.0f);
    (void)raiseBall(field, core::DVec3{0.0, 0.0, 0.0}, 12.0, 14.0f);
    const TerrainField before = field;
    const double peak = top(before, 0.5, 0.5);

    const auto untouchedOutside = [&](const TerrainField& after, core::DVec3 at, double reach) {
        // Everything more than `reach` from the brush on x or z is as it was.
        const std::vector<Voxel> was = voxelsIn(before, -30, -20, -30, 29, 30, 29);
        const std::vector<Voxel> now = voxelsIn(after, -30, -20, -30, 29, 30, 29);
        std::size_t at_ = 0;
        bool same = true;
        for (int z = -30; z <= 29; ++z) {
            for (int y = -20; y <= 30; ++y) {
                for (int x = -30; x <= 29; ++x, ++at_) {
                    const bool far = std::abs(static_cast<double>(x) + 0.5 - at.x) > reach ||
                                     std::abs(static_cast<double>(z) + 0.5 - at.z) > reach;
                    if (far && !(was[at_] == now[at_]))
                        same = false;
                }
            }
        }
        return same;
    };

    SUBCASE("a dig at the hill's foot")
    {
        const core::DVec3 foot{10.0, 0.0, 0.0};
        (void)fillBall(field, foot, 3.0, 0);
        CHECK(untouchedOutside(field, foot, 3.0 + 4.0));
        CHECK(top(field, 0.5, 0.5) == doctest::Approx(peak));
    }
    SUBCASE("a dig high on the hill")
    {
        const core::DVec3 high{3.0, 11.0, 0.0};
        (void)fillBall(field, high, 3.0, 0);
        CHECK(untouchedOutside(field, high, 3.0 + 4.0));
        CHECK(top(field, 20.5, 0.5) == doctest::Approx(0.0).epsilon(0.01));
    }
    SUBCASE("a raise, a smooth and a flatten")
    {
        const core::DVec3 at{-8.0, 4.0, 5.0};
        (void)raiseBall(field, at, 4.0, 2.0f);
        (void)smoothBall(field, at, 4.0, 1.0f);
        (void)flattenBall(field, at, 4.0, 4.0f, 1.0f);
        CHECK(untouchedOutside(field, at, 4.0 + 4.0));
    }
}

// --- Heights -----------------------------------------------------------------------

TEST_CASE("fillFlat moves the top and keeps a tunnel under it (D162)")
{
    TerrainField field = flatField(0.0f);
    (void)fillBall(field, core::DVec3{0.0, -10.0, 0.0}, 2.5, 0);
    REQUIRE_FALSE(solidAt(field, 0.0, -10.0, 0.0));

    (void)fillFlat(field, core::DVec3{0.0, 0.0, 0.0}, 16.0f, 5.0f, 1);
    CHECK(top(field, 0.5, 0.5) == doctest::Approx(5.0).epsilon(0.01));
    CHECK_FALSE(solidAt(field, 0.0, -10.0, 0.0));

    (void)fillFlat(field, core::DVec3{0.0, 0.0, 0.0}, 16.0f, -3.0f, 1);
    CHECK(top(field, 0.5, 0.5) == doctest::Approx(-3.0).epsilon(0.01));
    CHECK_FALSE(solidAt(field, 0.0, -10.0, 0.0));
}

TEST_CASE("writeHeights lays each column at its own height")
{
    TerrainField field(settingsOf());
    constexpr core::u32 Columns = 40;
    std::vector<float> heights(Columns * Columns);
    for (core::u32 z = 0; z < Columns; ++z) {
        for (core::u32 x = 0; x < Columns; ++x)
            heights[z * Columns + x] = 3.0f + 0.25f * static_cast<float>(x) - 0.1f * static_cast<float>(z);
    }
    (void)writeHeights(field, -20, -20, Columns, heights, 2);
    for (core::u32 z = 2; z < Columns - 2; z += 7) {
        for (core::u32 x = 2; x < Columns - 2; x += 5) {
            const double expected = static_cast<double>(heights[z * Columns + x]);
            const std::optional<float> got =
                field.columnTop(static_cast<core::i32>(x) - 20, static_cast<core::i32>(z) - 20);
            REQUIRE(got.has_value());
            CHECK(static_cast<double>(*got) == doctest::Approx(expected).epsilon(0.01));
        }
    }
    CHECK(sampleField(field, core::DVec3{0.5, 0.0, 0.5}).material == 2);
}

TEST_CASE("a heightmap on empty ground is whole chunks under a thin skin")
{
    TerrainField field(settingsOf());
    constexpr core::u32 Columns = 64;
    const std::vector<float> heights(Columns * Columns, 20.0f);
    const EditReport report = writeHeights(field, 0, 0, Columns, heights, 1);
    CHECK(report.touched > 64u * 64u * 50u);
    // A slab `LaidDepth` deep, to a chunk boundary: from -32 to 20 is one whole
    // chunk a column and the one the surface is in, four columns of them.
    std::size_t uniform = 0;
    for (const TerrainField::Entry& entry : field.chunks())
        uniform += entry.second->uniform() ? 1u : 0u;
    CHECK(uniform >= 4u);
    CHECK(field.findChunk(ChunkKey{0, -2, 0}) == nullptr);
    CHECK(field.bytes() < 256u * 1024u);
    CHECK(top(field, 10.5, 40.5) == doctest::Approx(20.0).epsilon(0.005));
}

TEST_CASE("a height that is not a number, and a column with no material, are skipped")
{
    TerrainField field(settingsOf());
    const std::vector<float> heights{1.0f, std::numeric_limits<float>::quiet_NaN(), 3.0f, 4.0f};
    const std::vector<core::u8> materials{1, 1, 0, 1};
    (void)writeHeights(field, 0, 0, 4, heights, materials);
    CHECK(field.columnTop(0, 0).has_value());
    CHECK_FALSE(field.columnTop(1, 0).has_value());
    CHECK_FALSE(field.columnTop(2, 0).has_value());
    CHECK(field.columnTop(3, 0).has_value());
}

TEST_CASE("heightAt is the top over a cave, and nothing where there is no ground")
{
    TerrainField field = flatField(0.0f);
    (void)fillBall(field, core::DVec3{0.0, -8.0, 0.0}, 3.0, 0);
    CHECK(top(field, 0.5, 0.5) == doctest::Approx(0.0).epsilon(0.01));
    CHECK_FALSE(heightAt(field, 200.0, 0.0).has_value());
}

// --- The height brushes -------------------------------------------------------------

TEST_CASE("raising lifts the middle by the amount and leaves the rim")
{
    TerrainField field = flatField(0.0f);
    (void)raiseBall(field, core::DVec3{0.0, 0.0, 0.0}, 8.0, 3.0f);
    CHECK(top(field, 0.0, 0.0) == doctest::Approx(3.0).epsilon(0.05));
    CHECK(top(field, 9.5, 0.5) == doctest::Approx(0.0).epsilon(0.01));

    (void)raiseBall(field, core::DVec3{20.0, 0.0, 0.0}, 6.0, -2.0f);
    CHECK(top(field, 20.0, 0.0) == doctest::Approx(-2.0).epsilon(0.05));
}

TEST_CASE("raising the same spot again and again keeps growing the hill")
{
    TerrainField field = flatField(0.0f);
    for (int stroke = 0; stroke < 10; ++stroke)
        (void)raiseBall(field, core::DVec3{0.0, 0.0, 0.0}, 6.0, 2.0f);
    // Ten strokes of two metres, not a plateau at the brush's reach.
    CHECK(top(field, 0.0, 0.0) > 15.0);
}

TEST_CASE("raising over a tunnel lifts the ground and keeps the tunnel")
{
    TerrainField field = flatField(0.0f);
    (void)fillBall(field, core::DVec3{0.0, -14.0, 0.0}, 2.5, 0);
    (void)raiseBall(field, core::DVec3{0.0, 0.0, 0.0}, 6.0, 4.0f);
    CHECK(top(field, 0.0, 0.0) == doctest::Approx(4.0).epsilon(0.05));
    CHECK_FALSE(solidAt(field, 0.0, -14.0, 0.0));
}

TEST_CASE("raising empty ground lays ground when there is a material to lay")
{
    TerrainField field(settingsOf());
    CHECK(raiseBall(field, core::DVec3{0.0, 0.0, 0.0}, 5.0, 2.0f).touched == 0);
    (void)raiseBall(field, core::DVec3{0.0, 0.0, 0.0}, 5.0, 2.0f, 1);
    CHECK(top(field, 0.0, 0.0) == doctest::Approx(2.0).epsilon(0.05));
}

TEST_CASE("growing moves flat ground up by the amount, and eroding moves it down")
{
    TerrainField field = flatField(0.0f);
    (void)growBall(field, core::DVec3{0.0, 0.0, 0.0}, 6.0, 1.0f);
    CHECK(top(field, 0.0, 0.0) == doctest::Approx(1.0).epsilon(0.1));
    CHECK(top(field, 7.5, 0.5) == doctest::Approx(0.0).epsilon(0.01));

    (void)growBall(field, core::DVec3{20.0, 0.0, 0.0}, 6.0, -1.0f);
    CHECK(top(field, 20.0, 0.0) == doctest::Approx(-1.0).epsilon(0.1));
}

TEST_CASE("growing a wall brings it forward, and stands nothing under it")
{
    // **The owner's picture**: Add clicked on the side of the terrain stood
    // pillars under the click, because it raised columns. Grown, a wall comes
    // out towards the brush and the air below the brush stays air.
    TerrainField field(settingsOf());
    (void)fillBlock(field, core::DVec3{-10.0, 0.0, 0.0}, core::Vec3{20.0f, 20.0f, 20.0f}, 1);
    CHECK_FALSE(solidAt(field, 0.5, 0.0, 0.0));
    const std::vector<Voxel> below = voxelsIn(field, 0, -9, -4, 6, -6, 4);

    for (int stamp = 0; stamp < 2; ++stamp)
        (void)growBall(field, core::DVec3{0.0, 0.0, 0.0}, 5.0, 1.0f);
    CHECK(solidAt(field, 1.0, 0.0, 0.0));
    CHECK_FALSE(solidAt(field, 2.6, 0.0, 0.0));
    CHECK(voxelsIn(field, 0, -9, -4, 6, -6, 4) == below);
    CHECK(top(field, -5.0, 0.0) == doctest::Approx(10.0).epsilon(0.01));
}

TEST_CASE("growing over nothing does nothing")
{
    TerrainField field(settingsOf());
    CHECK(growBall(field, core::DVec3{0.0, 0.0, 0.0}, 5.0, 1.0f, 1).touched == 0);
    CHECK(field.empty());
}

TEST_CASE("smoothing takes the edge off a step")
{
    TerrainField field(settingsOf());
    constexpr core::u32 Columns = 32;
    std::vector<float> heights(Columns * Columns);
    for (core::u32 z = 0; z < Columns; ++z) {
        for (core::u32 x = 0; x < Columns; ++x)
            heights[z * Columns + x] = x < Columns / 2 ? 0.0f : 4.0f;
    }
    (void)writeHeights(field, -16, -16, Columns, heights, 1);
    const double step = top(field, 1.5, 0.5) - top(field, -1.5, 0.5);
    for (int pass = 0; pass < 4; ++pass)
        (void)smoothBall(field, core::DVec3{0.0, 2.0, 0.0}, 5.0, 1.0f);
    CHECK(top(field, 1.5, 0.5) - top(field, -1.5, 0.5) < step * 0.8);
}

TEST_CASE("flattening pulls the ground in reach to the plane")
{
    TerrainField field = flatField(0.0f);
    (void)raiseBall(field, core::DVec3{0.0, 0.0, 0.0}, 8.0, 5.0f);
    for (int pass = 0; pass < 3; ++pass)
        (void)flattenBall(field, core::DVec3{0.0, 3.0, 0.0}, 6.0, 1.0f, 1.0f);
    CHECK(top(field, 0.0, 0.0) == doctest::Approx(1.0).epsilon(0.35));
}

TEST_CASE("painting changes what ground is made of, and nothing else")
{
    TerrainField field = flatField(0.0f);
    const std::vector<Voxel> before = voxelsIn(field, -4, -4, -4, 4, 4, 4);
    CHECK(paintBall(field, core::DVec3{0.0, 0.0, 0.0}, 3.0, 0).touched == 0);
    CHECK(paintBall(field, core::DVec3{0.0, 0.0, 0.0}, 3.0, 5).touched > 0);
    const std::vector<Voxel> after = voxelsIn(field, -4, -4, -4, 4, 4, 4);
    for (std::size_t at = 0; at < before.size(); ++at)
        CHECK(before[at].occupancy == after[at].occupancy);
    CHECK(sampleField(field, core::DVec3{0.5, -0.5, 0.5}).material == 5);

    CHECK(replaceMaterial(field, core::DVec3{-2.0, -2.0, -2.0}, core::DVec3{2.0, 2.0, 2.0}, 5, 7).touched > 0);
    CHECK(sampleField(field, core::DVec3{0.5, -0.5, 0.5}).material == 7);
    CHECK(replaceMaterial(field, core::DVec3{-2.0, -2.0, -2.0}, core::DVec3{2.0, 2.0, 2.0}, 7, 0).touched == 0);
}

TEST_CASE("nothing is written outside the world's floor and ceiling")
{
    TerrainField field(settingsOf());
    (void)fillBall(field, core::DVec3{0.0, 64.0, 0.0}, 6.0, 1);
    (void)fillBall(field, core::DVec3{0.0, -64.0, 0.0}, 6.0, 1);
    for (const TerrainField::Entry& entry : field.chunks()) {
        CHECK(entry.first.y * 32 >= -64 - 32);
        CHECK(entry.first.y * 32 < 64);
    }
    CHECK_FALSE(solidAt(field, 0.0, 65.0, 0.0));
    CHECK_FALSE(solidAt(field, 0.0, -65.0, 0.0));
}

// --- Raycasting -----------------------------------------------------------------------

TEST_CASE("a ray meets flat ground at its height, facing up")
{
    const TerrainField field = flatField(3.4f);
    const std::optional<TerrainHit> hit =
        raycastField(field, core::DVec3{0.5, 20.0, 0.5}, core::Vec3{0.0f, -1.0f, 0.0f}, 100.0);
    REQUIRE(hit.has_value());
    CHECK(hit->position.y == doctest::Approx(3.4).epsilon(0.005));
    CHECK(static_cast<double>(hit->normal.y) > 0.99);
    CHECK(hit->material == 1);
}

TEST_CASE("a ray at the sky misses, one from inside hits at once")
{
    const TerrainField field = flatField(0.0f);
    CHECK_FALSE(raycastField(field, core::DVec3{0.0, 5.0, 0.0}, core::Vec3{0.0f, 1.0f, 0.0f}, 1000.0).has_value());
    const std::optional<TerrainHit> inside =
        raycastField(field, core::DVec3{0.0, -5.0, 0.0}, core::Vec3{1.0f, 0.0f, 0.0f}, 10.0);
    REQUIRE(inside.has_value());
    CHECK(inside->distance == doctest::Approx(0.0));
}

TEST_CASE("a long ray crosses empty chunks and still meets the ground beyond them")
{
    TerrainField field(settingsOf());
    (void)fillBlock(field, core::DVec3{500.0, 0.0, 0.0}, core::Vec3{8.0f, 8.0f, 8.0f}, 3);
    const std::optional<TerrainHit> hit =
        raycastField(field, core::DVec3{0.0, 0.5, 0.5}, core::Vec3{1.0f, 0.0f, 0.0f}, 1000.0);
    REQUIRE(hit.has_value());
    CHECK(hit->position.x == doctest::Approx(496.0).epsilon(0.001));
    CHECK(static_cast<double>(hit->normal.x) < -0.99);
}

TEST_CASE("the palette names what the field only numbers")
{
    CHECK(terrainMaterial(0) == nullptr);
    REQUIRE(terrainMaterial(1) != nullptr);
    CHECK(std::string(terrainMaterial(1)->name) == "Grass");
    CHECK(terrainPalette().size() == 8);
}
