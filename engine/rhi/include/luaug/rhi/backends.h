// Backend constructors (ADR 0023).
//
// There is no plugin ABI and no self-registering static: consoles and iOS want
// a fully static, dead-strippable binary, and a registry defeats both. Each
// backend the build compiled in declares its creator here, and `app` owns the
// single hand-written switch that picks one. A backend that is switched off is
// not declared, so choosing it is a compile error rather than a null at
// startup.
#pragma once

#include "luaug/core/error.h"
#include "luaug/rhi/device.h"

#include <memory>

namespace luaug::rhi {

// Null on failure, with `outError` filled when it is not null.
using DeviceResult = std::unique_ptr<IDevice>;

#if LUAUG_RHI_NULL
[[nodiscard]] DeviceResult createNullDevice(const DeviceDesc& desc, core::EngineError* outError = nullptr);
#endif

#if LUAUG_RHI_SDLGPU
[[nodiscard]] DeviceResult createSdlGpuDevice(const DeviceDesc& desc, core::EngineError* outError = nullptr);
#endif

#if LUAUG_RHI_CAPTURE
[[nodiscard]] DeviceResult createCaptureDevice(const DeviceDesc& desc, core::EngineError* outError = nullptr);
#endif

} // namespace luaug::rhi
