#include "luaug/render/transform_history.h"

#include "luaug/scene/components.h"
#include "luaug/scene/world.h"

namespace luaug::render {

void TransformHistory::capture(const scene::World& world)
{
    ++stamp_;

    const auto record = [&](core::InstanceId id, const core::CFrameD& cframe) {
        if (entries_.size() <= id.index)
            entries_.resize(static_cast<std::size_t>(id.index) + 1);
        entries_[id.index] = Entry{.generation = id.generation, .stamp = stamp_, .cframe = cframe};
    };

    world.parts().forEach([&](core::InstanceId id, const scene::PartComponent& part) { record(id, part.cframe); });
    world.cameras().forEach(
        [&](core::InstanceId id, const scene::CameraComponent& camera) { record(id, camera.cframe); });
}

const core::CFrameD* TransformHistory::previous(core::InstanceId id) const noexcept
{
    if (!id.valid() || id.index >= entries_.size())
        return nullptr;
    const Entry& entry = entries_[id.index];
    if (entry.generation != id.generation || entry.stamp != stamp_)
        return nullptr;
    return &entry.cframe;
}

void TransformHistory::clear() noexcept
{
    entries_.clear();
    stamp_ = 0;
}

} // namespace luaug::render
