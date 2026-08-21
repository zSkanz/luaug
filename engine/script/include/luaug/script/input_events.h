// `InputService`'s raw event surface: `InputBegan`, `InputChanged`, `InputEnded`
// and the `InputObject` they carry (api-design.md §2.4, ADR 0041).
//
// **These are fed from the Input Action System's dispatch and never from the
// operating system**, which is the whole reason ADR 0041 could amend ADR 0029's
// "only input model" clause. The objection to raw events was never that they
// exist; it was to events read from the OS, which arrive on the wall clock, know
// nothing of what the UI consumed, and cannot be replayed. Coming out of
// `input::InputSystem`'s own tick, all three are answered by machinery that was
// already there.
//
// `script` links `input` already (the `InputAction` method bindings do), so this
// file names `input::RawInputEvent` directly rather than inventing a second POD
// for the same fact.
#pragma once

#include "luaug/input/input.h"

#include <span>

struct lua_State;

namespace luaug::script {

// Installs the `InputObject` metatable and its getters. Runs at boot with the
// other datatypes, before the sandbox.
void registerInputTypes(lua_State* L);

// Enqueues one fire per event on `InputService`, in the order the system
// produced them -- which is `KeyCode` ascending, then the pointer, then the
// wheel, then the gamepad axes (R10: an observable order comes from something
// that promises one).
//
// A no-op when `InputService` was never created, which is the common case in a
// world whose scripts do not read input at all.
void fireInputEvents(lua_State* L, std::span<const input::RawInputEvent> events);

} // namespace luaug::script
