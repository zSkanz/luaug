// The SDL3 GPU backend: the v1 default (ADR 0005).
//
// The seam was shaped to sit close to SDL_GPU so this adapter stays thin, and
// mostly it is. Two places it is not, and both are the adapter absorbing a cost
// so callers do not pay it:
//
//   Passes. SDL_GPU has mutually exclusive render, compute and copy passes;
//   the seam has one command list. Uploading opens a copy pass lazily and
//   beginRenderPass closes it, so "upload, then draw" works the way it reads.
//
//   Staging. `upload` takes bytes. The transfer buffer, the map, the copy pass
//   and the release all happen here, because a caller that had to know about
//   them would be writing SDL_GPU code through a wrapper.

#include "luaug/core/log.h"
#include "luaug/core/text_key.h"
#include "luaug/platform/sdl_interop.h"
#include "luaug/rhi/backends.h"
#include "luaug/rhi/sdlgpu_interop.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gpu.h>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

#include "sdlgpu_enums.h"

namespace luaug::rhi {
namespace {

using sdlgpu::bytesPerPixel;
using sdlgpu::fromSdl;
using sdlgpu::fromSdlShaderFormats;
using sdlgpu::toSdl;

// Ids are the slot index plus one, and a destroyed slot is never reused. That
// costs a pointer per dead resource for the life of the device and buys the
// property that a stale handle resolves to null rather than to whatever
// resource landed in the recycled slot -- a silently wrong texture is far worse
// to debug than a missing one.
template <class T>
[[nodiscard]] u32 addSlot(std::vector<T>& table, T value)
{
    table.push_back(value);
    return static_cast<u32>(table.size());
}

template <class T>
[[nodiscard]] T* slot(std::vector<T>& table, u32 id) noexcept
{
    return (id != 0 && id <= table.size()) ? &table[id - 1] : nullptr;
}

struct TextureEntry
{
    SDL_GPUTexture* texture = nullptr;
    TextureFormat format = TextureFormat::Undefined;
    u32 width = 0;
    u32 height = 0;
    // Swapchain textures belong to SDL and must not be released. Their slot is
    // reused every frame, which is also what keeps the table from growing once
    // per frame forever.
    bool owned = true;
};

class SdlGpuDevice;

class SdlGpuCmdList final : public ICmdList
{
public:
    explicit SdlGpuCmdList(SdlGpuDevice& device) noexcept : device_(device) {}

    void begin(SDL_GPUCommandBuffer* buffer) noexcept { buffer_ = buffer; }
    [[nodiscard]] SDL_GPUCommandBuffer* buffer() const noexcept { return buffer_; }
    [[nodiscard]] SDL_GPURenderPass* renderPass() const noexcept { return renderPass_; }

    // Closes whatever pass is open. Called before a pass of a different kind
    // starts and before submit, so no caller has to track pass state.
    void endOpenPass() noexcept;

    void beginRenderPass(const RenderPassDesc& desc) override;
    void endRenderPass() override;

    void setPipeline(PipelineHandle pipeline) override;
    void setViewport(const Viewport& viewport) override;
    void setScissor(const Rect& scissor) override;

    void bindVertexBuffers(u32 firstSlot, std::span<const BufferHandle> buffers) override;
    void bindIndexBuffer(BufferHandle buffer, IndexType type) override;

    void bindUniforms(ShaderStage stage, u32 slotIndex, std::span<const std::byte> data) override;
    void bindTextures(ShaderStage stage, u32 firstSlot, std::span<const TextureBinding> bindings) override;

    void draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) override;
    void drawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance) override;

    void upload(BufferHandle buffer, std::span<const std::byte> data, u32 offsetBytes) override;
    void uploadTexture(TextureHandle texture, std::span<const std::byte> data, u32 mipLevel) override;

    void pushDebugGroup(std::string_view name) override;
    void popDebugGroup() override;

private:
    [[nodiscard]] SDL_GPUCopyPass* ensureCopyPass() noexcept;

    SdlGpuDevice& device_;
    SDL_GPUCommandBuffer* buffer_ = nullptr;
    SDL_GPURenderPass* renderPass_ = nullptr;
    SDL_GPUCopyPass* copyPass_ = nullptr;
};

