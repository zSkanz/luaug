// Stable handles to things that live in a slot and can be reused
// (architecture.md §2, §4).
//
// The generation is what makes reuse safe: a slot freed and refilled hands out
// a handle that compares unequal to every handle that pointed at the old
// occupant, so a stale reference is *detectable* rather than silently aliasing
// whatever moved in. That detection is the whole reason use-after-Destroy is a
// keyed error in this engine instead of a mysterious read (api-design.md
// divergence #25).
#pragma once

#include "luaug/core/types.h"

namespace luaug::core
{

// The identity of an instance, engine-wide. 8 bytes, trivially copyable, and
// the payload of every `TAG_INSTANCE` userdata (architecture.md §5) -- which is
// why it must stay exactly this size and shape.
//
// Generation 0 is reserved for "no instance": a default-constructed InstanceId
// is null, and a live slot's generation is never 0. That is why `valid()` can
// be a single comparison and why zero-initialised memory reads as absent.
struct InstanceId
{
    u32 index = 0;
    u32 generation = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return generation != 0; }
    [[nodiscard]] constexpr bool operator==(const InstanceId&) const noexcept = default;
};

static_assert(sizeof(InstanceId) == 8, "InstanceId is the userdata payload; its size is an ABI decision");

} // namespace luaug::core
