#include <luaug/app/picking.h>
#include <luaug/scene/components.h>
#include <luaug/scene/world.h>

#include <cmath>
#include <limits>

namespace luaug::app {
using core::CFrameD;
using core::DVec3;
using core::f32;
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

std::optional<PickHit> pickNearest(const scene::World& world, const PickRay& ray) noexcept
{
    std::optional<PickHit> best;

    world.parts().forEach([&](core::InstanceId id, const scene::PartComponent& part) {
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

} // namespace luaug::app
