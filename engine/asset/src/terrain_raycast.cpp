#include "luaug/asset/terrain.h"

#include <algorithm>
#include <cmath>

namespace luaug::asset {
namespace {

using core::DVec3;
using core::Vec3;

// How many bisection steps refine the crossing once the march has bracketed it.
//
// Twenty halvings take one voxel to about a millionth of it, which is far below
// anything a brush cares about and cheap enough that the count is a constant
// rather than a tolerance somebody tunes. A tolerance would also have to be in
// metres, and this function is used at voxel sizes it does not know in advance.
constexpr int RefineSteps = 20;

// **The march step is a fraction of a voxel, not a voxel** (D-shaped hazard, and
// worth stating). The field is a distance function only near the surface: far
// from it a sample saturates, so a sphere-tracing march would either overshoot
// or crawl. A fixed sub-voxel step cannot skip a surface thinner than the step,
// and half a voxel is fine enough that no terrain feature the mesher can express
// is missed -- the mesher itself only resolves at one voxel.
constexpr double StepFraction = 0.5;

} // namespace

std::optional<TerrainHit> raycastField(const TerrainField& field, DVec3 origin, Vec3 direction, double maxDistance)
{
    // **Every promotion is explicit**, which is R9's f32/f64 split showing up as
    // an ergonomic cost: a direction is an extent and is `f32`, a ray's origin is
    // a world position and is `f64`, and the two have to meet somewhere. Clang
    // diagnoses each implicit widening and MSVC does not, so leaving them
    // implicit would be a Linux-only build break every time.
    const double dirX = static_cast<double>(direction.x);
    const double dirY = static_cast<double>(direction.y);
    const double dirZ = static_cast<double>(direction.z);
    const double length = std::sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ);
    if (!(length > 0.0) || !(maxDistance > 0.0)) {
        return std::nullopt;
    }
    const DVec3 step{dirX / length, dirY / length, dirZ / length};

    const double voxel = static_cast<double>(field.settings().voxelSize);
    if (!(voxel > 0.0)) {
        return std::nullopt;
    }

    // **Trilinear between the eight surrounding samples, never snapped to the
    // nearest one.**
    //
    // Snapping was the first version and it is wrong in a way that looks almost
    // right: it makes the field a STEP function with steps one voxel wide, so a
    // bisection converges on the step's edge rather than on the surface, and
    // every hit lands half a voxel out. On a flat field at y = 4 with a
    // half-metre voxel, it reported 4.25.
    //
    // Interpolating also makes this agree with the MESHER, which places a vertex
    // by linear interpolation along an edge for the same reason -- so a hit sits
    // on the triangle that was drawn there, and a decal placed at one is flush
    // with the ground rather than sunk into it.
    const auto sampleAt = [&](const DVec3& at) {
        const double gridX = at.x / voxel;
        const double gridY = at.y / voxel;
        const double gridZ = at.z / voxel;
        const double baseX = std::floor(gridX);
        const double baseY = std::floor(gridY);
        const double baseZ = std::floor(gridZ);
        const auto lowX = static_cast<core::i32>(baseX);
        const auto lowY = static_cast<core::i32>(baseY);
        const auto lowZ = static_cast<core::i32>(baseZ);
        const auto tx = static_cast<float>(gridX - baseX);
        const auto ty = static_cast<float>(gridY - baseY);
        const auto tz = static_cast<float>(gridZ - baseZ);

        const auto blend = [](float a, float b, float t) { return a + (b - a) * t; };

        float corner[8];
        core::u8 materials[8];
        for (int at8 = 0; at8 < 8; ++at8) {
            const FieldSample got = field.sample(lowX + (at8 & 1), lowY + ((at8 >> 1) & 1), lowZ + ((at8 >> 2) & 1));
            corner[at8] = got.distance;
            materials[at8] = got.material;
        }

        const float x00 = blend(corner[0], corner[1], tx);
        const float x10 = blend(corner[2], corner[3], tx);
        const float x01 = blend(corner[4], corner[5], tx);
        const float x11 = blend(corner[6], corner[7], tx);
        const float y0 = blend(x00, x10, ty);
        const float y1 = blend(x01, x11, ty);

        // The material of the NEAREST corner rather than a blend: a material is
        // an identity, and the average of "rock" and "sand" is neither.
        const int nearest = (tx >= 0.5f ? 1 : 0) | (ty >= 0.5f ? 2 : 0) | (tz >= 0.5f ? 4 : 0);
        return FieldSample{blend(y0, y1, tz), materials[nearest]};
    };

    const auto distanceAt = [&](double along) {
        return sampleAt(DVec3{origin.x + step.x * along, origin.y + step.y * along, origin.z + step.z * along});
    };

    const double marchStep = voxel * StepFraction;

    FieldSample previous = distanceAt(0.0);
    // **A ray that starts inside the ground hits at once.** Refusing would be
    // wrong for the case that produces it: a brush dragged into a hillside, or a
    // camera inside terrain, both want the surface they are already past rather
    // than nothing.
    if (previous.distance <= 0.0f) {
        const Vec3 normal{0.0f, 1.0f, 0.0f};
        return TerrainHit{origin, normal, 0.0, previous.material};
    }

    double previousAlong = 0.0;
    for (double along = marchStep; along <= maxDistance; along += marchStep) {
        const FieldSample current = distanceAt(along);
        if (current.distance <= 0.0f) {
            // Bracketed between `previousAlong` (air) and `along` (ground).
            // Bisected rather than interpolated linearly: the field is only
            // approximately linear between samples, and near a brick boundary it
            // is not linear at all.
            double low = previousAlong;
            double high = along;
            for (int refine = 0; refine < RefineSteps; ++refine) {
                const double middle = (low + high) * 0.5;
                if (distanceAt(middle).distance <= 0.0f) {
                    high = middle;
                }
                else {
                    low = middle;
                }
            }

            const double hitAlong = high;
            const DVec3 position{origin.x + step.x * hitAlong, origin.y + step.y * hitAlong,
                                 origin.z + step.z * hitAlong};

            // The gradient by central differences, one voxel apart, which is the
            // same normal the mesher gives that surface -- so a decal placed on a
            // raycast hit sits flush with the triangle under it.
            const auto latticeX = static_cast<core::i32>(std::lround(position.x / voxel));
            const auto latticeY = static_cast<core::i32>(std::lround(position.y / voxel));
            const auto latticeZ = static_cast<core::i32>(std::lround(position.z / voxel));
            const float dx = field.sample(latticeX + 1, latticeY, latticeZ).distance -
                             field.sample(latticeX - 1, latticeY, latticeZ).distance;
            const float dy = field.sample(latticeX, latticeY + 1, latticeZ).distance -
                             field.sample(latticeX, latticeY - 1, latticeZ).distance;
            const float dz = field.sample(latticeX, latticeY, latticeZ + 1).distance -
                             field.sample(latticeX, latticeY, latticeZ - 1).distance;
            Vec3 normal{dx, dy, dz};
            const float normalLength = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
            if (normalLength < 1e-8f) {
                normal = Vec3{0.0f, 1.0f, 0.0f};
            }
            else {
                normal = Vec3{normal.x / normalLength, normal.y / normalLength, normal.z / normalLength};
            }

            return TerrainHit{position, normal, hitAlong, current.material};
        }

        previous = current;
        previousAlong = along;
    }

    return std::nullopt;
}

} // namespace luaug::asset
