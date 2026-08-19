// The M1 stopgap that lets a Luau script drive the frame (roadmap M1's
// "temporary minimal binding that M2 replaces").
//
// Everything here is scaffolding with a demolition date. M2 builds the real
// thing: `require("@luaug/...")` resolved through Luau.Require, Instances and
// services over the ECS, deferred signals, and `task` on the fixed-tick
// scheduler. None of that exists yet, and inventing a fraction of it now would
// mean designing M2's seams badly and in a hurry.
//
// So this is deliberately the smallest surface that can express the M1
// deliverable -- a clear colour and some debug geometry -- as one global table,
// obviously temporary, with nothing that looks like a service. It is easier to
// delete something that never pretended to be the real API.
#pragma once

#include <optional>

#include "luaug/core/error.h"
#include "luaug/core/types.h"
#include "luaug/render/debug_draw.h"
#include "luaug/rhi/types.h"

namespace luaug::app
{

class ScriptHost;

} // namespace luaug::app

struct lua_State;

namespace luaug::app
{

// What the bindings read and write. Owned by the frame loop; the API only ever
// holds a pointer, so a script cannot outlive the state it mutates.
struct PreviewState
{
    render::DebugDraw* draw = nullptr;
    rhi::ColorRgba clearColor{};
    // Set when the script registered a frame callback. Without one the engine
    // draws its own default scene, so `luaug-host script.luau` does something
    // visible whether or not the script cares about rendering.
    bool hasFrameHook = false;
};

// Installs the `luaug` global. Takes the raw VM rather than the host because it
// runs from ScriptHost's GlobalInstaller hook -- the window after the standard
// libraries open and before the sandbox freezes the globals table, at which
// point the host is still being constructed. Calling it any later fails inside
// the VM rather than returning something anyone can see, which is exactly how
// this was got wrong the first time.
void installPreviewApi(lua_State* L, PreviewState& state);

// Calls whatever the script registered with `luaug.onFrame`. Returns the error
// that ended it, which the caller reports and then stops calling -- a script
// that throws every frame would otherwise produce one error per frame forever.
[[nodiscard]] std::optional<core::EngineError> callFrameHook(
    ScriptHost& host, core::u64 tick, core::f64 elapsedSeconds);

} // namespace luaug::app
