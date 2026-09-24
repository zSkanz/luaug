#include "luaug/scene/tile_outline.h"

#include <algorithm>
#include <map>
#include <utility>

namespace luaug::scene {
namespace {

struct Corner
{
    i32 x = 0;
    i32 y = 0;
    [[nodiscard]] constexpr auto operator<=>(const Corner&) const noexcept = default;
};

// A directed boundary edge, solid on its left.
struct Edge
{
    Corner from;
    Corner to;
    bool used = false;
};

// Which way an edge points, as an index turning counter-clockwise: +x, +y,
// -x, -y. A left turn is +1, straight on 0, a right turn +3.
[[nodiscard]] int directionOf(const Edge& edge) noexcept
{
    if (edge.to.x > edge.from.x)
        return 0;
    if (edge.to.y > edge.from.y)
        return 1;
    if (edge.to.x < edge.from.x)
        return 2;
    return 3;
}

} // namespace

std::vector<std::vector<core::Vec2>> tileOutlines(const Tilemap2DComponent& tilemap)
{
    std::vector<Edge> edges;
    for (const auto& [key, chunk] : tilemap.chunks) {
        for (i32 ly = 0; ly < TileChunkEdge; ++ly) {
            for (i32 lx = 0; lx < TileChunkEdge; ++lx) {
                if (chunk[static_cast<usize>(ly * TileChunkEdge + lx)] == 0)
                    continue;
                const i32 x = key.x * TileChunkEdge + lx;
                const i32 y = key.y * TileChunkEdge + ly;
                // Each side facing an empty cell, directed so the solid cell is
                // on its left.
                if (tilemap.cell(x, y - 1) == 0)
                    edges.push_back(Edge{{x, y}, {x + 1, y}});
                if (tilemap.cell(x + 1, y) == 0)
                    edges.push_back(Edge{{x + 1, y}, {x + 1, y + 1}});
                if (tilemap.cell(x, y + 1) == 0)
                    edges.push_back(Edge{{x + 1, y + 1}, {x, y + 1}});
                if (tilemap.cell(x - 1, y) == 0)
                    edges.push_back(Edge{{x, y + 1}, {x, y}});
            }
        }
    }
    // Edges by where they start, in key order, so the walk below is a function
    // of the map and not of the order its blocks were painted in.
    std::sort(edges.begin(), edges.end(),
              [](const Edge& a, const Edge& b) { return a.from != b.from ? a.from < b.from : a.to < b.to; });
    std::multimap<Corner, usize> starting;
    for (usize at = 0; at < edges.size(); ++at)
        starting.emplace(edges[at].from, at);

    std::vector<std::vector<core::Vec2>> loops;
    for (usize first = 0; first < edges.size(); ++first) {
        if (edges[first].used)
            continue;
        std::vector<Corner> corners;
        usize current = first;
        while (!edges[current].used) {
            edges[current].used = true;
            corners.push_back(edges[current].from);
            // Where two regions touch at a corner two edges start there. The
            // sharpest LEFT turn keeps to the region being walked round, so
            // each region closes a loop of its own.
            const int incoming = directionOf(edges[current]);
            usize next = edges.size();
            int bestTurn = 4;
            const auto [begin, end] = starting.equal_range(edges[current].to);
            for (auto candidate = begin; candidate != end; ++candidate) {
                if (edges[candidate->second].used && candidate->second != first)
                    continue;
                const int turn = (directionOf(edges[candidate->second]) - incoming + 4) % 4;
                // Left (1) before straight (0) before right (3); never back (2).
                const int rank = turn == 1 ? 0 : turn == 0 ? 1 : turn == 3 ? 2 : 3;
                if (rank < bestTurn) {
                    bestTurn = rank;
                    next = candidate->second;
                }
            }
            if (next == edges.size() || next == first)
                break;
            current = next;
        }

        // Straight runs as one segment: a corner where the direction does not
        // change is not a corner.
        std::vector<core::Vec2> loop;
        const usize count = corners.size();
        for (usize at = 0; at < count; ++at) {
            const Corner& before = corners[(at + count - 1) % count];
            const Corner& here = corners[at];
            const Corner& after = corners[(at + 1) % count];
            const i32 inX = here.x - before.x;
            const i32 inY = here.y - before.y;
            const i32 outX = after.x - here.x;
            const i32 outY = after.y - here.y;
            if (inX * outY - inY * outX == 0)
                continue;
            loop.push_back(core::Vec2{static_cast<f32>(here.x), static_cast<f32>(here.y)});
        }
        if (loop.size() >= 3)
            loops.push_back(std::move(loop));
    }
    return loops;
}

} // namespace luaug::scene
