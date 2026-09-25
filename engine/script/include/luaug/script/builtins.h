#pragma once

// The builtins written in Luau: `Collector` and `Promise` (ADR 0094).
//
// Compiled at build time by `luaug_luauembed` from `runtime/builtins/`, with
// the runtime's own compile options, and embedded here as bytecode -- so every
// profile runs them, the shipping one with no compiler included.

#include <cstddef>
#include <span>

struct lua_State;

namespace luaug::script {

struct BuiltinChunk
{
    // The global it installs as.
    const char* global;
    const unsigned char* bytecode;
    std::size_t size;
};

// In install order. Generated.
[[nodiscard]] std::span<const BuiltinChunk> builtinChunks() noexcept;

// Runs each chunk and installs what it returns as its global. Before the
// sandbox closes, and after every global the builtins use (`task`, `Signal`,
// `typeof`): they are Luau running against the engine's own surface. A chunk
// that fails is logged and left out, and the rest still install.
void installBuiltins(lua_State* L);

} // namespace luaug::script
