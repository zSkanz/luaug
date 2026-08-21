#include "luaug/audio/audio.h"
#include "luaug/audio/scene_types.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <doctest/doctest.h>
#include <optional>
#include <string>
#include <vector>

#include "class_descriptors.gen.h"

namespace {

namespace audio = luaug::audio;
namespace core = luaug::core;
namespace scene = luaug::scene;

using core::InstanceId;

// One tick at the default rate. Named, because every duration below is a
// multiple of it and a literal 1/60 in eight places is a number nobody can
// change.
constexpr double Tick = 1.0 / 60.0;

struct Fixture
{
    core::AtomTable atoms;
    scene::ClassRegistry classes;
    scene::EnumRegistry enums;
    std::optional<scene::World> world;
    audio::AudioSystem system;

    Fixture()
    {
        scene::generated::registerClasses(classes, atoms);
        audio::registerSceneTypes(classes, atoms);
        scene::generated::registerEnums(enums, atoms);
        world.emplace(classes, enums, atoms, 1u);
        // Headless: the timeline is what these cases are about, and a test that
        // opened a device would be a test that behaved differently on a machine
        // with no sound card.
        REQUIRE_FALSE(system.start(true).has_value());
    }

    InstanceId make(const char* className)
    {
        const scene::ClassId id = classes.findId(atoms.intern(className));
        REQUIRE(id != scene::InvalidClass);
        return world->create(id);
    }

    [[nodiscard]] scene::SoundComponent& sound(InstanceId id)
    {
        scene::SoundComponent* component = world->sounds().find(id);
        REQUIRE(component != nullptr);
        return *component;
    }

    void run(int ticks)
    {
        for (int index = 0; index < ticks; ++index)
            system.tick(*world, Tick);
    }

    [[nodiscard]] std::vector<std::string> events()
    {
        std::vector<std::string> names;
        for (const scene::Change& change : world->changes().take()) {
            if (change.kind == scene::ChangeKind::InstanceEventNoArgs)
                names.emplace_back(atoms.text(change.name));
        }
        return names;
    }
};

} // namespace

TEST_CASE("the timeline is the simulation's, tick by tick")
{
    Fixture fixture;
    const InstanceId id = fixture.make("Sound");
    fixture.sound(id).playing = true;

    fixture.run(30);
    // Half a second of ticks is half a second of timeline, exactly -- no device
    // involved, no wall clock consulted. That is the whole of Decision 9.
    CHECK(fixture.sound(id).timePosition == doctest::Approx(0.5).epsilon(0.001));
}

TEST_CASE("PlaybackSpeed scales the timeline")
{
    Fixture fixture;
    const InstanceId id = fixture.make("Sound");
    fixture.sound(id).playing = true;
    fixture.sound(id).playbackSpeed = 2.0f;

    fixture.run(15);
    CHECK(fixture.sound(id).timePosition == doctest::Approx(0.5).epsilon(0.001));
}

TEST_CASE("Ended fires once, on the tick the timeline reaches the end")
{
    Fixture fixture;
    const InstanceId id = fixture.make("Sound");
    fixture.sound(id).playing = true;
    (void)fixture.events(); // `Loaded`, which fires on the first tick.

    fixture.run(30);
    const std::vector<std::string> early = fixture.events();
    CHECK(std::ranges::find(early, "Ended") == early.end());

    fixture.run(60);
    const std::vector<std::string> late = fixture.events();
    CHECK(std::ranges::count(late, "Ended") == 1);
    CHECK_FALSE(fixture.sound(id).playing);

    // And not again. It is a past-tense fact about reaching the end, and a
    // stopped sound does not keep reaching it.
    fixture.run(60);
    const std::vector<std::string> after = fixture.events();
    CHECK(std::ranges::find(after, "Ended") == after.end());
}

TEST_CASE("a looped sound wraps and never ends")
{
    Fixture fixture;
    const InstanceId id = fixture.make("Sound");
    fixture.sound(id).playing = true;
    fixture.sound(id).looped = true;
    (void)fixture.events();

    fixture.run(150);
    CHECK(fixture.sound(id).playing);
    const std::vector<std::string> seen = fixture.events();
    CHECK(std::ranges::find(seen, "Ended") == seen.end());

    // Wrapped rather than reset: 150 ticks is 2.5 seconds, so the position is
    // half a second in and not zero. A loop that reset would drift against
    // everything else in the scene by the fraction of a tick it overshot by.
    CHECK(fixture.sound(id).timePosition == doctest::Approx(0.5).epsilon(0.01));
}

TEST_CASE("Loaded fires once per content")
{
    Fixture fixture;
    (void)fixture.make("Sound");

    fixture.run(1);
    const std::vector<std::string> first = fixture.events();
    CHECK(std::ranges::count(first, "Loaded") == 1);

    // Bound to a local rather than compared in place: `events()` drains, so two
    // calls in one expression compare an iterator from one drain against the
    // end of a different, empty one.
    fixture.run(5);
    const std::vector<std::string> rest = fixture.events();
    CHECK(std::ranges::find(rest, "Loaded") == rest.end());
}

TEST_CASE("a headless system reports no device and counts no underruns")
{
    Fixture fixture;
    const audio::AudioStats stats = fixture.system.stats();
    // The gate's number, on the tier that has no sound card -- which is every CI
    // runner. Zero here is a real zero: no callback ran, so none could starve.
    CHECK_FALSE(stats.deviceOpen);
    CHECK(stats.underruns == 0);
    CHECK(stats.droppedCommands == 0);
}
