// The capture backend: records what was asked of the GPU, asks nothing of one.
//
// This is the blocking render-regression gate (architecture.md §9). A scenario
// runs a frame through this device, and CI compares the recorded stream against
// a checked-in golden. Because no driver is involved, a GPU vendor's update
// cannot turn the merge queue red -- real images run nightly and non-blocking
// for that reason.
//
// Everything here exists to make two runs of the same frame produce byte-
// identical text:
//
//   - Handle ids are sequential per resource kind. A pointer would make every
//     capture unique, which is why the seam uses integer handles at all.
//   - Floats are quantized to four decimals and written from an integer, so no
//     libc's float formatting can make two identical frames disagree, and a
//     last-bit difference in a matrix multiply does not fail the gate.
//   - Enums are recorded by name. Renumbering an enum should not invalidate
//     every golden, and a name is what a failing diff should show a human.
//   - **Uniform blocks are recorded by content digest, not only by size.** They
//     were recorded by size alone through M4, which is why the milestone's
//     goldens were green while the renderer lit every scene with the struct
//     defaults: every matrix, every light and every material colour a frame
//     carries travels through `bindUniforms`, and the gate was looking at how
//     many bytes went past. A gate that cannot see the values is a gate that
//     can only report that the frame still has the same shape.

#include "luaug/rhi/backends.h"
#include "luaug/rhi/capture.h"

#include <cmath>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

namespace luaug::rhi {
namespace {

constexpr i64 kQuantizeScale = 10000;

std::string quantized(f32 value)
{
    const auto scaled = static_cast<i64>(std::llround(static_cast<f64>(value) * static_cast<f64>(kQuantizeScale)));
    const bool negative = scaled < 0;
    const auto magnitude = static_cast<u64>(negative ? -scaled : scaled);

    std::string out;
    if (negative)
        out += '-';
    out += std::to_string(magnitude / static_cast<u64>(kQuantizeScale));
    out += '.';

    const std::string fraction = std::to_string(magnitude % static_cast<u64>(kQuantizeScale));
    out.append(4 - fraction.size(), '0');
    out += fraction;
    return out;
}

// The 64-bit FNV-1a of a uniform block's *quantized* contents.
//
// Quantized rather than hashed raw, and it is the whole reason this is usable in
// a golden compared byte-for-byte on two tiers: the raw bytes of a matrix
// product differ between MSVC and Clang in the last bit, and a hash turns one
// bit into a completely different number. Rounding each float onto the same
// four-decimal grid the rest of the stream uses gives the digest exactly the
// tolerance `real` already has, and no more.
//
// Every uniform block this engine binds is an array of f32 rows -- the
// `static_assert`s in `render/shader_types.h` are what say so -- so the block is
// read as floats. A tail of fewer than four bytes is folded in raw, which cannot
// happen today and must not silently vanish if it ever does.
[[nodiscard]] std::string uniformDigest(std::span<const std::byte> data)
{
    u64 hash = 1469598103934665603ull;
    const auto fold = [&hash](u64 value) {
        for (int shift = 0; shift < 64; shift += 8) {
            hash ^= (value >> shift) & 0xFFull;
            hash *= 1099511628211ull;
        }
    };

    usize offset = 0;
    for (; offset + sizeof(f32) <= data.size(); offset += sizeof(f32)) {
        f32 value = 0.0f;
        std::memcpy(&value, data.data() + offset, sizeof(value));
        // Through the same `llround` `quantized` uses, so a value that prints
        // one way cannot hash another.
        const auto scaled = static_cast<i64>(std::llround(static_cast<f64>(value) * static_cast<f64>(kQuantizeScale)));
        // -0.0 and 0.0 quantize to the same integer here, which is what makes a
        // sign that no arithmetic can observe unable to fail the gate either.
        fold(static_cast<u64>(scaled));
    }
    for (; offset < data.size(); ++offset)
        fold(static_cast<u64>(std::to_integer<unsigned char>(data[offset])));

    std::string out;
    out.reserve(16);
    for (int shift = 60; shift >= 0; shift -= 4)
        out += "0123456789abcdef"[(hash >> shift) & 0xFull];
    return out;
}

std::string escaped(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == '"' || c == '\\')
            out += '\\';
        // Debug names are developer ASCII; a control character in one is a bug
        // worth seeing rather than smuggling into the golden.
        out += (c >= 0x20) ? c : '?';
    }
    return out;
}

