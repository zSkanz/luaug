// The navigation seam, compiled and implemented once.
//
// This does not test navigation, because there is none (ADR 0022, and the top of
// `nav.h`). What it tests is that the seam is valid C++ and that a class can
// actually satisfy it -- which is the only thing that can rot in a header with
// no callers, and the reason `engine/nav` has a target at all.
//
// The stub is also the seam's first reader, which is worth more than it sounds:
// writing it is what would expose a signature that cannot be implemented, an
// `std::optional` that should have been a pair, or a `const` in the wrong place.
#include "luaug/nav/nav.h"

#include <doctest/doctest.h>
#include <memory>

using namespace luaug;
using namespace luaug::nav;

namespace {

// A world consisting of one flat tile at the origin, which is enough to have an
// opinion about every method.
class FlatGround final : public INavigation
{
public:
    [[nodiscard]] bool tileResident(NavTileId tile) const noexcept override
    {
        return tile.x == 0 && tile.z == 0 && tile.layer == 0;
    }

    [[nodiscard]] std::optional<NavPath> findPath(core::DVec3 from, core::DVec3 to, const NavAgent& agent) override
    {
        (void)agent;
        if (!onGround(from) || !onGround(to)) {
            return std::nullopt;
        }
        NavPath path;
        path.points = {from, to};
        path.complete = true;
        return path;
    }

    [[nodiscard]] std::optional<core::DVec3> nearestPoint(core::DVec3 point, f32 maxDistance,
                                                          const NavAgent& agent) const override
    {
        (void)agent;
        const core::DVec3 projected{point.x, 0.0, point.z};
        if (std::abs(point.y) > static_cast<core::f64>(maxDistance) || !onGround(projected)) {
            return std::nullopt;
        }
        return projected;
    }

    [[nodiscard]] core::DVec3 raycastWalkable(core::DVec3 from, core::DVec3 to, const NavAgent& agent) const override
    {
        (void)agent;
        return onGround(to) ? to : from;
    }

private:
    [[nodiscard]] static bool onGround(core::DVec3 point) noexcept
    {
        return std::abs(point.x) <= 128.0 && std::abs(point.z) <= 128.0;
    }
};

} // namespace

TEST_CASE("the navigation seam can be implemented")
{
    // Held by the INTERFACE, never by the concrete type: the whole point of a
    // seam is that a caller cannot see what is behind it (R17).
    const std::unique_ptr<INavigation> nav = std::make_unique<FlatGround>();

    CHECK(nav->tileResident({.x = 0, .z = 0, .layer = 0}));
    CHECK_FALSE(nav->tileResident({.x = 4, .z = 0, .layer = 0}));

    const NavAgent agent;
    const auto path = nav->findPath({0.0, 0.0, 0.0}, {10.0, 0.0, 10.0}, agent);
    REQUIRE(path.has_value());
    CHECK(path->complete);
    CHECK(path->points.size() == 2);

    // Off the mesh is `nullopt`, not an empty path. A caller that cannot tell
    // "no way there" from "a path of no steps" is one that spins.
    CHECK_FALSE(nav->findPath({0.0, 0.0, 0.0}, {5000.0, 0.0, 0.0}, agent).has_value());

    CHECK(nav->nearestPoint({3.0, 1.0, 4.0}, 2.0f, agent).has_value());
    CHECK_FALSE(nav->nearestPoint({3.0, 90.0, 4.0}, 2.0f, agent).has_value());
}

TEST_CASE("a path is absolute f64, because a rebase happens between frames")
{
    // The one thing about this seam that is a real decision rather than a
    // placeholder (ADR 0014). A path is held across frames; the floating origin
    // moves between them; a path in rebased f32 space would silently drift out
    // from under whoever is walking it.
    static_assert(std::is_same_v<decltype(NavPath::points)::value_type, core::DVec3>,
                  "a path must be absolute world coordinates");

    const std::unique_ptr<INavigation> nav = std::make_unique<FlatGround>();
    const auto path = nav->findPath({0.0, 0.0, 0.0}, {1.0, 0.0, 1.0}, NavAgent{});
    REQUIRE(path.has_value());
    CHECK(path->points.front().x == doctest::Approx(0.0));
}

TEST_CASE("areas are a set rather than a choice")
{
    // A query asks what it may walk on, and that is a SET -- "walkable or
    // swimmable, but not the door I have no key for". An enum would force a
    // caller to run one query per area and merge the answers.
    const NavArea both = NavArea::Walkable | NavArea::Water;
    CHECK(static_cast<core::u32>(both) ==
          (static_cast<core::u32>(NavArea::Walkable) | static_cast<core::u32>(NavArea::Water)));
    CHECK(static_cast<core::u32>(NavArea::None) == 0u);
}
