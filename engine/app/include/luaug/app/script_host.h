// Minimal Luau host: create a sandboxed VM, run one script, surface failures
// as structured engine errors.
//
// This lives in `app` for M0 only. architecture.md §2 reserves Luau headers
// for `script` (the module that owns bindings, the VM pool and the scheduler),
// and M2 moves this there as that module is built. Putting an `engine/script`
// module here now would mean inventing the seams M2 is meant to design, so the
// host keeps the VM until there is a real script module to own it.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "luaug/core/error.h"

struct lua_State;

namespace luaug::app
{

class ScriptHost
{
public:
    ScriptHost();
    ~ScriptHost();

    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    // Compiles and runs `source` on its own sandboxed thread.
    // Returns nullopt on success, or the structured error that ended it.
    [[nodiscard]] std::optional<core::EngineError> run(std::string_view source, std::string_view chunkName);
    [[nodiscard]] std::optional<core::EngineError> runFile(const std::filesystem::path& path);

    [[nodiscard]] lua_State* state() const noexcept { return state_; }

private:
    lua_State* state_ = nullptr;
};

} // namespace luaug::app
