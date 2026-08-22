#pragma once

#include <luaug/core/id.h>
#include <luaug/core/math.h>
#include <luaug/core/types.h>

#include <optional>

// Turning a click into an instance.
//
// This header exists as arithmetic rather than as a UI callback on purpose. A
// picking bug reproduces by clicking, and a bug that reproduces by clicking is
// one nobody fixes twice: the aspect-ratio error that is exactly right at the
// centre of the screen and wrong at every edge is the canonical example, and it
// is invisible to anyone testing by aiming at what they meant to hit. Everything
// here takes numbers and returns numbers, and `picking_tests.cpp` aims at the
// corners.
//
// **The ray's origin is f64 and its direction is f32**, which is ADR 0013 and
// ADR 0014's split applied where it belongs: a world position four kilometres
// out needs the precision, a direction never does.

namespace luaug::scene {
class World;
}

namespace luaug::app {
using core::f32;

// Where a viewport sits inside the window, in pixels, y down -- the space mouse
// events arrive in. An editor's 3D view is a panel with the rest of the UI
// around it, so a pick can never assume the viewport is the window.
struct ViewportRect
{
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 width = 1.0f;
    f32 height = 1.0f;
};

struct PickRay
{
    core::DVec3 origin;
    // Unit length.
    core::Vec3 direction{0.0f, 0.0f, -1.0f};
};

struct PickHit
{
    core::InstanceId instance;
    // Metres along the ray. Distance rather than a point, because the caller
    // that wants the point can compute it and the caller comparing two hits
    // wants this.
    f32 distance = 0.0f;
};

// The ray through a pixel, derived from the projection matrix rather than from a
// field of view and an aspect ratio.
//
// `projection.m[0][0]` and `m[1][1]` are the two tangents the camera was built
// with, so reading them back carries the aspect and the field of view together
// and cannot disagree with what was rendered -- which a separately-passed fov
// eventually would. `shadow.h` already reads them the same way for the same
// reason.
//
// `view` is the camera's view matrix and `cameraOrigin` the f64 world position
// the render snapshot is relative to (`RenderCamera::origin`). A pixel outside
// `rect` still produces a ray; clamping is the caller's decision, because an
// editor dragging past the edge of its viewport wants the ray it would have had.
[[nodiscard]] PickRay rayThroughPixel(const core::Mat4& projection, const core::Mat4& view, core::DVec3 cameraOrigin,
                                      const ViewportRect& rect, core::Vec2 pixel) noexcept;

// Ray against a box with a transform: the slab test in the box's own space.
//
// Returns the distance to the nearest intersection ahead of the origin, or
// nothing. **A ray starting inside the box hits at zero** rather than missing,
// which is what an editor wants when the camera is inside the thing being
// clicked; the alternative is a part that becomes unselectable when you walk
// into it.
[[nodiscard]] std::optional<f32> intersectBox(const PickRay& ray, const core::CFrameD& cframe,
                                              core::Vec3 size) noexcept;

// The nearest part the ray hits.
//
// **Every part is pickable, including a fully transparent one and one with no
// collision.** An editor is not a camera: a raycast through the physics world
// would silently skip lights, triggers, anchored decoration with collision off
// and anything at all transparent, and an object that cannot be clicked is an
// object that has to be found in a tree view by name. The cost is that an
// invisible part can take a click; the alternative is that it can never take
// one.
//
// Ties break on the lower instance id, so a pick is reproducible when two faces
// are coplanar -- which is the flagship's terraced ground everywhere.
//
// **Everything is tested as its box, including a ball, a cylinder and a mesh.**
// A `MeshPart` carries the same transform and extents as any other part, so its
// box is its bounds and a click near a concave mesh's corner can land on
// nothing visible. This is an approximation and it is written down rather than
// discovered: a milestone that adds shape-exact picking will find this comment,
// and until one does, being able to select a mesh at all is worth more than
// being unable to miss it.
[[nodiscard]] std::optional<PickHit> pickNearest(const scene::World& world, const PickRay& ray) noexcept;

} // namespace luaug::app
