#include "luaug/app/chunk_overlay.h"

#include "luaug/app/streaming_host.h"
#include "luaug/asset/chunk.h"
#include "luaug/render/debug_draw.h"

#include <vector>

namespace luaug::app {

using core::f32;
using core::f64;

namespace {

// What a cell's state looks like. Deliberately not a gradient: these are five
// discrete answers and a person reading a grid needs to count them, not
// interpolate between them.
//
// Resident is green because it is the state everything else is on its way to;
// loading is amber because it is the one that is allowed to be temporary;
// failed is red because it is the only one that is not.
[[nodiscard]] render::DebugColor colorOf(asset::ChunkState state) noexcept
{
    switch (state) {
    case asset::ChunkState::Resident:
        return render::DebugColor::fromLinear(0.25f, 0.85f, 0.35f);
    case asset::ChunkState::Decoded:
        return render::DebugColor::fromLinear(0.25f, 0.70f, 0.95f);
    case asset::ChunkState::Loading:
        return render::DebugColor::fromLinear(0.95f, 0.70f, 0.20f);
    case asset::ChunkState::Failed:
        return render::DebugColor::fromLinear(0.95f, 0.25f, 0.25f);
    case asset::ChunkState::Unloaded:
        break;
    }
    // Known to the index and nothing more, which is most of a big world -- so it
    // is drawn dim rather than not at all. Where the grid ENDS is a fact worth
    // seeing: a world that stops half a kilometre short of where somebody is
    // walking looks exactly like a streaming bug until you can see the edge.
    return render::DebugColor::fromLinear(0.28f, 0.28f, 0.32f);
}

// The ring a focus is asking for, as a square rather than a circle, because the
// radius is compared against a CELL and the cells are square. A circle here
// would be a prettier picture of a different rule.
void drawRing(core::DVec3 at, f64 radius, f64 groundY, render::DebugColor color, render::DebugDraw& draw)
{
    const auto corner = [&](f64 dx, f64 dz) {
        return core::toVec3(core::DVec3{at.x + dx * radius, groundY, at.z + dz * radius});
    };
    draw.line(corner(-1.0, -1.0), corner(1.0, -1.0), color);
    draw.line(corner(1.0, -1.0), corner(1.0, 1.0), color);
    draw.line(corner(1.0, 1.0), corner(-1.0, 1.0), color);
    draw.line(corner(-1.0, 1.0), corner(-1.0, -1.0), color);
}

} // namespace

void drawChunkGrid(const StreamingHost& streaming, f64 groundY, render::DebugDraw& draw)
{
    if (!streaming.active())
        return;

    const f32 chunkSize = streaming.index().chunkSize;
    if (chunkSize <= 0.0f)
        return;

    const std::vector<asset::StreamingManager::ChunkView> cells = streaming.view();
    for (const asset::StreamingManager::ChunkView& cell : cells) {
        const core::DAABB bounds = asset::chunkBounds(cell.id, chunkSize);
        const render::DebugColor color = colorOf(cell.state);

        // **One layer above another, a metre apart.** All three size classes
        // share one grid size, so their cells land on exactly the same lines and
        // drawing them at one height would make three grids look like one --
        // which is the thing the panel already gets wrong and the reason it
        // draws three separate maps.
        const f64 y = groundY + static_cast<f64>(cell.id.layer);

        const auto at = [&](f64 x, f64 z) { return core::toVec3(core::DVec3{x, y, z}); };
        const core::Vec3 a = at(bounds.min.x, bounds.min.z);
        const core::Vec3 b = at(bounds.max.x, bounds.min.z);
        const core::Vec3 c = at(bounds.max.x, bounds.max.z);
        const core::Vec3 d = at(bounds.min.x, bounds.max.z);
        draw.line(a, b, color);
        draw.line(b, c, color);
        draw.line(c, d, color);
        draw.line(d, a, color);

        // A post at one corner, so a cell reads as a cell from inside it. All
        // four would quadruple the line count for a picture that is no clearer:
        // from ground level the near edge of a square is a line, and one
        // vertical is what tells you which side of it you are on.
        draw.line(a, a + core::Vec3{0.0f, kChunkPostMetres, 0.0f}, color);
    }

    // Every focus's own two radii, so "why is that cell not loading" has a
    // picture rather than a number: a cell outside the load ring is not a bug,
    // and a cell inside the min ring that is not resident is.
    const render::DebugColor minColor = render::DebugColor::fromLinear(0.95f, 0.95f, 0.35f);
    const render::DebugColor loadColor = render::DebugColor::fromLinear(0.55f, 0.55f, 0.65f);
    for (const asset::StreamingFocus& focus : streaming.collectFoci()) {
        // Layer 0's pair. A focus carries one per size class and drawing six
        // squares around one character is a picture nobody can read -- the
        // detail layer is the one whose ring a person is standing on.
        drawRing(focus.position, focus.minRadiusFor(0), groundY, minColor, draw);
        drawRing(focus.position, focus.loadRadiusFor(0), groundY, loadColor, draw);
    }
}

} // namespace luaug::app
