// Subsystem bring-up and the frame loop (architecture.md §2 "app", §3).
#pragma once

#include "luaug/core/error.h"
#include "luaug/core/types.h"
#include "luaug/render/settings.h"
#include "luaug/rhi/types.h"

#include <filesystem>
#include <optional>
#include <string>

namespace luaug::app {

using core::i32;
using core::u64;

struct EngineOptions
{
    // A directory is a project root and gets the full mount; a file is mounted
    // as a single entry `Script` (M2 brief, Decision 9). The scripts do not run
    // before the loop any more -- they are deferred, and their first resumption
    // is the first drain of the first tick.
    std::filesystem::path scriptPath;

    // The world's deterministic stream. Fixed rather than drawn from a clock,
    // because a replay stores the seed and nothing else (ADR 0025) -- and
    // because two runs of the same script must produce the same world hash,
    // which is the M2 gate.
    u64 worldSeed = 1;

    // No window. The frame loop, the device and the render target all still
    // exist -- this is the CI harness and the shape a dedicated server would
    // take, not a mode where rendering is skipped.
    bool headless = false;

    // Zero means run until the window is closed. Any other value is a frame
    // budget, which is the only thing that makes a headless run terminate.
    u64 frames = 0;

    // Exit when the frame budget is spent instead of continuing to run.
    bool exitAfterFrames = false;

    // Empty means take no screenshot. Requires `headless`: a windowed frame
    // renders into the swapchain, which has been presented and is gone by the
    // time anyone could read it.
    std::filesystem::path screenshotPath;

    // Where to write the recorded command stream. Only the capture backend
    // records one; asking any other backend for it is a usage error rather
    // than an empty file, because an empty golden would pass forever.
    std::filesystem::path capturePath;

    // Runs the conformance suite from this directory instead of a normal
    // session: every `*.spec.luau` under it is mounted as an entry `Script` and
    // the run ends by itself once the suite reports (api-design.md §3).
    std::filesystem::path conformanceRoot;

    // Runs the record/replay determinism gate over this directory instead of a
    // session. A replay creates no device and no window (see `replay.h`), so it
    // is a mode rather than a flag on a normal run.
    std::filesystem::path replayRoot;

    // Rewrites each scenario's `trace.txt` from this build instead of comparing
    // against it. The only way a legitimate semantic change gets a new golden.
    bool replayRecord = false;

    // Runs the editor-seam proof over this directory, which holds two projects
    // `a/` and `b/` (see `two_worlds.h`). A mode rather than a flag on a normal
    // run: what it compares is three sessions against each other, so there is
    // no single session for the frame budget or the screenshot path to describe.
    std::filesystem::path twoWorldsRoot;

    // Where that proof leaves its four PNGs, or empty to write none. The
    // assertions compare pixels in memory, so the files are evidence for a
    // human and never an input to the result.
    std::filesystem::path twoWorldsOutDir;

    // Runs the simulation benchmarks over this directory. Like a replay it
    // opens no device: what it measures is the tick, and a tick that depended
    // on a swapchain would be the finding rather than the measurement.
    std::filesystem::path benchRoot;
    u64 benchRepeats = 3;

    // Where a conformance run writes its machine-readable per-case report.
    // Empty writes none. `luaug test` reads this rather than the console,
    // because every console line is catalog-resolved (M3 brief, Decision 6).
    std::filesystem::path testReportPath;

    // `ws://127.0.0.1:<port>/<path>` -- the dev server this engine dials out to
    // (ADR 0035). Empty means no watcher is attached and no control code runs
    // at all. The engine never listens, in any profile.
    std::string devControlUrl;
    // Proves this connection is the engine `luaug dev` launched: a loopback
    // listener is reachable by every process on the machine.
    std::string devControlToken;

    rhi::BackendId backend = rhi::BackendId::SdlGpu;

    // Print a frame-time summary at exit. Off by default because the numbers
    // are wall-clock and a run that is not being measured should not pay for
    // keeping them (R10 forbids simulation reading them at all).
    // The visual editor rather than a game (ADR 0046, post-v1 phase 1). A normal
    // windowed session in every other respect: the same world, the same tick,
    // the same renderer -- what changes is that the world is drawn into a panel
    // and the shell around it can select and edit what it draws.
    //
    // Not a mode that hijacks the loop the way `--replay` and `--two-worlds`
    // do, and deliberately so: an editor that ran a different loop from the game
    // would be an editor that shows you something other than your game.
    bool editor = false;

    // `[project] scene` -- the scene a run of this project starts with,
    // content-relative. Empty means the project starts with whatever its scripts
    // build, which is every example before `06-scene`.
    //
    // The EDITOR may open a different one: it remembers what the person had open
    // and falls back to this. Which scene a run starts with is the project's
    // decision; which scene an editor opens is the person's.
    std::string startupScene;

    // **Write the world the scripts just built to a scene, then exit.**
    //
    // The one thing ADR 0047's migration needs and cannot do without: a project
    // whose world is in its code has no way to get that world into a file, and
    // retyping it by hand into JSON is not a migration anybody performs. This
    // boots the project, lets the entry scripts build whatever they build, and
    // writes it -- the same `writeScene` the editor's Save calls, so what comes
    // out is what the editor would have written.
    //
    // Headless and one frame. It is a capture, not a run.
    std::filesystem::path saveScenePath;

    bool frameStats = false;

    // M7's gate, as a flag. Empty writes no report and asserts nothing.
    //
    // The HOST enforces it rather than a script or a separate checker, the
    // same way `--replay` compares against its own golden: the numbers exist
    // only inside the frame loop, and a gate that has to be re-derived from a
    // log line is a gate that rots the first time the line is reworded.
    // A failed soak is a non-zero exit; `soak.h` holds the arithmetic.
    std::filesystem::path soakReportPath;

    // The DECLARED ceiling. Zero asserts no ceiling -- a number only whoever
    // is running a particular fly-through can supply, and one this code has
    // no business inventing a default for.
    u64 soakCeilingBytes = 0;

    // What "the world loaded" means for this particular scene. Zero asserts
    // nothing; see `soak.h` for why a soak needs to be told.
    u64 soakMinimumInstances = 0;

    // The quality family, already resolved through its three layers by the time
    // it gets here (project_config.h): a preset, the project file, the flags.
    // The host applies it to the renderer and never re-derives it.
    render::GraphicsSettings graphics;

    // The game's own window title, passed through rather than translated -- it
    // is the game's string and not the engine's (R3, and the split
    // `log()`/`logText()` draws). Empty uses the engine's titled window.
    std::string windowTitle;

    i32 width = 1280;
    i32 height = 720;
};

// Runs to completion. Returns the first error that stopped it, or nothing on a
// clean exit.
[[nodiscard]] std::optional<core::EngineError> run(const EngineOptions& options);

} // namespace luaug::app
