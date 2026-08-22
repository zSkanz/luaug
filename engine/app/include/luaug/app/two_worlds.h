// The editor-seam proof (roadmap M8; ADR 0017's standing condition).
//
// ADR 0017 declined a visual editor for v1 on the explicit condition that
// nothing in v1 hard-codes an assumption that blocks one. The concrete thing a
// phase-2 editor needs first -- and what prefab-isolation mode is made of -- is
// **two `WorldHost`s alive at once**, each with its own `ScriptRuntime`, which
// is two Luau VMs, rendered into two targets.
//
// Networking is the second caller and the reason this matters more than it did
// when it was only the editor's: the post-v1 multiplayer design puts an
// authoritative world and a replica in one process over a loopback transport,
// which is impossible if anything here assumes one world.
//
// **Why this is a mode and not a unit test.** Two hosts that both construct
// prove nothing -- a global both of them scribble into would still construct.
// What has to be proven is that the two are INDEPENDENT, and the only
// instrument that can say so is a differential over what each one draws:
//
//   - each world's image, rendered while the other is alive, must be
//     byte-identical to that world rendered alone, and
//   - the two images must differ from each other.
//
// The first half catches a shared static: if world B's contents reach world A's
// frame, A stops matching its own solo render. The second half catches the
// vacuous pass -- two empty worlds agree perfectly (D043 is what that costs).
//
// Like `--replay` and `--bench` this is a mode rather than a flag on a normal
// run: it owns its own device, its own frame loop and its own exit, because
// what it measures is three sessions compared against each other.
#pragma once

#include "luaug/core/error.h"
#include "luaug/rhi/backends.h"

#include <filesystem>
#include <optional>

namespace luaug::app {

struct TwoWorldsOptions
{
    // A directory holding two project subdirectories, `a/` and `b/`. Two
    // projects rather than one run twice, because a world that differs only in
    // its seed would make the "the images differ" half depend on the scripts
    // having used the seed.
    std::filesystem::path root;

    // Where the four PNGs go, or empty to write none. Evidence for a human;
    // the assertions themselves compare pixels in memory and never read a file
    // back, so a gate result never depends on a file having been written.
    std::filesystem::path outputDir;

    rhi::BackendId backend = rhi::BackendId::SdlGpu;

    // How many ticks each world runs before it is rendered. Enough that the
    // scripts have run and the world is settled; every world here is scripted
    // rather than simulated, so this is small on purpose.
    core::u64 ticks = 8;

    core::i32 width = 640;
    core::i32 height = 360;
};

// Runs the proof. Returns the first failure, or nothing when the seam is open.
//
// A machine with no usable GPU is not a failure of anything under test: the
// error carries `rhi.err.device_create_failed`, which `main` already maps to
// the exit code CTest reads as a skip.
[[nodiscard]] std::optional<core::EngineError> runTwoWorldsGate(const TwoWorldsOptions& options);

} // namespace luaug::app
