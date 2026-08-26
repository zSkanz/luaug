#include "luaug/render/shadow.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace luaug::render {

// How many candidates the ranking will look at in one frame. Past this a light
// is refused without being weighed, which is reported rather than silent -- and
// the number is far above the tile budget on purpose, so the ORDER is decided
// among everything plausible even though only a few can be served.
inline constexpr core::usize kMaxRankedCandidates = 256;

// A spot's projection covers a little more than its cone. Fitted exactly, the
// rim lands on the tile edge where a filter tap reads off the map.
inline constexpr f32 kSpotFovMargin = 1.1f;
inline constexpr f32 kMinSpotFov = 0.1f;
inline constexpr f32 kMaxSpotFov = 2.8f;
inline constexpr f32 kHalfPi = 1.5707963267948966f;
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

// How much wider than its contents a freshly fitted box is made.
//
// This is the fit's hysteresis, and without it the memory below buys nothing: a
// box sized exactly to what casts stops covering it as soon as anything moves,
// so it would refit every frame and re-quantise every shadow edge every frame,
// which is the defect (D048). A quarter is enough that a character walking at
// seven metres a second inside a fifteen-metre cascade keeps one box for about a
// second, and small enough that the texel density the caster fit exists to
// recover is not given straight back.
constexpr f32 kFitMargin = 1.25f;

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

