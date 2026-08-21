#include "luaug/render/shadow.h"

#include <algorithm>
#include <cmath>

namespace luaug::render {
namespace {

using core::DVec3;
using core::f64;
using core::Mat4;
using core::Vec3;

// An orthographic projection with depth in [0, 1], matching `core::perspective`.
// Written here rather than in `core` because it is a shadow-map fit rather than
// a general camera projection, and a `core::orthographic` with no other caller
// would be a type nobody has checked the conventions of.
[[nodiscard]] Mat4 orthographic(f32 halfExtent, f32 nearZ, f32 farZ) noexcept
{
    Mat4 result;
    result.m[0][0] = 1.0f / halfExtent;
    result.m[1][1] = 1.0f / halfExtent;
    result.m[2][2] = 1.0f / (nearZ - farZ);
    result.m[3][2] = nearZ / (nearZ - farZ);
    return result;
}

// A sun exactly overhead makes the obvious up vector parallel to the view, which
// produces a NaN basis. Picking the alternate up here is cheaper than a scene
// that flickers when the clock passes noon.
[[nodiscard]] Vec3 lightUp(Vec3 direction) noexcept
{
    return std::fabs(direction.y) > 0.99f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 1.0f, 0.0f};
}

} // namespace

void shadowSplits(f32 nearPlane, f32 farDistance, f32 lambda, f32 (&out)[kShadowCascadeCount + 1]) noexcept
{
    const f32 near = std::max(nearPlane, 1e-3f);
    const f32 far = std::max(farDistance, near * 1.001f);
    const f32 ratio = far / near;
    const f32 range = far - near;

    out[0] = near;
    out[kShadowCascadeCount] = far;
    for (u32 index = 1; index < kShadowCascadeCount; ++index) {
        const f32 fraction = static_cast<f32>(index) / static_cast<f32>(kShadowCascadeCount);
        // GPU Gems 3 chapter 10: the logarithmic scheme puts detail where
        // perspective needs it and starves the distance; the uniform one does
        // the opposite. `lambda` is the blend, and the whole point is that
        // neither end is usable alone.
        const f32 logarithmic = near * std::pow(ratio, fraction);
        const f32 uniform = near + range * fraction;
        out[index] = lambda * logarithmic + (1.0f - lambda) * uniform;
    }
}

ShadowCascades fitShadowCascades(const ShadowFit& fit) noexcept
{
    ShadowCascades cascades;

    const Vec3 direction = core::normalize(fit.sunDirection);
    const Vec3 up = lightUp(direction);
    // Rotation only, with the light's view looking along -direction. A world
    // point's light-space coordinate is this times the point, and rounding
    // happens in that space.
    const Mat4 lightRotation = core::lookAt(Vec3{}, -direction, up);

    f32 splits[kShadowCascadeCount + 1]{};
    shadowSplits(fit.nearPlane, kShadowDistance, kShadowSplitLambda, splits);

    for (u32 index = 0; index < kShadowCascadeCount; ++index) {
        const f32 sliceNear = splits[index];
        const f32 sliceFar = splits[index + 1];

        // The bounding sphere of the slice's eight corners. A SPHERE rather than
        // a box, because a box fitted to the corners changes size as the camera
        // turns and a sphere does not -- which is the difference between a
        // stable texel grid and one that breathes with the mouse.
        //
        // The centroid of a symmetric slice lies on the camera axis, so the
        // radius is a function of the two distances and the field of view and of
        // nothing that changes per frame.
        const Vec3 centre = fit.forward * ((sliceNear + sliceFar) * 0.5f);
        f32 radius = 0.0f;
        for (const f32 depth : {sliceNear, sliceFar}) {
            const Vec3 axis = fit.forward * depth;
            const Vec3 offset = fit.right * (depth * fit.tanHalfFovX) + fit.up * (depth * fit.tanHalfFovY);
            radius = std::max(radius, core::length(axis + offset - centre));
        }
        radius = std::max(radius, 1e-3f);

        const f32 texel = (2.0f * radius) / static_cast<f32>(kShadowTileResolution);

        // The snap, in f64 and against the WORLD position, for the reason
        // `ShadowFit::origin` documents. What comes out is under half a texel
        // and fits an f32 with room to spare, which is why the rounding happens
        // here rather than in a shader.
        const DVec3 centreWorld{fit.origin.x + static_cast<f64>(centre.x), fit.origin.y + static_cast<f64>(centre.y),
                                fit.origin.z + static_cast<f64>(centre.z)};
        const f64 lightX = static_cast<f64>(lightRotation.m[0][0]) * centreWorld.x +
                           static_cast<f64>(lightRotation.m[1][0]) * centreWorld.y +
                           static_cast<f64>(lightRotation.m[2][0]) * centreWorld.z;
        const f64 lightY = static_cast<f64>(lightRotation.m[0][1]) * centreWorld.x +
                           static_cast<f64>(lightRotation.m[1][1]) * centreWorld.y +
                           static_cast<f64>(lightRotation.m[2][1]) * centreWorld.z;

        const auto texelD = static_cast<f64>(texel);
        const f64 residualX = lightX - std::round(lightX / texelD) * texelD;
        const f64 residualY = lightY - std::round(lightY / texelD) * texelD;

        const f32 depthRange = 2.0f * radius + kShadowCasterMargin;
        Mat4 view = core::lookAt(centre + direction * (radius + kShadowCasterMargin), centre, up);
        // Plus, not minus: the box's centre in light space is MINUS the view's
        // translation, so adding the residual to the translation subtracts it
        // from the centre and lands the box on the lattice.
        view.m[3][0] += static_cast<f32>(residualX);
        view.m[3][1] += static_cast<f32>(residualY);

        cascades.viewProjection[index] = orthographic(radius, 0.0f, depthRange) * view;
        cascades.farDistance[index] = sliceFar;
        cascades.texelWorld[index] = texel;
        cascades.depthRange[index] = depthRange;
    }

    return cascades;
}

} // namespace luaug::render