class SdlGpuDevice final : public IDevice
{
public:
    SdlGpuDevice(SDL_GPUDevice* device, ShaderFormat shaderFormat) noexcept
        : device_(device), shaderFormat_(shaderFormat), cmdList_(*this)
    {}

    ~SdlGpuDevice() override
    {
        if (device_ == nullptr)
            return;

        SDL_WaitForGPUIdle(device_);

        for (SDL_GPUGraphicsPipeline* pipeline : pipelines_)
            if (pipeline != nullptr)
                SDL_ReleaseGPUGraphicsPipeline(device_, pipeline);
        for (SDL_GPUShader* shader : shaders_)
            if (shader != nullptr)
                SDL_ReleaseGPUShader(device_, shader);
        for (SDL_GPUSampler* sampler : samplers_)
            if (sampler != nullptr)
                SDL_ReleaseGPUSampler(device_, sampler);
        for (const TextureEntry& entry : textures_)
            if (entry.texture != nullptr && entry.owned)
                SDL_ReleaseGPUTexture(device_, entry.texture);
        for (SDL_GPUBuffer* buffer : buffers_)
            if (buffer != nullptr)
                SDL_ReleaseGPUBuffer(device_, buffer);

        for (const ClaimedWindow& claimed : windows_)
            SDL_ReleaseWindowFromGPUDevice(device_, claimed.window);

        SDL_DestroyGPUDevice(device_);
    }

    [[nodiscard]] BackendId backend() const noexcept override { return BackendId::SdlGpu; }

    [[nodiscard]] Capabilities caps() const noexcept override
    {
        Capabilities caps;
        caps.shaderFormat = shaderFormat_;
        caps.maxTextureSize = 16384;
        caps.rendersPixels = true;
        return caps;
    }

    [[nodiscard]] bool claimWindow(platform::Window& window) override
    {
        SDL_Window* native = platform::nativeWindow(window);
        if (!SDL_ClaimWindowForGPUDevice(device_, native))
            return false;

        // One texture slot per window, rewritten each acquire. Handing out a
        // fresh handle per frame would grow the table forever.
        const u32 id = addSlot(textures_, TextureEntry{.owned = false});
        windows_.push_back({native, id});
        return true;
    }

    void releaseWindow(platform::Window& window) override
    {
        SDL_Window* native = platform::nativeWindow(window);
        for (usize i = 0; i < windows_.size(); ++i) {
            if (windows_[i].window != native)
                continue;

            if (TextureEntry* entry = slot(textures_, windows_[i].textureId); entry != nullptr)
                *entry = TextureEntry{.owned = false};

            SDL_ReleaseWindowFromGPUDevice(device_, native);
            windows_.erase(windows_.begin() + static_cast<isize>(i));
            return;
        }
    }

    [[nodiscard]] BufferHandle createBuffer(const BufferDesc& desc) override
    {
        const SDL_GPUBufferCreateInfo info{
            .usage = toSdl(desc.usage),
            .size = desc.sizeBytes,
            .props = 0,
        };
        SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(device_, &info);
        if (buffer == nullptr)
            return {};

        if (!desc.debugName.empty())
            SDL_SetGPUBufferName(device_, buffer, std::string(desc.debugName).c_str());

        return {addSlot(buffers_, buffer)};
    }

    [[nodiscard]] TextureHandle createTexture(const TextureDesc& desc) override
    {
        const SDL_GPUTextureCreateInfo info{
            .type = SDL_GPU_TEXTURETYPE_2D,
            .format = toSdl(desc.format),
            .usage = toSdl(desc.usage),
            .width = desc.width,
            .height = desc.height,
            .layer_count_or_depth = desc.layers,
            .num_levels = desc.mipLevels,
            .sample_count = SDL_GPU_SAMPLECOUNT_1,
            .props = 0,
        };
        SDL_GPUTexture* texture = SDL_CreateGPUTexture(device_, &info);
        if (texture == nullptr)
            return {};

        if (!desc.debugName.empty())
            SDL_SetGPUTextureName(device_, texture, std::string(desc.debugName).c_str());

        return {addSlot(textures_, TextureEntry{
                                       .texture = texture,
                                       .format = desc.format,
                                       .width = desc.width,
                                       .height = desc.height,
                                       .owned = true,
                                   })};
    }

