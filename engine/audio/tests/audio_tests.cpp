#include "luaug/audio/audio.h"
#include "luaug/audio/scene_types.h"
#include "luaug/platform/async_io.h"
#include "luaug/platform/file.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <string>
#include <thread>
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
void writeTone(const std::filesystem::path& path, luaug::core::u32 rate, luaug::core::u32 frames,
               luaug::core::u32 firstFrame = 0)
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
        // `firstFrame` starts the tone part-way through, so a short file can
        // hold exactly the samples a long one has at some later point.
        const double phase =
            2.0 * 3.14159265358979 * 440.0 * static_cast<double>(frame + firstFrame) / static_cast<double>(rate);
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

// Runs frames until the prefetch pipeline is empty, or gives up.
//
// **Bounded, and it sleeps.** A test that spins forever on a defect reports as a
// hung machine rather than as a failure -- and the sleep is the point rather
// than a delay: the read is on the IO thread and the decode is on a worker, so
// a busy loop on this thread would starve the very work it is waiting for.
void settleAudio(Fixture& fixture, int frames = 2000)
{
    for (int frame = 0; frame < frames; ++frame) {
        fixture.system.update(*fixture.world, InstanceId{});
        const audio::AudioStats stats = fixture.system.stats();
        if (stats.clipsLoaded + stats.clipsMissing > 0)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // One more, so the pass that installs is not the pass the loop stopped on.
    fixture.system.update(*fixture.world, InstanceId{});
}

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

// --- Which side a sound comes out of -----------------------------------------
//
// Distance alone is half of positional: it says a sound is near without saying
// it is on your left, so turning around changed nothing at all. These cases are
// the other half, and they are written against the LISTENER's frame rather than
// against world axes, because "left" is a fact about where somebody is facing.

namespace {

// A listener at `at`, facing `towards`. Built through the engine's own
// `lookAtCFrame` rather than by writing a matrix here, so the case cannot pass
// against a convention this file invented -- the look axis is -Z and column 0
// is right, and a test that assumed otherwise would agree with itself forever.
[[nodiscard]] luaug::core::CFrameD listenerAt(luaug::core::DVec3 at, luaug::core::DVec3 towards)
{
    return luaug::core::lookAtCFrame(at, towards, luaug::core::Vec3{0.0f, 1.0f, 0.0f});
}

} // namespace

TEST_CASE("a source is panned by where it is across the listener")
{
    // Facing -Z, which is where `lookAtCFrame` puts a listener told to look at
    // something in front of it.
    const luaug::core::CFrameD ear = listenerAt({0.0, 0.0, 0.0}, {0.0, 0.0, -10.0});

    // Straight ahead and straight behind are both centre, and that is honest
    // rather than wrong: two channels cannot tell front from back without a
    // head model, and choosing a side for a sound that is on neither would be
    // an invention.
    CHECK(audio::detail::panOf(ear, {0.0, 0.0, -10.0}) == doctest::Approx(0.0).epsilon(0.001));
    CHECK(audio::detail::panOf(ear, {0.0, 0.0, 10.0}) == doctest::Approx(0.0).epsilon(0.001));

    // +X is the listener's right, because column 0 is the right vector.
    CHECK(audio::detail::panOf(ear, {10.0, 0.0, 0.0}) == doctest::Approx(1.0).epsilon(0.001));
    CHECK(audio::detail::panOf(ear, {-10.0, 0.0, 0.0}) == doctest::Approx(-1.0).epsilon(0.001));

    // **Distance does not change the side.** A source at the same angle pans
    // the same however far away it is -- the falloff is what distance is for,
    // and mixing the two would make a far sound drift towards the middle.
    CHECK(audio::detail::panOf(ear, {1.0, 0.0, 0.0}) == doctest::Approx(1.0).epsilon(0.001));
    CHECK(audio::detail::panOf(ear, {1000.0, 0.0, 0.0}) == doctest::Approx(1.0).epsilon(0.001));

    // Forty-five degrees to the right is the sine of forty-five degrees.
    CHECK(audio::detail::panOf(ear, {5.0, 0.0, -5.0}) == doctest::Approx(0.7071).epsilon(0.01));
}

TEST_CASE("turning around swaps the sides")
{
    // The case the reporter would run: stand still, look the other way, and the
    // sound has to move. Against a listener read as a POSITION rather than as a
    // frame -- which is what it was -- both of these answer zero.
    const luaug::core::CFrameD facingAway = listenerAt({0.0, 0.0, 0.0}, {0.0, 0.0, -10.0});
    const luaug::core::CFrameD facingBack = listenerAt({0.0, 0.0, 0.0}, {0.0, 0.0, 10.0});

    const luaug::core::DVec3 source{10.0, 0.0, 0.0};
    CHECK(audio::detail::panOf(facingAway, source) == doctest::Approx(1.0).epsilon(0.001));
    CHECK(audio::detail::panOf(facingBack, source) == doctest::Approx(-1.0).epsilon(0.001));
}

TEST_CASE("a source with no side is centred rather than guessed at")
{
    const luaug::core::CFrameD ear = listenerAt({0.0, 0.0, 0.0}, {0.0, 0.0, -10.0});

    // Directly overhead and directly below have no horizontal direction at all,
    // and a sound standing exactly on the listener is the one case where
    // hard-panning would be most wrong.
    CHECK(audio::detail::panOf(ear, {0.0, 10.0, 0.0}) == doctest::Approx(0.0).epsilon(0.001));
    CHECK(audio::detail::panOf(ear, {0.0, -10.0, 0.0}) == doctest::Approx(0.0).epsilon(0.001));
    CHECK(audio::detail::panOf(ear, {0.0, 0.0, 0.0}) == doctest::Approx(0.0).epsilon(0.001));

    // And elevation does not steal the side: a source up and to the right is
    // still fully to the right, because the pan is an azimuth.
    CHECK(audio::detail::panOf(ear, {10.0, 50.0, 0.0}) == doctest::Approx(1.0).epsilon(0.001));
}

TEST_CASE("the pan holds its power across the field")
{
    luaug::core::f32 left = 0.0f;
    luaug::core::f32 right = 0.0f;

    audio::detail::panGains(-1.0f, left, right);
    CHECK(left == doctest::Approx(1.0).epsilon(0.001));
    CHECK(right == doctest::Approx(0.0).epsilon(0.001));

    audio::detail::panGains(1.0f, left, right);
    CHECK(left == doctest::Approx(0.0).epsilon(0.001));
    CHECK(right == doctest::Approx(1.0).epsilon(0.001));

    audio::detail::panGains(0.0f, left, right);
    CHECK(left == doctest::Approx(0.7071).epsilon(0.001));
    CHECK(right == doctest::Approx(0.7071).epsilon(0.001));

    // **Constant POWER, which is the whole reason it is a quarter turn and not
    // a straight line.** Two channels at half amplitude are quieter than one at
    // full, so a linear pan dips in the middle and a sound crossing in front of
    // you audibly ducks as it passes. The sum of squares is one everywhere.
    for (const luaug::core::f32 pan : {-1.0f, -0.5f, 0.0f, 0.3f, 1.0f}) {
        audio::detail::panGains(pan, left, right);
        CHECK(left * left + right * right == doctest::Approx(1.0).epsilon(0.001));
    }

    // Out of range is clamped rather than wrapped: a pan of two is a caller's
    // arithmetic error, and cos of a bigger angle would swing it back towards
    // the middle and then to the wrong side entirely.
    audio::detail::panGains(4.0f, left, right);
    CHECK(right == doctest::Approx(1.0).epsilon(0.001));
    audio::detail::panGains(-4.0f, left, right);
    CHECK(left == doctest::Approx(1.0).epsilon(0.001));
}

// --- The prefetch (D129) -----------------------------------------------------
//
// **What this is for, and what it deliberately is not.** A sound's first play
// used to decode the whole file wherever it was first asked for, and for a long
// track that is tens of megabytes of f32 produced before the frame could end.
// The prefetch moves that work off the frame -- an asynchronous read and a
// decode job, both started from `update` and never from `tick`.
//
// The floor is what makes it legal. If the tick asks for a clip the prefetch has
// not landed, it decodes it itself, exactly as it always did. So the ANSWER is
// the same whether the prefetch won its race or not, and the world hash cannot
// learn anything about how fast the disk is (R10). The cases below assert both
// halves, because either one alone is either a stall or a determinism break.

TEST_CASE("a sound that is not playing still has its clip fetched")
{
    // The whole point: a sound authored in a scene is prefetched from the first
    // frame the scene is alive, so by the time anything plays it there is
    // nothing left to decode. `playing` is never set here.
    REQUIRE(luaug::platform::initIo());
    Fixture fixture;
    ContentFixture content;
    fixture.system.setContentMounts(&content.mounts);

    const InstanceId id = fixture.make("Sound");
    fixture.sound(id).content = "asset://sfx/tone.wav";

    CHECK(fixture.system.stats().clipsLoaded == 0);

    settleAudio(fixture);

    CHECK(fixture.system.stats().clipsLoaded == 1);
}

TEST_CASE("the tick answers the same whether the prefetch won or not")
{
    // **The determinism assertion, and it is the reason the synchronous path
    // stays.** One system is given every chance to prefetch; the other is given
    // none and goes straight to a tick. Both must report the same duration, or
    // the world hash depends on the disk.
    ContentFixture content;

    double withPrefetch = 0.0;
    {
        REQUIRE(luaug::platform::initIo());
        Fixture fixture;
        fixture.system.setContentMounts(&content.mounts);
        const InstanceId id = fixture.make("Sound");
        fixture.sound(id).content = "asset://sfx/long.wav";
        settleAudio(fixture);
        withPrefetch = fixture.system.clipDuration("asset://sfx/long.wav");
    }

    double without = 0.0;
    {
        Fixture fixture;
        fixture.system.setContentMounts(&content.mounts);
        // No `update` at all, so nothing was ever prefetched.
        without = fixture.system.clipDuration("asset://sfx/long.wav");
    }

    CHECK(withPrefetch > 2.9);
    CHECK(withPrefetch == doctest::Approx(without));
}

TEST_CASE("a URN that names nothing is fetched once and does not leak its slot")
{
    // D131's class of defect: a failed asynchronous read whose slot nobody
    // releases, and at five hundred and twelve every read in the process is
    // refused. Six hundred attempts is past that by a margin.
    REQUIRE(luaug::platform::initIo());
    Fixture fixture;
    ContentFixture content;
    fixture.system.setContentMounts(&content.mounts);

    const InstanceId id = fixture.make("Sound");
    for (int attempt = 0; attempt < 600; ++attempt) {
        fixture.sound(id).content = "asset://sfx/not-a-file.wav";
        fixture.system.update(*fixture.world, InstanceId{});
    }

    // And a real one still resolves afterwards, which is the assertion that
    // actually catches exhaustion: the counters would look fine either way.
    fixture.sound(id).content = "asset://sfx/tone.wav";
    settleAudio(fixture);
    CHECK(fixture.system.clipDuration("asset://sfx/tone.wav") > 0.0);
}

TEST_CASE("tearing down with a fetch in flight leaves nothing behind")
{
    // A decode job writes into memory the system owns, and returning while one
    // runs is a use-after-free that reproduces on a fast machine and never on a
    // slow one -- the lesson `MeshLoader` and `UiText` each paid for once.
    REQUIRE(luaug::platform::initIo());
    ContentFixture content;
    {
        Fixture fixture;
        fixture.system.setContentMounts(&content.mounts);
        const InstanceId id = fixture.make("Sound");
        fixture.sound(id).content = "asset://sfx/long.wav";
        // One pass only: the read is started and deliberately not waited for.
        fixture.system.update(*fixture.world, InstanceId{});
    }
    // Reaching here without a fault is the assertion.
    CHECK(true);
}

// --- The rest of D129: a header for the tick, and a stream for long files ------
//
// The prefetch left two things. A sound made and played on one tick still made
// the TICK decode its file, because the tick needs its length; and a long file
// was decoded whole and held, seventy megabytes of f32 for three minutes. The
// tick reads the file's header now, and a long file streams.

namespace {

// A content directory with a long file, and short ones holding exactly the
// samples the long one has at its start and at five seconds in.
struct StreamFixture
{
    std::filesystem::path root;
    luaug::asset::ContentMounts mounts;

    StreamFixture()
    {
        std::error_code ec;
        root = std::filesystem::temp_directory_path(ec) / "luaug-audio-stream";
        std::filesystem::remove_all(root, ec);
        // 48 kHz, so nothing is resampled and two decodes of the same samples
        // are the same numbers.
        writeTone(root / "music" / "song.wav", 48000u, 48000u * 12u);
        writeTone(root / "sfx" / "head.wav", 48000u, 48000u);
        writeTone(root / "sfx" / "five.wav", 48000u, 48000u, 48000u * 5u);
        mounts.mountDirectory(root);
    }

    ~StreamFixture()
    {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

// What one sound sounds like for `frames` frames from `at` seconds in.
std::vector<float> listen(const luaug::asset::ContentMounts& mounts, const char* content, double at, core::u32 frames,
                          bool looped = false)
{
    Fixture fixture;
    fixture.system.setContentMounts(&mounts);
    const InstanceId id = fixture.make("Sound");
    fixture.sound(id).content = content;
    fixture.sound(id).timePosition = at;
    fixture.sound(id).looped = looped;
    fixture.sound(id).playing = true;
    fixture.system.update(*fixture.world, InstanceId{});
    std::vector<float> out(static_cast<std::size_t>(frames) * 2u);
    fixture.system.renderInto(out);
    return out;
}

} // namespace

TEST_CASE("a sound made and played on one tick is timed from its header, and the tick decodes nothing")
{
    Fixture fixture;
    ContentFixture content;
    fixture.system.setContentMounts(&content.mounts);

    const InstanceId id = fixture.make("Sound");
    fixture.sound(id).content = "asset://sfx/long.wav";
    fixture.sound(id).playing = true;
    fixture.system.tick(*fixture.world, Tick);

    // The length came from the WAV header: nothing was decoded, by the tick or
    // by anything else.
    CHECK(fixture.system.stats().tickDecodes == 0);
    CHECK(fixture.system.stats().clipsLoaded == 0);
    CHECK(fixture.system.clipDuration("asset://sfx/long.wav") == doctest::Approx(3.0).epsilon(0.001));
}

TEST_CASE("a file's declared length is read from its header, Ogg Vorbis included")
{
    // A WAV, through the decoder's header.
    {
        StreamFixture content;
        std::vector<std::byte> bytes;
        REQUIRE(luaug::platform::readFile(content.root / "sfx" / "head.wav", bytes));
        const std::optional<core::u64> frames = audio::detail::probeFrames(bytes);
        REQUIRE(frames.has_value());
        CHECK(*frames == 48000u);
    }

    // An Ogg Vorbis stream, by hand: an identification header on the first
    // page saying 44.1 kHz, and a last page whose granule is ten seconds of
    // source samples. Nothing here decodes; the probe reads two numbers.
    std::vector<std::byte> ogg;
    const auto put = [&ogg](std::initializer_list<int> values) {
        for (const int value : values)
            ogg.push_back(static_cast<std::byte>(value));
    };
    const auto putLe = [&ogg](core::u64 value, int width) {
        for (int index = 0; index < width; ++index)
            ogg.push_back(static_cast<std::byte>((value >> (8 * index)) & 0xFFu));
    };
    const auto page = [&](core::u64 granule, std::initializer_list<int> segments) {
        put({'O', 'g', 'g', 'S', 0, 2});
        putLe(granule, 8);
        putLe(1, 4);
        putLe(0, 4);
        putLe(0, 4);
        ogg.push_back(static_cast<std::byte>(segments.size()));
        put(segments);
    };
    page(0, {30});
    put({1, 'v', 'o', 'r', 'b', 'i', 's'});
    putLe(0, 4);
    put({2});
    putLe(44100, 4);
    putLe(0, 12);
    // A page no packet ends in carries a granule of all ones and is skipped.
    page(441000, {});
    page(~core::u64{0}, {});
    const std::optional<core::u64> vorbis = audio::detail::probeFrames(ogg);
    REQUIRE(vorbis.has_value());
    CHECK(*vorbis == 480000u);

    // And a file that says nothing is nothing, not a guess.
    const std::vector<std::byte> noise(64, std::byte{0x5A});
    CHECK_FALSE(audio::detail::probeFrames(noise).has_value());
}

TEST_CASE("a long file streams: it is not decoded whole, and it sounds the same")
{
    StreamFixture content;

    {
        Fixture fixture;
        fixture.system.setContentMounts(&content.mounts);
        const InstanceId id = fixture.make("Sound");
        fixture.sound(id).content = "asset://music/song.wav";
        fixture.sound(id).playing = true;
        fixture.system.tick(*fixture.world, Tick);
        fixture.system.update(*fixture.world, InstanceId{});
        CHECK(fixture.system.stats().clipsLoaded == 1);
        CHECK(fixture.system.stats().clipsStreamed == 1);
        CHECK(fixture.system.clipDuration("asset://music/song.wav") == doctest::Approx(12.0).epsilon(0.0001));
    }

    // The first tenth of a second of the streamed song is the same samples as
    // the decoded file holding the same tone.
    const std::vector<float> streamed = listen(content.mounts, "asset://music/song.wav", 0.0, 4800u);
    const std::vector<float> decoded = listen(content.mounts, "asset://sfx/head.wav", 0.0, 4800u);
    CHECK(streamed == decoded);
    CHECK(std::ranges::any_of(streamed, [](float sample) { return std::abs(sample) > 0.1f; }));
}

TEST_CASE("a streamed sound seeks: five seconds in is what the file holds at five seconds")
{
    StreamFixture content;
    const std::vector<float> seeked = listen(content.mounts, "asset://music/song.wav", 5.0, 4800u);
    const std::vector<float> expected = listen(content.mounts, "asset://sfx/five.wav", 0.0, 4800u);
    CHECK(seeked == expected);
}

TEST_CASE("a streamed sound that is looped wraps at the file's end and keeps playing")
{
    StreamFixture content;
    // A tenth of a second before the end, and a fifth of a second of output:
    // half of it is the tail and half is the head again.
    const std::vector<float> once = listen(content.mounts, "asset://music/song.wav", 11.9, 9600u);
    CHECK(std::all_of(once.begin() + 4800 * 2, once.end(), [](float sample) { return sample == 0.0f; }));

    const std::vector<float> looped = listen(content.mounts, "asset://music/song.wav", 11.9, 9600u, true);
    const std::vector<float> head = listen(content.mounts, "asset://sfx/head.wav", 0.0, 4800u);
    CHECK(std::equal(head.begin(), head.end(), looped.begin() + 4800 * 2));
}

TEST_CASE("a decoded sound that is looped loses no frame at the loop point")
{
    // Found writing the streamed case above: wrapping the cursor skipped the
    // output frame it happened on, so every loop of every looped sound had one
    // frame of silence in it -- a click, sixty times a minute on a short loop.
    StreamFixture content;
    const std::vector<float> looped = listen(content.mounts, "asset://sfx/head.wav", 0.9, 9600u, true);
    const std::vector<float> head = listen(content.mounts, "asset://sfx/head.wav", 0.0, 4800u);
    CHECK(std::equal(head.begin(), head.end(), looped.begin() + 4800 * 2));
}
