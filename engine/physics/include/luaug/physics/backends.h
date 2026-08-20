// Backend constructors (ADR 0023), the same shape `rhi/backends.h` has.
//
// No plugin ABI and no self-registering static: consoles and iOS want a fully
// static, dead-strippable binary, and a registry defeats both. A backend the
// build did not compile is not declared here, so selecting it is a compile
// error rather than a null at startup.
#pragma once

#include "luaug/core/error.h"
#include "luaug/physics/physics.h"

#include <memory>

namespace luaug::physics {

using PhysicsResult = std::unique_ptr<IPhysics3D>;

#if LUAUG_PHYSICS_JOLT
// Null on failure, with `outError` filled when it is not null. Initialises
// Jolt's process-wide state (allocator, factory, type registry) on the first
// call and tears it down when the last instance is destroyed -- which is why
// this is a creator rather than a constructor a caller could invoke twice.
[[nodiscard]] PhysicsResult createJoltPhysics(core::EngineError* outError = nullptr);
#endif

} // namespace luaug::physics
