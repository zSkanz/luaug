#include "luaug/render/clusters.h"

#include <cmath>
#include <doctest/doctest.h>
#include <vector>

using namespace luaug;
using luaug::render::buildClusters;
using luaug::render::ClusterGrid;
using luaug::render::clusterIndexOf;
using luaug::render::clusterSliceConstants;
using luaug::render::clusterSliceOf;
using luaug::render::kClusterCount;
using luaug::render::kClusterOffsetShift;
using luaug::render::kClusterSlices;
using luaug::render::kClusterTilesX;
using luaug::render::kClusterTilesY;
using luaug::render::kMaxClusteredLights;
using luaug::render::RenderCamera;
using luaug::render::RenderLight;

namespace {

[[nodiscard]] RenderCamera testCamera() noexcept
{
    RenderCamera camera;
    camera.valid = true;
    camera.nearPlane = 0.1f;
    camera.farPlane = 400.0f;
    camera.projection =
        core::perspective(55.0f * 3.14159265f / 180.0f, 16.0f / 9.0f, camera.nearPlane, camera.farPlane);
    // The camera sits at the origin of the snapshot's space and looks down -Z,
    // which is what an identity view means here.
    camera.view = core::Mat4{};
    camera.viewProjection = camera.projection;
    return camera;
}

// The two numbers a grid texel packs, unpacked the way the shader does.
void unpackCluster(const ClusterGrid& clusters, core::u32 cluster, core::u32& outOffset, core::u32& outCount)
{
    const core::f32 packed = clusters.grid[cluster];
    outOffset = static_cast<core::u32>(packed / kClusterOffsetShift);
    outCount = static_cast<core::u32>(packed - static_cast<core::f32>(outOffset) * kClusterOffsetShift);
}

[[nodiscard]] RenderLight lightAt(core::Vec3 position, core::f32 range) noexcept
{
    RenderLight light;
    light.position = position;
    light.range = range;
    light.brightness = 1.0f;
    return light;
}

} // namespace

TEST_CASE("a cluster's index is its texel's row-major position, and both sides agree")
{
    // The bug this exists for: the assignment numbered clusters slice-major
    // while the shader read the grid texture row-major, so every fragment looked
    // up some other part of the screen. Both sides were internally consistent,
    // every unit test passed, and it was visible only as light clipped into hard
    // horizontal bands.
    //
    // The shader computes `x = slice * TILES_X + tileX` and `y = tileY` against
    // a texture `kClusterGridWidth` wide. This is that, written once.
    for (core::u32 slice = 0; slice < kClusterSlices; ++slice) {
        for (core::u32 tileY = 0; tileY < kClusterTilesY; ++tileY) {
            for (core::u32 tileX = 0; tileX < kClusterTilesX; ++tileX) {
                const core::u32 texelX = slice * kClusterTilesX + tileX;
                CHECK(clusterIndexOf(tileX, tileY, slice) == tileY * luaug::render::kClusterGridWidth + texelX);
                CHECK(clusterIndexOf(tileX, tileY, slice) < kClusterCount);
            }
        }
    }
}

TEST_CASE("exponential slicing puts the near plane in slice zero and the far one in the last")
{
    core::f32 scale = 0.0f;
    core::f32 bias = 0.0f;
    clusterSliceConstants(0.1f, 500.0f, scale, bias);

    CHECK(clusterSliceOf(0.1f, scale, bias) == 0u);
    CHECK(clusterSliceOf(0.05f, scale, bias) == 0u);
    CHECK(clusterSliceOf(499.0f, scale, bias) == kClusterSlices - 1);
    CHECK(clusterSliceOf(100000.0f, scale, bias) == kClusterSlices - 1);

    // Monotone, and that is the property the shader depends on: it computes the
    // same expression and must land in the same slice the assignment used.
    core::u32 previous = 0;
    for (core::f32 depth = 0.1f; depth < 500.0f; depth *= 1.15f) {
        const core::u32 slice = clusterSliceOf(depth, scale, bias);
        CHECK(slice >= previous);
        previous = slice;
    }

    // And it is exponential rather than uniform, which is the whole reason for
    // the logarithm: half the slices are spent inside the first two per cent of
    // the range under a uniform scheme, and here they are not.
    CHECK(clusterSliceOf(1.0f, scale, bias) > 4u);
    CHECK(clusterSliceOf(1.0f, scale, bias) < kClusterSlices / 2);
}

TEST_CASE("a light in front of the camera reaches clusters; one behind it reaches none")
{
    const RenderCamera camera = testCamera();
    ClusterGrid clusters;

    const std::vector<RenderLight> lights{lightAt(core::Vec3{0.0f, 0.0f, -5.0f}, 4.0f)};
    buildClusters(camera, lights, clusters);
    CHECK(clusters.lightCount == 1u);
    CHECK(clusters.indexCount > 0u);

    // Entirely behind the near plane. Assigning it would be a light that costs
    // every fragment in a tile and contributes nothing.
    const std::vector<RenderLight> behind{lightAt(core::Vec3{0.0f, 0.0f, 40.0f}, 4.0f)};
    buildClusters(camera, behind, clusters);
    CHECK(clusters.lightCount == 1u);
    CHECK(clusters.indexCount == 0u);
}