std::string_view name(TextureFormat value)
{
    switch (value) {
    case TextureFormat::Undefined:
        return "Undefined";
    case TextureFormat::R8Unorm:
        return "R8Unorm";
    case TextureFormat::Rg8Unorm:
        return "Rg8Unorm";
    case TextureFormat::Rgba8Unorm:
        return "Rgba8Unorm";
    case TextureFormat::Rgba8UnormSrgb:
        return "Rgba8UnormSrgb";
    case TextureFormat::Bgra8Unorm:
        return "Bgra8Unorm";
    case TextureFormat::Bgra8UnormSrgb:
        return "Bgra8UnormSrgb";
    case TextureFormat::R32Float:
        return "R32Float";
    case TextureFormat::Rgba16Float:
        return "Rgba16Float";
    case TextureFormat::Rgba32Float:
        return "Rgba32Float";
    case TextureFormat::D16Unorm:
        return "D16Unorm";
    case TextureFormat::D24UnormS8Uint:
        return "D24UnormS8Uint";
    case TextureFormat::D32Float:
        return "D32Float";
    case TextureFormat::D32FloatS8Uint:
        return "D32FloatS8Uint";
    case TextureFormat::Bc1RgbaUnorm:
        return "Bc1RgbaUnorm";
    case TextureFormat::Bc3RgbaUnorm:
        return "Bc3RgbaUnorm";
    case TextureFormat::Bc5RgUnorm:
        return "Bc5RgUnorm";
    case TextureFormat::Bc7RgbaUnorm:
        return "Bc7RgbaUnorm";
    }
    return "?";
}

std::string_view name(PrimitiveType value)
{
    switch (value) {
    case PrimitiveType::TriangleList:
        return "TriangleList";
    case PrimitiveType::TriangleStrip:
        return "TriangleStrip";
    case PrimitiveType::LineList:
        return "LineList";
    case PrimitiveType::LineStrip:
        return "LineStrip";
    case PrimitiveType::PointList:
        return "PointList";
    }
    return "?";
}

std::string_view name(IndexType value)
{
    switch (value) {
    case IndexType::U16:
        return "U16";
    case IndexType::U32:
        return "U32";
    }
    return "?";
}

std::string_view name(ShaderStage value)
{
    switch (value) {
    case ShaderStage::Vertex:
        return "Vertex";
    case ShaderStage::Fragment:
        return "Fragment";
    }
    return "?";
}

std::string_view name(LoadOp value)
{
    switch (value) {
    case LoadOp::Load:
        return "Load";
    case LoadOp::Clear:
        return "Clear";
    case LoadOp::DontCare:
        return "DontCare";
    }
    return "?";
}

std::string_view name(StoreOp value)
{
    switch (value) {
    case StoreOp::Store:
        return "Store";
    case StoreOp::DontCare:
        return "DontCare";
    }
    return "?";
}

std::string_view name(FillMode value)
{
    switch (value) {
    case FillMode::Solid:
        return "Solid";
    case FillMode::Wireframe:
        return "Wireframe";
    }
    return "?";
}

std::string_view name(CullMode value)
{
    switch (value) {
    case CullMode::None:
        return "None";
    case CullMode::Front:
        return "Front";
    case CullMode::Back:
        return "Back";
    }
    return "?";
}

// Builds one JSON object per line. Keys appear in call order, which is fixed by
// the code below rather than by any container's iteration order -- the point of
// a canonical format is that nothing about the machine can reorder it.
class Line
{
public:
    explicit Line(std::string_view op)
    {
        text_ = R"({"op":")";
        text_ += op;
        text_ += '"';
    }

    Line& num(std::string_view key, u64 value) { return raw(key, std::to_string(value)); }
    Line& num(std::string_view key, i64 value) { return raw(key, std::to_string(value)); }
    Line& real(std::string_view key, f32 value) { return raw(key, quantized(value)); }

    Line& str(std::string_view key, std::string_view value)
    {
        return raw(key, std::string{'"'} + escaped(value) + '"');
    }

    Line& flag(std::string_view key, bool value) { return raw(key, value ? "true" : "false"); }

    [[nodiscard]] std::string finish()
    {
        text_ += "}\n";
        return std::move(text_);
    }

private:
    Line& raw(std::string_view key, std::string_view value)
    {
        text_ += ",\"";
        text_ += key;
        text_ += "\":";
        text_ += value;
        return *this;
    }

    std::string text_;
};

class CaptureDevice;

