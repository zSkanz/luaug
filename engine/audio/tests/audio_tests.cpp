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
        // **Longer than the placeholder tone**, which is the whole point of it:
        // every duration case below is a comparison against one second, and a
        // fixture that was only ever shorter than that could not tell a sound
        // measured by its file from one measured by the constant.
        writeTone(root / "sfx" / "long.wav", 44100u, 44100u * 3u);
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

TEST_CASE("a decoded sound is as long as its file, not as long as the placeholder")
{
    // D092. `tick` measured every sound in the world against a one-second
    // constant, which was correct for the whole of M6 -- nothing decoded a file,
    // so every sound WAS a one-second tone -- and was never revisited when M7
    // made `Content` real.
    Fixture fixture;
    ContentFixture content;
    fixture.system.setContentMounts(&content.mounts);

    // Both directions, because a length that is merely "not one" could still be
    // any wrong number: a quarter of a second is shorter than the placeholder
    // and three seconds is longer, and each is its own file's own length.
    CHECK(fixture.system.clipDuration("asset://sfx/tone.wav") == doctest::Approx(0.25).epsilon(0.01));
    CHECK(fixture.system.clipDuration("asset://sfx/long.wav") == doctest::Approx(3.0).epsilon(0.01));

    // And a content that names nothing is still one second, because one second
    // is how long the tone such a sound plays lasts.
    CHECK(fixture.system.clipDuration("asset://sfx/absent.wav") == doctest::Approx(1.0).epsilon(0.001));
    CHECK(fixture.system.clipDuration("") == doctest::Approx(1.0).epsilon(0.001));
}

TEST_CASE("Ended fires at the end of the FILE")
{
    Fixture fixture;
    ContentFixture content;
    fixture.system.setContentMounts(&content.mounts);

    const InstanceId id = fixture.make("Sound");
    scene::SoundComponent& sound = fixture.sound(id);
    sound.content = "asset://sfx/long.wav";
    sound.playing = true;
    (void)fixture.events(); // `Loaded`, which fires on the first tick.

    // **The second the defect stopped at.** A two-minute track that plays for
    // one second is what was reported, and this is the same thing at three.
    fixture.run(60);
    CHECK(fixture.sound(id).playing);
    const std::vector<std::string> atOne = fixture.events();
    CHECK(std::ranges::find(atOne, "Ended") == atOne.end());

    fixture.run(60);
    CHECK(fixture.sound(id).playing);

    // Past three seconds, with a tick of slack for the frame or two a resampler
    // moves the boundary by.
    fixture.run(65);
    CHECK_FALSE(fixture.sound(id).playing);
    const std::vector<std::string> atEnd = fixture.events();
    CHECK(std::ranges::count(atEnd, "Ended") == 1);
    CHECK(fixture.sound(id).timePosition == doctest::Approx(3.0).epsilon(0.01));
}

TEST_CASE("a looped sound wraps at the file's length")
{
    Fixture fixture;
    ContentFixture content;
    fixture.system.setContentMounts(&content.mounts);

    const InstanceId id = fixture.make("Sound");
    scene::SoundComponent& sound = fixture.sound(id);
    sound.content = "asset://sfx/long.wav";
    sound.playing = true;
    sound.looped = true;
    (void)fixture.events();

    // Three and a half seconds of a three-second file is half a second in --
    // wrapped by the FILE's length.
    fixture.run(210);
    CHECK(fixture.sound(id).playing);
    CHECK(fixture.sound(id).timePosition == doctest::Approx(0.5).epsilon(0.02));

    // And the quarter-second file is what makes the case discriminating: 0.7 s
    // of it is 0.2 s in, which is a number neither the old constant nor the
    // other file could produce.
    fixture.sound(id).content = "asset://sfx/tone.wav";
    fixture.sound(id).timePosition = 0.0;
    fixture.run(42);
    CHECK(fixture.sound(id).timePosition == doctest::Approx(0.2).epsilon(0.05));
}

