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

// How coarsely a fitted extent is rounded up, as a fraction of the slice's own
// radius.
//
// **A box that tracked its contents exactly would breathe**, and breathing is
// the artifact the sphere fit was chosen to avoid in the first place: a cascade
// whose extent changes changes its texel size, and every shadow edge in it
// shifts. Rounding the extent up onto a ladder means it changes rarely and in
// steps, instead of every frame by a little -- and between steps the texel grid
// is exactly as stable as the sphere's was.
//
// A sixteenth is fine enough that the fit still recovers most of what a thin
// light-space band wastes, and coarse enough that a caster drifting across the
// slice boundary does not ripple the extent.
constexpr f32 kFitQuantum = 1.0f / 16.0f;

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

        // **The box, fitted to what is actually there.** The slice's sphere says
        // what the cascade must be able to cover; the casters say what it needs
        // to. A light near the horizon makes those two wildly different: the
        // sphere is sized by the camera's reach and the content projects into a
        // thin band inside it, so most of the tile holds nothing and the shadows
        // are drawn with the texels that are left.
        //
        // Only x and y are fitted, and the box stays SQUARE. A rectangular one
        // would recover more -- the wasted axis is usually just one -- and it
        // would give the cascade two texel sizes, which the filter radius, the
        // normal offset and the depth bias are all expressed against as one
        // number. That is a bigger change than this defect needs.
        f32 extent = radius;
        Vec3 boxCentre = centre;
        if (!fit.casters.empty()) {
            const Vec3 lightX{lightRotation.m[0][0], lightRotation.m[1][0], lightRotation.m[2][0]};
            const Vec3 lightY{lightRotation.m[0][1], lightRotation.m[1][1], lightRotation.m[2][1]};

            f32 minX = 0.0f;
            f32 maxX = 0.0f;
            f32 minY = 0.0f;
            f32 maxY = 0.0f;
            bool any = false;
            for (const ShadowCasterBounds& caster : fit.casters) {
                // Only what this cascade will actually draw. The same test the
                // renderer culls with, so the box cannot be fitted to a caster
                // that is then culled away -- which would leave a shadow off the
                // edge of its own map.
                const Vec3 toCaster = caster.centre - centre;
                const f32 reach = radius + caster.radius;
                if (core::dot(toCaster, toCaster) > reach * reach)
                    continue;

                const f32 x = core::dot(lightX, caster.centre);
                const f32 y = core::dot(lightY, caster.centre);
                if (!any) {
                    minX = x - caster.radius;
                    maxX = x + caster.radius;
                    minY = y - caster.radius;
                    maxY = y + caster.radius;
                    any = true;
                    continue;
                }
                minX = std::min(minX, x - caster.radius);
                maxX = std::max(maxX, x + caster.radius);
                minY = std::min(minY, y - caster.radius);
                maxY = std::max(maxY, y + caster.radius);
            }

            // Intersected with what the slice needs, and this is the half that
            // is easy to get wrong: a caster far larger than the cascade -- a
            // ground plate, say -- would otherwise drag the box off to its own
            // centre, and cascade zero would end up fitted to a piece of world
            // the camera cannot see. The box covers the part of the SLICE that
            // has content in it, which is a different sentence.
            const f32 sliceX = core::dot(lightX, centre);
            const f32 sliceY = core::dot(lightY, centre);
            minX = std::max(minX, sliceX - radius);
            maxX = std::min(maxX, sliceX + radius);
            minY = std::max(minY, sliceY - radius);
            maxY = std::min(maxY, sliceY + radius);
            any = any && minX < maxX && minY < maxY;

            if (any) {
                const f32 wanted = std::max((maxX - minX) * 0.5f, (maxY - minY) * 0.5f);
                // Up onto the ladder, and never past what the slice needs: a
                // box larger than the sphere would only be spending resolution
                // on geometry the camera cannot see.
                const f32 quantum = std::max(radius * kFitQuantum, 1e-4f);
                extent = std::min(radius, std::ceil(std::max(wanted, quantum) / quantum) * quantum);

                // The slice centre, slid in the light's own x and y onto the
                // content's centre. Its depth along the light is left alone --
                // that axis is the depth range's business, below.
                const f32 wantX = (minX + maxX) * 0.5f;
                const f32 wantY = (minY + maxY) * 0.5f;
                boxCentre = centre + lightX * (wantX - core::dot(lightX, centre)) +
                            lightY * (wantY - core::dot(lightY, centre));
            }
        }

        const f32 texel = (2.0f * extent) / static_cast<f32>(kShadowTileResolution);

        // The snap, in f64 and against the WORLD position, for the reason
        // `ShadowFit::origin` documents. What comes out is under half a texel
        // and fits an f32 with room to spare, which is why the rounding happens
        // here rather than in a shader.
        const DVec3 centreWorld{fit.origin.x + static_cast<f64>(boxCentre.x),
                                fit.origin.y + static_cast<f64>(boxCentre.y),
                                fit.origin.z + static_cast<f64>(boxCentre.z)};
        const f64 lightX = static_cast<f64>(lightRotation.m[0][0]) * centreWorld.x +
                           static_cast<f64>(lightRotation.m[1][0]) * centreWorld.y +
                           static_cast<f64>(lightRotation.m[2][0]) * centreWorld.z;
        const f64 lightY = static_cast<f64>(lightRotation.m[0][1]) * centreWorld.x +
                           static_cast<f64>(lightRotation.m[1][1]) * centreWorld.y +
                           static_cast<f64>(lightRotation.m[2][1]) * centreWorld.z;

        const auto texelD = static_cast<f64>(texel);
        const f64 residualX = lightX - std::round(lightX / texelD) * texelD;
        const f64 residualY = lightY - std::round(lightY / texelD) * texelD;

        // The depth range stays the SLICE's, not the box's: the box was narrowed
        // across the light, and how far along it a caster may stand is a
        // different question with the same answer it had before.
        const f32 depthRange = 2.0f * radius + kShadowCasterMargin;
        Mat4 view = core::lookAt(boxCentre + direction * (radius + kShadowCasterMargin), boxCentre, up);
        // Plus, not minus: the box's centre in light space is MINUS the view's
        // translation, so adding the residual to the translation subtracts it
        // from the centre and lands the box on the lattice.
        view.m[3][0] += static_cast<f32>(residualX);
        view.m[3][1] += static_cast<f32>(residualY);

        cascades.viewProjection[index] = orthographic(extent, 0.0f, depthRange) * view;
        cascades.farDistance[index] = sliceFar;
        cascades.texelWorld[index] = texel;
        cascades.depthRange[index] = depthRange;
        cascades.cullCentre[index] = centre;
        cascades.cullRadius[index] = radius;
    }

    return cascades;
}

} // namespace luaug::render