    [[nodiscard]] SamplerHandle createSampler(const SamplerDesc& desc) override
    {
        const SDL_GPUSamplerCreateInfo info{
            .min_filter = toSdl(desc.minFilter),
            .mag_filter = toSdl(desc.magFilter),
            .mipmap_mode = toSdl(desc.mipmapMode),
            .address_mode_u = toSdl(desc.addressU),
            .address_mode_v = toSdl(desc.addressV),
            .address_mode_w = toSdl(desc.addressW),
            .mip_lod_bias = 0.0f,
            .max_anisotropy = 0.0f,
            .compare_op = SDL_GPU_COMPAREOP_NEVER,
            .min_lod = 0.0f,
            .max_lod = 1000.0f,
            .enable_anisotropy = false,
            .enable_compare = false,
            .padding1 = 0,
            .padding2 = 0,
            .props = 0,
        };
        SDL_GPUSampler* sampler = SDL_CreateGPUSampler(device_, &info);
        return sampler != nullptr ? SamplerHandle{addSlot(samplers_, sampler)} : SamplerHandle{};
    }

    [[nodiscard]] ShaderHandle createShader(const ShaderDesc& desc) override
    {
        const std::string entryPoint(desc.entryPoint);
        const SDL_GPUShaderCreateInfo info{
            .code_size = desc.code.size(),
            .code = reinterpret_cast<const Uint8*>(desc.code.data()),
            .entrypoint = entryPoint.c_str(),
            .format = toSdl(desc.format),
            .stage = toSdl(desc.stage),
            .num_samplers = desc.samplerCount,
            .num_storage_textures = 0,
            .num_storage_buffers = 0,
            .num_uniform_buffers = desc.uniformBufferCount,
            .props = 0,
        };
        SDL_GPUShader* shader = SDL_CreateGPUShader(device_, &info);
        return shader != nullptr ? ShaderHandle{addSlot(shaders_, shader)} : ShaderHandle{};
    }

    [[nodiscard]] PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc& desc) override;

    void destroy(BufferHandle handle) override
    {
        if (SDL_GPUBuffer** entry = slot(buffers_, handle.id); entry != nullptr && *entry != nullptr) {
            SDL_ReleaseGPUBuffer(device_, *entry);
            *entry = nullptr;
        }
    }

    void destroy(TextureHandle handle) override
    {
        if (TextureEntry* entry = slot(textures_, handle.id); entry != nullptr && entry->texture != nullptr) {
            if (entry->owned)
                SDL_ReleaseGPUTexture(device_, entry->texture);
            *entry = TextureEntry{};
        }
    }

    void destroy(SamplerHandle handle) override
    {
        if (SDL_GPUSampler** entry = slot(samplers_, handle.id); entry != nullptr && *entry != nullptr) {
            SDL_ReleaseGPUSampler(device_, *entry);
            *entry = nullptr;
        }
    }

    void destroy(ShaderHandle handle) override
    {
        if (SDL_GPUShader** entry = slot(shaders_, handle.id); entry != nullptr && *entry != nullptr) {
            SDL_ReleaseGPUShader(device_, *entry);
            *entry = nullptr;
        }
    }

    void destroy(PipelineHandle handle) override
    {
        if (SDL_GPUGraphicsPipeline** entry = slot(pipelines_, handle.id); entry != nullptr && *entry != nullptr) {
            SDL_ReleaseGPUGraphicsPipeline(device_, *entry);
            *entry = nullptr;
        }
    }

    [[nodiscard]] ICmdList* beginFrame() override
    {
        cmdList_.begin(SDL_AcquireGPUCommandBuffer(device_));
        return &cmdList_;
    }

    [[nodiscard]] Swapchain acquireSwapchain(platform::Window& window) override;

    void submitAndPresent() override
    {
        if (cmdList_.buffer() == nullptr)
            return;

        cmdList_.endOpenPass();
        SDL_SubmitGPUCommandBuffer(cmdList_.buffer());
        cmdList_.begin(nullptr);
    }

    void waitIdle() override { SDL_WaitForGPUIdle(device_); }

    [[nodiscard]] bool readTexture(TextureHandle texture, std::span<std::byte> out) override;

    // Used by the command list, which lives inside this file, and by the
    // interop accessors at the bottom of it.
    [[nodiscard]] SDL_GPUDevice* handle() const noexcept { return device_; }
    [[nodiscard]] SDL_GPUCommandBuffer* commandBuffer() const noexcept { return cmdList_.buffer(); }
    [[nodiscard]] SDL_GPURenderPass* renderPass() const noexcept { return cmdList_.renderPass(); }
    [[nodiscard]] SDL_GPUBuffer* buffer(BufferHandle handle) noexcept
    {
        SDL_GPUBuffer** entry = slot(buffers_, handle.id);
        return entry != nullptr ? *entry : nullptr;
    }
    [[nodiscard]] TextureEntry* texture(TextureHandle handle) noexcept { return slot(textures_, handle.id); }
    [[nodiscard]] SDL_GPUSampler* sampler(SamplerHandle handle) noexcept
    {
        SDL_GPUSampler** entry = slot(samplers_, handle.id);
        return entry != nullptr ? *entry : nullptr;
    }
    [[nodiscard]] SDL_GPUShader* shader(ShaderHandle handle) noexcept
    {
        SDL_GPUShader** entry = slot(shaders_, handle.id);
        return entry != nullptr ? *entry : nullptr;
    }
    [[nodiscard]] SDL_GPUGraphicsPipeline* pipeline(PipelineHandle handle) noexcept
    {
        SDL_GPUGraphicsPipeline** entry = slot(pipelines_, handle.id);
        return entry != nullptr ? *entry : nullptr;
    }

