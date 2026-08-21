// The M7 gate's instrument: what a five-minute fly-through has to prove.
//
// The roadmap states the gate as "peak memory under the declared ceiling, zero
// frame hitches >33 ms attributable to streaming (frame-time histogram
// asserted)". This is that assertion, and it exists as a class rather than as
// twenty lines inside the frame loop for one reason: the gate's arithmetic is
// the part most likely to be wrong, and arithmetic inside a frame loop is
// arithmetic nobody can test.
//
// **A third check is here that the roadmap does not name**, and it is the one
// that earned its place. D032 was a streaming world that grew by a thousand
// instances every fifteen seconds and never shrank -- the frame times degraded
// smoothly from 100 fps to 35 over five minutes with no hitch anywhere, so a
// gate watching only for hitches would have shipped it. Peak memory would have
// caught it eventually, but only with a ceiling tight enough to be a nuisance.
// What actually distinguishes a streaming world from a growing one is that the
// instance count FLATTENS, and that is a thing to measure directly.
#pragma once

#include "luaug/core/error.h"
#include "luaug/core/types.h"

#include <string>
#include <vector>

namespace luaug::app {

using core::f64;
using core::u64;
using core::usize;

struct SoakThresholds
{
    // A frame slower than this is a hitch. 33 ms is the roadmap's number: two
    // frames at 60 Hz, the point at which a person sees a stutter rather than
    // a slow frame.
    //
    // Stricter than the roadmap's wording, and deliberately: it says "hitches
    // attributable to streaming", and nothing here can attribute. A hitch from
    // an unrelated cause fails this gate too. That is the honest trade -- the
    // alternative is an attribution heuristic whose failures are invisible.
    f64 hitchMs = 33.0;

    // Zero asserts nothing. A ceiling is a per-scene number and only the caller
    // running a particular fly-through knows what it declared.
    u64 memoryCeilingBytes = 0;

    // How much the last quarter of the run may exceed the second quarter.
    // Not zero: a world whose chunk residency varies with where the circuit
    // happens to end legitimately differs by a few per cent between windows.
    f64 growthTolerance = 0.15;

    // Below this many instances a proportional test is noise -- a world with
    // thirty instances that ends with thirty-five has grown by seventeen per
    // cent and by five things.
    u64 growthFloor = 256;

    // The world the soak claims to be soaking has to be THERE. Zero asserts
    // nothing; a scene-specific number is the caller's to declare.
    //
    // This exists because the gate passed vacuously once and would have gone on
    // passing: `--rhi=null` skipped the content mount entirely, so a five-minute
    // fly-through over a 289-chunk world ran in 0.17 s over eleven instances and
    // reported a clean bill of health. Flat frame times over an empty world are
    // exactly what a leak-detector should see, which is why it cannot be the
    // only thing it looks at.
    u64 minimumInstances = 0;
};

struct SoakSample
{
    f64 frameMs = 0.0;
    u64 residentBytes = 0;
    u64 instanceCount = 0;
};

struct SoakVerdict
{
    bool ok = true;

    // Populated whatever the verdict, because a passing soak's numbers are the
    // baseline the next one is read against.
    usize frames = 0;
    f64 medianMs = 0.0;
    f64 p99Ms = 0.0;
    f64 worstMs = 0.0;
    usize hitches = 0;
    u64 peakResidentBytes = 0;
    u64 finalResidentBytes = 0;
    u64 earlyInstances = 0;
    u64 lateInstances = 0;
    u64 peakInstances = 0;

    // Keyed rather than prose (R3), in the order they were found, and empty
    // when `ok`. A gate's failure message is the most-read string it has, and
    // it is exactly the kind that gets written in English because "it is only
    // a diagnostic" -- which is the sentence R3 exists to refuse.
    std::vector<core::EngineError> failures;
};

class SoakRecorder
{
public:
    // Warm-up frames are dropped from every statistic here, for the reason
    // `--frame-stats` drops them: the first frames are shader creation and the
    // first mesh upload, and a gate that counts them is a gate on startup.
    explicit SoakRecorder(usize warmupFrames = 60) : m_warmup(warmupFrames) {}

    void sample(SoakSample sample);

    [[nodiscard]] SoakVerdict evaluate(const SoakThresholds& thresholds) const;

    // The histogram the gate is required to assert, as JSON, alongside the
    // verdict. Buckets are fixed rather than derived from the data so that two
    // runs' reports can be diffed.
    [[nodiscard]] std::string report(const SoakThresholds& thresholds) const;

    [[nodiscard]] usize size() const noexcept { return m_samples.size(); }

private:
    usize m_warmup;
    usize m_seen = 0;
    std::vector<SoakSample> m_samples;
};

// Upper edges in milliseconds; the last bucket is everything above the last
// edge. Named here because the report writes them and a reader needs them.
inline constexpr f64 SoakHistogramEdges[] = {8.0, 16.7, 20.0, 25.0, 33.0, 50.0, 100.0};

} // namespace luaug::app
