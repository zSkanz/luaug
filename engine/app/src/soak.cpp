#include "luaug/app/soak.h"

#include "luaug/core/i18n.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
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
#ifdef LUAUG_SANITIZERS_ENABLED
        // **Measured and reported, not gating, under a sanitizer.** An
        // instrumented build pays two to three times the memory before the
        // engine allocates a byte -- shadow pages and redzones -- so this check
        // would be measuring the tool. The first sanitizer run this repository
        // ever performed failed here at 410 MiB against a 192 MiB ceiling while
        // reporting a median frame of 1.03 ms, which is the shape of a gate
        // asserting the wrong thing.
        //
        // It goes to `quarantined` rather than being skipped, so the number is
        // still in the report and a real regression is still visible to anybody
        // reading it -- the same distinction D066 put this list here for.
        verdict.quarantined.push_back(core::makeError(LUAUG_TR("engine.soak.err.over_ceiling"), args));
#else
        verdict.failures.push_back(core::makeError(LUAUG_TR("engine.soak.err.over_ceiling"), args));
#endif
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
        // **Quarantined, not gating** (D066, §12: two flakes). The same binary
        // over the same 5,939 frames reports a resident set that differs by a
        // quarter between runs, and it passes alone as often as it fails -- so
        // what this compares is partly the machine. Streaming's materialisation
        // budget is denominated in MILLISECONDS: under load fewer chunks
        // materialise per frame, the resident set lags the focus, and the
        // balance between materialising and evicting shifts across exactly the
        // quarters this reads.
        //
        // It keeps running and keeps saying what it saw, because a leak in a
        // streamed world has no other instrument. The replacement has to be
        // denominated in something load cannot move -- the honest one is a
        // focus path that RETURNS to where it started, since a resident set
        // that does not come back with it is a leak whatever the budget did.
        verdict.quarantined.push_back(core::makeError(LUAUG_TR("engine.soak.err.growing"), args));
    }

    // --- A place visited twice: D066's named successor, built ----------------
    //
    // **The comparison load cannot move.** The quarantined check above reads two
    // windows of a run and therefore reads how far materialisation fell behind
    // in each of them, which is a fact about the machine as much as about the
    // engine. This reads the SAME PLACE twice: whatever the millisecond budget
    // did in between, the same position holds the same chunks, so the resident
    // set has to come back to what it was. A world that leaked has more, and no
    // budget can produce that.
    //
    // **A revisited place rather than a return to the start**, which is a
    // correction the first run of this check produced rather than a design: the
    // flagship's fly-through never comes back to where it began -- its nearest
    // approach was 797 metres -- because a walk leg followed by a flight leg
    // goes onward. Any place visited early and again late makes the same claim
    // and covers that path as well as a circuit.
    if (thresholds.returnRadiusMetres > 0.0f && m_samples.size() >= 8) {
        const auto distance = [](core::Vec3 a, core::Vec3 b) noexcept {
            const f64 dx = static_cast<f64>(a.x) - static_cast<f64>(b.x);
            const f64 dy = static_cast<f64>(a.y) - static_cast<f64>(b.y);
            const f64 dz = static_cast<f64>(a.z) - static_cast<f64>(b.z);
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        };

        const usize count = m_samples.size();
        const usize earlyEnd = count / 4;
        const usize lateBegin = count - count / 4;

        // **Strided, so the search is bounded rather than quadratic in the run.**
        // A ten-minute soak is tens of thousands of frames and the honest answer
        // does not need every pair: the focus moves continuously, so sampling
        // one frame in N finds the same nearest approach to within how far it
        // travels in N frames. The stride is chosen so that neither side ever
        // examines more than `kRevisitSamples` points.
        constexpr usize kRevisitSamples = 384;
        const usize earlyStride = earlyEnd > kRevisitSamples ? earlyEnd / kRevisitSamples : 1;
        const usize lateSpan = count - lateBegin;
        const usize lateStride = lateSpan > kRevisitSamples ? lateSpan / kRevisitSamples : 1;

        f64 closest = std::numeric_limits<f64>::max();
        f64 furthest = 0.0;
        usize bestEarly = 0;
        usize bestLate = 0;
        for (usize early = 0; early < earlyEnd; early += earlyStride) {
            for (usize late = lateBegin; late < count; late += lateStride) {
                const f64 apart = distance(m_samples[early].focus, m_samples[late].focus);
                furthest = std::max(furthest, apart);
                if (apart < closest) {
                    closest = apart;
                    bestEarly = early;
                    bestLate = late;
                }
            }
        }

        verdict.furthestMetres = furthest;
        verdict.closestReturnMetres = closest == std::numeric_limits<f64>::max() ? 0.0 : closest;

        // The path has to actually GO somewhere, or every frame revisits every
        // other one and the check is vacuous in the other direction.
        const bool moved = furthest > static_cast<f64>(thresholds.departureMetres);
        const bool revisited = moved && closest <= static_cast<f64>(thresholds.returnRadiusMetres);

        verdict.focusReturned = revisited;
        if (revisited) {
            verdict.departureInstances = m_samples[bestEarly].instanceCount;
            verdict.returnInstances = m_samples[bestLate].instanceCount;
            verdict.revisitFrameGap = bestLate - bestEarly;

            const auto allowed =
                static_cast<u64>(static_cast<f64>(verdict.departureInstances) * (1.0 + thresholds.returnTolerance));
            if (verdict.departureInstances > thresholds.growthFloor && verdict.returnInstances > allowed) {
                const core::I18nArg args[] = {{"start", static_cast<core::i64>(verdict.departureInstances)},
                                              {"back", static_cast<core::i64>(verdict.returnInstances)}};
                verdict.failures.push_back(core::makeError(LUAUG_TR("engine.soak.err.return_grew"), args));
            }
        }
        else {
            // **A failure and not a skip.** A caller that declared a radius said
            // its path revisits somewhere; if it did not, this check measured
            // nothing, and a check that quietly does not run is the exact shape
            // of gate this file already caught once.
            const core::I18nArg args[] = {{"radius", static_cast<core::f64>(thresholds.returnRadiusMetres)},
                                          {"departure", static_cast<core::f64>(thresholds.departureMetres)},
                                          {"closest", verdict.closestReturnMetres},
                                          {"furthest", verdict.furthestMetres}};
            verdict.failures.push_back(core::makeError(LUAUG_TR("engine.soak.err.never_returned"), args));
        }
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
    // The returning-focus check, whatever the verdict: a passing run's numbers
    // are the baseline the next one is read against, and `focusReturned` false
    // with a radius declared is the case that says the check measured nothing
    // rather than that it found nothing.
    out << "  \"focusReturned\": " << (verdict.focusReturned ? "true" : "false") << ",\n";
    out << "  \"departureInstances\": " << verdict.departureInstances << ",\n";
    out << "  \"returnInstances\": " << verdict.returnInstances << ",\n";
    out << "  \"closestReturnMetres\": " << fixed(verdict.closestReturnMetres, 2) << ",\n";
    out << "  \"furthestMetres\": " << fixed(verdict.furthestMetres, 2) << ",\n";
    out << "  \"revisitFrameGap\": " << verdict.revisitFrameGap << ",\n";
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
    out << (verdict.failures.empty() ? "" : "\n  ") << "],\n";

    // Reported beside the failures and never mixed into them: a run whose only
    // complaint is quarantined is a PASSING run, and the numbers are still here
    // for whoever comes to replace the instrument (D066).
    out << "  \"quarantined\": [";
    for (usize i = 0; i < verdict.quarantined.size(); ++i)
        out << (i == 0 ? "\n" : ",\n") << "    \"" << escaped(verdict.quarantined[i].message) << "\"";
    out << (verdict.quarantined.empty() ? "" : "\n  ") << "]\n";
    out << "}\n";
    return out.str();
}

} // namespace luaug::app
