// The sun's shadow map: its fixed dimensions, and the matrix that fits the
// world into it.
//
// Split out of `renderer_default.cpp` when M4.5 made the matrix answer a
// question a test has to be able to ask: *does a fixed world point stay on the
// same shadow texel when the camera moves a fraction of one?* It did not, and
// the crawling edges a human reported were the consequence. A function that
// only a pass list can call is a function nothing can hold to that.
//
// Everything here is deliberately a compile-time constant. One cascade, a fixed
// extent, a fixed resolution -- so the texel size is a constant too, and
// snapping to it needs no fit, no history and no per-frame state. The
// stable-fit machinery that a moving, camera-fitted box would need belongs with
// the cascades it exists for, which are not in v1's renderer.
#pragma once

#include "luaug/core/math.h"
#include "luaug/core/types.h"

namespace luaug::render {

using core::f32;
using core::u32;

// One cascade, and the roadmap says so. A fixed extent rather than a fit to the
// visible geometry: fitting makes the map's texels move with the camera, which
// makes shadow edges crawl.
inline constexpr u32 kShadowResolution = 2048;
inline constexpr f32 kShadowExtent = 60.0f;
inline constexpr f32 kShadowDepth = 200.0f;

// One shadow texel in light-space metres. 0.0586 m at the numbers above.
inline constexpr f32 kShadowTexel = (2.0f * kShadowExtent) / static_cast<f32>(kShadowResolution);

// The ortho box is a cube of half-extent `kShadowExtent` centred on the camera,
// so its corner reaches sqrt(3) times as far. `extract` culls against this: a
// caster further out can neither be seen nor cast into view.
inline constexpr f32 kShadowRadius = kShadowExtent * 1.7320508f;

// The sun's view-projection, in the snapshot's camera-relative space.
//
// `origin` is the camera's world position -- the same value `RenderCamera::origin`
// carries, and what every coordinate in the snapshot was measured from. It is
// here for one reason: **without it the texel grid crawls.** A world point's
// light-space coordinate is `L * (world - origin)`, so as the camera moves every
// point slides continuously across the map's texels, and `sampleSunShadow`
// resolves each tap with a binary compare -- a surface sitting near the bias
// threshold therefore flips between lit and shadowed from frame to frame. At the
// M4 example's orbit speed the grid slides 0.42 of a texel per frame, which
// reads as edges that crawl.
//
// So the grid is moved in whole texels only: where the camera lands in light
// space is rounded to the nearest texel and the view is translated by the
// remainder. The mapping from a world point to a texel then changes only when
// the camera crosses a boundary.
//
// **This fixes translation and not rotation.** A sun that turns rotates its own
// grid, and no snapping helps with that; it needs a normal-offset bias and a
// hardware comparison sampler, it is visible only while `ClockTime` is moving,
// and M4.5 leaves it out deliberately.
[[nodiscard]] core::Mat4 sunViewProjection(core::Vec3 sunDirection, core::DVec3 origin) noexcept;

} // namespace luaug::render
