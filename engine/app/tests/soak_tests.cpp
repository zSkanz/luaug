#include "luaug/app/soak.h"
#include "luaug/core/i18n.h"

#include <doctest/doctest.h>
#include <string>
#include <string_view>

using namespace luaug;
using namespace luaug::app;
using luaug::core::u64;

namespace {

// The real catalog, so these also prove every key `soak.cpp` raises exists in
// `i18n/en.json` -- a gate whose failure message is a bare key is a gate nobody
// reads twice.
void seedRealCatalog()
{
    const auto result = luaug::core::engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
}

// One frame of a healthy world: fast, flat memory, flat instance count.
void steady(SoakRecorder& recorder, int frames, f64 ms = 8.0, u64 instances = 4000, f64 streamingMs = 0.5)
{
    for (int i = 0; i < frames; ++i) {
        recorder.sample({.frameMs = ms,
                         .streamingMs = streamingMs,
                         .residentBytes = 512u * 1024u * 1024u,
                         .instanceCount = instances});
    }
}

[[nodiscard]] bool mentions(const SoakVerdict& verdict, std::string_view fragment)
{
    for (const core::EngineError& failure : verdict.failures) {
        if (failure.message.find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool mentionsQuarantined(const SoakVerdict& verdict, std::string_view fragment)
{
    for (const core::EngineError& note : verdict.quarantined) {
        if (note.message.find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("a flat world passes")
{
    seedRealCatalog();

    SoakRecorder recorder(60);
    steady(recorder, 1060);

    const SoakVerdict verdict = recorder.evaluate({});
    CHECK(verdict.ok);
    CHECK(verdict.frames == 1000);
    CHECK(verdict.hitches == 0);
    CHECK(verdict.earlyInstances == 4000);
    CHECK(verdict.lateInstances == 4000);
}

TEST_CASE("warm-up frames are not measured")
{
    SoakRecorder recorder(60);
    // The startup burst: sixty frames that would each be a hitch and would each
    // drag the median. They are exactly what a soak must not describe.
    for (int i = 0; i < 60; ++i) {
        recorder.sample({.frameMs = 200.0, .streamingMs = 150.0, .residentBytes = 0, .instanceCount = 0});
    }
    steady(recorder, 400);

    const SoakVerdict verdict = recorder.evaluate({});
    CHECK(verdict.frames == 400);
    CHECK(verdict.hitches == 0);
    CHECK(verdict.worstMs == doctest::Approx(8.0));
}

TEST_CASE("a run shorter than its warm-up fails rather than passes vacuously")
{
    seedRealCatalog();

    SoakRecorder recorder(60);
    steady(recorder, 10);

    const SoakVerdict verdict = recorder.evaluate({});
    CHECK_FALSE(verdict.ok);
    CHECK(verdict.frames == 0);
}

TEST_CASE("one frame that spent too long INSIDE STREAMING fails the gate")
{
    seedRealCatalog();

    SoakRecorder recorder(0);
    steady(recorder, 500);
    recorder.sample({.frameMs = 41.0, .streamingMs = 38.0, .residentBytes = 0, .instanceCount = 4000});
    steady(recorder, 500);

    const SoakVerdict verdict = recorder.evaluate({});
    CHECK_FALSE(verdict.ok);
    CHECK(verdict.hitches == 1);
    CHECK(verdict.worstStreamingMs == doctest::Approx(38.0));
    CHECK(mentions(verdict, "engine.soak.err.hitches"));
}

TEST_CASE("a long frame that streaming did not cause is not a hitch")
{
    seedRealCatalog();

    // The distinction the whole check exists for. This is the Tier-2 container
    // on a busy host: eighteen slow frames out of eighteen thousand, none of
    // them inside streaming. The first version of this gate failed on exactly
    // this and was measuring the runner rather than the engine.
    // Eighteen in EIGHTEEN THOUSAND, which is the ratio the container actually
    // produced. The ratio is the test: at that rate the p99 backstop below does
    // not notice, and at a rate a hundred times higher it should.
    SoakRecorder recorder(0);
    steady(recorder, 18000);
    for (int i = 0; i < 18; ++i) {
        recorder.sample({.frameMs = 83.0, .streamingMs = 0.4, .residentBytes = 0, .instanceCount = 4000});
    }

    const SoakVerdict verdict = recorder.evaluate({});
    CHECK(verdict.hitches == 0);
    CHECK(verdict.worstMs == doctest::Approx(83.0));
    CHECK(verdict.ok);
}

TEST_CASE("frames that are ALL slow fail the backstop, whatever the cause")
{
    seedRealCatalog();

    // The other half: a percentile tolerates noise and does not tolerate a
    // machine that is uniformly slow, which is worth failing even unattributed.
    SoakRecorder recorder(0);
    steady(recorder, 1000, 90.0);

    const SoakVerdict verdict = recorder.evaluate({});
    CHECK_FALSE(verdict.ok);
    CHECK(verdict.hitches == 0);
    CHECK(mentions(verdict, "engine.soak.err.slow_frames"));
}

TEST_CASE("the declared ceiling is only asserted when it is declared")
{
    seedRealCatalog();

    SoakRecorder recorder(0);
    for (int i = 0; i < 400; ++i) {
        recorder.sample({.frameMs = 8.0, .residentBytes = 900u * 1024u * 1024u, .instanceCount = 4000});
    }

    CHECK(recorder.evaluate({}).ok);
    CHECK(recorder.evaluate({.memoryCeilingBytes = 1024u * 1024u * 1024u}).ok);

    // **A breach is REPORTED in either build, and gates in only one of them.**
    // An instrumented build spends two to three times the memory on shadow
    // pages and redzones before the engine allocates a byte, so under a
    // sanitizer this check measures the tool -- the first sanitizer run this
    // repository ever performed failed here at 410 MiB against a 192 MiB
    // ceiling while reporting a median frame of 1.03 ms.
    //
    // Asserted as two shapes rather than compiled away, because "this assertion
    // does not exist in that configuration" is how a check quietly stops
    // existing in every configuration.
    const SoakVerdict breached = recorder.evaluate({.memoryCeilingBytes = 512u * 1024u * 1024u});
#ifdef LUAUG_SANITIZERS_ENABLED
    CHECK(mentionsQuarantined(breached, "engine.soak.err.over_ceiling"));
    CHECK(breached.ok);
    CHECK(breached.failures.empty());
#else
    CHECK(mentions(breached, "engine.soak.err.over_ceiling"));
    CHECK_FALSE(breached.ok);
    CHECK_FALSE(breached.failures.empty());
#endif
}

TEST_CASE("a world that grows and never shrinks fails, with no hitch anywhere")
{
    seedRealCatalog();

    // D032 in miniature, and the numbers are the ones the defect actually
    // produced: a thousand instances every fifteen seconds, every frame inside
    // budget. A gate watching only for hitches passes this.
    SoakRecorder recorder(0);
    for (int i = 0; i < 1000; ++i) {
        recorder.sample({.frameMs = 12.0,
                         .streamingMs = 0.5,
                         .residentBytes = 512u * 1024u * 1024u,
                         .instanceCount = 2000 + static_cast<u64>(i) * 10});
    }

    const SoakVerdict verdict = recorder.evaluate({});
    CHECK(verdict.hitches == 0);
    CHECK(verdict.earlyInstances < verdict.lateInstances);

    // **The check still fires; it no longer gates** (D066, MASTER_PROMPT.md
    // §12: two flakes). It flaked twice on the flagship -- the same binary over
    // the same 5,939 frames reporting resident sets a quarter apart, failing
    // and then passing on the next run with the machine to itself -- and the
    // cause is understood: streaming's materialisation budget is denominated in
    // milliseconds, so under load the resident set lags the focus and the
    // balance this reads across quarters shifts with the machine.
    //
    // Widening the tolerance until it stopped complaining would have removed
    // the only instrument watching a streamed world for a leak, which is what
    // D032 cost to find. So it keeps measuring and keeps saying what it saw,
    // and this case holds the distinction: the complaint is present, and the
    // run is a pass.
    CHECK(verdict.ok);
    CHECK_FALSE(mentions(verdict, "engine.soak.err.growing"));
    CHECK(mentionsQuarantined(verdict, "engine.soak.err.growing"));
}

TEST_CASE("a world that breathes is not a world that leaks")
{
    seedRealCatalog();

    // A circuit whose chunk residency rises and falls. The fourth quarter is
    // higher than the second here, and legitimately: the gate's tolerance is
    // what separates this from the case above.
    SoakRecorder recorder(0);
    for (int i = 0; i < 1000; ++i) {
        const auto wobble = static_cast<u64>((i % 100) * 3);
        recorder.sample({.frameMs = 12.0, .residentBytes = 0, .instanceCount = 4000 + wobble});
    }

    CHECK(recorder.evaluate({}).ok);
}

TEST_CASE("a small world is not called a leak by a proportional test")
{
    seedRealCatalog();

    // Thirty instances that become forty is a sixty-seven per cent rise and
    // ten things. The floor is what stops the gate reporting it.
    SoakRecorder recorder(0);
    for (int i = 0; i < 1000; ++i) {
        recorder.sample({.frameMs = 8.0, .residentBytes = 0, .instanceCount = 30 + static_cast<u64>(i) / 100});
    }

    CHECK(recorder.evaluate({}).ok);
}

TEST_CASE("the report carries the histogram the gate is required to assert")
{
    seedRealCatalog();

    SoakRecorder recorder(0);
    steady(recorder, 100, 5.0);
    steady(recorder, 10, 40.0, 4000, 40.0);

    const std::string report = recorder.report({});
    CHECK(report.find("\"histogram\"") != std::string::npos);
    CHECK(report.find("\"ok\": false") != std::string::npos);
    CHECK(report.find("\"hitches\": 10") != std::string::npos);
    // The histogram is of WHOLE frames even though the hitch check is not: it is
    // the evidence a human reads, and "the frames were slow" is what they are
    // looking at when they open it.
    CHECK(report.find("\"worstStreamingMs\": 40.000") != std::string::npos);
    // Every bucket edge is named, so a reader does not have to have this header.
    CHECK(report.find("\"upperMs\": 33.0") != std::string::npos);
    CHECK(report.find("\"upperMs\": null") != std::string::npos);
}