TEST_CASE("the mixer keeps its cursor unless the timeline has really moved")
{
    // D093, as the rule rather than as the plumbing. The two clocks are the same
    // clock in a healthy run and are quantised differently -- the simulation
    // steps in whole ticks, the device in whole buffers -- so on any given frame
    // one is ahead of the other by less than either quantum.
    constexpr double tolerance = 0.25;

    // A tick and a buffer apart is the ordinary case, and taking the timeline
    // here is what replayed sixteen milliseconds of audio sixty times a second.
    CHECK_FALSE(audio::detail::shouldTakeTimeline(1.000, 0.984, 3.0, false, tolerance));
    CHECK_FALSE(audio::detail::shouldTakeTimeline(0.984, 1.000, 3.0, false, tolerance));
    CHECK_FALSE(audio::detail::shouldTakeTimeline(1.0, 1.0, 3.0, false, tolerance));

    // A script seeking is not a quantisation, and the mixer follows it.
    CHECK(audio::detail::shouldTakeTimeline(1.0, 2.5, 3.0, false, tolerance));
    CHECK(audio::detail::shouldTakeTimeline(2.5, 0.0, 3.0, false, tolerance));

    // **A loop is compared the short way round.** The frame in which the mixer
    // has wrapped and the timeline has not is a whole clip apart by subtraction
    // and a millisecond apart in fact; calling it a seek would put a click at
    // the top of every loop.
    CHECK_FALSE(audio::detail::shouldTakeTimeline(0.001, 2.999, 3.0, true, tolerance));
    CHECK_FALSE(audio::detail::shouldTakeTimeline(2.999, 0.001, 3.0, true, tolerance));
    // And the same two numbers on a sound that does NOT loop are exactly the
    // seek they look like.
    CHECK(audio::detail::shouldTakeTimeline(0.001, 2.999, 3.0, false, tolerance));
    // A seek inside a looped sound is still a seek.
    CHECK(audio::detail::shouldTakeTimeline(0.1, 1.6, 3.0, true, tolerance));
}

TEST_CASE("an audition plays a file without a Sound and without the world")
{
    // The editor's preview. It is deliberately not a `Sound` and deliberately
    // not a voice: the one thing it has that neither of those may have is a
    // cursor the wall clock drives.
    Fixture fixture;
    ContentFixture content;
    fixture.system.setContentMounts(&content.mounts);

    CHECK_FALSE(fixture.system.auditioning());

    fixture.system.audition("asset://sfx/long.wav", 0.5f, 1.0f);
    CHECK(fixture.system.auditioning());
    CHECK(fixture.system.auditioning("asset://sfx/long.wav"));
    // Which is what a play button on one row asks so that the OTHER rows do not
    // draw themselves as playing.
    CHECK_FALSE(fixture.system.auditioning("asset://sfx/tone.wav"));

    // Starting another replaces it. Two previews at once is not a thing anybody
    // asked for.
    fixture.system.audition("asset://sfx/tone.wav", 0.5f, 1.0f);
    CHECK(fixture.system.auditioning("asset://sfx/tone.wav"));
    CHECK_FALSE(fixture.system.auditioning("asset://sfx/long.wav"));

    fixture.system.stopAudition();
    CHECK_FALSE(fixture.system.auditioning());

    // **A file that will not decode is not auditioned.** A button that latched
    // on silence would say the opposite of what happened; the warning the
    // decoder already logged is the honest answer.
    fixture.system.audition("asset://sfx/absent.wav", 0.5f, 1.0f);
    CHECK_FALSE(fixture.system.auditioning());

    // And it holds a clip pointer like every voice does, so replacing the
    // mounts has to take it with them.
    fixture.system.audition("asset://sfx/tone.wav", 0.5f, 1.0f);
    REQUIRE(fixture.system.auditioning());
    fixture.system.setContentMounts(nullptr);
    CHECK_FALSE(fixture.system.auditioning());
}

TEST_CASE("an audition does not touch the sound it was started from")
{
    Fixture fixture;
    ContentFixture content;
    fixture.system.setContentMounts(&content.mounts);

    const InstanceId id = fixture.make("Sound");
    scene::SoundComponent& sound = fixture.sound(id);
    sound.content = "asset://sfx/long.wav";
    sound.timePosition = 1.25;

    fixture.system.audition(sound.content, sound.volume, sound.playbackSpeed);
    fixture.run(30);

    // `Playing` is the game's state and `TimePosition` is the simulation's, and
    // a preview owns neither. The timeline did not advance because the sound is
    // not playing -- which is exactly the state an audition has to work in.
    CHECK(fixture.system.auditioning());
    CHECK_FALSE(fixture.sound(id).playing);
    CHECK(fixture.sound(id).timePosition == doctest::Approx(1.25));
    const std::vector<std::string> seen = fixture.events();
    CHECK(std::ranges::find(seen, "Ended") == seen.end());
}
