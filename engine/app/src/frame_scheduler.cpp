#include "luaug/app/frame_scheduler.h"

#include "luaug/core/log.h"
#include "luaug/core/text_key.h"

#include <array>

namespace luaug::app {

Frame FrameScheduler::beginFrame(u64 nowNs) noexcept
{
    Frame frame;
    frame.index = totalFrames_;

    if (!started_) {
        // The first frame has no previous one to measure from. Reporting zero
        // beats reporting however long the process took to start, which is what
        // a naive `now - 0` would give and what would make the first frame look
        // like a hitch in every profile.
        started_ = true;
        lastNs_ = nowNs;
        ++totalFrames_;
        return frame;
    }

    // Monotonic clock, so this cannot go backwards -- but a paused debugger can
    // make it enormous, and treating that as real time would burn the catch-up
    // budget on the first frame after every breakpoint.
    const u64 elapsedNs = nowNs - lastNs_;
    lastNs_ = nowNs;

    frame.renderDt = static_cast<f64>(elapsedNs) / 1'000'000'000.0;
    accumulator_ += frame.renderDt;

    const f64 maxAccumulated = timing_.fixedDt * static_cast<f64>(timing_.maxCatchUpTicks);
    if (accumulator_ > maxAccumulated) {
        frame.clamped = true;
        accumulator_ = maxAccumulated;
    }

    while (accumulator_ >= timing_.fixedDt) {
        accumulator_ -= timing_.fixedDt;
        ++frame.simTicks;
    }

    totalTicks_ += frame.simTicks;
    frame.alpha = static_cast<f32>(accumulator_ / timing_.fixedDt);

    if (frame.clamped) {
        const std::array<core::I18nArg, 2> args{core::I18nArg{"ticks", static_cast<core::i64>(frame.simTicks)},
                                                core::I18nArg{"ms", frame.renderDt * 1000.0}};
        core::log(core::LogLevel::Warn, LUAUG_TR("engine.frame.warn.catch_up_clamped"), args);
    }

    ++totalFrames_;
    return frame;
}

} // namespace luaug::app