TEST_CASE("the ninth light lights something, which is the whole point of this pass")
{
    // M4 shipped eight lights per draw, unculled, and ADR 0038 names that as one
    // of the four gaps. The claim being checked is the narrow one that matters:
    // a light past the eighth is assigned rather than silently dropped.
    const RenderCamera camera = testCamera();
    ClusterGrid clusters;

    std::vector<RenderLight> lights;
    for (core::u32 index = 0; index < 24; ++index) {
        const auto offset = static_cast<core::f32>(index) - 11.5f;
        lights.push_back(lightAt(core::Vec3{offset * 0.7f, 0.0f, -12.0f}, 3.0f));
    }
    buildClusters(camera, lights, clusters);
    CHECK(clusters.lightCount == 24u);

    std::vector<bool> assigned(lights.size(), false);
    for (core::u32 cluster = 0; cluster < kClusterCount; ++cluster) {
        core::u32 offset = 0;
        core::u32 count = 0;
        unpackCluster(clusters, cluster, offset, count);
        for (core::u32 i = 0; i < count; ++i) {
            const auto index = static_cast<core::usize>(clusters.indices[offset + i]);
            REQUIRE(index < assigned.size());
            assigned[index] = true;
        }
    }
    for (core::usize index = 0; index < assigned.size(); ++index)
        CHECK(assigned[index]);
}

TEST_CASE("a cluster's run is contiguous, ascending, and bounded")
{
    const RenderCamera camera = testCamera();
    ClusterGrid clusters;

    std::vector<RenderLight> lights;
    for (core::u32 index = 0; index < 40; ++index) {
        const auto angle = static_cast<core::f32>(index) * 0.7f;
        lights.push_back(lightAt(core::Vec3{std::cos(angle) * 3.0f, std::sin(angle) * 2.0f, -9.0f}, 6.0f));
    }
    buildClusters(camera, lights, clusters);

    core::u32 previousEnd = 0;
    for (core::u32 cluster = 0; cluster < kClusterCount; ++cluster) {
        core::u32 offset = 0;
        core::u32 count = 0;
        unpackCluster(clusters, cluster, offset, count);
        if (count == 0)
            continue;

        // Runs are laid out in cluster order and never overlap, which is what
        // the prefix sum buys and what the shader assumes when it reads `count`
        // entries starting at `offset`.
        CHECK(offset >= previousEnd);
        previousEnd = offset + count;
        CHECK(previousEnd <= clusters.indexCount);

        // Ascending by light index within the run: the two passes visit lights
        // in the same order, so this is a pure function of the snapshot (R10)
        // even though nothing here is simulation.
        core::f32 previous = -1.0f;
        for (core::u32 i = 0; i < count; ++i) {
            CHECK(clusters.indices[offset + i] > previous);
            previous = clusters.indices[offset + i];
        }
    }
}

TEST_CASE("the same snapshot builds the same tables, byte for byte")
{
    const RenderCamera camera = testCamera();
    std::vector<RenderLight> lights;
    for (core::u32 index = 0; index < 30; ++index) {
        const auto angle = static_cast<core::f32>(index) * 1.3f;
        lights.push_back(lightAt(core::Vec3{std::cos(angle) * 5.0f, std::sin(angle) * 3.0f, -14.0f}, 7.0f));
    }

    ClusterGrid first;
    ClusterGrid second;
    buildClusters(camera, lights, first);
    buildClusters(camera, lights, second);

    CHECK(first.grid == second.grid);
    CHECK(first.indices == second.indices);
    CHECK(first.lightData == second.lightData);
    CHECK(first.indexCount == second.indexCount);
}

TEST_CASE("a light is assigned to the tile it is actually over, not to its mirror")
{
    // The one axis that is easy to get backwards and invisible in a symmetric
    // scene: tile Y counts DOWN the screen because the shader derives it from
    // `SV_Position.y`, while normalised device Y runs up. A light placed high in
    // the world must land in a LOW tile index.
    const RenderCamera camera = testCamera();
    ClusterGrid clusters;
    const std::vector<RenderLight> lights{lightAt(core::Vec3{0.0f, 3.0f, -10.0f}, 1.0f)};
    buildClusters(camera, lights, clusters);
    REQUIRE(clusters.indexCount > 0u);

    core::u32 lowestTileY = kClusterTilesY;
    core::u32 highestTileY = 0;
    for (core::u32 cluster = 0; cluster < kClusterCount; ++cluster) {
        core::u32 offset = 0;
        core::u32 count = 0;
        unpackCluster(clusters, cluster, offset, count);
        if (count == 0)
            continue;
        const core::u32 tileY = cluster / luaug::render::kClusterGridWidth;
        lowestTileY = tileY < lowestTileY ? tileY : lowestTileY;
        highestTileY = tileY > highestTileY ? tileY : highestTileY;
    }

    CHECK(highestTileY < kClusterTilesY / 2);
    CHECK(lowestTileY <= highestTileY);
}

TEST_CASE("more lights than the budget are refused rather than wrapped")
{
    const RenderCamera camera = testCamera();
    ClusterGrid clusters;

    std::vector<RenderLight> lights;
    for (core::u32 index = 0; index < kMaxClusteredLights + 40; ++index)
        lights.push_back(lightAt(core::Vec3{0.0f, 0.0f, -8.0f}, 2.0f));
    buildClusters(camera, lights, clusters);

    // The budget is a budget and it is enforced at the table's edge. A count
    // that wrapped would index past the light texture, which is the difference
    // between "the two hundred and fifty-seventh light does not light" and "the
    // frame reads whatever is next in memory".
    CHECK(clusters.lightCount == kMaxClusteredLights);
    for (core::u32 index = 0; index < clusters.indexCount; ++index)
        CHECK(clusters.indices[index] < static_cast<core::f32>(kMaxClusteredLights));
    // And a pile of overlapping lights is exactly the case the per-cluster bound
    // exists for, so it must report having bound something.
    CHECK(clusters.overflowClusters > 0u);
}
