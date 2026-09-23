// Particles (F2): what a `ParticleEmitter` makes, simulated on the frame.
//
// **A picture, not the world.** A particle collides with nothing, no script can
// find one and nothing the simulation decides depends on where it went -- so it
// lives here, on the render clock, and a scene with ten thousand of them pays
// for them in one draw rather than in the world hash, the snapshot and the
// replication budget. What IS world state is the emitter: its configuration,
// and a running total of the bursts a script asked for, which this reads and
// never writes.
//
// **Seeded per emitter**, from its instance id, so two runs of one headless
// capture spawn the same particles in the same places -- the property a golden
// image needs, bought for the price of one generator per emitter.
#pragma once

#include "luaug/core/id.h"
#include "luaug/core/math.h"
#include "luaug/core/random.h"
#include "luaug/core/types.h"
#include "luaug/scene/components.h"

#include <vector>

namespace luaug::scene {
class World;
}

namespace luaug::render {

struct RenderWorld;

class ParticleSystem
{
public:
    // Particles one emitter may hold at once; past it, new ones are not born.
    static constexpr core::usize MaxPerEmitter = 4096;
    // Particles drawn in one frame, nearest first when there are more.
    static constexpr core::usize MaxDrawn = 32768;
    // The longest step one update takes. A frame that stalled for a second
    // would otherwise fire a second's worth of stream in one place.
    static constexpr core::f64 MaxStep = 0.1;
    // The most a burst spawns in one frame, whatever a script asked for.
    static constexpr core::u64 MaxBurstPerFrame = 1000;

    // Advances every emitter under `root` by `dt` seconds: births, motion and
    // deaths. An emitter that has gone takes its particles with it.
    void update(const scene::World& world, core::InstanceId root, core::f64 dt);

    // Appends this frame's particles to `out`, relative to its camera and
    // sorted back to front, which is the order blending needs.
    void append(RenderWorld& out) const;

    [[nodiscard]] core::usize liveCount() const noexcept;
    [[nodiscard]] core::usize emitterCount() const noexcept { return m_emitters.size(); }
    void clear() noexcept { m_emitters.clear(); }

private:
    struct Particle
    {
        core::DVec3 position;
        core::Vec3 velocity;
        core::f32 age = 0.0f;
        core::f32 lifetime = 1.0f;
    };

    struct Emitter
    {
        core::InstanceId id;
        // The configuration as of the last update, for drawing.
        scene::ParticleEmitterComponent config;
        std::vector<Particle> particles;
        // The fraction of a particle the stream owes from earlier frames.
        core::f64 carry = 0.0;
        // How much of `config.emitted` has been spawned.
        core::u64 spawnedBursts = 0;
        core::Pcg32 random{1};
        bool seen = false;
    };

    // Where an emitter's parent is, and the box particles are born in: a part's
    // volume, or a point for an attachment. False when it has neither.
    [[nodiscard]] static bool anchorOf(const scene::World& world, core::InstanceId id, core::CFrameD& frame,
                                       core::Vec3& extent) noexcept;
    static void spawn(Emitter& emitter, const core::CFrameD& frame, core::Vec3 extent, core::u64 count);

    // Sorted by instance id, so an update walks them in one order on every run.
    std::vector<Emitter> m_emitters;
};

} // namespace luaug::render
