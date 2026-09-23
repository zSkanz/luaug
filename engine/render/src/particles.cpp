#include "luaug/render/particles.h"

#include "luaug/render/render_world.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <cmath>

namespace luaug::render {
namespace {

using core::f32;
using core::f64;
using core::u64;
using core::usize;

constexpr f32 kPi = 3.14159265358979f;

[[nodiscard]] core::Vec3 lerp(core::Vec3 a, core::Vec3 b, f32 t) noexcept
{
    return core::Vec3{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

[[nodiscard]] f32 lerp(f32 a, f32 b, f32 t) noexcept
{
    return a + (b - a) * t;
}

// Where an emitter's generator starts: its instance, mixed so neighbouring ids
// do not start neighbouring sequences.
[[nodiscard]] u64 seedOf(core::InstanceId id) noexcept
{
    u64 seed = (static_cast<u64>(id.generation) << 32) ^ id.index;
    seed ^= seed >> 33;
    seed *= 0xFF51AFD7ED558CCDull;
    seed ^= seed >> 33;
    return seed;
}

} // namespace

bool ParticleSystem::anchorOf(const scene::World& world, core::InstanceId id, core::CFrameD& frame,
                              core::Vec3& extent) noexcept
{
    const core::InstanceId parent = world.parentOf(id);
    // An attachment is a point, and a direction -- the usual way to aim a jet.
    if (const scene::AttachmentComponent* attachment = world.attachments().find(parent); attachment != nullptr) {
        frame = attachment->worldCFrame;
        extent = core::Vec3{0.0f, 0.0f, 0.0f};
        return true;
    }
    // A part is a volume: particles are born anywhere inside it, which is what
    // makes a burning crate burn all over rather than from its centre.
    for (core::InstanceId cursor = parent; cursor.valid(); cursor = world.parentOf(cursor)) {
        if (const scene::PartComponent* part = world.parts().find(cursor); part != nullptr) {
            frame = part->cframe;
            extent = cursor == parent ? part->size : core::Vec3{0.0f, 0.0f, 0.0f};
            return true;
        }
    }
    return false;
}

void ParticleSystem::spawn(Emitter& emitter, const core::CFrameD& frame, core::Vec3 extent, u64 count)
{
    const scene::ParticleEmitterComponent& config = emitter.config;
    if (config.lifetime <= 0.0f)
        return;
    // The emitter's up, and two directions square to it, for the cone.
    const core::Vec3 up = core::normalize(frame.rotation * core::Vec3{0.0f, 1.0f, 0.0f});
    const core::Vec3 side = core::normalize(frame.rotation * core::Vec3{1.0f, 0.0f, 0.0f});
    const core::Vec3 back = core::cross(side, up);
    const f32 spread = std::clamp(config.spreadAngle, 0.0f, 180.0f) * (kPi / 180.0f);

    for (u64 born = 0; born < count && emitter.particles.size() < MaxPerEmitter; ++born) {
        Particle particle;
        // Uniform over the cap of the cone, not over its angle: an even spray
        // rather than one bunched along the axis.
        const f32 cosTheta = 1.0f - static_cast<f32>(emitter.random.nextDouble()) * (1.0f - std::cos(spread));
        const f32 sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
        const f32 phi = static_cast<f32>(emitter.random.nextDouble()) * 2.0f * kPi;
        const core::Vec3 direction{
            up.x * cosTheta + (side.x * std::cos(phi) + back.x * std::sin(phi)) * sinTheta,
            up.y * cosTheta + (side.y * std::cos(phi) + back.y * std::sin(phi)) * sinTheta,
            up.z * cosTheta + (side.z * std::cos(phi) + back.z * std::sin(phi)) * sinTheta,
        };
        particle.velocity = direction * config.speed;

        const core::Vec3 local{
            (static_cast<f32>(emitter.random.nextDouble()) - 0.5f) * extent.x,
            (static_cast<f32>(emitter.random.nextDouble()) - 0.5f) * extent.y,
            (static_cast<f32>(emitter.random.nextDouble()) - 0.5f) * extent.z,
        };
        const core::Vec3 offset = frame.rotation * local;
        particle.position =
            core::DVec3{frame.position.x + static_cast<f64>(offset.x), frame.position.y + static_cast<f64>(offset.y),
                        frame.position.z + static_cast<f64>(offset.z)};
        // A tenth either way, so a stream does not pulse with every particle
        // born in one frame dying in one frame.
        particle.lifetime = config.lifetime * (0.9f + 0.2f * static_cast<f32>(emitter.random.nextDouble()));
        emitter.particles.push_back(particle);
    }
}

void ParticleSystem::update(const scene::World& world, core::InstanceId root, f64 dt)
{
    const auto step = static_cast<f32>(std::clamp(dt, 0.0, MaxStep));
    for (Emitter& emitter : m_emitters)
        emitter.seen = false;

    world.particleEmitters().forEach([&](core::InstanceId id, const scene::ParticleEmitterComponent& config) {
        if (world.destroyed(id))
            return;
        // Only what is in the world being drawn: an emitter in a stamp being
        // edited elsewhere, or under nothing, has no place to be.
        bool inWorld = false;
        for (core::InstanceId cursor = world.parentOf(id); cursor.valid(); cursor = world.parentOf(cursor))
            inWorld = inWorld || cursor == root;
        if (!inWorld)
            return;

        auto at = std::lower_bound(m_emitters.begin(), m_emitters.end(), id, [](const Emitter& e, core::InstanceId v) {
            return e.id.index != v.index ? e.id.index < v.index : e.id.generation < v.generation;
        });
        if (at == m_emitters.end() || !(at->id == id)) {
            Emitter fresh;
            fresh.id = id;
            fresh.random = core::Pcg32{seedOf(id), 0x7061727469636C65ull};
            at = m_emitters.insert(at, std::move(fresh));
        }
        Emitter& emitter = *at;
        emitter.seen = true;
        emitter.config = config;

        // Age and move what is alive, and let the dead go.
        const f32 keep = std::max(0.0f, 1.0f - config.drag * step);
        for (Particle& particle : emitter.particles) {
            particle.velocity = (particle.velocity + config.acceleration * step) * keep;
            particle.position.x += static_cast<f64>(particle.velocity.x * step);
            particle.position.y += static_cast<f64>(particle.velocity.y * step);
            particle.position.z += static_cast<f64>(particle.velocity.z * step);
            particle.age += step;
        }
        std::erase_if(emitter.particles, [](const Particle& particle) { return particle.age >= particle.lifetime; });

        core::CFrameD frame;
        core::Vec3 extent;
        if (!anchorOf(world, id, frame, extent)) {
            emitter.carry = 0.0;
            emitter.spawnedBursts = config.emitted;
            return;
        }

        // Bursts first: what a script asked for since the last look.
        if (config.emitted > emitter.spawnedBursts) {
            const u64 owed = std::min<u64>(config.emitted - emitter.spawnedBursts, MaxBurstPerFrame);
            spawn(emitter, frame, extent, owed);
        }
        emitter.spawnedBursts = config.emitted;

        // Then the stream.
        if (config.enabled && config.rate > 0.0f) {
            emitter.carry += static_cast<f64>(config.rate) * static_cast<f64>(step);
            const auto whole = static_cast<u64>(emitter.carry);
            emitter.carry -= static_cast<f64>(whole);
            spawn(emitter, frame, extent, whole);
        }
    });

    std::erase_if(m_emitters, [](const Emitter& emitter) { return !emitter.seen; });
}

void ParticleSystem::append(RenderWorld& out) const
{
    const core::DVec3 origin = out.camera.origin;
    const usize first = out.particles.size();
    for (const Emitter& emitter : m_emitters) {
        const scene::ParticleEmitterComponent& config = emitter.config;
        for (const Particle& particle : emitter.particles) {
            const f32 t = std::clamp(particle.age / std::max(particle.lifetime, 1e-4f), 0.0f, 1.0f);
            const core::Vec3 color = lerp(core::Vec3{config.color.r, config.color.g, config.color.b},
                                          core::Vec3{config.colorEnd.r, config.colorEnd.g, config.colorEnd.b}, t);
            const f32 opacity = std::clamp(1.0f - lerp(config.transparency, config.transparencyEnd, t), 0.0f, 1.0f);
            if (opacity <= 0.0f)
                continue;
            RenderParticle drawn;
            drawn.position = core::toVec3(particle.position - origin);
            drawn.size = std::max(0.0f, lerp(config.size, config.sizeEnd, t));
            drawn.color[0] = color.x * config.brightness;
            drawn.color[1] = color.y * config.brightness;
            drawn.color[2] = color.z * config.brightness;
            drawn.color[3] = opacity;
            drawn.emission = std::clamp(config.lightEmission, 0.0f, 1.0f);
            drawn.shape = config.shape;
            drawn.distance = core::length(drawn.position);
            out.particles.push_back(drawn);
        }
    }

    // Back to front, which is what blending over one another needs; and when
    // there are more than can be drawn, the nearest are the ones kept.
    std::sort(out.particles.begin() + static_cast<std::ptrdiff_t>(first), out.particles.end(),
              [](const RenderParticle& a, const RenderParticle& b) { return a.distance > b.distance; });
    if (out.particles.size() - first > MaxDrawn)
        out.particles.erase(out.particles.begin() + static_cast<std::ptrdiff_t>(first),
                            out.particles.end() - static_cast<std::ptrdiff_t>(MaxDrawn));
}

usize ParticleSystem::liveCount() const noexcept
{
    usize count = 0;
    for (const Emitter& emitter : m_emitters)
        count += emitter.particles.size();
    return count;
}

} // namespace luaug::render
