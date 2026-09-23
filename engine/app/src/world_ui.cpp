#include "luaug/app/world_ui.h"

#include "luaug/app/picking.h"
#include "luaug/scene/components.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace luaug::app {
namespace {

using core::f32;
using core::f64;
using core::u32;
using core::Vec2;
using core::Vec3;

[[nodiscard]] Vec3 relative(core::DVec3 point, core::DVec3 origin) noexcept
{
    return Vec3{static_cast<f32>(point.x - origin.x), static_cast<f32>(point.y - origin.y),
                static_cast<f32>(point.z - origin.z)};
}

// The face's outward normal, its right and its up as the viewer standing in
// front of it reads them, in the part's own axes. `Front` is `-Z`, the way the
// part's `LookVector` points; a side face is read upright; the top is read with
// the part's front at its top edge, and the bottom as seen from below.
struct FaceFrame
{
    Vec3 normal;
    Vec3 right;
    Vec3 up;
};

[[nodiscard]] FaceFrame faceFrame(core::i32 face) noexcept
{
    switch (face) {
    case 1: // Back
        return {{0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    case 2: // Top
        return {{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}};
    case 3: // Bottom
        return {{0.0f, -1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}};
    case 4: // Right
        return {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}};
    case 5: // Left
        return {{-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}};
    default: // Front
        return {{0.0f, 0.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    }
}

// The size of the part along a direction in its own axes -- one of the three.
[[nodiscard]] f32 extentAlong(Vec3 axis, Vec3 size) noexcept
{
    return std::abs(axis.x) * size.x + std::abs(axis.y) * size.y + std::abs(axis.z) * size.z;
}

// Whether `id` is in a place world UI is drawn from: under the workspace, or
// under the UI service with an adornee in the world.
[[nodiscard]] bool under(const scene::World& world, core::InstanceId id, core::InstanceId root) noexcept
{
    for (core::InstanceId cursor = world.parentOf(id); cursor.valid(); cursor = world.parentOf(cursor)) {
        if (cursor == root)
            return true;
    }
    return false;
}

// The part a gui hangs on: its adornee, or its parent when that is a part.
[[nodiscard]] core::InstanceId partFor(const scene::World& world, core::InstanceId gui, core::InstanceId adornee)
{
    if (adornee.valid() && world.alive(adornee) && world.parts().find(adornee) != nullptr)
        return adornee;
    const core::InstanceId parent = world.parentOf(gui);
    return world.parts().find(parent) != nullptr ? parent : core::InstanceId{};
}

[[nodiscard]] core::u8 toByte(f32 value) noexcept
{
    return static_cast<core::u8>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

// One tree's quads, placed and appended, and grouped into runs.
void emitCanvas(const ui::DrawList& list, const CanvasPlacement& placement, f32 brightness, bool onTop,
                std::span<const rhi::TextureHandle> textures, render::RenderWorld& out)
{
    for (const ui::DrawQuad& quad : list.quads) {
        f32 minX = quad.min.x;
        f32 minY = quad.min.y;
        f32 maxX = quad.max.x;
        f32 maxY = quad.max.y;
        f32 u0 = quad.uvMin.x;
        f32 v0 = quad.uvMin.y;
        f32 u1 = quad.uvMax.x;
        f32 v1 = quad.uvMax.y;

        // **The clip, on the canvas and on the CPU**: a scissor is a screen
        // rectangle and this is not on the screen. An upright quad is cut to
        // it with its UVs cut in proportion; a turned or leaning one is drawn
        // whole, which is the one thing a `ClipsDescendants` in the world does
        // not do.
        const bool upright = quad.turn.x == 1.0f && quad.turn.y == 0.0f && quad.slant == 0.0f;
        if (upright && quad.scissor < list.scissors.size() && quad.scissor != 0) {
            const core::Rect& clip = list.scissors[quad.scissor];
            const f32 width = maxX - minX;
            const f32 height = maxY - minY;
            const f32 left = std::max(minX, clip.min.x);
            const f32 top = std::max(minY, clip.min.y);
            const f32 right = std::min(maxX, clip.max.x);
            const f32 bottom = std::min(maxY, clip.max.y);
            if (!(right > left) || !(bottom > top))
                continue;
            if (width > 0.0f && height > 0.0f) {
                const f32 du = (u1 - u0) / width;
                const f32 dv = (v1 - v0) / height;
                u0 += (left - minX) * du;
                u1 -= (maxX - right) * du;
                v0 += (top - minY) * dv;
                v1 -= (maxY - bottom) * dv;
            }
            minX = left;
            minY = top;
            maxX = right;
            maxY = bottom;
        }

        const rhi::TextureHandle texture =
            quad.texture < textures.size() ? textures[quad.texture] : rhi::TextureHandle{};
        const u32 firstVertex = static_cast<u32>(out.worldUiVertices.size());
        render::WorldUiRun* run = out.worldUiRuns.empty() ? nullptr : &out.worldUiRuns.back();
        if (run == nullptr || run->texture != texture || run->brightness != brightness || run->alwaysOnTop != onTop ||
            run->firstVertex + run->vertexCount != firstVertex) {
            out.worldUiRuns.push_back(render::WorldUiRun{firstVertex, 0, texture, brightness, onTop});
            run = &out.worldUiRuns.back();
        }

        // The same corner arithmetic as the screen's (`buildUiGeometry`): the
        // lean first, then the turn, then -- here -- the canvas into the world.
        const f32 halfX = (quad.max.x - quad.min.x) * 0.5f;
        const f32 halfY = (quad.max.y - quad.min.y) * 0.5f;
        const f32 centreX = quad.min.x + halfX;
        const f32 centreY = quad.min.y + halfY;
        const f32 lean = quad.slant * (quad.max.y - quad.min.y);
        const auto corner = [&](f32 x, f32 y, f32 u, f32 v, f32 shift) {
            const f32 placedX = x + shift;
            const f32 turnedX = quad.turn.x * placedX - quad.turn.y * y + quad.turnOffset.x;
            const f32 turnedY = quad.turn.y * placedX + quad.turn.x * y + quad.turnOffset.y;
            const Vec3 world = placement.topLeft + placement.right * turnedX + placement.down * turnedY;
            render::WorldUiVertex vertex;
            vertex.x = world.x;
            vertex.y = world.y;
            vertex.z = world.z;
            vertex.r = toByte(quad.color.r);
            vertex.g = toByte(quad.color.g);
            vertex.b = toByte(quad.color.b);
            vertex.a = toByte(quad.alpha);
            vertex.localX = x - centreX;
            vertex.localY = y - centreY;
            vertex.halfX = halfX;
            vertex.halfY = halfY;
            vertex.radius = quad.cornerRadius;
            vertex.u = u;
            vertex.v = v;
            return vertex;
        };
        const render::WorldUiVertex a = corner(minX, minY, u0, v0, lean);
        const render::WorldUiVertex b = corner(maxX, minY, u1, v0, lean);
        const render::WorldUiVertex c = corner(maxX, maxY, u1, v1, 0.0f);
        const render::WorldUiVertex d = corner(minX, maxY, u0, v1, 0.0f);
        out.worldUiVertices.insert(out.worldUiVertices.end(), {a, b, c, a, c, d});
        run->vertexCount += 6;
    }
}

} // namespace

std::optional<CanvasPlacement> placeBillboard(const scene::BillboardGuiComponent& gui, core::DVec3 anchor,
                                              const render::RenderCamera& camera, Vec2 viewport)
{
    const Vec3 centre = relative(anchor, camera.origin);
    const f32 distance = core::length(centre);
    if (gui.maxDistance > 0.0f && distance > gui.maxDistance)
        return std::nullopt;

    // Behind the camera's plane is nowhere to draw a thing that faces it.
    const core::Mat4 cameraToWorld = core::inverse(camera.view);
    const Vec3 right = core::normalize(core::transformDirection(cameraToWorld, Vec3{1.0f, 0.0f, 0.0f}));
    const Vec3 up = core::normalize(core::transformDirection(cameraToWorld, Vec3{0.0f, 1.0f, 0.0f}));
    const Vec3 forward = core::normalize(core::transformDirection(cameraToWorld, Vec3{0.0f, 0.0f, -1.0f}));
    const f32 depth = core::dot(centre, forward);
    if (!(depth > 0.0f))
        return std::nullopt;

    // **A metre is a metre and a pixel is a pixel.** One screen pixel at this
    // depth is `2 * depth / (projection[1][1] * height)` metres, so an offset
    // keeps its size on the screen and a scale keeps its size in the world.
    const f32 focal = camera.projection.m[1][1];
    const f32 metresPerPixel = focal > 0.0f && viewport.y > 0.0f ? 2.0f * depth / (focal * viewport.y) : 0.0f;
    const Vec2 canvas{gui.size.x.scale * BillboardPixelsPerMetre + gui.size.x.offset,
                      gui.size.y.scale * BillboardPixelsPerMetre + gui.size.y.offset};
    const Vec2 world{gui.size.x.scale + gui.size.x.offset * metresPerPixel,
                     gui.size.y.scale + gui.size.y.offset * metresPerPixel};
    if (!(canvas.x > 0.0f) || !(canvas.y > 0.0f) || !(world.x > 0.0f) || !(world.y > 0.0f))
        return std::nullopt;

    CanvasPlacement placement;
    placement.canvas = canvas;
    placement.right = right * (world.x / canvas.x);
    placement.down = up * -(world.y / canvas.y);
    placement.topLeft = centre - right * (world.x * 0.5f) + up * (world.y * 0.5f);
    placement.distance = distance;
    return placement;
}

std::optional<CanvasPlacement> placeSurface(const scene::SurfaceGuiComponent& gui, const core::CFrameD& part, Vec3 size,
                                            core::DVec3 cameraOrigin)
{
    const FaceFrame frame = faceFrame(gui.face);
    const f32 width = extentAlong(frame.right, size);
    const f32 height = extentAlong(frame.up, size);
    const f32 depth = extentAlong(frame.normal, size);
    if (!(width > 0.0f) || !(height > 0.0f) || !(gui.pixelsPerMetre > 0.0f))
        return std::nullopt;

    const Vec3 normal = core::transformDirection(part, frame.normal);
    const Vec3 right = core::transformDirection(part, frame.right);
    const Vec3 up = core::transformDirection(part, frame.up);
    // A millimetre off the face: close enough to read as printed on it, far
    // enough that the depth test never has to choose between the two.
    constexpr f32 Lift = 0.001f;
    const Vec3 centre = relative(part.position, cameraOrigin) + normal * (depth * 0.5f + Lift);

    CanvasPlacement placement;
    placement.canvas = Vec2{width * gui.pixelsPerMetre, height * gui.pixelsPerMetre};
    const f32 step = 1.0f / gui.pixelsPerMetre;
    placement.right = right * step;
    placement.down = up * -step;
    placement.topLeft = centre - right * (width * 0.5f) + up * (height * 0.5f);
    placement.distance = core::length(centre);
    return placement;
}

namespace {

// One world canvas this frame: where it is, and how it is drawn.
struct Tree
{
    core::InstanceId id;
    CanvasPlacement placement;
    f32 brightness = 1.0f;
    bool onTop = false;
    // The part it is printed on or floats over.
    core::InstanceId adornee;
};

// Every enabled canvas the camera could see, placed, in pool order.
[[nodiscard]] std::vector<Tree> collectCanvases(scene::World& world, core::InstanceId workspace,
                                                core::InstanceId uiService, Vec2 viewport,
                                                const render::RenderCamera& camera)
{
    std::vector<Tree> trees;
    const auto reachable = [&](core::InstanceId id) {
        return (workspace.valid() && under(world, id, workspace)) || (uiService.valid() && under(world, id, uiService));
    };

    world.billboardGuis().forEach([&](core::InstanceId id, const scene::BillboardGuiComponent& gui) {
        if (!gui.enabled || !reachable(id))
            return;
        const core::InstanceId part = partFor(world, id, gui.adornee);
        const scene::PartComponent* component = part.valid() ? world.parts().find(part) : nullptr;
        if (component == nullptr)
            return;
        const core::DVec3 anchor{component->cframe.position.x + static_cast<f64>(gui.worldOffset.x),
                                 component->cframe.position.y + static_cast<f64>(gui.worldOffset.y),
                                 component->cframe.position.z + static_cast<f64>(gui.worldOffset.z)};
        if (const std::optional<CanvasPlacement> placement = placeBillboard(gui, anchor, camera, viewport))
            trees.push_back(Tree{id, *placement, gui.brightness, gui.alwaysOnTop, part});
    });
    world.surfaceGuis().forEach([&](core::InstanceId id, const scene::SurfaceGuiComponent& gui) {
        if (!gui.enabled || !reachable(id))
            return;
        const core::InstanceId part = partFor(world, id, gui.adornee);
        const scene::PartComponent* component = part.valid() ? world.parts().find(part) : nullptr;
        if (component == nullptr)
            return;
        if (const std::optional<CanvasPlacement> placement =
                placeSurface(gui, component->cframe, component->size, camera.origin))
            trees.push_back(Tree{id, *placement, gui.brightness, gui.alwaysOnTop, part});
    });
    return trees;
}

} // namespace

void buildWorldUi(scene::World& world, core::InstanceId workspace, core::InstanceId uiService, Vec2 viewport,
                  std::span<const rhi::TextureHandle> textures, ui::DrawList& scratch, render::RenderWorld& out)
{
    if (!out.camera.valid)
        return;

    std::vector<Tree> trees = collectCanvases(world, workspace, uiService, viewport, out.camera);

    // **Back to front**, as every blended surface has to be: a near label over
    // a far one, never the reverse. Stable, so two at one distance keep pool
    // order from frame to frame.
    std::stable_sort(trees.begin(), trees.end(),
                     [](const Tree& a, const Tree& b) { return a.placement.distance > b.placement.distance; });

    for (const Tree& tree : trees) {
        ui::layoutCanvas(world, tree.id, tree.placement.canvas);
        ui::buildCanvasDrawList(world, tree.id, scratch);
        emitCanvas(scratch, tree.placement, tree.brightness, tree.onTop, textures, out);
    }
}

std::optional<WorldUiPick> pickWorldUi(scene::World& world, core::InstanceId workspace, core::InstanceId uiService,
                                       Vec2 viewport, const render::RenderCamera& camera, Vec2 pointer,
                                       const SolidAlong& solidAlong)
{
    if (!camera.valid || !(viewport.x > 0.0f) || !(viewport.y > 0.0f))
        return std::nullopt;
    // The pointer's ray, in the camera-relative space every placement is in.
    const PickRay ray = rayThroughPixel(camera.projection, camera.view, core::DVec3{},
                                        ViewportRect{0.0f, 0.0f, viewport.x, viewport.y}, pointer);
    const Vec3 origin{static_cast<f32>(ray.origin.x), static_cast<f32>(ray.origin.y), static_cast<f32>(ray.origin.z)};

    std::optional<WorldUiPick> best;
    bool bestOnTop = false;
    for (const Tree& tree : collectCanvases(world, workspace, uiService, viewport, camera)) {
        const CanvasPlacement& at = tree.placement;
        // The rectangle's plane: `right` and `down` are one pixel each, and
        // the face looks along the opposite of their cross product.
        const Vec3 facing = core::cross(at.right, at.down) * -1.0f;
        const f32 approach = core::dot(ray.direction, facing);
        // Edge-on, or seen from behind: a sign is read from its front.
        if (!(approach < -1e-8f))
            continue;
        const f32 along = core::dot(at.topLeft - origin, facing) / approach;
        if (!(along > 0.0f))
            continue;
        const Vec3 onPlane = origin + ray.direction * along - at.topLeft;
        const Vec2 pixel{core::dot(onPlane, at.right) / core::dot(at.right, at.right),
                         core::dot(onPlane, at.down) / core::dot(at.down, at.down)};
        if (pixel.x < 0.0f || pixel.y < 0.0f || pixel.x > at.canvas.x || pixel.y > at.canvas.y)
            continue;

        // A canvas that could not win is not laid out: an on-top one already
        // found beats anything that is not, and a nearer one of the same kind
        // beats a farther one.
        if (best.has_value() && (bestOnTop && !tree.onTop))
            continue;
        if (best.has_value() && bestOnTop == tree.onTop && along >= best->distance)
            continue;

        ui::layoutCanvas(world, tree.id, at.canvas);
        const core::InstanceId element = ui::hitTestCanvas(world, tree.id, pixel);
        if (!element.valid())
            continue;
        // Something solid in front of it hides it, unless it is drawn on top of
        // everything. A centimetre of slack, because a surface canvas floats a
        // millimetre off the face of the part behind it.
        if (!tree.onTop && solidAlong) {
            if (const std::optional<f32> solid = solidAlong(origin, ray.direction, tree.adornee);
                solid.has_value() && *solid < along - 0.01f)
                continue;
        }
        best = WorldUiPick{element, along};
        bestOnTop = tree.onTop;
    }
    return best;
}

} // namespace luaug::app
