#include "luaug/scene/sprite_animation.h"

#include "luaug/scene/world.h"

#include <cmath>
#include <vector>

namespace luaug::scene {

void stepSpriteAnimators(World& world, f64 fixedDt)
{
    if (world.spriteAnimators().size() == 0)
        return;

    // Collected first and written after: a write goes through `setProperty`,
    // and the pool is not walked while the world is being written to.
    struct Write
    {
        core::InstanceId animator;
        core::InstanceId sprite;
        core::Vec2 offset;
        core::Vec2 size;
        bool ended = false;
    };
    std::vector<Write> writes;
    world.spriteAnimators().forEach([&](core::InstanceId id, SpriteAnimatorComponent& animator) {
        if (!animator.playing)
            return;
        const core::InstanceId sprite = world.parentOf(id);
        if (!sprite.valid() || world.parts2d().find(sprite) == nullptr)
            return;

        const i32 count = animator.frameCount > 0 ? animator.frameCount : 1;
        if (animator.frame >= count)
            animator.frame = count - 1;
        animator.phase += fixedDt * static_cast<f64>(animator.framesPerSecond);
        bool ended = false;
        if (animator.phase >= 1.0) {
            const f64 turns = std::floor(animator.phase);
            animator.phase -= turns;
            // In f64 and reduced before it is narrowed: a slow tick on a fast
            // animation turns many pages, never more than an i32 holds.
            const f64 reached = static_cast<f64>(animator.frame) + turns;
            if (animator.looped) {
                animator.frame = static_cast<i32>(std::fmod(reached, static_cast<f64>(count)));
            }
            else if (reached >= static_cast<f64>(count)) {
                animator.frame = count - 1;
                animator.phase = 0.0;
                animator.finished = true;
                ended = true;
            }
            else {
                animator.frame = static_cast<i32>(reached);
            }
        }

        const i32 columns = animator.columns > 0 ? animator.columns : 1;
        const i32 index = animator.firstFrame + animator.frame;
        const core::Vec2 offset{animator.sheetOffset.x + static_cast<f32>(index % columns) * animator.frameSize.x,
                                animator.sheetOffset.y + static_cast<f32>(index / columns) * animator.frameSize.y};
        writes.push_back(Write{id, sprite, offset, animator.frameSize, ended});
    });

    const core::NameAtom offsetName = world.atoms().intern("ImageRectOffset");
    const core::NameAtom sizeName = world.atoms().intern("ImageRectSize");
    const core::NameAtom playingName = world.atoms().intern("Playing");
    for (const Write& write : writes) {
        // `setProperty` answers `Unchanged` for a rectangle already there, and
        // enqueues nothing for it.
        (void)world.setProperty(write.sprite, offsetName, Value{write.offset});
        (void)world.setProperty(write.sprite, sizeName, Value{write.size});
        // A non-looped run's end is its `Playing` going false, which a script
        // hears as that property changing (ADR 0102).
        if (write.ended)
            (void)world.setProperty(write.animator, playingName, Value{false});
    }
}

} // namespace luaug::scene