private:
    using isize = std::ptrdiff_t;

    struct ClaimedWindow
    {
        SDL_Window* window = nullptr;
        u32 textureId = 0;
    };

    SDL_GPUDevice* device_ = nullptr;
    ShaderFormat shaderFormat_ = ShaderFormat::Unknown;
    SdlGpuCmdList cmdList_;

    std::vector<SDL_GPUBuffer*> buffers_;
    std::vector<TextureEntry> textures_;
    std::vector<SDL_GPUSampler*> samplers_;
    std::vector<SDL_GPUShader*> shaders_;
    std::vector<SDL_GPUGraphicsPipeline*> pipelines_;
    std::vector<ClaimedWindow> windows_;
};

PipelineHandle SdlGpuDevice::createGraphicsPipeline(const GraphicsPipelineDesc& desc)
{
    std::vector<SDL_GPUVertexBufferDescription> vertexBuffers;
    vertexBuffers.reserve(desc.vertexBuffers.size());
    for (const VertexBufferLayout& layout : desc.vertexBuffers) {
        vertexBuffers.push_back({
            .slot = layout.slot,
            .pitch = layout.strideBytes,
            .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
            .instance_step_rate = 0,
        });
    }

    std::vector<SDL_GPUVertexAttribute> attributes;
    attributes.reserve(desc.vertexAttributes.size());
    for (const VertexAttribute& attribute : desc.vertexAttributes) {
        attributes.push_back({
            .location = attribute.location,
            .buffer_slot = attribute.bufferSlot,
            .format = toSdl(attribute.format),
            .offset = attribute.offsetBytes,
        });
    }

    std::vector<SDL_GPUColorTargetDescription> colorTargets;
    colorTargets.reserve(desc.colorTargets.size());
    for (const ColorTargetDesc& target : desc.colorTargets) {
        colorTargets.push_back({
            .format = toSdl(target.format),
            .blend_state =
                {
                    .src_color_blendfactor = toSdl(target.blend.srcColor),
                    .dst_color_blendfactor = toSdl(target.blend.dstColor),
                    .color_blend_op = toSdl(target.blend.colorOp),
                    .src_alpha_blendfactor = toSdl(target.blend.srcAlpha),
                    .dst_alpha_blendfactor = toSdl(target.blend.dstAlpha),
                    .alpha_blend_op = toSdl(target.blend.alphaOp),
                    .color_write_mask = 0xF,
                    .enable_blend = target.blend.enabled,
                    .enable_color_write_mask = false,
                    .padding1 = 0,
                    .padding2 = 0,
                },
        });
    }

    SDL_GPUGraphicsPipelineCreateInfo info{};
    // Null shaders reach SDL, which rejects the pipeline and says why. Checking
    // here first would turn a described failure into a silent empty handle.
    info.vertex_shader = shader(desc.vertexShader);
    info.fragment_shader = shader(desc.fragmentShader);
    info.vertex_input_state = {
        .vertex_buffer_descriptions = vertexBuffers.data(),
        .num_vertex_buffers = static_cast<Uint32>(vertexBuffers.size()),
        .vertex_attributes = attributes.data(),
        .num_vertex_attributes = static_cast<Uint32>(attributes.size()),
    };
    info.primitive_type = toSdl(desc.primitive);
    info.rasterizer_state.fill_mode = toSdl(desc.rasterizer.fillMode);
    info.rasterizer_state.cull_mode = toSdl(desc.rasterizer.cullMode);
    info.rasterizer_state.front_face = toSdl(desc.rasterizer.frontFace);
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.depth_stencil_state.compare_op = toSdl(desc.depthStencil.depthCompare);
    info.depth_stencil_state.enable_depth_test = desc.depthStencil.depthTest;
    info.depth_stencil_state.enable_depth_write = desc.depthStencil.depthWrite;
    info.target_info = {
        .color_target_descriptions = colorTargets.data(),
        .num_color_targets = static_cast<Uint32>(colorTargets.size()),
        .depth_stencil_format = toSdl(desc.depthStencilFormat),
        .has_depth_stencil_target = desc.depthStencilFormat != TextureFormat::Undefined,
        .padding1 = 0,
        .padding2 = 0,
        .padding3 = 0,
    };

    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device_, &info);
    return pipeline != nullptr ? PipelineHandle{addSlot(pipelines_, pipeline)} : PipelineHandle{};
}

