#include "luaug/render/render_world.h"

#include "luaug/scene/world.h"

namespace luaug::render
{
namespace
{

// Walks up rather than down. A downward walk from the root would visit every
// instance in the world to find the parts; this visits only the parts, and pays
// the depth of each. A world where that is the wrong trade is a world with deep
// trees and few parts, which is not the shape any of this is built for.
[[nodiscard]] bool inWorld(const scene::World& world, core::InstanceId id, core::InstanceId root) noexcept
{
    for (core::InstanceId cursor = id; cursor.valid(); cursor = world.parentOf(cursor))
    {
        if (cursor == root)
            return true;
    }
    return false;
}

} // namespace

void extract(const scene::World& world, core::InstanceId root, RenderWorld& out)
{
    out.clear();
    if (!root.valid())
        return;

    world.parts().forEach(
        [&](core::InstanceId id, const scene::PartComponent& part)
        {
            if (!inWorld(world, id, root))
                return;

            out.parts.push_back(RenderPart{
                .cframe = part.cframe,
                .size = part.size,
                .color = part.color,
                .transparency = part.transparency,
                .shape = part.shape,
            });
        });
}

} // namespace luaug::render
