// Where a thing was one tick ago, so a frame can be drawn between two ticks
// (architecture.md §3, D047).
//
// **The simulation is a fixed 60 Hz and a display is not.** This machine renders
// about a hundred frames for every sixty ticks, so without interpolation two
// frames out of three show the world exactly where the previous one did while
// the third jumps a whole tick forward. A human walking `examples/10-open-world`
// described that as the world vibrating, and they were describing it precisely.
//
// `FrameScheduler` has computed the factor for this since M1 -- `Frame::alpha`,
// "where rendering sits between the last tick and the next" -- and until M8
// nothing read it. This is the consumer.
//
// **It lives in `render` and not in `scene`, deliberately.** A previous
// transform is not world state: it is not simulated, it must not enter the world
// hash, and a replay must not carry it. Keeping it here means `scene`'s
// components are untouched, `World::snapshot` still memcpys the same bytes, and
// every recorded determinism trace stays valid.
#pragma once

#include "luaug/core/id.h"
#include "luaug/core/math.h"
#include "luaug/core/types.h"

#include <vector>

namespace luaug::scene {
class World;
}

namespace luaug::render {

class TransformHistory
{
public:
    // Records where every part and camera is RIGHT NOW. Called at the start of
    // each tick, so that once the tick has run this holds the state the frame is
    // interpolating FROM and the world holds the state it is interpolating TO.
    //
    // A frame that runs no ticks leaves both alone, which is exactly right: the
    // interval has not changed, only where in it the frame sits.
    void capture(const scene::World& world);

    // Where this instance was at the last capture, or null when it was not
    // there -- something that streamed in, or was created, has no previous and
    // must be drawn where it is rather than smeared in from nowhere.
    [[nodiscard]] const core::CFrameD* previous(core::InstanceId id) const noexcept;

    void clear() noexcept;

private:
    struct Entry
    {
        // Both are needed and for different reasons: the generation catches a
        // slot reused by a different instance, and the stamp catches an
        // instance that existed at some earlier capture but not the last one.
        core::u32 generation = 0;
        core::u64 stamp = 0;
        core::CFrameD cframe;
    };

    // Indexed by `InstanceId::index`, which is what makes the lookup a bounds
    // check rather than a hash -- and an unordered container here would be a
    // determinism smell even though nothing iterates it (R10).
    std::vector<Entry> entries_;
    core::u64 stamp_ = 0;
};

} // namespace luaug::render
