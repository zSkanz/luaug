// The two static shapes the physics seam learned for terrain (ADR 0066, F1 A2).
//
// **Nothing above L2 names these yet**, which is what makes the step landable on
// its own: a `ShapeType` enumerator, a `buildShape` case and a test each, with
// no caller. What is asserted here is the seam's own contract -- that a height
// field is a surface at the heights it was given, that a triangle mesh is the
// triangles it was given, that both are forced static however they were asked
// for, and that every description the backend cannot honour is refused rather
// than approximated.
#include "luaug/core/i18n.h"
#include "luaug/physics/backends.h"
#include "luaug/physics/physics.h"
#include "luaug/physics/types.h"

#include <chrono>
#include <cmath>
#include <doctest/doctest.h>
#include <vector>

using namespace luaug;
using namespace luaug::physics;

namespace {

constexpr f32 kFixedDt = 1.0f / 60.0f;

// A grid Jolt will accept: `sampleCount / blockSize` must be at least 2, and a
// power of two is the cheapest. 8 over the default block of 2 is four blocks a
// side, which is small enough to write out by hand in a test and large enough to
// have an interior an edit can miss.
constexpr u32 kSamples = 8;

struct Fixture
{
    Fixture()
    {
        // **So a Jolt assert says which one it was.** `assertFailedImpl` logs
        // the file, the line and the expression through the catalog, and with
        // none loaded that arrives as an opaque hash.
        const auto catalog = core::engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
        REQUIRE_MESSAGE(catalog.ok, catalog.diagnostic);
        physics = createJoltPhysics();
        REQUIRE(physics != nullptr);
        world = physics->createWorld(WorldDesc{});
        REQUIRE(world.valid());
    }

    ~Fixture() { physics->destroyWorld(world); }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    void run(int ticks)
    {
        for (int i = 0; i < ticks; ++i) {
            physics->step(world, kFixedDt);
        }
    }

    PhysicsResult physics;
    WorldHandle world;
};

// A flat field at `height`, 64 m on a side.
[[nodiscard]] std::vector<float> flatHeights(float height)
{
    return std::vector<float>(static_cast<core::usize>(kSamples) * kSamples, height);
}

[[nodiscard]] BodyDesc heightFieldDesc(const std::vector<float>& heights)
{
    BodyDesc desc;
    desc.shape.type = ShapeType::HeightField;
    // The footprint. `size.y` is deliberately not consulted by a height field --
    // the samples ARE the heights -- and is left at 1 to make that visible.
    desc.shape.size = core::Vec3{64.0f, 1.0f, 64.0f};
    desc.shape.heights = heights;
    desc.shape.heightSampleCount = kSamples;
    // **The range the field is allowed to reach**, reserved at construction
    // because it cannot be widened afterwards. Without it a flat field quantises
    // into a zero-wide range and every later edit clamps back to the height it
    // already had -- reporting success and moving nothing, which is how the two
    // in-place cases below first failed.
    desc.shape.heightMin = -64.0f;
    desc.shape.heightMax = 64.0f;
    desc.motion = MotionType::Static;
    desc.userData = 1;
    return desc;
}

[[nodiscard]] BodyDesc cubeAbove(core::DVec3 at)
{
    BodyDesc desc;
    desc.shape.type = ShapeType::Box;
    desc.shape.size = core::Vec3{1.0f, 1.0f, 1.0f};
    desc.transform.position = at;
    desc.motion = MotionType::Dynamic;
    desc.density = 1000.0f;
    desc.userData = 2;
    return desc;
}

} // namespace

TEST_CASE("a height field is a surface at the heights it was given")
{
    Fixture fixture;

    // A field at y = 5, so a cube dropped from above rests at 5 plus its own
    // half-extent. Asserting the RESTING HEIGHT rather than merely that
    // something stopped it: a shape built at the wrong scale or offset still
    // stops a cube, at the wrong place.
    const std::vector<float> heights = flatHeights(5.0f);
    const BodyHandle ground = fixture.physics->createBody(fixture.world, heightFieldDesc(heights));
    REQUIRE(ground.valid());

    const BodyHandle cube = fixture.physics->createBody(fixture.world, cubeAbove({0.0, 12.0, 0.0}));
    REQUIRE(cube.valid());

    fixture.run(240);

    const BodyState state = fixture.physics->bodyState(fixture.world, cube);
    CHECK(state.transform.position.y == doctest::Approx(5.5).epsilon(0.05));
}