class CaptureCmdList final : public ICmdList
{
public:
    explicit CaptureCmdList(std::string& stream) noexcept : stream_(stream) {}

    void beginRenderPass(const RenderPassDesc& desc) override
    {
        stream_ += Line("beginRenderPass")
                       .num("colorCount", static_cast<u64>(desc.colorAttachments.size()))
                       .num("depth", static_cast<u64>(desc.depthStencil.texture.id))
                       .str("name", desc.debugName)
                       .finish();

        for (const ColorAttachment& color : desc.colorAttachments) {
            stream_ += Line("colorAttachment")
                           .num("texture", static_cast<u64>(color.texture.id))
                           .str("load", name(color.loadOp))
                           .str("store", name(color.storeOp))
                           .real("r", color.clearColor.r)
                           .real("g", color.clearColor.g)
                           .real("b", color.clearColor.b)
                           .real("a", color.clearColor.a)
                           .finish();
        }

        if (desc.depthStencil.texture.valid()) {
            stream_ += Line("depthAttachment")
                           .str("load", name(desc.depthStencil.loadOp))
                           .str("store", name(desc.depthStencil.storeOp))
                           .real("clear", desc.depthStencil.clearDepth)
                           .finish();
        }
    }

    void endRenderPass() override { stream_ += Line("endRenderPass").finish(); }

    void setPipeline(PipelineHandle pipeline) override
    {
        stream_ += Line("setPipeline").num("pipeline", static_cast<u64>(pipeline.id)).finish();
    }

    void setViewport(const Viewport& viewport) override
    {
        stream_ += Line("setViewport")
                       .real("x", viewport.x)
                       .real("y", viewport.y)
                       .real("w", viewport.width)
                       .real("h", viewport.height)
                       .real("minDepth", viewport.minDepth)
                       .real("maxDepth", viewport.maxDepth)
                       .finish();
    }

    void setScissor(const Rect& scissor) override
    {
        stream_ += Line("setScissor")
                       .num("x", static_cast<i64>(scissor.x))
                       .num("y", static_cast<i64>(scissor.y))
                       .num("w", static_cast<i64>(scissor.width))
                       .num("h", static_cast<i64>(scissor.height))
                       .finish();
    }

    void bindVertexBuffers(u32 firstSlot, std::span<const BufferHandle> buffers) override
    {
        Line line("bindVertexBuffers");
        line.num("firstSlot", static_cast<u64>(firstSlot)).num("count", static_cast<u64>(buffers.size()));
        stream_ += line.finish();

        for (const BufferHandle buffer : buffers)
            stream_ += Line("vertexBuffer").num("buffer", static_cast<u64>(buffer.id)).finish();
    }

    void bindIndexBuffer(BufferHandle buffer, IndexType type) override
    {
        stream_ += Line("bindIndexBuffer").num("buffer", static_cast<u64>(buffer.id)).str("type", name(type)).finish();
    }

    // Only the byte count, not the contents. A uniform block is usually a
    // matrix, and a golden full of quantized matrices would be unreadable and
    // would fail on the last bit of a projection -- exactly the flakiness this
    // gate exists to avoid. The size still catches a layout change, which is
    // the regression that matters at this level; whether the matrix is right is
    // what the nightly image comparison is for.
    void bindUniforms(ShaderStage stage, u32 slot, std::span<const std::byte> data) override
    {
        stream_ += Line("bindUniforms")
                       .str("stage", name(stage))
                       .num("slot", static_cast<u64>(slot))
                       .num("bytes", static_cast<u64>(data.size()))
                       // What the frame is actually made of. See `uniformDigest`
                       // for why it is a digest of quantized floats rather than
                       // of the bytes.
                       .str("digest", uniformDigest(data))
                       .finish();
    }

    void bindTextures(ShaderStage stage, u32 firstSlot, std::span<const TextureBinding> bindings) override
    {
        stream_ += Line("bindTextures")
                       .str("stage", name(stage))
                       .num("firstSlot", static_cast<u64>(firstSlot))
                       .num("count", static_cast<u64>(bindings.size()))
                       .finish();

        for (const TextureBinding& binding : bindings) {
            stream_ += Line("textureBinding")
                           .num("texture", static_cast<u64>(binding.texture.id))
                           .num("sampler", static_cast<u64>(binding.sampler.id))
                           .finish();
        }
    }

