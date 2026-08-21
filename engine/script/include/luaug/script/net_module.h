// `@std/net` in the game VM (api-design.md §7, ADR 0012, ADR 0030).
//
// The reasoning -- what is in v1, what is deliberately absent, and why the
// naming follows Lute rather than ADR 0034 -- lives at the top of
// `net_module.cpp`, beside the code it governs.
#pragma once

#include "luaug/core/types.h"

struct lua_State;

namespace luaug::script {

using core::usize;

// Registers every `@std/*` module this build provides. Called once during boot,
// beside `registerRequire`.
void registerStdModules(lua_State* L);

// The module opener, exposed for the test that requires it directly rather than
// through `require` -- and for nothing else.
int openStdNet(lua_State* L);

// Resumes the coroutines whose requests have finished. Called by the host at a
// frame safe point, exactly like `resumeAreaWaiters`: a completion arriving on a
// worker thread must not enter game code where it landed (R10).
void resumeNetWaiters(lua_State* L);

// Submitted and not yet taken. For a debug panel and for a test that needs to
// know when to stop pumping.
[[nodiscard]] usize outstandingNetRequests(lua_State* L);

} // namespace luaug::script