Swapchain SdlGpuDevice::acquireSwapchain(platform::Window& window)
{
    SDL_GPUCommandBuffer* buffer = cmdList_.buffer();
    if (buffer == nullptr)
        return {};

    SDL_Window* native = platform::nativeWindow(window);
    u32 textureId = 0;
    for (const ClaimedWindow& claimed : windows_) {
        if (claimed.window == native) {
            textureId = claimed.textureId;
            break;
        }
    }
    if (textureId == 0)
        return {};

    SDL_GPUTexture* swapchainTexture = nullptr;
    Uint32 width = 0;
    Uint32 height = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(buffer, native, &swapchainTexture, &width, &height) ||
        swapchainTexture == nullptr) {
        // A minimized or occluded window has no backbuffer this frame. Normal,
        // not an error -- the seam says so and every caller has to handle it.
        return {};
    }

    const TextureFormat format = fromSdl(SDL_GetGPUSwapchainTextureFormat(device_, native));
    TextureEntry* entry = slot(textures_, textureId);
    *entry = TextureEntry{
        .texture = swapchainTexture,
        .format = format,
        .width = width,
        .height = height,
        .owned = false,
    };

    return {.texture = TextureHandle{textureId}, .width = width, .height = height, .format = format};
}

bool SdlGpuDevice::readTexture(TextureHandle texture, std::span<std::byte> out)
{
    const TextureEntry* entry = slot(textures_, texture.id);
    if (entry == nullptr || entry->texture == nullptr)
        return false;

    const u32 pixelSize = bytesPerPixel(entry->format);
    if (pixelSize == 0)
        return false;

    const usize needed = static_cast<usize>(entry->width) * entry->height * pixelSize;
    if (out.size() < needed)
        return false;

    const SDL_GPUTransferBufferCreateInfo transferInfo{
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
        .size = static_cast<Uint32>(needed),
        .props = 0,
    };
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &transferInfo);
    if (transfer == nullptr)
        return false;

    SDL_GPUCommandBuffer* buffer = SDL_AcquireGPUCommandBuffer(device_);
    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(buffer);

    const SDL_GPUTextureRegion region{
        .texture = entry->texture,
        .mip_level = 0,
        .layer = 0,
        .x = 0,
        .y = 0,
        .z = 0,
        .w = entry->width,
        .h = entry->height,
        .d = 1,
    };
    const SDL_GPUTextureTransferInfo destination{
        .transfer_buffer = transfer,
        .offset = 0,
        .pixels_per_row = entry->width,
        .rows_per_layer = entry->height,
    };
    SDL_DownloadFromGPUTexture(pass, &region, &destination);
    SDL_EndGPUCopyPass(pass);

    // Blocking by design: this is the screenshot path, and the caller was told.
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(buffer);
    if (fence == nullptr) {
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
        return false;
    }
    SDL_WaitForGPUFences(device_, true, &fence, 1);
    SDL_ReleaseGPUFence(device_, fence);

    bool ok = false;
    if (const void* mapped = SDL_MapGPUTransferBuffer(device_, transfer, false); mapped != nullptr) {
        std::memcpy(out.data(), mapped, needed);
        SDL_UnmapGPUTransferBuffer(device_, transfer);
        ok = true;
    }

    SDL_ReleaseGPUTransferBuffer(device_, transfer);
    return ok;
}

