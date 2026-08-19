// The frame pipeline's two clocks (architecture.md §3).
//
// SimClock is a fixed tick -- default 1/60, configurable later through
// PhysicsService.FixedTimestep -- and every deterministic thing in the engine
// lives inside one. RenderClock is variable and only rendering sees it, which
// interpolates between the last two sim states by `alpha`.
//
// This is the shape a rollback-capable engine needs and it is deliberately the
// starting point rather than something retrofitted (ADR 0016).
#pragma once

#include "luaug/core/types.h"

namespace luaug::app
{

using core::f32;
using core::f64;
using core::u32;
using core::u64;

struct FrameTiming
{
    f64 fixedDt = 1.0 / 60.0;

    // A frame that owes more than this many ticks stops trying to catch up:
    // the accumulator is clamped and the time is dropped. Without it, a machine
    // that cannot keep up runs more ticks, which makes it slower, which owes it
    // more ticks -- the spiral of death. Dropping simulated time is the lesser
    // evil and the one every fixed-tick engine picks.
    u32 maxCatchUpTicks = 4;
};

struct Frame
{
    u64 index = 0;
    // Seconds since the previous frame, on the wall clock.
    f64 renderDt = 0.0;
    // Fixed steps this frame owes the simulation, already clamped.
    u32 simTicks = 0;
    // Where rendering sits between the last tick and the next, in [0, 1).
    // Passed to render::extract so a 144 Hz display shows smooth motion from a
    // 60 Hz simulation.
    f32 alpha = 0.0f;
    // True when maxCatchUpTicks clamped this frame -- simulated time was
    // dropped, and anything measuring simulation rate should know.
    bool clamped = false;
};

class FrameScheduler
{
public:
    explicit FrameScheduler(FrameTiming timing = {}) noexcept : timing_(timing) {}

    // Takes the current time rather than reading a clock, so a test can drive
    // the accumulator through exact sequences -- including the catch-up clamp,
    // which is otherwise reachable only by making a machine slow. Nothing about
    // the tick schedule should depend on being able to reproduce a stall.
    [[nodiscard]] Frame beginFrame(u64 nowNs) noexcept;

    [[nodiscard]] u64 totalTicks() const noexcept { return totalTicks_; }
    [[nodiscard]] u64 totalFrames() const noexcept { return totalFrames_; }
    [[nodiscard]] const FrameTiming& timing() const noexcept { return timing_; }

private:
    FrameTiming timing_;
    u64 lastNs_ = 0;
    bool started_ = false;
    f64 accumulator_ = 0.0;
    u64 totalTicks_ = 0;
    u64 totalFrames_ = 0;
};

} // namespace luaug::app
