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
// **`root` is the same root the renderer draws from, and passing it is not
// optional.** Picking used to walk the whole part pool, which is not the same
// set as the one on screen: an instance that is in the world's pools but not
// under the root is never drawn -- and was still pickable, so a click landed on
// an outline around nothing. A part orphaned by anything at all becomes
// permanently clickable and permanently invisible, and `clearScene` cannot
// reach it to remove it either, because that walks the root's children too.
//
// The rule is one sentence: **you can pick what could be drawn.** Transparent
// and non-colliding stay pickable, for the reasons above; not-in-the-world does
// not, because it is not a thing the person is looking at.
[[nodiscard]] std::optional<PickHit> pickNearest(const scene::World& world, core::InstanceId root,
                                                 const PickRay& ray) noexcept;

// --- The manipulators -------------------------------------------------------
//
// **All of this is arithmetic and none of it is a UI callback**, for the reason
// the top of this file gives about picking and which is stronger here: a
// manipulator bug reproduces by DRAGGING, and a bug that reproduces by dragging
// is one nobody fixes twice. An axis nearly edge-on to the camera, a drag that
// starts a few pixels off the handle, a viewport that is not square -- each is
// invisible when you aim at what you meant to grab, and each is a case in
// `picking_tests.cpp`.
//
// The split is: **where the gizmo is** (a frame and a scale), **what the ray
// hit** (a handle), and **where the drag has got to** (a point or an angle).
// Nothing here computes a delta, and that is deliberate: a delta taken between
// two consecutive rays accumulates its own error and drifts over a long drag.
// The caller records what it got on the first frame and diffs against that, so
// a drag is exact however long it lasts and however slowly it is made.

// The point on the screen a world position falls at, or nothing when it is
// behind the camera.
//
// **The exact inverse of `rayThroughPixel`**, and it reads its tangents back off
// the same projection matrix for the same reason: a second copy of the field of
// view is a second thing to disagree with the image. `picking_tests.cpp` checks
// the round trip at the corners, which is where an aspect-ratio error lives.
[[nodiscard]] std::optional<core::Vec2> worldToViewport(const core::Mat4& projection, const core::Mat4& view,
                                                        core::DVec3 cameraOrigin, const ViewportRect& rect,
                                                        core::DVec3 world) noexcept;

// How many world metres one pixel covers at `world`, given this projection and
// this viewport.
//
// A manipulator is a constant size ON SCREEN -- a handle that shrank into
// nothing as you flew away would be a handle you could not grab -- so its
// geometry is built in metres and scaled by this. Zero when the point is behind
// the camera or the viewport is collapsed, which the caller reads as "do not
// draw one".
[[nodiscard]] f32 metresPerPixel(const core::Mat4& projection, const ViewportRect& rect, core::DVec3 cameraOrigin,
                                 core::DVec3 world) noexcept;

enum class GizmoMode : core::u8
{
    Translate,
    Rotate,
    Scale,
};

// Where a manipulator is and how big it is drawn.
//
// `transform` is the selection's own frame in local space and the world's axes
// in world space -- which one is a person's choice and not this file's. `size`
// is the length of an axis in METRES, already scaled so it comes out the size it
// should be on screen.
struct GizmoFrame
{
    core::CFrameD transform;
    f32 size = 1.0f;
};

// Which part of the manipulator a ray hit.
struct GizmoHandle
{
    // 0 is X, 1 is Y, 2 is Z. For a plane handle it is the plane's NORMAL,
    // which is the axis the drag does NOT move along.
    core::u8 axis = 0;
    // The square between two axes: a drag in both of them at once, which is how
    // anything is moved across a floor.
    bool plane = false;
    // The middle: uniform scale, and for translate the screen-facing plane.
    bool uniform = false;
    // Metres along the ray. The caller does not need it; `pickGizmo` does, to
    // choose between two handles one ray crosses.
    f32 distance = 0.0f;

    [[nodiscard]] constexpr bool operator==(const GizmoHandle&) const noexcept = default;
};

// The handle this ray hits, or nothing.
//
// **Nearest wins, and the middle wins ties.** A ray through the origin of a
// translate gizmo crosses all three axes and both of the handles that meet
// there; picking the nearest is what makes grabbing the one you are pointing at
// possible at all, and the centre handle is deliberately tested first because it
// is the smallest and the easiest to miss.
[[nodiscard]] std::optional<GizmoHandle> pickGizmo(const PickRay& ray, const GizmoFrame& frame,
                                                   GizmoMode mode) noexcept;

// Where along the handle this ray is pointing, in world space.
//
// For an axis handle it is the point on that axis nearest the ray; for a plane
// handle it is where the ray meets the plane. **A POINT and not a delta**: the
// caller takes this once when the drag begins and diffs against it, which is
// what makes a drag exact rather than an accumulation of per-frame differences.
//
// Nothing when the ray is parallel to what it is being solved against, which is
// the axis edge-on to the camera and is a real thing to point at.
[[nodiscard]] std::optional<core::DVec3> gizmoDragPoint(const PickRay& ray, const GizmoFrame& frame,
                                                        const GizmoHandle& handle) noexcept;

// The angle around the handle's axis this ray points at, in radians, measured in
// the plane the ring lies in. A point and not a delta, for the same reason.
[[nodiscard]] std::optional<f32> gizmoDragAngle(const PickRay& ray, const GizmoFrame& frame,
                                                const GizmoHandle& handle) noexcept;

} // namespace luaug::app