ShadowCascades fitShadowCascades(const ShadowFit& fit, const ShadowCascades* previous) noexcept
{
    ShadowCascades cascades;

    const Vec3 direction = core::normalize(fit.sunDirection);
    const Vec3 up = lightUp(direction);
    // Rotation only, with the light's view looking along -direction. A world
    // point's light-space coordinate is this times the point, and rounding
    // happens in that space.
    const Mat4 lightRotation = core::lookAt(Vec3{}, -direction, up);

    f32 splits[kShadowCascadeCount + 1]{};
    shadowSplits(fit.nearPlane, fit.distance, kShadowSplitLambda, splits);

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
        // Whether this cascade is reusing last frame's box. It decides two
        // things: the extent and centre above, and whether the snap below runs
        // at all -- see the comment there, which is D050.
        bool keptBox = false;
        // Where the box was last frame, in THIS frame's light space. The snap
        // rounds against it rather than against the world, which is what keeps a
        // turning light from sweeping the lattice out from under every shadow in
        // the picture (D050).
        f32 keptCentreX = 0.0f;
        f32 keptCentreY = 0.0f;
        bool hasKeptCentre = false;
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
                //
                // **Grown past what is wanted, on purpose.** The margin is what
                // gives the fit below something to keep: a box sized exactly to
                // its contents stops covering them the moment anything moves,
                // and a box that refits every frame is the crawl this whole
                // mechanism exists to remove (D048).
                const f32 quantum = std::max(radius * kFitQuantum, 1e-4f);
                const f32 grown = std::max(wanted * kFitMargin, quantum);
                extent = std::min(radius, std::ceil(grown / quantum) * quantum);

                // The slice centre, slid in the light's own x and y onto the
                // content's centre. Its depth along the light is left alone --
                // that axis is the depth range's business, below.
                f32 wantX = (minX + maxX) * 0.5f;
                f32 wantY = (minY + maxY) * 0.5f;

                // **The fit's memory, and what it is FOR changed at D050.**
                //
                // Keeping a box outright -- same centre, same extent, same
                // lattice -- is the most stable thing a cascade can do, and it
                // is what happens when the content has not moved out of it. In
                // an open world that is rarer than it sounds: the ground fills
                // the slice, so the content's light-space bounds ARE the slice's
                // own and they follow the camera exactly. The slack below is one
                // texel, because the box is quantised to texels anyway and one
                // of them at the very edge of a cascade -- inside the blend band
                // to the next -- is not a shadow anybody can see. Without it the
                // test failed by a hundredth of a metre every single frame, on
                // the snap's own displacement, and the memory never engaged.
                //
                // When the box cannot be kept, the snap below moves it RELATIVE
                // to where it was rather than onto a lattice fixed in the world.
                // That distinction is the whole of this defect.
                if (previous != nullptr && previous->fitted && previous->boxExtent[index] > 0.0f) {
                    const f32 keptExtent = previous->boxExtent[index];
                    const core::DVec3& keptWorld = previous->boxCentreWorld[index];
                    const Vec3 keptLocal{static_cast<f32>(keptWorld.x - fit.origin.x),
                                         static_cast<f32>(keptWorld.y - fit.origin.y),
                                         static_cast<f32>(keptWorld.z - fit.origin.z)};
                    keptCentreX = core::dot(lightX, keptLocal);
                    keptCentreY = core::dot(lightY, keptLocal);
                    hasKeptCentre = true;

                    const f32 slack = (2.0f * keptExtent) / static_cast<f32>(fit.tileResolution);
                    const bool covers =
                        minX >= keptCentreX - keptExtent - slack && maxX <= keptCentreX + keptExtent + slack &&
                        minY >= keptCentreY - keptExtent - slack && maxY <= keptCentreY + keptExtent + slack;
                    // Never keep a box far larger than the content: that is what
                    // stops one which once held a tower staying tower-sized for
                    // the rest of the session. Half rather than "any smaller",
                    // so the two rules cannot oscillate against each other.
                    const bool worthShrinking = wanted * kFitMargin < keptExtent * 0.5f;
                    if (covers && !worthShrinking && keptExtent <= radius) {
                        extent = keptExtent;
                        wantX = keptCentreX;
                        wantY = keptCentreY;
                        keptBox = true;
                    }
                }

                boxCentre = centre + lightX * (wantX - core::dot(lightX, centre)) +
                            lightY * (wantY - core::dot(lightY, centre));
            }
        }

        const f32 texel = (2.0f * extent) / static_cast<f32>(fit.tileResolution);

        // **The snap, and what it rounds AGAINST is the whole of D050.**
        //
        // A cascade that follows the camera has to move, and moving it by a
        // fraction of a texel drags every shadow edge across the grid it is
        // rasterised on. The cure is to move it in WHOLE TEXELS, which is
        // standard and which this has always done. What was wrong is the
        // reference: it rounded `R(t) * centreWorld` -- an absolute position, in
        // a light space that TURNS with the sun. With the box half a kilometre
        // from the world origin and a day of ten minutes, that coordinate sweeps
        // several texels per frame on its own, so the rounding flipped
        // constantly and the whole shadow map jumped a texel at a time in a
        // direction nothing in the scene explained. A human watching a tree's
        // shadow described it exactly: not sliding, trembling.
        //
        // Rounding the box's MOVEMENT instead of its position fixes it, and it
        // is the stronger statement anyway. What temporal stability asks for is
        // that this frame's grid line up with the LAST frame's -- not with a
        // grid fixed in the world, which stops being fixed the moment the light
        // turns. So the delta from where the box was is what gets quantised, and
        // the lattice is carried forward from frame to frame instead of being
        // re-derived from an absolute that is sliding underneath it.
        //
        // The first frame of a run has nothing to carry, so it rounds against
        // the world once. That is as good a starting phase as any.
        const Vec3 lightAxisX{lightRotation.m[0][0], lightRotation.m[1][0], lightRotation.m[2][0]};
        const Vec3 lightAxisY{lightRotation.m[0][1], lightRotation.m[1][1], lightRotation.m[2][1]};
        const auto texelD = static_cast<f64>(texel);

        if (!keptBox) {
            f64 offsetX = 0.0;
            f64 offsetY = 0.0;

            if (hasKeptCentre) {
                const f64 deltaX = static_cast<f64>(core::dot(lightAxisX, boxCentre) - keptCentreX);
                const f64 deltaY = static_cast<f64>(core::dot(lightAxisY, boxCentre) - keptCentreY);
                offsetX = std::round(deltaX / texelD) * texelD - deltaX;
                offsetY = std::round(deltaY / texelD) * texelD - deltaY;
            }
            else {
                const DVec3 centreWorld{fit.origin.x + static_cast<f64>(boxCentre.x),
                                        fit.origin.y + static_cast<f64>(boxCentre.y),
                                        fit.origin.z + static_cast<f64>(boxCentre.z)};
                const f64 lightPosX = static_cast<f64>(lightAxisX.x) * centreWorld.x +
                                      static_cast<f64>(lightAxisX.y) * centreWorld.y +
                                      static_cast<f64>(lightAxisX.z) * centreWorld.z;
                const f64 lightPosY = static_cast<f64>(lightAxisY.x) * centreWorld.x +
                                      static_cast<f64>(lightAxisY.y) * centreWorld.y +
                                      static_cast<f64>(lightAxisY.z) * centreWorld.z;
                offsetX = std::round(lightPosX / texelD) * texelD - lightPosX;
                offsetY = std::round(lightPosY / texelD) * texelD - lightPosY;
            }

            // Applied to the CENTRE rather than folded into the view's
            // translation, so that what is remembered for the next frame is the
            // snapped centre and not the one before it -- otherwise the next
            // frame would measure its delta from a position nothing was ever
            // rendered at.
            boxCentre = boxCentre + lightAxisX * static_cast<f32>(offsetX) + lightAxisY * static_cast<f32>(offsetY);
        }

        // The depth range stays the SLICE's, not the box's: the box was narrowed
        // across the light, and how far along it a caster may stand is a
        // different question with the same answer it had before.
        const f32 depthRange = 2.0f * radius + kShadowCasterMargin;
        const Mat4 view = core::lookAt(boxCentre + direction * (radius + kShadowCasterMargin), boxCentre, up);

        cascades.viewProjection[index] = orthographic(extent, 0.0f, depthRange) * view;
        cascades.farDistance[index] = sliceFar;
        cascades.texelWorld[index] = texel;
        cascades.depthRange[index] = depthRange;
        // Kept for the next frame, in world space -- see `ShadowCascades`.
        cascades.boxCentreWorld[index] =
            DVec3{fit.origin.x + static_cast<f64>(boxCentre.x), fit.origin.y + static_cast<f64>(boxCentre.y),
                  fit.origin.z + static_cast<f64>(boxCentre.z)};
        cascades.boxExtent[index] = extent;

        cascades.cullCentre[index] = centre;
        cascades.cullRadius[index] = radius;
    }

    cascades.fitted = true;
    return cascades;
}

