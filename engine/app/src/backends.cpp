#include "luaug/app/backends.h"

#include "luaug/core/text_key.h"

namespace luaug::app {
namespace {

// Compile-time membership. A backend that is off is not merely unavailable at
// runtime -- its creator is not declared, so naming it here would not link.
// That is the property ADR 0023 is buying: what the binary contains is decided
// by the build, and it is visible in one file.
constexpr bool kHasSdlGpu =
#if LUAUG_RHI_SDLGPU
    true;
#else
    false;
#endif

constexpr bool kHasCapture =
#if LUAUG_RHI_CAPTURE
    true;
#else
    false;
#endif

constexpr bool kHasNull =
#if LUAUG_RHI_NULL
    true;
#else
    false;
#endif

} // namespace

std::optional<rhi::BackendId> parseBackendId(std::string_view name)
{
    if (name == "sdlgpu" && kHasSdlGpu)
        return rhi::BackendId::SdlGpu;
    if (name == "capture" && kHasCapture)
        return rhi::BackendId::Capture;
    if (name == "null" && kHasNull)
        return rhi::BackendId::Null;
    return std::nullopt;
}

std::string_view availableBackendNames()
{
    // Spelled out per combination rather than assembled at runtime: this is a
    // compile-time fact, and building it into a string would allocate to
    // describe something that cannot change.
    if constexpr (kHasSdlGpu && kHasCapture && kHasNull)
        return "sdlgpu, capture, null";
    else if constexpr (kHasSdlGpu && kHasCapture)
        return "sdlgpu, capture";
    else if constexpr (kHasSdlGpu && kHasNull)
        return "sdlgpu, null";
    else if constexpr (kHasCapture && kHasNull)
        return "capture, null";
    else if constexpr (kHasSdlGpu)
        return "sdlgpu";
    else if constexpr (kHasCapture)
        return "capture";
    else if constexpr (kHasNull)
        return "null";
    else
        return "(none)";
}

std::string_view backendName(rhi::BackendId backend)
{
    switch (backend) {
    case rhi::BackendId::SdlGpu:
        return "sdlgpu";
    case rhi::BackendId::Capture:
        return "capture";
    case rhi::BackendId::Null:
        return "null";
    }
    return "unknown";
}

rhi::DeviceResult createDevice(const rhi::DeviceDesc& desc, core::EngineError* outError)
{
    switch (desc.backend) {
    case rhi::BackendId::SdlGpu:
#if LUAUG_RHI_SDLGPU
        return rhi::createSdlGpuDevice(desc, outError);
#else
        break;
#endif
    case rhi::BackendId::Capture:
#if LUAUG_RHI_CAPTURE
        return rhi::createCaptureDevice(desc, outError);
#else
        break;
#endif
    case rhi::BackendId::Null:
#if LUAUG_RHI_NULL
        return rhi::createNullDevice(desc, outError);
#else
        break;
#endif
    }

    if (outError != nullptr)
        *outError = core::makeError(LUAUG_TR("rhi.err.backend_not_compiled"));
    return nullptr;
}

} // namespace luaug::app