TEST_CASE("a height field and a triangle mesh are static however they were asked for")
{
    Fixture fixture;

    // **The arithmetic this rule exists for.** Mass is `volume x density`
    // clamped to a one-gram floor, and both shapes report a volume of zero -- so
    // a dynamic terrain body would weigh a gram, carry a kilometre-wide
    // collider, be activated, and leave.
    const std::vector<float> heights = flatHeights(0.0f);
    BodyDesc field = heightFieldDesc(heights);
    field.motion = MotionType::Dynamic;
    const BodyHandle ground = fixture.physics->createBody(fixture.world, field);
    REQUIRE(ground.valid());

    // A static body reports `active == false` forever, which is how this is
    // observable through the seam without exposing a motion getter.
    fixture.run(120);
    CHECK_FALSE(fixture.physics->bodyState(fixture.world, ground).active);
    // And it has not moved, which is the thing a one-gram dynamic body would
    // fail spectacularly.
    CHECK(fixture.physics->bodyState(fixture.world, ground).transform.position.y == doctest::Approx(0.0));

    // The same for a triangle mesh: one quad, two triangles, asked for dynamic.
    const std::vector<core::Vec3> points{
        {-10.0f, 0.0f, -10.0f}, {10.0f, 0.0f, -10.0f}, {10.0f, 0.0f, 10.0f}, {-10.0f, 0.0f, 10.0f}};
    const std::vector<u32> indices{0, 2, 1, 0, 3, 2};

    BodyDesc mesh;
    mesh.shape.type = ShapeType::TriangleMesh;
    mesh.shape.points = points;
    mesh.shape.indices = indices;
    mesh.transform.position = core::DVec3{100.0, 0.0, 0.0};
    mesh.motion = MotionType::Dynamic;
    mesh.userData = 3;
    const BodyHandle quad = fixture.physics->createBody(fixture.world, mesh);
    REQUIRE(quad.valid());

    fixture.run(120);
    CHECK_FALSE(fixture.physics->bodyState(fixture.world, quad).active);
    CHECK(fixture.physics->bodyState(fixture.world, quad).transform.position.y == doctest::Approx(0.0));
}

TEST_CASE("a triangle mesh collides as its triangles, and a hole in it is a hole")
{
    Fixture fixture;

    // **Concave, which is the whole reason this shape type exists.** Two quads
    // side by side with a gap between them: a convex hull of the same points
    // would span the gap, so a cube dropped down the middle falling through is
    // the assertion that these really are the triangles.
    const std::vector<core::Vec3> points{
        {-12.0f, 0.0f, -6.0f}, {-4.0f, 0.0f, -6.0f}, {-4.0f, 0.0f, 6.0f}, {-12.0f, 0.0f, 6.0f},
        {4.0f, 0.0f, -6.0f},   {12.0f, 0.0f, -6.0f}, {12.0f, 0.0f, 6.0f}, {4.0f, 0.0f, 6.0f},
    };
    // **Wound so the normal points UP**, which the first version of this case
    // got backwards and spent a failing run learning: `(v1-v0) x (v2-v0)` for
    // `0,1,2` on this vertex order points at negative y, and a `MeshShape` is
    // one-sided -- so the slab was there, facing down, and the cube fell
    // through the floor it should have landed on.
    const std::vector<u32> indices{0, 2, 1, 0, 3, 2, 4, 6, 5, 4, 7, 6};

    BodyDesc mesh;
    mesh.shape.type = ShapeType::TriangleMesh;
    mesh.shape.points = points;
    mesh.shape.indices = indices;
    mesh.motion = MotionType::Static;
    mesh.userData = 1;
    REQUIRE(fixture.physics->createBody(fixture.world, mesh).valid());

    // Onto the left slab: stopped.
    const BodyHandle onSlab = fixture.physics->createBody(fixture.world, cubeAbove({-8.0, 6.0, 0.0}));
    // Down the gap: through.
    const BodyHandle throughGap = fixture.physics->createBody(fixture.world, cubeAbove({0.0, 6.0, 0.0}));
    REQUIRE(onSlab.valid());
    REQUIRE(throughGap.valid());

    fixture.run(180);

    CHECK(fixture.physics->bodyState(fixture.world, onSlab).transform.position.y == doctest::Approx(0.5).epsilon(0.1));
    CHECK(fixture.physics->bodyState(fixture.world, throughGap).transform.position.y < -5.0);
}

