// Navigation: where an agent can walk, and how it gets somewhere (ADR 0089).
//
// **A tiled navmesh, built where it is asked for.** A query builds -- or
// rebuilds, if what stands in it changed -- the tiles its two ends span, and a
// path through ground no query has reached comes back partial. Nothing is baked
// offline: the world is data that a script and the editor both change, and an
// asset that goes stale the moment a wall moves is the failure this avoids.
//
// **What is walkable is what is static and solid**: anchored, colliding parts
// under `Workspace`, and `Terrain`'s ground. Recast builds it and Detour
// answers from it, behind this header -- no Recast type appears here (R17).
//
// **Deterministic** (R10): Recast is single-threaded and fed in a fixed order,
// tiles are built in tile order, and every query is a function of the world and
// the queries before it.
#pragma once

#include "luaug/core/id.h"
#include "luaug/core/math.h"
#include "luaug/core/types.h"

#include <memory>
#include <optional>
#include <vector>

namespace luaug::scene {
class World;
}

namespace luaug::nav {

using core::f32;
using core::u32;
using core::u64;
using core::usize;

// The one agent a mesh is built for. A mesh is eroded by the agent's radius,
// so a second size is a second mesh -- which is what every engine adds as its
// second agent type, and not what the first needs.
struct NavAgent
{
    f32 radius = 0.5f;
    f32 height = 2.0f;
    // How high a step it walks up rather than around.
    f32 maxClimb = 0.5f;
    // Degrees.
    f32 maxSlope = 45.0f;

    [[nodiscard]] constexpr bool operator==(const NavAgent&) const noexcept = default;
};

struct NavPath
{
    // World-space and absolute, f64 like every position in the tree (ADR 0014).
    std::vector<core::DVec3> points;

    // False when the path stops short of the goal -- the goal is off the mesh,
    // or past ground no query has built. A partial path is useful: walking most
    // of the way and asking again is what a game wants.
    bool complete = false;
};

class INavigation
{
public:
    virtual ~INavigation() = default;

    // `Workspace`: what is under it is the world.
    virtual void setWorkspace(core::InstanceId workspace) noexcept = 0;
    // The agent the mesh is for. A different one invalidates every tile.
    virtual void setAgent(const NavAgent& agent) = 0;
    // The simulation tick. What stands in the world is gathered again on a new
    // tick, and within one only after a write through the world's own verbs
    // (`World::mutations`) or a terrain edit -- so a wall a script builds is
    // seen by its next query, and a quiet write into a component (the physics
    // mirror's) is seen by the next tick's.
    virtual void setTick(u64 tick) noexcept = 0;

    // A path between two absolute points. `std::nullopt` means there is none
    // from here: the start is not on the mesh. A path that does not reach the
    // goal comes back with `complete` false.
    [[nodiscard]] virtual std::optional<NavPath> findPath(core::DVec3 from, core::DVec3 to) = 0;

    // The nearest point on the mesh within `maxDistance`: what a spawn, a
    // teleport and a click-to-move need before they can ask for a path.
    [[nodiscard]] virtual std::optional<core::DVec3> nearestPoint(core::DVec3 point, f32 maxDistance) = 0;

    // A straight walk from `from` towards `to`, stopping where the mesh does --
    // the cheap question an agent asks before a search. `std::nullopt` when
    // `from` is not on the mesh.
    [[nodiscard]] virtual std::optional<core::DVec3> raycast(core::DVec3 from, core::DVec3 to) = 0;

    // Builds (or rebuilds, where dirty) every tile over a box, ahead of the
    // first path through it. Answers how many tiles it built.
    virtual usize buildRegion(core::DVec3 minimum, core::DVec3 maximum) = 0;

    // Forgets every tile; the next query rebuilds what it needs.
    virtual void invalidate() = 0;

    // Tiles built and holding a mesh.
    [[nodiscard]] virtual usize tileCount() const noexcept = 0;
};

// Over Recast/Detour, reading `world`. Null in a build without it.
[[nodiscard]] std::unique_ptr<INavigation> createNavigation(const scene::World& world);

} // namespace luaug::nav
