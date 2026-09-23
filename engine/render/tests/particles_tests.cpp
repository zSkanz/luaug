// Particles (F2): what an emitter makes, and when -- on the frame, from a seed.
#include "luaug/render/particles.h"
#include "luaug/render/render_world.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/components.h"
#include "luaug/scene/enum_registry.h"
#include "luaug/scene/world.h"

#include <doctest/doctest.h>

using namespace luaug;
using namespace luaug::render;

namespace {

struct ParticleFixture
{
    core::AtomTable atoms;
    scene::ClassRegistry classes;
    scene::EnumRegistry enums;
    scene::ClassId folderClass = classes.registerClass({.name = atoms.intern("Folder")});
    scene::ClassId partClass = classes.registerClass({.name = atoms.intern("Part")});
    scene::ClassId emitterClass = classes.registerClass({.name = atoms.intern("ParticleEmitter")});
    scene::World world{classes, enums, atoms, 1234u};
    core::InstanceId root;
    core::InstanceId part;
    core::InstanceId emitter;
    ParticleSystem system;

    ParticleFixture()
    {
        root = world.create(folderClass);
        part = world.create(partClass);
        world.parts().add(part, scene::PartComponent{});
        world.parts().find(part)->cframe.position = core::DVec3{0.0, 10.0, 0.0};
        world.parts().find(part)->size = core::Vec3{0.0f, 0.0f, 0.0f};
        REQUIRE_FALSE(world.setParent(part, root).has_value());
        emitter = world.create(emitterClass);
        world.particleEmitters().add(emitter, scene::ParticleEmitterComponent{});
        REQUIRE_FALSE(world.setParent(emitter, part).has_value());
    }

    scene::ParticleEmitterComponent& config() { return *world.particleEmitters().find(emitter); }

    void run(int frames, double dt = 1.0 / 60.0)
    {
        for (int at = 0; at < frames; ++at)
            system.update(world, root, dt);
    }
};

} // namespace

TEST_CASE("a stream is born at its rate, whatever the frame rate")
{
    ParticleFixture fixture;
    fixture.config().rate = 30.0f;
    fixture.config().lifetime = 100.0f;
    fixture.run(60);
    CHECK(fixture.system.liveCount() == 30);

    // The same second cut into twice as many frames: the same count, because
    // the fraction a frame owes is carried rather than dropped.
    ParticleFixture fine;
    fine.config().rate = 30.0f;
    fine.config().lifetime = 100.0f;
    fine.run(120, 1.0 / 120.0);
    CHECK(fine.system.liveCount() == 30);
}

TEST_CASE("particles die at the end of their lives, and a disabled emitter only bursts")
{
    ParticleFixture fixture;
    fixture.config().rate = 60.0f;
    fixture.config().lifetime = 0.5f;
    fixture.run(60);
    // Half a second of stream, give or take the tenth of variation each life has.
    CHECK(fixture.system.liveCount() >= 26);
    CHECK(fixture.system.liveCount() <= 34);

    fixture.config().enabled = false;
    fixture.run(60);
    CHECK(fixture.system.liveCount() == 0);

    fixture.config().emitted += 25;
    fixture.run(1);
    CHECK(fixture.system.liveCount() == 25);
    // Seen once is spawned once.
    fixture.run(1);
    CHECK(fixture.system.liveCount() == 25);
}

TEST_CASE("they fly along the emitter's up, fall with acceleration, and are drawn back to front")
{
    ParticleFixture fixture;
    fixture.config().enabled = false;
    fixture.config().speed = 10.0f;
    fixture.config().spreadAngle = 0.0f;
    fixture.config().lifetime = 5.0f;
    fixture.config().emitted = 3;
    fixture.run(1);
    fixture.run(30);

    RenderWorld drawn;
    drawn.camera.origin = core::DVec3{0.0, 0.0, 0.0};
    fixture.system.append(drawn);
    REQUIRE(drawn.particles.size() == 3);
    // Straight up from the part, a jet with no spread: above where they began.
    for (const RenderParticle& particle : drawn.particles) {
        CHECK(static_cast<double>(particle.position.y) > 10.0);
        CHECK(static_cast<double>(particle.position.x) == doctest::Approx(0.0).epsilon(1e-3));
    }
    for (std::size_t at = 1; at < drawn.particles.size(); ++at)
        CHECK(drawn.particles[at - 1].distance >= drawn.particles[at].distance);

    // Gravity turns them round.
    fixture.config().acceleration = core::Vec3{0.0f, -40.0f, 0.0f};
    fixture.run(60);
    RenderWorld later;
    fixture.system.append(later);
    REQUIRE(later.particles.size() == 3);
    CHECK(static_cast<double>(later.particles[0].position.y) < static_cast<double>(drawn.particles[0].position.y));
}

TEST_CASE("the same emitter, the same frames: the same particles")
{
    ParticleFixture first;
    ParticleFixture second;
    for (ParticleFixture* fixture : {&first, &second}) {
        fixture->config().spreadAngle = 90.0f;
        fixture->config().rate = 50.0f;
        fixture->run(45);
    }
    RenderWorld a;
    RenderWorld b;
    first.system.append(a);
    second.system.append(b);
    REQUIRE(a.particles.size() == b.particles.size());
    REQUIRE_FALSE(a.particles.empty());
    for (std::size_t at = 0; at < a.particles.size(); ++at) {
        CHECK(a.particles[at].position.x == b.particles[at].position.x);
        CHECK(a.particles[at].position.z == b.particles[at].position.z);
    }
}

TEST_CASE("an emitter that leaves the world takes its particles, and one under nothing makes none")
{
    ParticleFixture fixture;
    fixture.config().lifetime = 100.0f;
    fixture.run(30);
    REQUIRE(fixture.system.liveCount() > 0);
    REQUIRE_FALSE(fixture.world.setParent(fixture.emitter, core::InstanceId{}).has_value());
    fixture.run(1);
    CHECK(fixture.system.liveCount() == 0);
    CHECK(fixture.system.emitterCount() == 0);
}
