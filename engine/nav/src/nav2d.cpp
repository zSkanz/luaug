// Paths on the 2D plane (ADR 0098).
//
// A 2D world is a grid more often than a mesh: a level painted in tiles is
// already one, and a mesh built over it would be a slower way of knowing the
// same thing. So this is A* over cells -- the first `Tilemap2D`'s size and
// alignment, or a metre where there is none -- in a window around the two
// ends, with every colliding tile and every anchored, colliding `Part2D` a
// wall. Deterministic by construction: a fixed window, a fixed neighbour
// order, and ties broken by cell index.
#include "luaug/nav/nav.h"
#include "luaug/scene/components.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <tuple>
#include <vector>

namespace luaug::nav {
namespace {

using core::f64;
using core::i32;
using core::i64;
using core::Vec2;

// How far past the two ends the search may go, in cells, and the most cells a
// window may hold -- a path across a continent of tiles is refused rather than
// allowed to stall the tick.
constexpr i32 MarginCells = 48;
constexpr i64 MostCells = 512 * 512;

// Whether a point is inside a part's box, turned.
[[nodiscard]] bool insidePart(const scene::Part2DComponent& part, Vec2 point) noexcept
{
    const f32 c = std::cos(-part.rotation);
    const f32 s = std::sin(-part.rotation);
    const Vec2 offset{point.x - part.position.x, point.y - part.position.y};
    const Vec2 local{offset.x * c - offset.y * s, offset.x * s + offset.y * c};
    return std::abs(local.x) <= part.size.x * 0.5f && std::abs(local.y) <= part.size.y * 0.5f;
}

} // namespace

std::optional<NavPath2D> findPath2D(const scene::World& world, core::InstanceId workspace, Vec2 from, Vec2 to)
{
    const auto inWorld = [&](core::InstanceId id) { return workspace.valid() && world.isAncestorOf(workspace, id); };

    // The grid: the first tilemap's, in instance order.
    f32 cell = 1.0f;
    Vec2 origin{0.0f, 0.0f};
    bool aligned = false;
    world.tilemaps2d().forEach([&](core::InstanceId id, const scene::Tilemap2DComponent& tilemap) {
        if (aligned || !inWorld(id) || tilemap.cellSize <= 0.0f)
            return;
        cell = tilemap.cellSize;
        origin = tilemap.position;
        aligned = true;
    });

    const auto cellOf = [&](f32 value, f32 base) { return static_cast<i32>(std::floor((value - base) / cell)); };
    const i32 fromX = cellOf(from.x, origin.x);
    const i32 fromY = cellOf(from.y, origin.y);
    const i32 toX = cellOf(to.x, origin.x);
    const i32 toY = cellOf(to.y, origin.y);
    const i32 minX = std::min(fromX, toX) - MarginCells;
    const i32 minY = std::min(fromY, toY) - MarginCells;
    const i32 maxX = std::max(fromX, toX) + MarginCells;
    const i32 maxY = std::max(fromY, toY) + MarginCells;
    const i64 width = static_cast<i64>(maxX) - minX + 1;
    const i64 height = static_cast<i64>(maxY) - minY + 1;
    if (width * height > MostCells)
        return std::nullopt;

    // **The walls**, stamped once over the window: tiles, then parts.
    std::vector<unsigned char> blocked(static_cast<std::size_t>(width * height), 0);
    const auto index = [&](i32 x, i32 y) {
        return static_cast<std::size_t>((static_cast<i64>(y) - minY) * width + (static_cast<i64>(x) - minX));
    };
    const auto centre = [&](i32 x, i32 y) {
        return Vec2{origin.x + (static_cast<f32>(x) + 0.5f) * cell, origin.y + (static_cast<f32>(y) + 0.5f) * cell};
    };
    world.tilemaps2d().forEach([&](core::InstanceId id, const scene::Tilemap2DComponent& tilemap) {
        if (!inWorld(id) || !tilemap.collides || tilemap.cellSize <= 0.0f)
            return;
        for (i32 y = minY; y <= maxY; ++y) {
            for (i32 x = minX; x <= maxX; ++x) {
                const Vec2 at = centre(x, y);
                const auto tx = static_cast<i32>(std::floor((at.x - tilemap.position.x) / tilemap.cellSize));
                const auto ty = static_cast<i32>(std::floor((at.y - tilemap.position.y) / tilemap.cellSize));
                if (tilemap.cell(tx, ty) != 0)
                    blocked[index(x, y)] = 1;
            }
        }
    });
    world.parts2d().forEach([&](core::InstanceId id, const scene::Part2DComponent& part) {
        if (!inWorld(id) || !part.anchored || !part.canCollide || part.sensor)
            return;
        const f32 reach = std::sqrt(part.size.x * part.size.x + part.size.y * part.size.y) * 0.5f;
        const i32 x0 = std::max(minX, cellOf(part.position.x - reach, origin.x));
        const i32 x1 = std::min(maxX, cellOf(part.position.x + reach, origin.x));
        const i32 y0 = std::max(minY, cellOf(part.position.y - reach, origin.y));
        const i32 y1 = std::min(maxY, cellOf(part.position.y + reach, origin.y));
        for (i32 y = y0; y <= y1; ++y) {
            for (i32 x = x0; x <= x1; ++x) {
                if (insidePart(part, centre(x, y)))
                    blocked[index(x, y)] = 1;
            }
        }
    });

    if (blocked[index(fromX, fromY)] != 0)
        return std::nullopt;

    // **A* over the window**, eight neighbours, no corner cut.
    constexpr f64 Diagonal = 1.4142135623730951;
    const auto heuristic = [&](i32 x, i32 y) {
        const f64 dx = std::abs(static_cast<f64>(x - toX));
        const f64 dy = std::abs(static_cast<f64>(y - toY));
        return (dx + dy) + (Diagonal - 2.0) * std::min(dx, dy);
    };
    const std::size_t count = blocked.size();
    std::vector<f64> cost(count, std::numeric_limits<f64>::infinity());
    std::vector<std::size_t> cameFrom(count, count);
    std::vector<unsigned char> closed(count, 0);
    using Entry = std::tuple<f64, std::size_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    const std::size_t start = index(fromX, fromY);
    const std::size_t goal = index(toX, toY);
    cost[start] = 0.0;
    open.emplace(heuristic(fromX, fromY), start);
    std::size_t best = start;
    f64 bestH = heuristic(fromX, fromY);
    const i32 offsets[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    while (!open.empty()) {
        const std::size_t current = std::get<1>(open.top());
        open.pop();
        if (closed[current] != 0)
            continue;
        closed[current] = 1;
        const i32 cx = static_cast<i32>(static_cast<i64>(current) % width) + minX;
        const i32 cy = static_cast<i32>(static_cast<i64>(current) / width) + minY;
        if (const f64 h = heuristic(cx, cy); h < bestH) {
            bestH = h;
            best = current;
        }
        if (current == goal)
            break;
        for (const auto& step : offsets) {
            const i32 nx = cx + step[0];
            const i32 ny = cy + step[1];
            if (nx < minX || nx > maxX || ny < minY || ny > maxY)
                continue;
            const std::size_t next = index(nx, ny);
            if (blocked[next] != 0 || closed[next] != 0)
                continue;
            const bool diagonal = step[0] != 0 && step[1] != 0;
            // Around a wall's corner, never across it.
            if (diagonal && (blocked[index(cx + step[0], cy)] != 0 || blocked[index(cx, cy + step[1])] != 0))
                continue;
            const f64 through = cost[current] + (diagonal ? Diagonal : 1.0);
            if (through < cost[next]) {
                cost[next] = through;
                cameFrom[next] = current;
                open.emplace(through + heuristic(nx, ny), next);
            }
        }
    }

    const bool complete = closed[goal] != 0;
    const std::size_t end = complete ? goal : best;
    std::vector<std::size_t> cells;
    for (std::size_t at = end; at != count; at = cameFrom[at])
        cells.push_back(at);
    std::reverse(cells.begin(), cells.end());

    // **Where it turns**: the start, every cell the direction changes at, and
    // the end -- the goal itself when it was reached.
    NavPath2D path;
    path.complete = complete;
    path.points.push_back(from);
    const auto cellXY = [&](std::size_t at) {
        return std::pair{static_cast<i32>(static_cast<i64>(at) % width) + minX,
                         static_cast<i32>(static_cast<i64>(at) / width) + minY};
    };
    for (std::size_t at = 1; at + 1 < cells.size(); ++at) {
        const auto [px, py] = cellXY(cells[at - 1]);
        const auto [x, y] = cellXY(cells[at]);
        const auto [nx, ny] = cellXY(cells[at + 1]);
        if (x - px != nx - x || y - py != ny - y)
            path.points.push_back(centre(x, y));
    }
    if (complete)
        path.points.push_back(to);
    else if (cells.size() > 1) {
        const auto [x, y] = cellXY(cells.back());
        path.points.push_back(centre(x, y));
    }
    return path;
}

} // namespace luaug::nav