// --- command list -----------------------------------------------------------

void SdlGpuCmdList::endOpenPass() noexcept
{
    if (renderPass_ != nullptr) {
        SDL_EndGPURenderPass(renderPass_);
        renderPass_ = nullptr;
    }
    if (copyPass_ != nullptr) {
        SDL_EndGPUCopyPass(copyPass_);
        copyPass_ = nullptr;
    }
}

SDL_GPUCopyPass* SdlGpuCmdList::ensureCopyPass() noexcept
{
    if (renderPass_ != nullptr) {
        // Uploading mid-pass is not something the backend can paper over: the
        // GPU is rasterizing into the target right now. Saying so beats a
        // driver-level crash three frames later.
        core::log(core::LogLevel::Error, LUAUG_TR("rhi.err.upload_inside_pass"));
        return nullptr;
    }
    if (copyPass_ == nullptr && buffer_ != nullptr)
        copyPass_ = SDL_BeginGPUCopyPass(buffer_);
    return copyPass_;
}

void SdlGpuCmdList::beginRenderPass(const RenderPassDesc& desc)
{
    endOpenPass();
    if (buffer_ == nullptr)
        return;

    std::vector<SDL_GPUColorTargetInfo> colors;
    colors.reserve(desc.colorAttachments.size());
    for (const ColorAttachment& attachment : desc.colorAttachments) {
        TextureEntry* entry = device_.texture(attachment.texture);
        if (entry == nullptr || entry->texture == nullptr)
            continue;

        SDL_GPUColorTargetInfo info{};
        info.texture = entry->texture;
        info.clear_color = {attachment.clearColor.r, attachment.clearColor.g, attachment.clearColor.b,
                            attachment.clearColor.a};
        info.load_op = toSdl(attachment.loadOp);
        info.store_op = toSdl(attachment.storeOp);
        colors.push_back(info);
    }

    SDL_GPUDepthStencilTargetInfo depth{};
    const TextureEntry* depthEntry = device_.texture(desc.depthStencil.texture);
    const bool hasDepth = depthEntry != nullptr && depthEntry->texture != nullptr;
    if (hasDepth) {
        depth.texture = depthEntry->texture;
        depth.clear_depth = desc.depthStencil.clearDepth;
        depth.load_op = toSdl(desc.depthStencil.loadOp);
        depth.store_op = toSdl(desc.depthStencil.storeOp);
        depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    }

    renderPass_ =
        SDL_BeginGPURenderPass(buffer_, colors.data(), static_cast<Uint32>(colors.size()), hasDepth ? &depth : nullptr);
}

void SdlGpuCmdList::endRenderPass()
{
    if (renderPass_ != nullptr) {
        SDL_EndGPURenderPass(renderPass_);
        renderPass_ = nullptr;
    }
}

void SdlGpuCmdList::setPipeline(PipelineHandle pipeline)
{
    if (renderPass_ == nullptr)
        return;
    if (SDL_GPUGraphicsPipeline* native = device_.pipeline(pipeline); native != nullptr)
        SDL_BindGPUGraphicsPipeline(renderPass_, native);
}

void SdlGpuCmdList::setViewport(const Viewport& viewport)
{
    if (renderPass_ == nullptr)
        return;

    const SDL_GPUViewport native{viewport.x,      viewport.y,        viewport.width,
                                 viewport.height, viewport.minDepth, viewport.maxDepth};
    SDL_SetGPUViewport(renderPass_, &native);
}

void SdlGpuCmdList::setScissor(const Rect& scissor)
{
    if (renderPass_ == nullptr)
        return;

    const SDL_Rect native{scissor.x, scissor.y, scissor.width, scissor.height};
    SDL_SetGPUScissor(renderPass_, &native);
}

