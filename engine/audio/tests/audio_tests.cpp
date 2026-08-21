#include "luaug/audio/audio.h"
#include "luaug/audio/scene_types.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <cmath>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
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

// ---------------------------------------------------------------------------
// `Sound.Content` (roadmap M7: it stops being `Inert`).

namespace {

// A WAV written by the test rather than checked into the repository.
//
// Deliberately: a fixture asset is a binary in git that nobody can diff and that
// ADR 0032 exists to keep out, and a tone is forty lines of arithmetic. It also
// means the test states its own expectations -- the length below is not a fact
// about a file somebody has to go and open.
void writeTone(const std::filesystem::path& path, luaug::core::u32 rate, luaug::core::u32 frames)
{
    std::vector<char> bytes;
    const auto put = [&bytes](const void* data, std::size_t size) {
        const auto* const at = static_cast<const char*>(data);
        bytes.insert(bytes.end(), at, at + size);
    };
    const auto putU32 = [&put](luaug::core::u32 value) { put(&value, sizeof(value)); };
    const auto putU16 = [&put](luaug::core::u16 value) { put(&value, sizeof(value)); };

    const luaug::core::u32 dataBytes = frames * 2u;
    put("RIFF", 4);
    putU32(36u + dataBytes);
    put("WAVE", 4);
    put("fmt ", 4);
    putU32(16u);
    putU16(1u); // PCM
    putU16(1u); // mono
    putU32(rate);
    putU32(rate * 2u);
    putU16(2u);
    putU16(16u);
    put("data", 4);
    putU32(dataBytes);
    for (luaug::core::u32 frame = 0; frame < frames; ++frame) {
        const double phase = 2.0 * 3.14159265358979 * 440.0 * frame / static_cast<double>(rate);
        const auto sample = static_cast<luaug::core::i16>(20000.0 * std::sin(phase));
        putU16(static_cast<luaug::core::u16>(sample));
    }

    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// A content directory with one sound in it, removed on the way out.
struct ContentFixture
{
    std::filesystem::path root;
    luaug::asset::ContentMounts mounts;

    ContentFixture()
    {
        std::error_code ec;
        root = std::filesystem::temp_directory_path(ec) / "luaug-audio-content";
        std::filesystem::remove_all(root, ec);
        writeTone(root / "sfx" / "tone.wav", 44100u, 11025u);
        mounts.mountDirectory(root);
    }

    ~ContentFixture()
    {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

} // namespace

TEST_CASE("a sound whose content resolves plays the file rather than the tone")
{
    Fixture fixture;
    ContentFixture content;
    fixture.system.setContentMounts(&content.mounts);

    const InstanceId id = fixture.make("Sound");
    scene::SoundComponent& sound = fixture.sound(id);
    sound.content = "asset://sfx/tone.wav";
    sound.playing = true;
    fixture.system.tick(*fixture.world, Tick);
    fixture.system.update(*fixture.world, InstanceId{});

    // Decoded exactly once, however many frames run: the cache is what stops a
    // sound re-decoding its file sixty times a second.
    CHECK(fixture.system.stats().clipsLoaded == 1);
    CHECK(fixture.system.stats().clipsMissing == 0);

    fixture.system.tick(*fixture.world, Tick);
    fixture.system.update(*fixture.world, InstanceId{});
    CHECK(fixture.system.stats().clipsLoaded == 1);
}

TEST_CASE("a sound whose content names nothing still plays, as the placeholder")
{
    // The decision M6 made and M7 keeps: a sound that went silent because a file
    // was missing is a bug report about the sound. It is counted rather than
    // hidden, which is what makes "did my audio load" answerable.
    Fixture fixture;
    ContentFixture content;
    fixture.system.setContentMounts(&content.mounts);

    const InstanceId id = fixture.make("Sound");
    scene::SoundComponent& sound = fixture.sound(id);
    sound.content = "asset://sfx/absent.wav";
    sound.playing = true;
    fixture.system.tick(*fixture.world, Tick);
    fixture.system.update(*fixture.world, InstanceId{});

    CHECK(fixture.system.stats().clipsLoaded == 0);
    CHECK(fixture.system.stats().clipsMissing == 1);
}

TEST_CASE("swapping the mounts drops what was decoded against the old ones")
{
    // A voice holds a raw pointer into a clip for the length of an audio
    // callback, so the cache can never drop one underneath it -- which makes
    // replacing the mounts the only moment a clip may go away, and it has to
    // take the voices with it.
    Fixture fixture;
    ContentFixture content;
    fixture.system.setContentMounts(&content.mounts);

    const InstanceId id = fixture.make("Sound");
    scene::SoundComponent& sound = fixture.sound(id);
    sound.content = "asset://sfx/tone.wav";
    sound.playing = true;
    fixture.system.tick(*fixture.world, Tick);
    fixture.system.update(*fixture.world, InstanceId{});
    REQUIRE(fixture.system.stats().clipsLoaded == 1);

    fixture.system.setContentMounts(nullptr);
    CHECK(fixture.system.stats().clipsLoaded == 0);
}
