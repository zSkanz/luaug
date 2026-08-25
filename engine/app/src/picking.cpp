#include <luaug/app/picking.h>
#include <luaug/scene/components.h>
#include <luaug/scene/world.h>

#include <cmath>
#include <limits>

namespace luaug::app {
using core::CFrameD;
using core::DVec3;
using core::f32;
using core::f64;
using core::Mat4;
using core::Vec2;
using core::Vec3;

PickRay rayThroughPixel(const Mat4& projection, const Mat4& view, DVec3 cameraOrigin, const ViewportRect& rect,
                        Vec2 pixel) noexcept
{
    // A zero-sized viewport is a panel that has been collapsed or has not been
    // laid out yet, and it arrives here on the frame a dock is dragged. Forward
    // is the honest answer: it hits whatever is in front of the camera, which is
    // wrong in a way that looks like nothing rather than wrong in a way that
    // produces NaN and poisons a comparison downstream.
    const f32 width = rect.width > 0.0f ? rect.width : 1.0f;
    const f32 height = rect.height > 0.0f ? rect.height : 1.0f;

    // Pixel to NDC. Y flips: mouse coordinates run down the screen and clip
    // space runs up it.
    const f32 ndcX = 2.0f * ((pixel.x - rect.x) / width) - 1.0f;
    const f32 ndcY = 1.0f - 2.0f * ((pixel.y - rect.y) / height);

    // The two tangents the projection was built from. Reading them back instead
    // of taking a field of view and an aspect ratio as arguments is what makes
    // this incapable of disagreeing with the matrix the frame was rendered with.
    const f32 tanX = projection.m[0][0] != 0.0f ? 1.0f / projection.m[0][0] : 1.0f;
    const f32 tanY = projection.m[1][1] != 0.0f ? 1.0f / projection.m[1][1] : 1.0f;

    // View space is right-handed looking down -Z (`core::math`'s `perspective`
    // and `lookAt` both say so), so the direction through a pixel is the
    // tangents scaled by NDC with a unit step forward.
    const Vec3 directionView{ndcX * tanX, ndcY * tanY, -1.0f};

    // The inverse view matrix rotates a view-space direction back into world
    // space. `transformDirection` drops the translation, which is what a
    // direction wants and also why the camera's position never enters here.
    const Mat4 viewInverse = core::inverse(view);
    const Vec3 directionWorld = core::normalize(core::transformDirection(viewInverse, directionView));

    return PickRay{cameraOrigin, directionWorld};
}

std::optional<f32> intersectBox(const PickRay& ray, const CFrameD& cframe, Vec3 size) noexcept
{
    // Into the box's own space. The origin difference is taken in f64 and
    // narrowed afterwards: the two positions can both be kilometres from the
    // world origin while their difference is centimetres, and doing the subtract
    // in f32 is exactly the precision loss ADR 0014 exists to prevent.
    const Vec3 toOrigin = core::toVec3(ray.origin - cframe.position);

    // `Mat3`'s columns are the box's axes, so projecting onto each column is the
    // inverse rotation -- a rotation's transpose is its inverse, and writing it
    // as three dot products avoids building a matrix to throw away.
    const auto axis = [&cframe](core::i32 index) noexcept -> Vec3 {
        return Vec3{cframe.rotation.m[index][0], cframe.rotation.m[index][1], cframe.rotation.m[index][2]};
    };

    const Vec3 originLocal{core::dot(toOrigin, axis(0)), core::dot(toOrigin, axis(1)), core::dot(toOrigin, axis(2))};
    const Vec3 directionLocal{core::dot(ray.direction, axis(0)), core::dot(ray.direction, axis(1)),
                              core::dot(ray.direction, axis(2))};

    const Vec3 half{std::abs(size.x) * 0.5f, std::abs(size.y) * 0.5f, std::abs(size.z) * 0.5f};

    const f32 originAxes[3]{originLocal.x, originLocal.y, originLocal.z};
    const f32 directionAxes[3]{directionLocal.x, directionLocal.y, directionLocal.z};
    const f32 halfAxes[3]{half.x, half.y, half.z};

    f32 enter = -std::numeric_limits<f32>::infinity();
    f32 exit = std::numeric_limits<f32>::infinity();

    for (core::i32 index = 0; index < 3; ++index) {
        // A ray parallel to a slab either never enters it or is inside it for
        // its whole length; there is no intersection to compute, only a
        // rejection. Testing the origin against the slab is that rejection, and
        // skipping the divide is why it comes first.
        if (std::abs(directionAxes[index]) < 1e-8f) {
            if (originAxes[index] < -halfAxes[index] || originAxes[index] > halfAxes[index])
                return std::nullopt;
            continue;
        }

        const f32 inverseDirection = 1.0f / directionAxes[index];
        f32 near = (-halfAxes[index] - originAxes[index]) * inverseDirection;
        f32 far = (halfAxes[index] - originAxes[index]) * inverseDirection;
        if (near > far)
            std::swap(near, far);

        enter = near > enter ? near : enter;
        exit = far < exit ? far : exit;
        if (enter > exit)
            return std::nullopt;
    }

    // Entirely behind the camera.
    if (exit < 0.0f)
        return std::nullopt;

    // Inside the box: the entry is behind us and the exit is not, so the ray
    // starts in contact. Zero rather than the exit distance, because a caller
    // comparing hits wants "as near as it gets".
    return enter < 0.0f ? 0.0f : enter;
}

namespace {

// The same walk `render::extract` makes, and it has to be the same one: what is
// pickable is what could be drawn.
[[nodiscard]] bool inWorld(const scene::World& world, core::InstanceId id, core::InstanceId root) noexcept
{
    for (core::InstanceId cursor = id; cursor.valid(); cursor = world.parentOf(cursor)) {
        if (cursor == root)
            return true;
    }
    return false;
}

} // namespace

std::optional<PickHit> pickNearest(const scene::World& world, core::InstanceId root, const PickRay& ray) noexcept
{
    std::optional<PickHit> best;

    world.parts().forEach([&](core::InstanceId id, const scene::PartComponent& part) {
        // Not under the root means not on screen. The pool holds every part the
        // world has ever been told about, and an orphan is drawn by nothing and
        // was pickable by this -- which reads as a click selecting an outline
        // around empty space.
        if (!inWorld(world, id, root))
            return;

        const std::optional<f32> distance = intersectBox(ray, part.cframe, part.size);
        if (!distance.has_value())
            return;

        if (!best.has_value() || *distance < best->distance ||
            (*distance == best->distance && id.index < best->instance.index)) {
            best = PickHit{id, *distance};
        }
    });

    return best;
}

// --- The manipulators -------------------------------------------------------

namespace {

// The gizmo's three axes in world space, unit length. A `Mat3`'s columns are
// right, up and back, which is what `math.h` says at the type.
void gizmoAxes(const GizmoFrame& frame, Vec3 out[3]) noexcept
{
    const core::Mat3& basis = frame.transform.rotation;
    for (int axis = 0; axis < 3; ++axis)
        out[axis] = core::normalize(Vec3{basis.m[axis][0], basis.m[axis][1], basis.m[axis][2]});
}

// Where a ray and a segment come nearest each other.
//
// The standard closest-approach solve, with the degenerate case named rather
// than divided through: when the two are parallel there is no single nearest
// pair, and an axis pointing straight at the camera IS that case and is a thing
// people aim at.
struct Approach
{
    f32 distance = 0.0f;
    f32 alongRay = 0.0f;
    // As a fraction of the segment, so a caller can ask whether the nearest
    // point is on the arm without knowing how long the arm is.
    f32 alongSegment = 0.0f;
    bool valid = false;
};

[[nodiscard]] Approach approachSegment(const PickRay& ray, DVec3 from, DVec3 to) noexcept
{
    const Vec3 segment = core::toVec3(to - from);
    const f32 segmentLength = core::length(segment);
    if (segmentLength <= 0.0f)
        return {};

    const Vec3 segmentDir = segment * (1.0f / segmentLength);
    const Vec3 offset = core::toVec3(from - ray.origin);

    const f32 rayDotSegment = core::dot(ray.direction, segmentDir);
    const f32 denominator = 1.0f - rayDotSegment * rayDotSegment;
    if (denominator < 1e-6f)
        return {};

    const f32 offsetDotRay = core::dot(offset, ray.direction);
    const f32 offsetDotSegment = core::dot(offset, segmentDir);

    const f32 alongRay = (offsetDotRay - rayDotSegment * offsetDotSegment) / denominator;
    const f32 alongSegment = (rayDotSegment * offsetDotRay - offsetDotSegment) / denominator;

    const Vec3 onRay = ray.direction * alongRay;
    const Vec3 onSegment = offset + segmentDir * alongSegment;
    return Approach{core::length(onRay - onSegment), alongRay, alongSegment / segmentLength, true};
}

// Where a ray meets the plane through `point` with `normal`, as a distance
// along the ray. Nothing when the ray runs along the plane.
[[nodiscard]] std::optional<f32> intersectPlane(const PickRay& ray, DVec3 point, Vec3 normal) noexcept
{
    const f32 facing = core::dot(ray.direction, normal);
    if (facing > -1e-5f && facing < 1e-5f)
        return std::nullopt;

    return core::dot(core::toVec3(point - ray.origin), normal) / facing;
}

} // namespace

std::optional<core::Vec2> worldToViewport(const Mat4& projection, const Mat4& view, DVec3 cameraOrigin,
                                          const ViewportRect& rect, DVec3 world) noexcept
{
    const f32 width = rect.width > 0.0f ? rect.width : 1.0f;
    const f32 height = rect.height > 0.0f ? rect.height : 1.0f;

    // Camera-relative, and the subtraction is in f64. This is the one place the
    // precision can be lost and it is lost once (ADR 0014).
    const Vec3 relative = core::toVec3(world - cameraOrigin);
    const Vec3 viewSpace = core::transformPoint(view, relative);

    // View space looks down -Z, so anything at or behind the camera plane has
    // no point on the screen. Refused rather than mirrored: a position behind
    // the camera projected without this check comes out in front of it on the
    // opposite side, and a handle drawn there is a handle somebody chases.
    if (viewSpace.z > -1e-4f)
        return std::nullopt;

    // The same two tangents `rayThroughPixel` reads, inverted. Taking them off
    // the matrix rather than off a field of view is what makes this the exact
    // inverse of that rather than an approximation of it.
    const f32 tanX = projection.m[0][0] != 0.0f ? 1.0f / projection.m[0][0] : 1.0f;
    const f32 tanY = projection.m[1][1] != 0.0f ? 1.0f / projection.m[1][1] : 1.0f;

    const f32 ndcX = viewSpace.x / (tanX * -viewSpace.z);
    const f32 ndcY = viewSpace.y / (tanY * -viewSpace.z);

    // Back to pixels, with the same Y flip.
    return core::Vec2{rect.x + (ndcX * 0.5f + 0.5f) * width, rect.y + (0.5f - ndcY * 0.5f) * height};
}

f32 metresPerPixel(const Mat4& projection, const ViewportRect& rect, DVec3 cameraOrigin, DVec3 world) noexcept
{
    if (rect.height <= 0.0f)
        return 0.0f;

    const DVec3 offset = world - cameraOrigin;
    const f64 distance = std::sqrt(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z);
    if (distance <= 0.0)
        return 0.0f;

    // `projection.m[1][1]` IS `1 / tan(fovY / 2)`, so half the viewport height
    // times it is how many pixels a metre subtends at a metre away -- the same
    // reading `shadow.h` and the LOD selector already make of the same number.
    const f32 pixelsPerMetreAtOne = 0.5f * rect.height * projection.m[1][1];
    if (pixelsPerMetreAtOne <= 0.0f)
        return 0.0f;

    return static_cast<f32>(distance) / pixelsPerMetreAtOne;
}

std::optional<GizmoHandle> pickGizmo(const PickRay& ray, const GizmoFrame& frame, GizmoMode mode) noexcept
{
    if (frame.size <= 0.0f)
        return std::nullopt;

    Vec3 axes[3];
    gizmoAxes(frame, axes);
    const DVec3 centre = frame.transform.position;

    // Every tolerance below is a fraction of the gizmo, so a handle is as easy
    // to grab four kilometres away as it is at arm's length.
    const f32 size = frame.size;
    const f32 grab = size * 0.12f;

    std::optional<GizmoHandle> best;
    const auto consider = [&best](const GizmoHandle& candidate) {
        if (!best.has_value() || candidate.distance < best->distance)
            best = candidate;
    };

    // **The middle first**, because it is the smallest thing on the gizmo and
    // the one a near-miss should still find. A sphere rather than a disc: it has
    // no orientation, which is right for a handle that means "all of it".
    if (mode != GizmoMode::Rotate) {
        const f32 alongRay = core::dot(core::toVec3(centre - ray.origin), ray.direction);
        if (alongRay > 0.0f) {
            const Vec3 nearest = core::toVec3(ray.origin - centre) + ray.direction * alongRay;
            if (core::length(nearest) <= grab)
                consider(GizmoHandle{0, false, true, alongRay});
        }
    }

    for (core::u8 axis = 0; axis < 3; ++axis) {
        if (mode == GizmoMode::Rotate) {
            // A ring: where the ray meets the plane the ring lies in, at the
            // ring's own radius.
            const std::optional<f32> hit = intersectPlane(ray, centre, axes[axis]);
            if (!hit.has_value() || *hit <= 0.0f)
                continue;
            const Vec3 onPlane = core::toVec3(ray.origin - centre) + ray.direction * *hit;
            if (const f32 radius = core::length(onPlane); radius > size - grab && radius < size + grab)
                consider(GizmoHandle{axis, false, false, *hit});
            continue;
        }

        // The arm, from the centre outwards. The inner eighth belongs to the
        // centre handle, so a grab near the middle means "all of it" rather
        // than whichever arm happened to be nearest.
        const DVec3 tip = centre + core::toDVec3(axes[axis] * size);
        if (const Approach approach = approachSegment(ray, centre, tip);
            approach.valid && approach.distance <= grab && approach.alongRay > 0.0f && approach.alongSegment > 0.12f &&
            approach.alongSegment < 1.0f) {
            consider(GizmoHandle{axis, false, false, approach.alongRay});
        }

        // The plane square, out along the two axes this one is the normal of.
        // Translate has them and a rotate gizmo is rings and nothing else.
        if (mode == GizmoMode::Translate) {
            const std::optional<f32> hit = intersectPlane(ray, centre, axes[axis]);
            if (!hit.has_value() || *hit <= 0.0f)
                continue;
            const Vec3 onPlane = core::toVec3(ray.origin - centre) + ray.direction * *hit;
            const f32 u = core::dot(onPlane, axes[(axis + 1) % 3]);
            const f32 v = core::dot(onPlane, axes[(axis + 2) % 3]);
            const f32 inner = size * 0.25f;
            const f32 outer = size * 0.55f;
            if (u > inner && u < outer && v > inner && v < outer)
                consider(GizmoHandle{axis, true, false, *hit});
        }
    }

    return best;
}

std::optional<DVec3> gizmoDragPoint(const PickRay& ray, const GizmoFrame& frame, const GizmoHandle& handle) noexcept
{
    Vec3 axes[3];
    gizmoAxes(frame, axes);
    const DVec3 centre = frame.transform.position;

    if (handle.plane || handle.uniform) {
        // A plane handle solves against its own plane; the middle solves against
        // the one facing the camera, which is what makes a screen-space drag
        // follow the pointer exactly.
        const Vec3 normal = handle.plane ? axes[handle.axis] : ray.direction * -1.0f;
        const std::optional<f32> hit = intersectPlane(ray, centre, normal);
        if (!hit.has_value())
            return std::nullopt;
        return ray.origin + core::toDVec3(ray.direction * *hit);
    }

    // An axis handle: the point on the infinite LINE through the centre that
    // the ray comes nearest to -- the line rather than the arm, because a drag
    // that ran off the end of the arm would stop moving.
    const DVec3 far = centre + core::toDVec3(axes[handle.axis]);
    const Approach approach = approachSegment(ray, centre, far);
    if (!approach.valid)
        return std::nullopt;

    return centre + core::toDVec3(axes[handle.axis] * approach.alongSegment);
}

std::optional<f32> gizmoDragAngle(const PickRay& ray, const GizmoFrame& frame, const GizmoHandle& handle) noexcept
{
    Vec3 axes[3];
    gizmoAxes(frame, axes);
    const DVec3 centre = frame.transform.position;

    const std::optional<f32> hit = intersectPlane(ray, centre, axes[handle.axis]);
    if (!hit.has_value())
        return std::nullopt;

    const Vec3 onPlane = core::toVec3(ray.origin - centre) + ray.direction * *hit;
    // Measured against the ring's own other two axes, so the angle a drag
    // reports is the angle the ring turns through rather than one in a frame the
    // caller has to convert out of.
    const f32 u = core::dot(onPlane, axes[(handle.axis + 1) % 3]);
    const f32 v = core::dot(onPlane, axes[(handle.axis + 2) % 3]);
    if (u == 0.0f && v == 0.0f)
        return std::nullopt;

    return std::atan2(v, u);
}

} // namespace luaug::app
