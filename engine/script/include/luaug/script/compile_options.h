#pragma once

// **The options every Luau chunk the engine runs is compiled with** -- at run
// time (`modules.cpp`, `runtime.cpp`) and at build time (`luaug_luauembed`,
// which compiles the builtins written in Luau, ADR 0094). One definition, so a
// builtin compiled on the build machine and a script compiled in the editor
// cannot come out of two different compilers.
//
// Only where the compiler is: a shipping build has none (ADR 0002), and this
// header needs its `luacode.h`.

#include <luacode.h>

namespace luaug::script {

// The library functions the compiler must not turn into a fastcall, because a
// fastcall reaches the C runtime directly and never sees the functions
// `installDeterministicMath` put in the table (R10). Null-terminated.
inline constexpr const char* DeterministicBuiltins[] = {
    "math.sin",  "math.cos",  "math.tan", "math.asin", "math.acos",  "math.atan", "math.atan2",   "math.sinh",
    "math.cosh", "math.tanh", "math.exp", "math.log",  "math.log10", "math.pow",  "vector.angle", nullptr,
};

inline void configureCompileOptions(lua_CompileOptions& options) noexcept
{
    options.optimizationLevel = 2;
    options.debugLevel = 2;
    // ADR 0013: these three are what make `Vector3.new(1, 2, 3)` a constant
    // rather than a call, and what makes a dynamic one a fastcall. The type
    // name is a checker hint only -- the folding comes from the library and
    // constructor names alone.
    options.vectorLib = "Vector3";
    options.vectorCtor = "new";
    options.vectorType = "Vector3";
    options.disabledBuiltins = DeterministicBuiltins;
}

} // namespace luaug::script
