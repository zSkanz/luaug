#include "luaug/core/phase.h"

namespace luaug::core
{

const char* phaseName(Phase phase) noexcept
{
    // Spelled exactly like the enumerators: these strings end up in profiler
    // zone names and log lines, and a capture is only useful if the label can be
    // grepped back to the phase it came from.
    switch (phase)
    {
    case Phase::FrameStart: return "FrameStart";
    case Phase::PreRender: return "PreRender";
    case Phase::ParallelWindowB: return "ParallelWindowB";
    case Phase::PreAnimation: return "PreAnimation";
    case Phase::PreSimulation: return "PreSimulation";
    case Phase::ParallelWindowA: return "ParallelWindowA";
    case Phase::PostSimulation: return "PostSimulation";
    case Phase::TaskResume: return "TaskResume";
    case Phase::Heartbeat: return "Heartbeat";
    // `Count` is a bound, not a phase, and neither is a value cast in from
    // outside the enum. Both fall through to the sentinel rather than to a
    // phase name, so a mislabelled zone reads as broken instead of as
    // FrameStart.
    case Phase::Count: break;
    }
    return "InvalidPhase";
}

} // namespace luaug::core
