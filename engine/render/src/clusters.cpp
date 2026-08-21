#include "luaug/render/clusters.h"

#include <algorithm>
#include <cmath>

namespace luaug::render {
namespace {

using core::usize;
using core::Vec3;

// The cluster's own bounds in view space, where the camera looks down -Z and
// depth is positive in front. Held as positive depths because that is what the
// slicing works in; the z bounds below are negated exactly once, on the way into
// the box.
struct ClusterBounds
{
    Vec3 low;
    Vec3 high;
};

// One tile's four rays, evaluated at a depth. The tile's screen rectangle in
// normalised device coordinates times the depth's own half-extents is the
// rectangle the cluster covers there.
[[nodiscard]] ClusterBounds boundsOf(u32 tileX, u32 tileY, f32 nearDepth, f32 farDepth, f32 tanHalfFovX,
                                     f32 tanHalfFovY) noexcept
{
    const f32 leftNdc = static_cast<f32>(tileX) / static_cast<f32>(kClusterTilesX) * 2.0f - 1.0f;
    const f32 rightNdc = static_cast<f32>(tileX + 1) / static_cast<f32>(kClusterTilesX) * 2.0f - 1.0f;
    // **Tile Y counts DOWN the screen**, because the shader derives it from
    // `SV_Position.y` and that runs downward while NDC +Y runs up. Getting this
    // backwards is a lighting bug that is invisible in a symmetric scene and
    // obvious in every other one, so the flip lives here rather than in the
    // shader where it would have to be remembered twice.
    const f32 topNdc = 1.0f - static_cast<f32>(tileY) / static_cast<f32>(kClusterTilesY) * 2.0f;
    const f32 bottomNdc = 1.0f - static_cast<f32>(tileY + 1) / static_cast<f32>(kClusterTilesY) * 2.0f;

    ClusterBounds bounds;
    // A frustum slice's x extent grows with depth, so the near plane supplies
    // one end of the box and the far plane the other -- taken as min and max
    // rather than assumed, because a tile straddling the centre has its widest
    // edge at the far plane on one side and the near plane on the other.
    const f32 xa = leftNdc * tanHalfFovX * nearDepth;
    const f32 xb = leftNdc * tanHalfFovX * farDepth;
    const f32 xc = rightNdc * tanHalfFovX * nearDepth;
    const f32 xd = rightNdc * tanHalfFovX * farDepth;
    const f32 ya = bottomNdc * tanHalfFovY * nearDepth;
    const f32 yb = bottomNdc * tanHalfFovY * farDepth;
    const f32 yc = topNdc * tanHalfFovY * nearDepth;
    const f32 yd = topNdc * tanHalfFovY * farDepth;

    bounds.low =
        Vec3{std::min(std::min(xa, xb), std::min(xc, xd)), std::min(std::min(ya, yb), std::min(yc, yd)), -farDepth};
    bounds.high =
        Vec3{std::max(std::max(xa, xb), std::max(xc, xd)), std::max(std::max(ya, yb), std::max(yc, yd)), -nearDepth};
    return bounds;
}

[[nodiscard]] bool sphereTouches(const ClusterBounds& bounds, Vec3 centre, f32 radius) noexcept
{
    // The closest point on the box to the sphere's centre, which is the whole
    // test: a sphere touches a box exactly when that point is within its radius.
    f32 distanceSquared = 0.0f;
    const f32 centreAxis[3]{centre.x, centre.y, centre.z};
    const f32 lowAxis[3]{bounds.low.x, bounds.low.y, bounds.low.z};
    const f32 highAxis[3]{bounds.high.x, bounds.high.y, bounds.high.z};
    for (u32 axis = 0; axis < 3; ++axis) {
        const f32 value = centreAxis[axis];
        if (value < lowAxis[axis]) {
            const f32 delta = lowAxis[axis] - value;
            distanceSquared += delta * delta;
        }
        else if (value > highAxis[axis]) {
            const f32 delta = value - highAxis[axis];
            distanceSquared += delta * delta;
        }
    }
    return distanceSquared <= radius * radius;
}

[[nodiscard]] f32 sliceDepth(u32 slice, f32 nearPlane, f32 ratio) noexcept
{
    return nearPlane * std::pow(ratio, static_cast<f32>(slice) / static_cast<f32>(kClusterSlices));
}

} // namespace

void ClusterGrid::clear() noexcept
{
    grid.assign(kClusterCount, 0.0f);
    indices.assign(kLightIndexCapacity, 0.0f);
    lightData.assign(static_cast<usize>(kMaxClusteredLights) * 3 * 4, 0.0f);
    lightCount = 0;
    indexCount = 0;
    overflowClusters = 0;
}

void clusterSliceConstants(f32 nearPlane, f32 farPlane, f32& outScale, f32& outBias) noexcept
{
    const f32 near = std::max(nearPlane, 1e-3f);
    const f32 far = std::max(farPlane, near * 1.001f);
    const f32 logRatio = std::log2(far / near);
    outScale = static_cast<f32>(kClusterSlices) / logRatio;
    outBias = -static_cast<f32>(kClusterSlices) * std::log2(near) / logRatio;
}

u32 clusterSliceOf(f32 viewDepth, f32 sliceScale, f32 sliceBias) noexcept
{
    const f32 raw = std::log2(std::max(viewDepth, 1e-6f)) * sliceScale + sliceBias;
    const f32 clamped = std::max(raw, 0.0f);
    const auto slice = static_cast<u32>(clamped);
    return slice < kClusterSlices ? slice : kClusterSlices - 1;
}

void buildClusters(const RenderCamera& camera, std::span<const RenderLight> lights, ClusterGrid& out)
{
    out.clear();

    const f32 nearPlane = std::max(camera.nearPlane, 1e-3f);
    // Clamped, and this is a real decision rather than a guard: an example may
    // set a far plane of five thousand, and slicing to it would put every light
    // in a scene of twenty metres into the first two slices. Lights past this
    // are still assigned -- the last slice absorbs them -- they simply stop
    // being separated from one another.
    const f32 farPlane = std::clamp(camera.farPlane, nearPlane * 2.0f, 500.0f);
    clusterSliceConstants(nearPlane, farPlane, out.sliceScale, out.sliceBias);
    const f32 ratio = farPlane / nearPlane;

    const f32 tanHalfFovX = camera.projection.m[0][0] != 0.0f ? 1.0f / camera.projection.m[0][0] : 1.0f;
    const f32 tanHalfFovY = camera.projection.m[1][1] != 0.0f ? 1.0f / camera.projection.m[1][1] : 1.0f;

    out.lightCount = static_cast<u32>(std::min<usize>(lights.size(), kMaxClusteredLights));
    if (out.lightCount == 0)
        return;

    // Each light, once, in view space. The snapshot's lights are camera-relative
    // in WORLD orientation, and clustering works in the camera's own frame.
    struct ViewLight
    {
        Vec3 position;
        f32 range = 0.0f;
        u32 tileLow[2]{};
        u32 tileHigh[2]{};
        u32 sliceLow = 0;
        u32 sliceHigh = 0;
        bool touchesFrustum = false;
    };
    std::vector<ViewLight> viewLights(out.lightCount);

    for (u32 index = 0; index < out.lightCount; ++index) {
        const RenderLight& light = lights[index];
        ViewLight& entry = viewLights[index];
        entry.position = core::transformPoint(camera.view, light.position);
        entry.range = std::max(light.range, 1e-3f);

        const f32 depth = -entry.position.z;
        const f32 lowDepth = depth - entry.range;
        const f32 highDepth = depth + entry.range;
        if (highDepth <= nearPlane)
            continue;
        entry.touchesFrustum = true;
        entry.sliceLow = clusterSliceOf(std::max(lowDepth, nearPlane), out.sliceScale, out.sliceBias);
        entry.sliceHigh = clusterSliceOf(std::max(highDepth, nearPlane), out.sliceScale, out.sliceBias);

        // The light's screen extent, from the bounding box of its sphere
        // projected at the CLOSEST depth it reaches. Conservative: a sphere's
        // silhouette is widest where it is nearest, and clamping that depth to
        // the near plane is what keeps a light the camera is inside from
        // projecting to nothing.
        const f32 projectionDepth = std::max(lowDepth, nearPlane);
        const f32 halfWidth = tanHalfFovX * projectionDepth;
        const f32 halfHeight = tanHalfFovY * projectionDepth;
        const f32 lowX = (entry.position.x - entry.range) / std::max(halfWidth, 1e-6f);
        const f32 highX = (entry.position.x + entry.range) / std::max(halfWidth, 1e-6f);
        const f32 lowY = (entry.position.y - entry.range) / std::max(halfHeight, 1e-6f);
        const f32 highY = (entry.position.y + entry.range) / std::max(halfHeight, 1e-6f);

        const auto toTileX = [](f32 ndc) {
            const f32 scaled = (ndc * 0.5f + 0.5f) * static_cast<f32>(kClusterTilesX);
            return static_cast<u32>(std::clamp(scaled, 0.0f, static_cast<f32>(kClusterTilesX) - 1.0f));
        };
        // Tile Y counts down the screen, so the HIGHER normalised coordinate is
        // the LOWER tile index -- see `boundsOf`.
        const auto toTileY = [](f32 ndc) {
            const f32 scaled = (0.5f - ndc * 0.5f) * static_cast<f32>(kClusterTilesY);
            return static_cast<u32>(std::clamp(scaled, 0.0f, static_cast<f32>(kClusterTilesY) - 1.0f));
        };
        entry.tileLow[0] = toTileX(lowX);
        entry.tileHigh[0] = toTileX(highX);
        entry.tileLow[1] = toTileY(highY);
        entry.tileHigh[1] = toTileY(lowY);
    }

    // Two passes over the same traversal: count, prefix-sum, then fill. That is
    // what makes each cluster's run contiguous AND ascending by light index --
    // one pass with per-cluster vectors would be an allocation per cluster per
    // frame and would still need the flatten.
    std::vector<u32> counts(kClusterCount, 0);
    const auto forEachTouched = [&](auto&& visit) {
        for (u32 index = 0; index < out.lightCount; ++index) {
            const ViewLight& entry = viewLights[index];
            if (!entry.touchesFrustum)
                continue;
            for (u32 slice = entry.sliceLow; slice <= entry.sliceHigh; ++slice) {
                const f32 nearDepth = sliceDepth(slice, nearPlane, ratio);
                const f32 farDepth = sliceDepth(slice + 1, nearPlane, ratio);
                for (u32 tileY = entry.tileLow[1]; tileY <= entry.tileHigh[1]; ++tileY) {
                    for (u32 tileX = entry.tileLow[0]; tileX <= entry.tileHigh[0]; ++tileX) {
                        const ClusterBounds bounds =
                            boundsOf(tileX, tileY, nearDepth, farDepth, tanHalfFovX, tanHalfFovY);
                        if (!sphereTouches(bounds, entry.position, entry.range))
                            continue;
                        const u32 cluster = clusterIndexOf(tileX, tileY, slice);
                        visit(cluster, index);
                    }
                }
            }
        }
    };

    forEachTouched([&](u32 cluster, u32) {
        if (counts[cluster] < kMaxLightsPerCluster)
            ++counts[cluster];
        else if (counts[cluster] == kMaxLightsPerCluster)
            ++out.overflowClusters;
    });

    std::vector<u32> offsets(kClusterCount, 0);
    u32 running = 0;
    for (u32 cluster = 0; cluster < kClusterCount; ++cluster) {
        offsets[cluster] = running;
        // A cluster whose run would not fit gets no run at all rather than a
        // truncated one: half a light list is a light that flickers on and off
        // as the camera moves, which is harder to diagnose than a light that is
        // simply missing and counted.
        if (running + counts[cluster] > kLightIndexCapacity) {
            counts[cluster] = 0;
            ++out.overflowClusters;
        }
        running += counts[cluster];
    }
    out.indexCount = running;

    std::vector<u32> written(kClusterCount, 0);
    forEachTouched([&](u32 cluster, u32 lightIndex) {
        if (written[cluster] >= counts[cluster])
            return;
        out.indices[offsets[cluster] + written[cluster]] = static_cast<f32>(lightIndex);
        ++written[cluster];
    });

    for (u32 cluster = 0; cluster < kClusterCount; ++cluster)
        out.grid[cluster] =
            static_cast<f32>(offsets[cluster]) * kClusterOffsetShift + static_cast<f32>(counts[cluster]);

    // The light table itself, in `GpuLight`'s own three rows so the shader
    // rebuilds the same struct it used to read out of a constant buffer.
    for (u32 index = 0; index < out.lightCount; ++index) {
        const RenderLight& light = lights[index];
        const ViewLight& entry = viewLights[index];
        f32* row = out.lightData.data() + static_cast<usize>(index) * 12;
        // Camera-relative in WORLD orientation, exactly as `GpuLight` has always
        // documented -- the view-space copy above exists for the clustering and
        // for nothing else. Shading stays in the space every other interpolant
        // is in; a light table in view space would have been one basis nobody
        // could see in the struct.
        (void)entry;
        row[0] = light.position.x;
        row[1] = light.position.y;
        row[2] = light.position.z;
        row[3] = light.range;
        row[4] = light.color.r * light.brightness;
        row[5] = light.color.g * light.brightness;
        row[6] = light.color.b * light.brightness;
        row[7] = 0.0f;
        row[8] = light.direction.x;
        row[9] = light.direction.y;
        row[10] = light.direction.z;
        row[11] = light.kind == LightKind::Spot ? light.spotCosHalfAngle : -1.0f;
    }
}

} // namespace luaug::render
