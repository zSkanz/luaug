// The RHI device and command interface (ADR 0005).
//
// One frame:
//
//     ICmdList* cmd = device.beginFrame();
//     const Swapchain target = device.acquireSwapchain(window);
//     if (target.texture.valid()) { cmd->beginRenderPass(...); ...; cmd->endRenderPass(); }
//     device.submitAndPresent();
//
// `acquireSwapchain` comes after `beginFrame` because every backend worth
// supporting acquires the backbuffer through a command buffer, and an
// invalid texture is a normal outcome -- a minimized window, or headless --
// not an error.
#pragma once

#include "luaug/core/error.h"
#include "luaug/rhi/descs.h"
#include "luaug/rhi/types.h"

#include <span>

namespace luaug::platform {
class Window;
}

namespace luaug::rhi {

struct DeviceDesc
{
    BackendId backend = BackendId::SdlGpu;
    // Turns on the backend's validation layer. Costly, and the M1 gate asks for
    // it to be clean, so it is on in dev builds and off in shipping.
    bool debug = false;
    // The blob format the shader pack will supply. A backend that cannot
    // consume it fails to create rather than failing at the first draw.
    ShaderFormat shaderFormat = ShaderFormat::Unknown;
};

// Queried, never assumed. Interfaces here are coarse by design (architecture.md
// §7): a capability exists when a render path gates on it, not before.
struct Capabilities
{
    ShaderFormat shaderFormat = ShaderFormat::Unknown;
    u32 maxTextureSize = 0;
    // False on backends that record or discard rather than rasterize, which is
    // how a caller knows a readback would be meaningless.
    bool rendersPixels = false;
};

struct Swapchain
{
    TextureHandle texture{};
    u32 width = 0;
    u32 height = 0;
    TextureFormat format = TextureFormat::Undefined;
};

// Recorded inside a frame; never outlives the `submitAndPresent` that ends it.
// Not an owning type -- the device owns the recording -- so it is handed out by
// pointer and never stored.
class ICmdList
{
public:
    ICmdList() = default;
    virtual ~ICmdList() = default;

    ICmdList(const ICmdList&) = delete;
    ICmdList& operator=(const ICmdList&) = delete;

    virtual void beginRenderPass(const RenderPassDesc& desc) = 0;
    virtual void endRenderPass() = 0;

    virtual void setPipeline(PipelineHandle pipeline) = 0;
    virtual void setViewport(const Viewport& viewport) = 0;
    virtual void setScissor(const Rect& scissor) = 0;

    virtual void bindVertexBuffers(u32 firstSlot, std::span<const BufferHandle> buffers) = 0;
    virtual void bindIndexBuffer(BufferHandle buffer, IndexType type) = 0;

    // Push-style: the data is copied into the frame's own storage, so the
    // caller's buffer may die on the next line. Sized in bytes rather than
    // typed, because the layout contract lives in the shader.
    virtual void bindUniforms(ShaderStage stage, u32 slot, std::span<const std::byte> data) = 0;
    virtual void bindTextures(ShaderStage stage, u32 firstSlot, std::span<const TextureBinding> bindings) = 0;

    virtual void draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) = 0;
    virtual void drawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset,
                             u32 firstInstance) = 0;

    // Staging is the backend's problem: it owns the transfer buffers, the copy
    // pass, and the decision about whether the write can be seen this frame.
    // Callers upload and draw; that is the whole point of the seam.
    virtual void upload(BufferHandle buffer, std::span<const std::byte> data, u32 offsetBytes) = 0;
    virtual void uploadTexture(TextureHandle texture, std::span<const std::byte> data, u32 mipLevel) = 0;

    // Named regions in a GPU capture. Free in shipping builds, invaluable in
    // every other one.
    virtual void pushDebugGroup(std::string_view name) = 0;
    virtual void popDebugGroup() = 0;
};

class IDevice
{
public:
    IDevice() = default;
    virtual ~IDevice() = default;

    IDevice(const IDevice&) = delete;
    IDevice& operator=(const IDevice&) = delete;

    [[nodiscard]] virtual BackendId backend() const noexcept = 0;
    [[nodiscard]] virtual Capabilities caps() const noexcept = 0;

    // A window must be claimed before its swapchain can be acquired, and
    // released before it is destroyed.
    [[nodiscard]] virtual bool claimWindow(platform::Window& window) = 0;
    virtual void releaseWindow(platform::Window& window) = 0;

    [[nodiscard]] virtual BufferHandle createBuffer(const BufferDesc& desc) = 0;
    [[nodiscard]] virtual TextureHandle createTexture(const TextureDesc& desc) = 0;
    [[nodiscard]] virtual SamplerHandle createSampler(const SamplerDesc& desc) = 0;
    [[nodiscard]] virtual ShaderHandle createShader(const ShaderDesc& desc) = 0;
    [[nodiscard]] virtual PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;

    virtual void destroy(BufferHandle handle) = 0;
    virtual void destroy(TextureHandle handle) = 0;
    virtual void destroy(SamplerHandle handle) = 0;
    virtual void destroy(ShaderHandle handle) = 0;
    virtual void destroy(PipelineHandle handle) = 0;

    [[nodiscard]] virtual ICmdList* beginFrame() = 0;
    [[nodiscard]] virtual Swapchain acquireSwapchain(platform::Window& window) = 0;
    virtual void submitAndPresent() = 0;
    virtual void waitIdle() = 0;

    // Blocking, and slow on purpose: it waits for the GPU to go idle and copies
    // the whole texture back. This is the screenshot and golden-image path
    // (roadmap M1's "agent eyes"), not something a frame may call. Streaming
    // readback would need a fence-based API and has no consumer yet.
    //
    // `out` must be at least width * height * bytesPerPixel of the texture's
    // format; false means the backend cannot read this texture back.
    [[nodiscard]] virtual bool readTexture(TextureHandle texture, std::span<std::byte> out) = 0;
};

} // namespace luaug::rhi
