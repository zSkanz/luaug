// The null backend: accepts every call, renders nothing.
//
// It exists for three jobs, all of which matter more than they sound. It is the
// device a headless logic test uses when it needs an IDevice but no pixels. It
// is the proof that `rhi_api` really is backend-neutral -- this file includes no
// SDL and no graphics API of any kind, and if that ever stops being possible the
// seam has leaked. And it is the control when a rendering bug might be in the
// caller rather than the driver.
//
// It does not validate. A device that rejected a bad handle would quietly
// become a second, weaker validation layer that the real backends do not share,
// and tests would start passing here for reasons that do not transfer.

#include <atomic>

#include "luaug/rhi/backends.h"

namespace luaug::rhi
{
namespace
{

class NullCmdList final : public ICmdList
{
public:
    void beginRenderPass(const RenderPassDesc&) override {}
    void endRenderPass() override {}

    void setPipeline(PipelineHandle) override {}
    void setViewport(const Viewport&) override {}
    void setScissor(const Rect&) override {}

    void bindVertexBuffers(u32, std::span<const BufferHandle>) override {}
    void bindIndexBuffer(BufferHandle, IndexType) override {}

    void bindUniforms(ShaderStage, u32, std::span<const std::byte>) override {}
    void bindTextures(ShaderStage, u32, std::span<const TextureBinding>) override {}

    void draw(u32, u32, u32, u32) override {}
    void drawIndexed(u32, u32, u32, i32, u32) override {}

    void upload(BufferHandle, std::span<const std::byte>, u32) override {}
    void uploadTexture(TextureHandle, std::span<const std::byte>, u32) override {}

    void pushDebugGroup(std::string_view) override {}
    void popDebugGroup() override {}
};

class NullDevice final : public IDevice
{
public:
    [[nodiscard]] BackendId backend() const noexcept override { return BackendId::Null; }

    [[nodiscard]] Capabilities caps() const noexcept override
    {
        Capabilities caps;
        caps.shaderFormat = ShaderFormat::Unknown;
        caps.maxTextureSize = 16384;
        caps.rendersPixels = false;
        return caps;
    }

    [[nodiscard]] bool claimWindow(platform::Window&) override { return true; }
    void releaseWindow(platform::Window&) override {}

    [[nodiscard]] BufferHandle createBuffer(const BufferDesc&) override { return {nextId()}; }
    [[nodiscard]] TextureHandle createTexture(const TextureDesc&) override { return {nextId()}; }
    [[nodiscard]] SamplerHandle createSampler(const SamplerDesc&) override { return {nextId()}; }
    [[nodiscard]] ShaderHandle createShader(const ShaderDesc&) override { return {nextId()}; }
    [[nodiscard]] PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc&) override { return {nextId()}; }

    void destroy(BufferHandle) override {}
    void destroy(TextureHandle) override {}
    void destroy(SamplerHandle) override {}
    void destroy(ShaderHandle) override {}
    void destroy(PipelineHandle) override {}

    [[nodiscard]] ICmdList* beginFrame() override { return &cmdList_; }

    // No backbuffer exists, and saying so is the point: a caller that handles
    // this correctly also handles a minimized window on a real backend.
    [[nodiscard]] Swapchain acquireSwapchain(platform::Window&) override { return {}; }

    void submitAndPresent() override {}
    void waitIdle() override {}

    [[nodiscard]] bool readTexture(TextureHandle, std::span<std::byte>) override { return false; }

private:
    // Ids start at 1 so that a default-constructed handle stays the null one.
    [[nodiscard]] u32 nextId() noexcept { return nextId_++; }

    NullCmdList cmdList_;
    u32 nextId_ = 1;
};

} // namespace

DeviceResult createNullDevice(const DeviceDesc&, core::EngineError*)
{
    return std::make_unique<NullDevice>();
}

} // namespace luaug::rhi