    void draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) override
    {
        stream_ += Line("draw")
                       .num("vertices", static_cast<u64>(vertexCount))
                       .num("instances", static_cast<u64>(instanceCount))
                       .num("firstVertex", static_cast<u64>(firstVertex))
                       .num("firstInstance", static_cast<u64>(firstInstance))
                       .finish();
    }

    void drawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance) override
    {
        stream_ += Line("drawIndexed")
                       .num("indices", static_cast<u64>(indexCount))
                       .num("instances", static_cast<u64>(instanceCount))
                       .num("firstIndex", static_cast<u64>(firstIndex))
                       .num("vertexOffset", static_cast<i64>(vertexOffset))
                       .num("firstInstance", static_cast<u64>(firstInstance))
                       .finish();
    }

    void upload(BufferHandle buffer, std::span<const std::byte> data, u32 offsetBytes) override
    {
        stream_ += Line("upload")
                       .num("buffer", static_cast<u64>(buffer.id))
                       .num("bytes", static_cast<u64>(data.size()))
                       .num("offset", static_cast<u64>(offsetBytes))
                       .finish();
    }

    void uploadTexture(TextureHandle texture, std::span<const std::byte> data, u32 mipLevel) override
    {
        stream_ += Line("uploadTexture")
                       .num("texture", static_cast<u64>(texture.id))
                       .num("bytes", static_cast<u64>(data.size()))
                       .num("mip", static_cast<u64>(mipLevel))
                       .finish();
    }

    void pushDebugGroup(std::string_view groupName) override
    {
        stream_ += Line("pushDebugGroup").str("name", groupName).finish();
    }

    void popDebugGroup() override { stream_ += Line("popDebugGroup").finish(); }

private:
    std::string& stream_;
};

class CaptureDevice final : public IDevice
{
public:
    CaptureDevice() : cmdList_(stream_) {}

    [[nodiscard]] BackendId backend() const noexcept override { return BackendId::Capture; }

    [[nodiscard]] Capabilities caps() const noexcept override
    {
        Capabilities caps;
        // A shader format is reported even though nothing is compiled here, and
        // that is the difference between a useful recording and a decorative
        // one: a caller that skips loading shaders because the device "has no
        // format" also skips creating pipelines and issuing draws, and the
        // capture ends up recording a frame nobody would ever render. This
        // backend exists to record what a real backend would be asked to do, so
        // it has to be askable.
        //
        // SPIR-V because the build always emits it, making the choice arbitrary
        // but fixed -- and a golden must not depend on which formats happened to
        // compile on the machine that recorded it.
        caps.shaderFormat = ShaderFormat::SpirV;
        caps.maxTextureSize = 16384;
        // Still false: no pixels exist, so a readback would be meaningless.
        // That is a separate question from whether shaders can be created, and
        // conflating the two is what made this backend blind to the debug pass.
        caps.rendersPixels = false;
        return caps;
    }

    [[nodiscard]] bool claimWindow(platform::Window&) override { return true; }
    void releaseWindow(platform::Window&) override {}

    [[nodiscard]] BufferHandle createBuffer(const BufferDesc& desc) override
    {
        const BufferHandle handle{nextBuffer_++};
        stream_ += Line("createBuffer")
                       .num("buffer", static_cast<u64>(handle.id))
                       .num("usage", static_cast<u64>(desc.usage))
                       .num("size", static_cast<u64>(desc.sizeBytes))
                       .str("name", desc.debugName)
                       .finish();
        return handle;
    }

    [[nodiscard]] TextureHandle createTexture(const TextureDesc& desc) override
    {
        const TextureHandle handle{nextTexture_++};
        stream_ += Line("createTexture")
                       .num("texture", static_cast<u64>(handle.id))
                       .str("format", name(desc.format))
                       .num("usage", static_cast<u64>(desc.usage))
                       .num("width", static_cast<u64>(desc.width))
                       .num("height", static_cast<u64>(desc.height))
                       .num("layers", static_cast<u64>(desc.layers))
                       .num("mips", static_cast<u64>(desc.mipLevels))
                       .str("name", desc.debugName)
                       .finish();
        return handle;
    }

    [[nodiscard]] SamplerHandle createSampler(const SamplerDesc& desc) override
    {
        const SamplerHandle handle{nextSampler_++};
        stream_ +=
            Line("createSampler").num("sampler", static_cast<u64>(handle.id)).str("name", desc.debugName).finish();
        return handle;
    }