void SdlGpuCmdList::bindVertexBuffers(u32 firstSlot, std::span<const BufferHandle> buffers)
{
    if (renderPass_ == nullptr || buffers.empty())
        return;

    std::vector<SDL_GPUBufferBinding> bindings;
    bindings.reserve(buffers.size());
    for (const BufferHandle handle : buffers)
        bindings.push_back({.buffer = device_.buffer(handle), .offset = 0});

    SDL_BindGPUVertexBuffers(renderPass_, firstSlot, bindings.data(), static_cast<Uint32>(bindings.size()));
}

void SdlGpuCmdList::bindIndexBuffer(BufferHandle buffer, IndexType type)
{
    if (renderPass_ == nullptr)
        return;

    const SDL_GPUBufferBinding binding{.buffer = device_.buffer(buffer), .offset = 0};
    SDL_BindGPUIndexBuffer(renderPass_, &binding, toSdl(type));
}

void SdlGpuCmdList::bindUniforms(ShaderStage stage, u32 slotIndex, std::span<const std::byte> data)
{
    if (buffer_ == nullptr)
        return;

    switch (stage) {
    case ShaderStage::Vertex:
        SDL_PushGPUVertexUniformData(buffer_, slotIndex, data.data(), static_cast<Uint32>(data.size()));
        break;
    case ShaderStage::Fragment:
        SDL_PushGPUFragmentUniformData(buffer_, slotIndex, data.data(), static_cast<Uint32>(data.size()));
        break;
    }
}

void SdlGpuCmdList::bindTextures(ShaderStage stage, u32 firstSlot, std::span<const TextureBinding> bindings)
{
    if (renderPass_ == nullptr || bindings.empty())
        return;

    std::vector<SDL_GPUTextureSamplerBinding> native;
    native.reserve(bindings.size());
    for (const TextureBinding& binding : bindings) {
        const TextureEntry* entry = device_.texture(binding.texture);
        native.push_back({
            .texture = entry != nullptr ? entry->texture : nullptr,
            .sampler = device_.sampler(binding.sampler),
        });
    }

    const auto count = static_cast<Uint32>(native.size());
    switch (stage) {
    case ShaderStage::Vertex:
        SDL_BindGPUVertexSamplers(renderPass_, firstSlot, native.data(), count);
        break;
    case ShaderStage::Fragment:
        SDL_BindGPUFragmentSamplers(renderPass_, firstSlot, native.data(), count);
        break;
    }
}

void SdlGpuCmdList::draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance)
{
    if (renderPass_ != nullptr)
        SDL_DrawGPUPrimitives(renderPass_, vertexCount, instanceCount, firstVertex, firstInstance);
}

void SdlGpuCmdList::drawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance)
{
    if (renderPass_ != nullptr)
        SDL_DrawGPUIndexedPrimitives(renderPass_, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void SdlGpuCmdList::upload(BufferHandle buffer, std::span<const std::byte> data, u32 offsetBytes)
{
    SDL_GPUBuffer* target = device_.buffer(buffer);
    if (target == nullptr || data.empty())
        return;

    SDL_GPUCopyPass* pass = ensureCopyPass();
    if (pass == nullptr)
        return;

    SDL_GPUDevice* device = device_.handle();
    const SDL_GPUTransferBufferCreateInfo info{
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = static_cast<Uint32>(data.size()),
        .props = 0,
    };
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &info);
    if (transfer == nullptr)
        return;

    if (void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false); mapped != nullptr) {
        std::memcpy(mapped, data.data(), data.size());
        SDL_UnmapGPUTransferBuffer(device, transfer);

        const SDL_GPUTransferBufferLocation source{.transfer_buffer = transfer, .offset = 0};
        const SDL_GPUBufferRegion destination{
            .buffer = target, .offset = offsetBytes, .size = static_cast<Uint32>(data.size())};
        SDL_UploadToGPUBuffer(pass, &source, &destination, false);
    }

    // SDL frees it "as soon as it is safe to do so", so releasing here does not
    // race the copy -- a per-frame pool is an optimization for whichever
    // workload first needs one.
    SDL_ReleaseGPUTransferBuffer(device, transfer);
}