TEST_CASE("an in-place height edit moves the ground without rebuilding the body")
{
    Fixture fixture;

    std::vector<float> heights = flatHeights(0.0f);
    const BodyHandle ground = fixture.physics->createBody(fixture.world, heightFieldDesc(heights));
    REQUIRE(ground.valid());

    // **Raised, not lowered, and the whole grid** -- so the assertion is about
    // where a cube comes to rest rather than about the broadphase bounds, which
    // the case below is for.
    const std::vector<float> raised(static_cast<core::usize>(kSamples) * kSamples, 4.0f);
    REQUIRE(fixture.physics->updateHeightField(fixture.world, ground, 0, 0, kSamples, kSamples, raised));

    const BodyHandle cube = fixture.physics->createBody(fixture.world, cubeAbove({0.0, 12.0, 0.0}));
    REQUIRE(cube.valid());
    fixture.run(240);

    CHECK(fixture.physics->bodyState(fixture.world, cube).transform.position.y == doctest::Approx(4.5).epsilon(0.05));
}

TEST_CASE("digging below the old bounds still collides, because the broadphase was told")
{
    Fixture fixture;

    // **The defect this case exists for is silent.** `SetHeights` changes the
    // shape's local bounding box and nothing recomputes the BODY's world bounds
    // on its own -- so a valley dug below the old minimum sits outside the box
    // the broadphase culls against, and a collider that is there stops being
    // hit. Without `NotifyShapeChanged` a cube dropped into the hole falls
    // through the floor it is standing on.
    std::vector<float> heights = flatHeights(0.0f);
    const BodyHandle ground = fixture.physics->createBody(fixture.world, heightFieldDesc(heights));
    REQUIRE(ground.valid());

    // A pit four samples across, six metres down -- well below the flat field's
    // old minimum of zero.
    std::vector<float> pit(16, -6.0f);
    REQUIRE(fixture.physics->updateHeightField(fixture.world, ground, 2, 2, 4, 4, pit));

    // The pit's centre in world space. The grid spans 64 m over 7 intervals, so
    // a sample is about 9.14 m apart and samples 2..5 straddle the middle.
    const BodyHandle cube = fixture.physics->createBody(fixture.world, cubeAbove({0.0, 10.0, 0.0}));
    REQUIRE(cube.valid());
    fixture.run(300);

    const f64 restY = fixture.physics->bodyState(fixture.world, cube).transform.position.y;
    // In the pit rather than through the world: it went below the old surface,
    // and it stopped.
    CHECK(restY < 0.0);
    CHECK(restY > -8.0);
}