    [[nodiscard]] ShaderHandle createShader(const ShaderDesc& desc) override
    {
        const ShaderHandle handle{nextShader_++};
        stream_ += Line("createShader")
                       .num("shader", static_cast<u64>(handle.id))
                       .str("stage", name(desc.stage))
                       .num("bytes", static_cast<u64>(desc.code.size()))
                       .num("samplers", static_cast<u64>(desc.samplerCount))
                       .num("uniformBuffers", static_cast<u64>(desc.uniformBufferCount))
                       .str("name", desc.debugName)
                       .finish();
        return handle;
    }

    [[nodiscard]] PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc& desc) override
    {
        const PipelineHandle handle{nextPipeline_++};
        stream_ += Line("createGraphicsPipeline")
                       .num("pipeline", static_cast<u64>(handle.id))
                       .num("vertexShader", static_cast<u64>(desc.vertexShader.id))
                       .num("fragmentShader", static_cast<u64>(desc.fragmentShader.id))
                       .str("primitive", name(desc.primitive))
                       .str("fill", name(desc.rasterizer.fillMode))
                       .str("cull", name(desc.rasterizer.cullMode))
                       .flag("depthTest", desc.depthStencil.depthTest)
                       .flag("depthWrite", desc.depthStencil.depthWrite)
                       .num("colorTargets", static_cast<u64>(desc.colorTargets.size()))
                       .str("depthFormat", name(desc.depthStencilFormat))
                       .str("name", desc.debugName)
                       .finish();
        return handle;
    }

    void destroy(BufferHandle handle) override { recordDestroy("buffer", handle.id); }
    void destroy(TextureHandle handle) override { recordDestroy("texture", handle.id); }
    void destroy(SamplerHandle handle) override { recordDestroy("sampler", handle.id); }
    void destroy(ShaderHandle handle) override { recordDestroy("shader", handle.id); }
    void destroy(PipelineHandle handle) override { recordDestroy("pipeline", handle.id); }

    [[nodiscard]] ICmdList* beginFrame() override
    {
        stream_ += Line("beginFrame").num("frame", frame_).finish();
        return &cmdList_;
    }

    // A fixed size rather than the real window's: a golden must not change
    // because someone resized a window, and a scenario that cares about size
    // renders to its own target.
    [[nodiscard]] Swapchain acquireSwapchain(platform::Window&) override
    {
        const TextureHandle handle{nextTexture_++};
        stream_ += Line("acquireSwapchain").num("texture", static_cast<u64>(handle.id)).finish();
        return {.texture = handle, .width = 1280, .height = 720, .format = TextureFormat::Bgra8Unorm};
    }

    void submitAndPresent() override
    {
        stream_ += Line("submitAndPresent").num("frame", frame_).finish();
        ++frame_;
    }

    void waitIdle() override {}

    [[nodiscard]] bool readTexture(TextureHandle, std::span<std::byte>) override { return false; }

    [[nodiscard]] const std::string& stream() const noexcept { return stream_; }

    void reset() noexcept
    {
        stream_.clear();
        frame_ = 0;
    }

private:
    void recordDestroy(std::string_view kind, u32 id)
    {
        stream_ += Line("destroy").str("kind", kind).num("id", static_cast<u64>(id)).finish();
    }

    std::string stream_;
    CaptureCmdList cmdList_;

    // Per-kind counters starting at 1, so an id is stable regardless of what
    // other resource types a scenario happens to create around it.
    u32 nextBuffer_ = 1;
    u32 nextTexture_ = 1;
    u32 nextSampler_ = 1;
    u32 nextShader_ = 1;
    u32 nextPipeline_ = 1;
    u64 frame_ = 0;
};

// `backend()` is the tag that makes this cast safe without RTTI: only
// CaptureDevice ever reports BackendId::Capture.
const CaptureDevice* asCapture(const IDevice& device) noexcept
{
    return device.backend() == BackendId::Capture ? static_cast<const CaptureDevice*>(&device) : nullptr;
}

} // namespace

DeviceResult createCaptureDevice(const DeviceDesc&, core::EngineError*)
{
    return std::make_unique<CaptureDevice>();
}

const std::string& captureStream(const IDevice& device)
{
    static const std::string empty;
    const CaptureDevice* capture = asCapture(device);
    return capture != nullptr ? capture->stream() : empty;
}

void resetCapture(IDevice& device)
{
    if (device.backend() == BackendId::Capture)
        static_cast<CaptureDevice&>(device).reset();
}

} // namespace luaug::rhi
