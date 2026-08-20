// The one place that knows which backends this build contains (ADR 0023).
//
// No plugin ABI, no self-registering statics: consoles and iOS want a fully
// static, dead-strippable binary, and a registry defeats both while making
// binary-size audits opaque. The entire mechanism is a hand-written switch in
// backends.cpp, and a shipping build compiles exactly one real backend per seam.
#pragma once

#include "luaug/core/error.h"
#include "luaug/rhi/backends.h"

#include <optional>
#include <string_view>

namespace luaug::app {

// Parses `--rhi=<name>`. Empty when the name is unknown OR when the backend
// exists in the seam but was not compiled into this build -- from a user's
// point of view those are the same failure, and the error names what is
// actually available.
[[nodiscard]] std::optional<rhi::BackendId> parseBackendId(std::string_view name);

// The names this build will accept, for the error message and `--help`.
[[nodiscard]] std::string_view availableBackendNames();

[[nodiscard]] std::string_view backendName(rhi::BackendId backend);

[[nodiscard]] rhi::DeviceResult createDevice(const rhi::DeviceDesc& desc, core::EngineError* outError = nullptr);

} // namespace luaug::app
