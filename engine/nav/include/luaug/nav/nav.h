// The navigation seam. **Nothing implements it, and nothing is meant to in v1.**
//
// M7's scope bullet is "Recast/Detour: vendored, seam defined, **no
// integration** (explicit non-goal, ADR 0022)", and R15 puts navmesh generation
// outside v1. So this header is the whole deliverable: Recast is pinned in the
// manifest at 1.6.0, its four library directories are on disk, and there is
// deliberately no CMake target for them -- a target with no caller would make
// the layering gate answer "who links Recast" with a name instead of the honest
// "nobody yet".
//
// **Why a header at all, if nothing calls it.** Because the expensive part of
// navigation is not the library, it is the decisions about where it sits: who
// owns the mesh, when it is built, what a query costs, and what a game asks for.
// Those decisions are cheap now, while the world format and the streaming
// manager are freshly in mind, and expensive later. What is written down here is
// the shape M7 believes; whoever builds it post-v1 is free to disagree, and will
// at least be disagreeing with something.
//
// **What is NOT decided here, deliberately**: the generation pipeline (offline,
// at load, or incremental), the agent/crowd model, dynamic obstacles, off-mesh
// links, and anything to do with cost fields. Every one of those is a design,
// and half a design in a header is worse than none.
#pragma once

#include "luaug/core/math.h"
#include "luaug/core/types.h"

#include <optional>
#include <span>
#include <vector>

namespace luaug::nav {

using core::f32;
using core::u16;
using core::u32;
using core::usize;

// A navigation mesh's identity, so that a streamed world can hold one per chunk.
//
// **Per chunk rather than per world, and that is the one thing this seam really
// does commit to.** A world larger than memory (architecture.md §10) cannot have
// a single navmesh: it would have to be built for terrain that is not resident
// and rebuilt whenever anything streamed in. Detour is tiled for exactly this
// reason, and a tile lining up with a streaming chunk is what makes residency
// one question rather than two.
struct NavTileId
{
    core::i32 x = 0;
    core::i32 z = 0;
    u16 layer = 0;

    friend constexpr bool operator==(NavTileId, NavTileId) noexcept = default;
};

// What a query is allowed to walk. A bitmask rather than an enum because a query
// asks for a SET -- "walkable or swimmable, but not the door I have no key for"
// -- and a set is what a mask is.
enum class NavArea : u32
{
    None = 0,
    Walkable = 1u << 0,
    Water = 1u << 1,
    Jump = 1u << 2,
    Door = 1u << 3,
    All = 0xFFFFFFFFu,
};

[[nodiscard]] constexpr NavArea operator|(NavArea a, NavArea b) noexcept
{
    return static_cast<NavArea>(static_cast<u32>(a) | static_cast<u32>(b));
}

// The agent a path is being found FOR. Radius and height are what turn a mesh
// into a walkable surface for a particular body, and they belong to the query
// rather than to the mesh: one world holds agents of several sizes, and building
// a mesh per size is the mistake this parameter exists to avoid making later.
struct NavAgent
{
    f32 radius = 0.5f;
    f32 height = 2.0f;
    // How far up a step the agent walks over rather than around. Matches
    // `CharacterBody.AutoStepHeight`, which is where the number should come
    // from: an agent that plans a path its own controller cannot walk is worse
    // than one that finds no path.
    f32 stepHeight = 0.35f;
    // Degrees. Matches `CharacterBody.MaxSlopeAngle` for the same reason.
    f32 maxSlopeAngle = 45.0f;
    NavArea areas = NavArea::Walkable;
};

struct NavPath
{
    // World-space, **f64** and absolute, like everything in the tree (ADR 0014).
    // A path is held across frames and a floating origin rebase happens between
    // them, so a path in rebased f32 space would silently move under its owner.
    std::vector<core::DVec3> points;

    // False when the path stops short of the goal. A partial path is USEFUL --
    // walking most of the way and re-planning is what a game wants -- so it is
    // returned rather than discarded, and the flag is how a caller tells.
    bool complete = false;
};

// The seam a future navigation module would implement.
//
// Poll-shaped and synchronous, with no callbacks, for the reason `ITransport`
// gives: a result that arrives whenever the work finished is a wall-clock entry
// into game code, and R10 does not survive one. A build that wants to spread
// path-finding over frames returns a partial path and is asked again.
class INavigation
{
public:
    virtual ~INavigation() = default;

    // Whether a tile is built and queryable. A streamed world asks this before
    // it asks for a path, and gets `false` for terrain that has not arrived.
    [[nodiscard]] virtual bool tileResident(NavTileId tile) const noexcept = 0;

    // A path between two absolute world points. `std::nullopt` means no path
    // exists; a `NavPath` with `complete == false` means one may exist but this
    // call did not reach it.
    //
    // The distinction is the interesting half: a caller that cannot tell "there
    // is no way there" from "I ran out of budget" either gives up on reachable
    // goals or retries unreachable ones forever.
    [[nodiscard]] virtual std::optional<NavPath> findPath(core::DVec3 from, core::DVec3 to, const NavAgent& agent) = 0;

    // The nearest point on the mesh to `point`, within `maxDistance`. What a
    // spawn, a teleport and a click-to-move all need before they can ask for a
    // path at all.
    [[nodiscard]] virtual std::optional<core::DVec3> nearestPoint(core::DVec3 point, f32 maxDistance,
                                                                  const NavAgent& agent) const = 0;

    // A straight walk from `from` towards `to`, stopping where the mesh does.
    // The cheap query, and the one an agent should try first: most steps of most
    // paths are a straight line, and a full search per frame is how navigation
    // becomes the frame's cost centre.
    [[nodiscard]] virtual core::DVec3 raycastWalkable(core::DVec3 from, core::DVec3 to,
                                                      const NavAgent& agent) const = 0;
};

// There is deliberately no factory here, and no `createRecastNavigation()`.
// ADR 0022 says the seam is defined and NOT integrated; a factory declaration
// with no definition is a link error waiting for whoever tries, which is a worse
// way to learn this than reading the top of this file.

} // namespace luaug::nav
