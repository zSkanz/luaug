// The SDL_GPU backend's sanctioned crack in the RHI wall -- the counterpart to
// platform/sdl_interop.h, and there for the same reason.
//
// R17 and ADR 0005 keep every SDL type out of `rhi_api`, and that is not a
// style preference: `rhi_capture` and `rhi_null` compile against those headers
// on a machine that has no SDL on its include path at all, which is the proof
// the seam is real. Widening the seam to hand out a device would end it.
//
// But Dear ImGui's SDL_GPU renderer (ADR 0011) is written against SDL_GPU. It
// needs the device, the command buffer of the frame being recorded, and the
// render pass open on it. Wrapping those would not hide SDL_GPU; it would only
// re-describe it in a second vocabulary that one caller uses.
//
// So the access is explicit, named, and lives OUTSIDE `include/`: this
// directory is on the include path only for a target that links
// `luaug::rhi_sdlgpu`, so reaching it is an edit to a CMakeLists rather than
// something an `#include` can do on its own. Every accessor answers null for a
// device that is not this backend's, which is what makes calling one under
// `--rhi=null` inert rather than undefined.
#pragma once

#include <luaug/rhi/types.h>

#include <SDL3/SDL_gpu.h>

namespace luaug::rhi {

class IDevice;

// Null unless `device` is the SDL_GPU backend. Valid for the device's lifetime.
[[nodiscard]] SDL_GPUDevice* nativeDevice(const IDevice& device) noexcept;

// The command buffer of the frame currently being recorded: null before the
// first `beginFrame()` and again after `submitAndPresent()` until the next one.
[[nodiscard]] SDL_GPUCommandBuffer* nativeCommandBuffer(const IDevice& device) noexcept;

// The render pass currently open on this frame's command list, or null when
// none is. It dies at the matching `endRenderPass()`; nothing may store it.
[[nodiscard]] SDL_GPURenderPass* nativeRenderPass(const IDevice& device) noexcept;

// The texture behind a handle, for the same consumer and on the same grounds as
// everything above it: ImGui's `ImTextureID` **is** an `SDL_GPUTexture*` at the
// pinned version, so an editor showing a rendered world inside a panel has no
// other way to name the thing it is showing. Wrapping it would re-describe
// SDL_GPU in a second vocabulary for one caller, which is what this file exists
// to refuse.
//
// Null for a handle this device does not own and for a device that is not this
// backend's. Valid until the texture is destroyed, which the caller who created
// it controls.
[[nodiscard]] SDL_GPUTexture* nativeTexture(const IDevice& device, TextureHandle handle) noexcept;

} // namespace luaug::rhi