// --- Local lights ------------------------------------------------------------

namespace {

// The six cube directions and the up vector each one needs, in `CubeFace` order.
//
// **The up vectors are not arbitrary.** A look-at whose forward is parallel to
// its up produces a degenerate basis, so the two vertical faces have to use a
// different up from the four horizontal ones -- and the choice fixes which way
// each face is oriented, which is a thing a shader and a test have to agree on
// rather than discover.
struct CubeAxis
{
    Vec3 forward;
    Vec3 up;
};

constexpr CubeAxis kCubeAxes[kPointShadowTiles] = {
    {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},  {{-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},  {{0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
    {{0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},  {{0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}},
};

// What a light is worth a tile for: how large it looms from the camera, which in
// camera-relative space is its range over its distance. A light the camera is
// inside gets the largest finite score there is rather than an infinity, so the
// ordering below never has to reason about one.
[[nodiscard]] f32 apparentSize(const LocalShadowCandidate& candidate) noexcept
{
    if (!(candidate.range > 0.0f))
        return 0.0f;
    const f32 distanceSquared = candidate.position.x * candidate.position.x +
                                candidate.position.y * candidate.position.y +
                                candidate.position.z * candidate.position.z;
    const f32 distance = std::sqrt(distanceSquared);
    // Inside its own range: as important as a light can be.
    if (distance <= candidate.range)
        return std::numeric_limits<f32>::max();
    return candidate.range / distance;
}

[[nodiscard]] bool isPoint(const LocalShadowCandidate& candidate) noexcept
{
    // The same test `GpuLight` documents, and deliberately not a separate enum:
    // two spellings of "which kind is this" is one that can disagree.
    return !(candidate.cosHalfAngle > -1.0f);
}

} // namespace

LocalShadowTileRect localShadowTileRect(u32 tile) noexcept
{
    LocalShadowTileRect rect;
    const u32 wrapped = tile % kLocalShadowTileCount;
    rect.x = (wrapped % kLocalShadowAtlasTiles) * kLocalShadowTileResolution;
    rect.y = (wrapped / kLocalShadowAtlasTiles) * kLocalShadowTileResolution;
    return rect;
}

LocalShadows fitLocalShadows(std::span<const LocalShadowCandidate> candidates, u32 tileBudget) noexcept
{
    LocalShadows out;
    if (candidates.empty() || tileBudget == 0)
        return out;
    if (tileBudget > kLocalShadowTileCount)
        tileBudget = kLocalShadowTileCount;

    // Ordered by apparent size, ties by index. **A stable sort is not enough on
    // its own** -- two lights at the same distance with the same range are a
    // real case, a corridor of identical lamps -- so the index is part of the
    // comparison rather than left to the algorithm's discretion.
    struct Ranked
    {
        u32 index;
        f32 size;
    };
    std::array<Ranked, kMaxRankedCandidates> ranked{};
    core::usize rankedCount = 0;
    for (core::usize index = 0; index < candidates.size() && rankedCount < ranked.size(); ++index) {
        const f32 size = apparentSize(candidates[index]);
        if (size <= 0.0f) {
            ++out.refused;
            continue;
        }
        ranked[rankedCount++] = Ranked{static_cast<u32>(index), size};
    }
    // Anything past the cap did not even get considered, and saying so is the
    // difference between a budget and a silent truncation.
    if (candidates.size() > ranked.size())
        out.refused += static_cast<u32>(candidates.size() - ranked.size());

    std::sort(ranked.begin(), ranked.begin() + static_cast<std::ptrdiff_t>(rankedCount),
              [](const Ranked& a, const Ranked& b) noexcept {
                  if (a.size != b.size)
                      return a.size > b.size;
                  return a.index < b.index;
              });

    for (core::usize slot = 0; slot < rankedCount; ++slot) {
        const u32 index = ranked[slot].index;
        const LocalShadowCandidate& candidate = candidates[index];
        const bool point = isPoint(candidate);
        const u32 tiles = point ? kPointShadowTiles : kSpotShadowTiles;

        if (out.tilesUsed + tiles > tileBudget) {
            // **Refused, and the loop keeps going.** A point light that does not
            // fit must not stop the four spots behind it from fitting -- the
            // budget is tiles and not a queue.
            ++out.refused;
            continue;
        }

        LocalShadow& shadow = out.entries[out.count];
        shadow.candidate = index;
        shadow.firstTile = out.tilesUsed;
        shadow.tileCount = tiles;
        shadow.nearPlane = kLocalShadowNearPlane;
        shadow.farPlane = candidate.range;

        if (point) {
            const Mat4 projection = core::perspective(kHalfPi, 1.0f, shadow.nearPlane, shadow.farPlane);
            for (u32 face = 0; face < kPointShadowTiles; ++face) {
                const CubeAxis& axis = kCubeAxes[face];
                shadow.viewProjection[face] =
                    projection * core::lookAt(candidate.position, candidate.position + axis.forward, axis.up);
            }
        }
        else {
            // **The full cone and a little more.** The projection has to cover
            // the cone the shading term evaluates, and a field of view fitted
            // exactly to the half angle puts the cone's rim on the tile's edge
            // where a filter tap reads off the map. Doubling the half angle and
            // adding a margin costs resolution and buys a shadow whose edge is
            // inside the picture.
            const f32 halfAngle = std::acos(std::clamp(candidate.cosHalfAngle, -1.0f, 1.0f));
            const f32 fov = std::clamp(2.0f * halfAngle * kSpotFovMargin, kMinSpotFov, kMaxSpotFov);
            const Vec3 up = std::abs(candidate.direction.y) > 0.99f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 1.0f, 0.0f};
            shadow.viewProjection[0] = core::perspective(fov, 1.0f, shadow.nearPlane, shadow.farPlane) *
                                       core::lookAt(candidate.position, candidate.position + candidate.direction, up);
        }

        out.tilesUsed += tiles;
        ++out.count;
    }

    return out;
}

} // namespace luaug::render
