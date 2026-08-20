// The fast world restart (ADR 0024 and its 2026-08-20 addendum; M3 brief
// Decisions 3, 10, 11 and 14).
//
// The whole model in one sentence: a reload destroys and rebuilds `WorldHost`,
// and nothing above it. The window, the RHI device, the renderer, the shader
// cache and the debug-draw state are owned by the frame loop, so "engine-side
// content survives" is a C++ lifetime rather than a promise anyone has to keep.
//
// The order is build-then-destroy, not destroy-then-build. A project that fails
// to mount -- one syntax error in one entry script, which is the common case in
// a loop whose point is that you save often -- leaves the previous world
// running and reports why.
#pragma once

#include <memory>
#include <optional>

#include "luaug/app/world_host.h"
#include "luaug/core/error.h"
#include "luaug/core/types.h"

namespace luaug::app
{

struct ReloadReport
{
    // False means nothing changed: `host` still points at the world it did
    // before the call, and `error` says what stopped the new one.
    bool ok = false;

    // The span ADR 0024's <500 ms budget is measured against: the safe point
    // through the new world being live (brief Decision 10). Wall-clock, and
    // therefore host instrumentation only -- it never reaches simulation state
    // and never enters the world hash (R10).
    core::f64 spanMs = 0.0;

    // Entry scripts the new world mounted, and how many of them would not
    // compile. Zero mounted from a project directory is a failure rather than a
    // fact, and so is any failure to compile: both are the reload equivalent of
    // a gate that passes while doing nothing (M2 Finding 19).
    core::u64 mountedScripts = 0;
    core::u64 loadFailures = 0;

    std::optional<core::EngineError> error;
};

// Rebuilds the world from source. Call only at the FrameStart safe point --
// never mid-tick, which is what keeps within-run determinism (architecture §4).
//
// `BindToClose` deliberately does NOT run on the outgoing world. It is the exit
// hook; `HotReloadService.BeforeReload` is the reload hook, and firing both
// would make every save look like a shutdown to a script that saves on close.
//
// A script that fails to compile is a failed RELOAD but not a failed boot, and
// the asymmetry is deliberate: at boot the engine has nothing better to offer
// than a world missing one script, while a reload has the world that was
// already running, which is strictly more useful than a broken one.
[[nodiscard]] ReloadReport reloadWorld(std::unique_ptr<WorldHost>& host, const WorldHostOptions& options);

} // namespace luaug::app
