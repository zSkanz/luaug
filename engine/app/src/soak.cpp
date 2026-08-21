#include "luaug/app/soak.h"

#include "luaug/core/i18n.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <string_view>

namespace luaug::app {
namespace {

// Fixed-format so a report is byte-comparable between two runs of the same
// build where the numbers happen to match. `std::to_string` on a double gives
// six decimals of noise and no control over it.
[[nodiscard]] std::string fixed(f64 value, int decimals)
{
    std::ostringstream out;
    out.setf(std::ios::fixed, std::ios::floatfield);
    out.precision(decimals);
    out << value;
    return out.str();
}

// Minimal, and sufficient for what goes through it: a catalog-formatted engine
// message. Control characters below 0x20 do not appear in one, and a full JSON
// escaper here would be untested code guarding against input that cannot arrive.
[[nodiscard]] std::string escaped(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

[[nodiscard]] f64 percentile(const std::vector<f64>& sorted, f64 fraction)
{
    if (sorted.empty()) {
        return 0.0;
    }
    // Nearest-rank, clamped. An interpolating percentile over frame times
    // invents a frame that did not happen, which is the wrong shape for a
    // number a gate compares against.
    const auto index = static_cast<usize>(std::llround(fraction * static_cast<f64>(sorted.size() - 1)));
    return sorted[std::min(index, sorted.size() - 1)];
}

} // namespace

void SoakRecorder::sample(SoakSample sample)
{
    m_seen += 1;
    if (m_seen <= m_warmup) {
        return;
    }
    m_samples.push_back(sample);
}

SoakVerdict SoakRecorder::evaluate(const SoakThresholds& thresholds) const
{
    SoakVerdict verdict;
    verdict.frames = m_samples.size();
    if (m_samples.empty()) {
        // Not a pass. A soak that recorded nothing proves nothing, and the one
        // way this happens is a run shorter than the warm-up -- which is a
        // mistake in how the gate was invoked rather than a healthy world.
        verdict.ok = false;
        verdict.failures.push_back(core::makeError(LUAUG_TR("engine.soak.err.no_frames")));
        return verdict;
    }

    std::vector<f64> times;
    times.reserve(m_samples.size());
    for (const SoakSample& sample : m_samples) {
        times.push_back(sample.frameMs);
        verdict.peakResidentBytes = std::max(verdict.peakResidentBytes, sample.residentBytes);
        verdict.peakInstances = std::max(verdict.peakInstances, sample.instanceCount);
        verdict.worstStreamingMs = std::max(verdict.worstStreamingMs, sample.streamingMs);
        // The ATTRIBUTED time, not the frame. See `SoakThresholds::hitchMs`.
        if (sample.streamingMs > thresholds.hitchMs) {
            verdict.hitches += 1;
        }
    }
    verdict.finalResidentBytes = m_samples.back().residentBytes;

    std::sort(times.begin(), times.end());
    verdict.medianMs = percentile(times, 0.5);
    verdict.p99Ms = percentile(times, 0.99);
    verdict.worstMs = times.back();

    // Quarters rather than halves. The first quarter is the world filling up --
    // it is SUPPOSED to grow there -- so the comparison is between the second
    // and the fourth, two windows that should both describe a world in a
    // steady state.
    const usize quarter = m_samples.size() / 4;
    if (quarter > 0) {
        for (usize i = quarter; i < quarter * 2; ++i) {
            verdict.earlyInstances = std::max(verdict.earlyInstances, m_samples[i].instanceCount);
        }
        for (usize i = quarter * 3; i < m_samples.size(); ++i) {
            verdict.lateInstances = std::max(verdict.lateInstances, m_samples[i].instanceCount);
        }
    }

    // Checked before anything else, because it is the check that says whether
    // the others measured anything at all.
    if (thresholds.minimumInstances > 0 && verdict.peakInstances < thresholds.minimumInstances) {
        const core::I18nArg args[] = {{"peak", static_cast<core::i64>(verdict.peakInstances)},
                                      {"minimum", static_cast<core::i64>(thresholds.minimumInstances)}};
        verdict.failures.push_back(core::makeError(LUAUG_TR("engine.soak.err.empty_world"), args));
    }

    if (verdict.hitches > 0) {
        const core::I18nArg args[] = {{"hitches", static_cast<core::i64>(verdict.hitches)},
                                      {"threshold", thresholds.hitchMs},
                                      {"worst", verdict.worstStreamingMs}};
        verdict.failures.push_back(core::makeError(LUAUG_TR("engine.soak.err.hitches"), args));
    }

    // The backstop. A percentile rather than a maximum, because one slow frame
    // in a hundred on a shared runner says nothing and every frame being slow
    // says everything.
    if (thresholds.wholeFrameP99Ms > 0.0 && verdict.p99Ms > thresholds.wholeFrameP99Ms) {
        const core::I18nArg args[] = {{"p99", verdict.p99Ms}, {"threshold", thresholds.wholeFrameP99Ms}};
        verdict.failures.push_back(core::makeError(LUAUG_TR("engine.soak.err.slow_frames"), args));
    }

    if (thresholds.memoryCeilingBytes > 0 && verdict.peakResidentBytes > thresholds.memoryCeilingBytes) {
        const core::I18nArg args[] = {
            {"peak", static_cast<core::i64>(verdict.peakResidentBytes / (1024 * 1024))},
            {"ceiling", static_cast<core::i64>(thresholds.memoryCeilingBytes / (1024 * 1024))}};
        verdict.failures.push_back(core::makeError(LUAUG_TR("engine.soak.err.over_ceiling"), args));
    }

    // Both conditions, and the floor is why: a proportional bound alone calls a
    // world that went from four instances to six a leak, and an absolute bound
    // alone scales wrong across scenes.
    const bool overFloor = verdict.lateInstances > thresholds.growthFloor;
    const auto ceiling =
        static_cast<u64>(static_cast<f64>(verdict.earlyInstances) * (1.0 + thresholds.growthTolerance));
    if (overFloor && verdict.earlyInstances > 0 && verdict.lateInstances > ceiling) {
        const core::I18nArg args[] = {{"early", static_cast<core::i64>(verdict.earlyInstances)},
                                      {"late", static_cast<core::i64>(verdict.lateInstances)}};
        verdict.failures.push_back(core::makeError(LUAUG_TR("engine.soak.err.growing"), args));
    }

    verdict.ok = verdict.failures.empty();
    return verdict;
}

std::string SoakRecorder::report(const SoakThresholds& thresholds) const
{
    const SoakVerdict verdict = evaluate(thresholds);

    std::array<usize, std::size(SoakHistogramEdges) + 1> buckets{};
    for (const SoakSample& sample : m_samples) {
        usize bucket = buckets.size() - 1;
        for (usize i = 0; i < std::size(SoakHistogramEdges); ++i) {
            if (sample.frameMs <= SoakHistogramEdges[i]) {
                bucket = i;
                break;
            }
        }
        buckets[bucket] += 1;
    }

    std::ostringstream out;
    out << "{\n";
    out << "  \"ok\": " << (verdict.ok ? "true" : "false") << ",\n";
    out << "  \"frames\": " << verdict.frames << ",\n";
    out << "  \"medianMs\": " << fixed(verdict.medianMs, 3) << ",\n";
    out << "  \"p99Ms\": " << fixed(verdict.p99Ms, 3) << ",\n";
    out << "  \"worstMs\": " << fixed(verdict.worstMs, 3) << ",\n";
    out << "  \"hitchMs\": " << fixed(thresholds.hitchMs, 3) << ",\n";
    out << "  \"hitches\": " << verdict.hitches << ",\n";
    out << "  \"worstStreamingMs\": " << fixed(verdict.worstStreamingMs, 3) << ",\n";
    out << "  \"wholeFrameP99Ms\": " << fixed(thresholds.wholeFrameP99Ms, 3) << ",\n";
    out << "  \"peakResidentBytes\": " << verdict.peakResidentBytes << ",\n";
    out << "  \"finalResidentBytes\": " << verdict.finalResidentBytes << ",\n";
    out << "  \"memoryCeilingBytes\": " << thresholds.memoryCeilingBytes << ",\n";
    out << "  \"earlyInstances\": " << verdict.earlyInstances << ",\n";
    out << "  \"lateInstances\": " << verdict.lateInstances << ",\n";
    out << "  \"peakInstances\": " << verdict.peakInstances << ",\n";
    out << "  \"minimumInstances\": " << thresholds.minimumInstances << ",\n";

    out << "  \"histogram\": [\n";
    for (usize i = 0; i < buckets.size(); ++i) {
        out << "    { \"upperMs\": ";
        if (i < std::size(SoakHistogramEdges)) {
            out << fixed(SoakHistogramEdges[i], 1);
        }
        else {
            out << "null";
        }
        out << ", \"frames\": " << buckets[i] << " }" << (i + 1 < buckets.size() ? "," : "") << "\n";
    }
    out << "  ],\n";

    out << "  \"failures\": [";
    for (usize i = 0; i < verdict.failures.size(); ++i) {
        // `message` rather than the key alone, and it costs nothing: `makeError`
        // prefixes every message with `[the.key]`, so a report stays greppable by
        // the stable identifier AND readable without the catalog.
        out << (i == 0 ? "\n" : ",\n") << "    \"" << escaped(verdict.failures[i].message) << "\"";
    }
    out << (verdict.failures.empty() ? "" : "\n  ") << "]\n";
    out << "}\n";
    return out.str();
}

} // namespace luaug::app
