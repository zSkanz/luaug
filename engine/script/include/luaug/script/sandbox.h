// The sandbox R4 actually requires (architecture.md §5, ADR 0020).
//
// `luaL_sandbox` is necessary and nowhere near sufficient. Reading the vendored
// implementation settled what it really does: it freezes the global table one
// level deep and sets the string metatable read-only, and it removes **nothing**
// -- despite a comment of its own that reads as though it does. Every name
// api-design.md §1.1 lists as removed is still sitting in `_G` after it runs.
//
// So the curation is ours, it happens before the freeze, and it is tested from
// C++ rather than from a spec: naming an undeclared global is itself a
// strict-mode error, so a conformance spec cannot legally reference `wait` to
// prove it is absent, and a `:: any` cast around one tests the cast (M2 brief,
// ruling R-D).
#pragma once

struct lua_State;

namespace luaug::script {

// Every name in api-design.md §1.1's removal list, in one place so that the
// list and the code cannot drift. Null-terminated.
extern const char* const RemovedGlobals[];

// Deletes the removed globals and installs the frozen-empty `_G`. Must run
// after `luaL_openlibs` -- there is nothing to remove before it -- and before
// the sandbox freezes the table.
void removeUnsafeGlobals(lua_State* L);

// `luaL_sandbox` plus the freezes it does not do. After this, `lua_setglobal`
// silently fails inside the VM, which is why every global has to be installed
// first.
void sealGlobals(lua_State* L);

// **`math` and `vector.angle` through the engine's own transcendentals**
// (ADR 0083). Luau's library calls each platform's C runtime, which rounds
// `sin` and the rest differently on Windows, Linux and macOS; a script's
// `math.sin` is on the simulation's path, so it answers with `core::dmath`
// instead. Same names, same arguments, same results to within an ulp -- and
// the same bits everywhere. Runs before the seal, like every other global.
void installDeterministicMath(lua_State* L);

// The builtins the compiler must leave alone for these to be reached are in
// `compile_options.h`, with every other compile option (ADR 0094).

} // namespace luaug::script
