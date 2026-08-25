#include "luaug/scene/pivot.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace luaug::scene {

using core::f64;

core::CFrameD pivotOffsetOf(const World& world, core::InstanceId id) noexcept
{
    const PVComponent* pv = world.pvInstances().find(id);
    return pv == nullptr ? core::CFrameD{} : pv->pivotOffset;
}

bool worldExtents(const World& world, core::InstanceId id, core::DVec3& minimum, core::DVec3& maximum)
{
    std::vector<core::InstanceId> descendants;
    world.collectDescendants(id, descendants);

    bool any = false;
    for (const core::InstanceId descendant : descendants) {
        const PartComponent* part = world.parts().find(descendant);
        if (part == nullptr)
            continue;

        // The standard OBB-to-AABB bound. `Mat3` is column-major -- `m[c][r]` --
        // so the inner index is the column and the outer is the world axis.
        const core::Mat3& r = part->cframe.rotation;
        const f64 half[3] = {
            static_cast<f64>(part->size.x) * 0.5,
            static_cast<f64>(part->size.y) * 0.5,
            static_cast<f64>(part->size.z) * 0.5,
        };
        f64 extents[3] = {0.0, 0.0, 0.0};
        for (int axis = 0; axis < 3; ++axis) {
            for (int local = 0; local < 3; ++local)
                extents[axis] += std::fabs(static_cast<f64>(r.m[local][axis])) * half[local];
        }

        const core::DVec3 centre = part->cframe.position;
        const core::DVec3 low{centre.x - extents[0], centre.y - extents[1], centre.z - extents[2]};
        const core::DVec3 high{centre.x + extents[0], centre.y + extents[1], centre.z + extents[2]};
        if (!any) {
            minimum = low;
            maximum = high;
            any = true;
            continue;
        }
        minimum = core::DVec3{std::min(minimum.x, low.x), std::min(minimum.y, low.y), std::min(minimum.z, low.z)};
        maximum = core::DVec3{std::max(maximum.x, high.x), std::max(maximum.y, high.y), std::max(maximum.z, high.z)};
    }
    return any;
}

core::CFrameD pivotBase(const World& world, core::InstanceId id)
{
    if (const ModelComponent* model = world.models().find(id); model != nullptr) {
        if (world.alive(model->primaryPart)) {
            if (const PartComponent* part = world.parts().find(model->primaryPart)) {
                // The primary part's OWN pivot, offset included: a model whose
                // primary part hinges about its edge hinges about that edge too,
                // which is the property that makes assigning a primary part mean
                // something beyond "pick a position".
                return part->cframe * pivotOffsetOf(world, model->primaryPart);
            }
        }

        core::DVec3 minimum;
        core::DVec3 maximum;
        core::CFrameD pivot;
        // An identity fallback would move a model built far from the origin by
        // its whole distance the first time anything pivoted it.
        if (worldExtents(world, id, minimum, maximum)) {
            pivot.position = core::DVec3{(minimum.x + maximum.x) * 0.5, (minimum.y + maximum.y) * 0.5,
                                         (minimum.z + maximum.z) * 0.5};
        }
        return pivot;
    }

    if (const PartComponent* part = world.parts().find(id); part != nullptr)
        return part->cframe;
    if (const CameraComponent* camera = world.cameras().find(id); camera != nullptr)
        return camera->cframe;
    return {};
}

core::CFrameD pivotOf(const World& world, core::InstanceId id)
{
    return pivotBase(world, id) * pivotOffsetOf(world, id);
}

} // namespace luaug::scene
