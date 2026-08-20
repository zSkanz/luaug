// The POD snapshot rendering reads (ADR 0027, architecture.md §4).
//
// Rendering never walks the ECS. It walks this, and the difference is the whole
// point: extraction happens once per frame at a known moment, so the renderer
// cannot observe a half-mutated world, cannot keep an `InstanceId` alive past
// its retirement, and can be handed to another thread the day one exists.
//
// It is a snapshot rather than a view for the same reason `scene`'s change queue
// carries POD facts: the two sides have different lifetimes and the seam is what
// keeps that from mattering.
#pragma once

#include <vector>

#include "luaug/core/id.h"
#include "luaug/core/math.h"
#include "luaug/core/types.h"

namespace luaug::scene
{
class World;
}

namespace luaug::render
{

using core::CFrameD;
using core::Color3;
using core::f32;
using core::usize;
using core::Vec3;

// One drawable part. Deliberately not an `InstanceId`: nothing downstream may
// resolve one, because by the time a frame is drawn the instance behind it may
// have been destroyed and retired.
struct RenderPart
{
    CFrameD cframe;
    Vec3 size{1.0f, 1.0f, 1.0f};
    Color3 color{1.0f, 1.0f, 1.0f};
    f32 transparency = 0.0f;
    // `Enum.PartShape`'s stored value. M4's renderer picks a mesh from it; the
    // debug path draws a wire box whatever it says, which is honest about what
    // M2 can show.
    core::i32 shape = 0;
};

struct RenderWorld
{
    std::vector<RenderPart> parts;

    void clear() noexcept { parts.clear(); }
};

// Fills `out` from the world's part storage, in the pool's dense order -- a pure
// function of the operation sequence, which is what R10 asks of anything that
// reaches observable output. Clears `out` first, so a caller reuses one buffer
// across frames and allocates once.
//
// `root` is `Workspace`: whatever is parented under it is in the world and
// whatever is not, is not (api-design.md §2.1). Passed in rather than looked up,
// because `render` has no business knowing what a service is.
void extract(const scene::World& world, core::InstanceId root, RenderWorld& out);

} // namespace luaug::render
