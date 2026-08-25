#include "luaug/script/stdlib.h"

#include <array>

namespace luaug::script {
namespace {

using N = StdName;

// **Sizes are deduced, never written.** A count beside a list is a second thing
// to keep in step with the list, and the one that is wrong is always the count.
//
// Two things below were got wrong on the first pass and caught by the test that
// reads the VM, which is the argument for having it: `math`'s constants -- `e`,
// `nan`, `phi`, `sqrt2`, `tau` -- are set with `lua_setfield` rather than listed
// in the registration table, so reading the table missed all five; and
// `buffer.readinteger` / `writeinteger` appear in the vendored source inside a
// block this pin does not build, so a source read that stopped at the names
// would have offered two functions no script can call.

// From `lbaselib.cpp`'s registration table, minus what `removeUnsafeGlobals`
// takes off: `getfenv`, `setfenv` and `newproxy` are really there in stock Luau
// and really gone here.
//
// `pairs`, `ipairs`, `pcall` and `xpcall` are registered outside that table --
// they carry a continuation -- and `unpack` is made a global by `ltablib.cpp`
// rather than by the base library. Reading the one table would have missed five
// names, which is why the test reads the VM rather than this file.
constexpr std::array Globals{
    N{"assert", "function"},   N{"error", "function"},        N{"gcinfo", "function"},   N{"getmetatable", "function"},
    N{"ipairs", "function"},   N{"next", "function"},         N{"pairs", "function"},    N{"pcall", "function"},
    N{"rawequal", "function"}, N{"rawget", "function"},       N{"rawlen", "function"},   N{"rawset", "function"},
    N{"select", "function"},   N{"setmetatable", "function"}, N{"tonumber", "function"}, N{"tostring", "function"},
    N{"type", "function"},     N{"typeof", "function"},       N{"unpack", "function"},   N{"xpcall", "function"},
    N{"_G", "table"},          N{"_VERSION", "string"},
};

constexpr std::array Math{
    N{"abs", "function"},
    N{"acos", "function"},
    N{"asin", "function"},
    N{"atan", "function"},
    N{"atan2", "function"},
    N{"ceil", "function"},
    N{"clamp", "function"},
    N{"cos", "function"},
    N{"cosh", "function"},
    N{"deg", "function"},
    N{"e", "number"},
    N{"exp", "function"},
    N{"floor", "function"},
    N{"fmod", "function"},
    N{"frexp", "function"},
    N{"huge", "number"},
    N{"isfinite", "function"},
    N{"isinf", "function"},
    N{"isnan", "function"},
    N{"ldexp", "function"},
    N{"lerp", "function"},
    N{"log", "function"},
    N{"log10", "function"},
    N{"map", "function"},
    N{"max", "function"},
    N{"min", "function"},
    N{"modf", "function"},
    N{"nan", "number"},
    N{"noise", "function"},
    N{"phi", "number"},
    N{"pi", "number"},
    N{"pow", "function"},
    N{"rad", "function"},
    N{"random", "function"},
    N{"randomseed", "function"},
    N{"round", "function"},
    N{"sign", "function"},
    N{"sin", "function"},
    N{"sinh", "function"},
    N{"sqrt", "function"},
    N{"sqrt2", "number"},
    N{"tan", "function"},
    N{"tanh", "function"},
    N{"tau", "number"},
};

constexpr std::array Table{
    N{"clear", "function"},  N{"clone", "function"},   N{"concat", "function"},   N{"create", "function"},
    N{"find", "function"},   N{"foreach", "function"}, N{"foreachi", "function"}, N{"freeze", "function"},
    N{"getn", "function"},   N{"insert", "function"},  N{"isfrozen", "function"}, N{"maxn", "function"},
    N{"move", "function"},   N{"pack", "function"},    N{"remove", "function"},   N{"sort", "function"},
    N{"unpack", "function"},
};

constexpr std::array Str{
    N{"byte", "function"},    N{"char", "function"},  N{"find", "function"},     N{"format", "function"},
    N{"gmatch", "function"},  N{"gsub", "function"},  N{"len", "function"},      N{"lower", "function"},
    N{"match", "function"},   N{"pack", "function"},  N{"packsize", "function"}, N{"rep", "function"},
    N{"reverse", "function"}, N{"split", "function"}, N{"sub", "function"},      N{"unpack", "function"},
    N{"upper", "function"},
};

constexpr std::array Coroutine{
    N{"close", "function"},   N{"create", "function"}, N{"isyieldable", "function"}, N{"resume", "function"},
    N{"running", "function"}, N{"status", "function"}, N{"wrap", "function"},        N{"yield", "function"},
};

constexpr std::array Bit32{
    N{"arshift", "function"}, N{"band", "function"},    N{"bnot", "function"},     N{"bor", "function"},
    N{"btest", "function"},   N{"bxor", "function"},    N{"byteswap", "function"}, N{"countlz", "function"},
    N{"countrz", "function"}, N{"extract", "function"}, N{"lrotate", "function"},  N{"lshift", "function"},
    N{"replace", "function"}, N{"rrotate", "function"}, N{"rshift", "function"},
};

constexpr std::array Utf8{
    N{"char", "function"},  N{"charpattern", "string"}, N{"codepoint", "function"},
    N{"codes", "function"}, N{"len", "function"},       N{"offset", "function"},
};

constexpr std::array Buffer{
    N{"copy", "function"},      N{"create", "function"},   N{"fill", "function"},        N{"fromstring", "function"},
    N{"len", "function"},       N{"readbits", "function"}, N{"readf32", "function"},     N{"readf64", "function"},
    N{"readi16", "function"},   N{"readi32", "function"},  N{"readi8", "function"},      N{"readstring", "function"},
    N{"readu16", "function"},   N{"readu32", "function"},  N{"readu8", "function"},      N{"tostring", "function"},
    N{"writebits", "function"}, N{"writef32", "function"}, N{"writef64", "function"},    N{"writei16", "function"},
    N{"writei32", "function"},  N{"writei8", "function"},  N{"writestring", "function"}, N{"writeu16", "function"},
    N{"writeu32", "function"},  N{"writeu8", "function"},
};

// The one where ADR 0034's rule is worth reading twice: reached off a MODULE, so
// every name is camelCase -- and `Vector3` is this table plus `new`, so
// `Vector3.magnitude` and `vector.magnitude` are the same function.
constexpr std::array Vector{
    N{"abs", "function"},       N{"angle", "function"},     N{"ceil", "function"}, N{"clamp", "function"},
    N{"create", "function"},    N{"cross", "function"},     N{"dot", "function"},  N{"floor", "function"},
    N{"lerp", "function"},      N{"magnitude", "function"}, N{"max", "function"},  N{"min", "function"},
    N{"normalize", "function"}, N{"one", "vector"},         N{"sign", "function"}, N{"zero", "vector"},
};

// **Three, and this is where the list stops being stock Luau's.** `os.difftime`
// is in the VM Luau ships and is taken off by `removeUnsafeGlobals`, so offering
// it would be offering something no script can call.
constexpr std::array Os{
    N{"clock", "function"},
    N{"date", "function"},
    N{"time", "function"},
};

constexpr std::array Debug{
    N{"info", "function"},
    N{"traceback", "function"},
};

constexpr std::array Libraries{
    StdLibrary{"math", Math},           StdLibrary{"table", Table},   StdLibrary{"string", Str},
    StdLibrary{"coroutine", Coroutine}, StdLibrary{"bit32", Bit32},   StdLibrary{"utf8", Utf8},
    StdLibrary{"buffer", Buffer},       StdLibrary{"vector", Vector}, StdLibrary{"os", Os},
    StdLibrary{"debug", Debug},
};

} // namespace

std::span<const StdName> stdGlobals() noexcept
{
    return Globals;
}

std::span<const StdLibrary> stdLibraries() noexcept
{
    return Libraries;
}

} // namespace luaug::script