void SdlGpuCmdList::uploadTexture(TextureHandle texture, std::span<const std::byte> data, u32 mipLevel)
{
    TextureEntry* entry = device_.texture(texture);
    if (entry == nullptr || entry->texture == nullptr || data.empty())
        return;

    SDL_GPUCopyPass* pass = ensureCopyPass();
    if (pass == nullptr)
        return;

    SDL_GPUDevice* device = device_.handle();
    const SDL_GPUTransferBufferCreateInfo info{
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = static_cast<Uint32>(data.size()),
        .props = 0,
    };
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &info);
    if (transfer == nullptr)
        return;

    if (void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false); mapped != nullptr) {
        std::memcpy(mapped, data.data(), data.size());
        SDL_UnmapGPUTransferBuffer(device, transfer);

        // Clamped at one: a 64x64 texture's seventh mip is 1x1, and `>> 6`
        // reaches zero one level later. A zero-sized region uploads nothing and
        // reports nothing, which is the shape of a texture that is fine until
        // somebody looks at its smallest level.
        const u32 mipWidth = std::max(1u, entry->width >> mipLevel);
        const u32 mipHeight = std::max(1u, entry->height >> mipLevel);
        const SDL_GPUTextureTransferInfo source{
            .transfer_buffer = transfer,
            .offset = 0,
            .pixels_per_row = mipWidth,
            .rows_per_layer = mipHeight,
        };
        const SDL_GPUTextureRegion region{
            .texture = entry->texture,
            .mip_level = mipLevel,
            .layer = 0,
            .x = 0,
            .y = 0,
            .z = 0,
            .w = mipWidth,
            .h = mipHeight,
            .d = 1,
        };
        SDL_UploadToGPUTexture(pass, &source, &region, false);
    }

    SDL_ReleaseGPUTransferBuffer(device, transfer);
}

void SdlGpuCmdList::pushDebugGroup(std::string_view name)
{
    if (buffer_ != nullptr)
        SDL_PushGPUDebugGroup(buffer_, std::string(name).c_str());
}

void SdlGpuCmdList::popDebugGroup()
{
    if (buffer_ != nullptr)
        SDL_PopGPUDebugGroup(buffer_);
}

// The one downcast in this file, and the only one that cannot be wrong:
// `backend()` is the question every IDevice answers about itself, so a device
// from another backend leaves here as null instead of as a bad cast.
[[nodiscard]] const SdlGpuDevice* asSdlGpu(const IDevice& device) noexcept
{
    return device.backend() == BackendId::SdlGpu ? static_cast<const SdlGpuDevice*>(&device) : nullptr;
}

} // namespace

// --- interop (luaug/rhi/sdlgpu_interop.h) -----------------------------------
//
// Defined here rather than in a file of their own because the type they reach
// into is this one, and it is deliberately unnameable outside this translation
// unit.

SDL_GPUDevice* nativeDevice(const IDevice& device) noexcept
{
    const SdlGpuDevice* self = asSdlGpu(device);
    return self != nullptr ? self->handle() : nullptr;
}

SDL_GPUCommandBuffer* nativeCommandBuffer(const IDevice& device) noexcept
{
    const SdlGpuDevice* self = asSdlGpu(device);
    return self != nullptr ? self->commandBuffer() : nullptr;
}

SDL_GPURenderPass* nativeRenderPass(const IDevice& device) noexcept
{
    const SdlGpuDevice* self = asSdlGpu(device);
    return self != nullptr ? self->renderPass() : nullptr;
}

DeviceResult createSdlGpuDevice(const DeviceDesc& desc, core::EngineError* outError)
{
    // Ask for every format the engine can supply and report back which one the
    // device chose, rather than demanding one and failing on a machine that
    // would have worked. `caps().shaderFormat` is what the shader pack loads
    // against.
    const SDL_GPUShaderFormat requested =
        desc.shaderFormat == ShaderFormat::Unknown
            ? (SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL)
            : toSdl(desc.shaderFormat);

    SDL_GPUDevice* device = SDL_CreateGPUDevice(requested, desc.debug, nullptr);
    if (device == nullptr) {
        if (outError != nullptr)
            *outError = core::makeError(LUAUG_TR("rhi.err.device_create_failed"), {}, SDL_GetError());
        return nullptr;
    }

    return std::make_unique<SdlGpuDevice>(device, fromSdlShaderFormats(SDL_GetGPUShaderFormats(device)));
}

} // namespace luaug::rhi