TEST_CASE("a description the backend cannot honour is refused rather than approximated")
{
    Fixture fixture;

    // Too few samples for one block pair: Jolt asserts on this rather than
    // returning an error, so it has to be refused before it reaches the builder.
    {
        const std::vector<float> tiny(4, 0.0f);
        BodyDesc desc = heightFieldDesc(tiny);
        desc.shape.heightSampleCount = 2;
        CHECK_FALSE(fixture.physics->createBody(fixture.world, desc).valid());
    }

    // A sample count the buffer cannot back. Building this would read past the
    // end of somebody's vector.
    {
        const std::vector<float> tooFew(9, 0.0f);
        BodyDesc desc = heightFieldDesc(tooFew);
        desc.shape.heightSampleCount = kSamples;
        CHECK_FALSE(fixture.physics->createBody(fixture.world, desc).valid());
    }

    // An index past the last vertex, which is refused rather than clamped: a
    // clamp builds a degenerate triangle out of a caller's bug and hides it
    // inside a collider that behaves almost right.
    {
        const std::vector<core::Vec3> points{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
        const std::vector<u32> indices{0, 1, 7};
        BodyDesc desc;
        desc.shape.type = ShapeType::TriangleMesh;
        desc.shape.points = points;
        desc.shape.indices = indices;
        desc.motion = MotionType::Static;
        CHECK_FALSE(fixture.physics->createBody(fixture.world, desc).valid());
    }

    // A partial triple is a description that cannot mean anything.
    {
        const std::vector<core::Vec3> points{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
        const std::vector<u32> indices{0, 1, 2, 0};
        BodyDesc desc;
        desc.shape.type = ShapeType::TriangleMesh;
        desc.shape.points = points;
        desc.shape.indices = indices;
        desc.motion = MotionType::Static;
        CHECK_FALSE(fixture.physics->createBody(fixture.world, desc).valid());
    }
}

TEST_CASE("an in-place edit refuses every rectangle Jolt would assert on")
{
    Fixture fixture;

    const std::vector<float> heights = flatHeights(0.0f);
    const BodyHandle ground = fixture.physics->createBody(fixture.world, heightFieldDesc(heights));
    REQUIRE(ground.valid());
    const std::vector<float> patch(16, 1.0f);

    // **Every one of these is an ASSERT inside Jolt**, which is a crash in a
    // debug build and undefined behaviour in a shipping one -- and a brush
    // dragged to the edge of a cell reaches all of them. Refusing is what turns
    // a class of crash into a stroke that does nothing.
    const u32 block = 2;

    // Misaligned start, on each axis.
    CHECK_FALSE(fixture.physics->updateHeightField(fixture.world, ground, 1, 0, 4, 4, patch));
    CHECK_FALSE(fixture.physics->updateHeightField(fixture.world, ground, 0, 1, 4, 4, patch));

    // Past the end.
    CHECK_FALSE(fixture.physics->updateHeightField(fixture.world, ground, kSamples - block, 0, 4, 4, patch));
    CHECK_FALSE(fixture.physics->updateHeightField(fixture.world, ground, 0, kSamples, 4, 4, patch));

    // A buffer that cannot back the rectangle asked for.
    {
        const std::vector<float> tooFew(4, 1.0f);
        CHECK_FALSE(fixture.physics->updateHeightField(fixture.world, ground, 0, 0, 4, 4, tooFew));
    }

    // An empty rectangle changes nothing and says so.
    CHECK_FALSE(fixture.physics->updateHeightField(fixture.world, ground, 0, 0, 0, 4, patch));

    // A body that is not a height field at all -- the handles crossed, which is
    // the mistake a caller with two collections makes.
    const BodyHandle cube = fixture.physics->createBody(fixture.world, cubeAbove({0.0, 20.0, 0.0}));
    REQUIRE(cube.valid());
    CHECK_FALSE(fixture.physics->updateHeightField(fixture.world, cube, 0, 0, 4, 4, patch));

    // And a handle nobody issued.
    CHECK_FALSE(fixture.physics->updateHeightField(fixture.world, BodyHandle{}, 0, 0, 4, 4, patch));

    // After all of that the ground is still the ground: a refusal changed
    // nothing, which is the half a `CHECK_FALSE` alone does not say.
    const BodyHandle probe = fixture.physics->createBody(fixture.world, cubeAbove({0.0, 8.0, 0.0}));
    REQUIRE(probe.valid());
    fixture.run(240);
    CHECK(fixture.physics->bodyState(fixture.world, probe).transform.position.y == doctest::Approx(0.5).epsilon(0.1));
}

// --- F1 step A3: what a terrain collider costs ------------------------------
//
// **Nothing in this repository measured a shape BUILD before this.** `physics1k`
// and `churn10k` move and re-target bodies without ever reshaping one, so every
// cost figure under ADR 0066 -- including the one the whole hybrid rests on,
// that `SetHeights` is much cheaper than a rebuild -- was a guess.
//
// Skipped by default and run on demand, the shape `asset`'s import bench already
// uses: a number that varies with the machine must not gate anything, and a
// measurement nobody can reproduce on their own hardware is not a measurement.
//
//     luaug_physics_tests --test-case="what a terrain collider costs" --no-skip
TEST_CASE("what a terrain collider costs" * doctest::skip())
{
    Fixture fixture;

    const auto ms = [](auto from, auto to) {
        return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(to - from).count();
    };

    // A 64 m cell at roughly half a metre, which is F1's working resolution.
    // 128 is a power of two, which Jolt documents as the cheapest, and 127
    // intervals over 64 m is 0.504 m.
    for (const u32 samples : {u32{128}, u32{256}}) {
        std::vector<float> heights(static_cast<core::usize>(samples) * samples);
        for (core::usize at = 0; at < heights.size(); ++at) {
            const auto x = static_cast<float>(at % samples);
            const auto z = static_cast<float>(at / samples);
            heights[at] = 8.0f * std::sin(x * 0.05f) * std::cos(z * 0.04f);
        }

        BodyDesc desc;
        desc.shape.type = ShapeType::HeightField;
        desc.shape.size = core::Vec3{64.0f, 1.0f, 64.0f};
        desc.shape.heights = heights;
        desc.shape.heightSampleCount = samples;
        desc.shape.heightMin = -64.0f;
        desc.shape.heightMax = 64.0f;
        desc.motion = MotionType::Static;

        const auto beforeCreate = std::chrono::steady_clock::now();
        const BodyHandle field = fixture.physics->createBody(fixture.world, desc);
        const auto afterCreate = std::chrono::steady_clock::now();
        REQUIRE(field.valid());

        // **The brush case**: an 8 m stroke is 16 samples at this resolution,
        // grown to the block alignment `updateHeightField` requires.
        const std::vector<float> patch(16 * 16, 3.0f);
        const auto beforeEdit = std::chrono::steady_clock::now();
        for (int i = 0; i < 100; ++i) {
            REQUIRE(fixture.physics->updateHeightField(fixture.world, field, 16, 16, 16, 16, patch));
        }
        const auto afterEdit = std::chrono::steady_clock::now();

        // **What it replaces.** The same visible change through the only other
        // route there is: a full description, destroyed and recreated.
        const auto beforeRebuild = std::chrono::steady_clock::now();
        for (int i = 0; i < 10; ++i) {
            REQUIRE(fixture.physics->updateBody(fixture.world, field, desc));
        }
        const auto afterRebuild = std::chrono::steady_clock::now();

        MESSAGE("heightfield " << samples << "^2  create=" << ms(beforeCreate, afterCreate)
                               << " ms  setHeights(16^2)=" << (ms(beforeEdit, afterEdit) / 100.0)
                               << " ms  rebuild=" << (ms(beforeRebuild, afterRebuild) / 10.0) << " ms");
    }

    // A triangle mesh of comparable density, which is what a bricked cell costs.
    for (const u32 grid : {u32{64}, u32{128}}) {
        std::vector<core::Vec3> points;
        points.reserve(static_cast<core::usize>(grid) * grid);
        for (u32 z = 0; z < grid; ++z) {
            for (u32 x = 0; x < grid; ++x) {
                points.push_back(core::Vec3{static_cast<float>(x), 0.0f, static_cast<float>(z)});
            }
        }
        std::vector<u32> indices;
        indices.reserve(static_cast<core::usize>(grid - 1) * (grid - 1) * 6);
        for (u32 z = 0; z + 1 < grid; ++z) {
            for (u32 x = 0; x + 1 < grid; ++x) {
                const u32 a = z * grid + x;
                indices.insert(indices.end(), {a, a + grid, a + 1, a + 1, a + grid, a + grid + 1});
            }
        }

        BodyDesc desc;
        desc.shape.type = ShapeType::TriangleMesh;
        desc.shape.points = points;
        desc.shape.indices = indices;
        desc.motion = MotionType::Static;

        const auto before = std::chrono::steady_clock::now();
        const BodyHandle mesh = fixture.physics->createBody(fixture.world, desc);
        const auto after = std::chrono::steady_clock::now();
        REQUIRE(mesh.valid());

        MESSAGE("trianglemesh " << grid << "^2 grid, " << (indices.size() / 3)
                                << " triangles  create=" << ms(before, after) << " ms");
    }
}
