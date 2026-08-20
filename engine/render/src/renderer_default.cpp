#include "luaug/core/i18n.h"
#include "luaug/core/text_key.h"
#include "luaug/render/renderer.h"
#include "luaug/render/shader_types.h"
#include "luaug/render/shadow.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace luaug::render {
namespace {

using core::f32;
using core::Mat4;
using core::u32;
using core::Vec3;

constexpr rhi::TextureFormat kHdrFormat = rhi::TextureFormat::Rgba16Float;
constexpr rhi::TextureFormat kDepthFormat = rhi::TextureFormat::D32Float;
constexpr rhi::TextureFormat kShadowFormat = rhi::TextureFormat::D32Float;

// An orthographic projection with depth in [0, 1], matching `core::perspective`.
// Written here rather than in `core` because it is a shadow-map fit rather than
// a general camera projection, and a `core::orthographic` with no other caller
// would be a type nobody has checked the conventions of.
[[nodiscard]] Mat4 orthographic(f32 halfExtent, f32 nearZ, f32 farZ) noexcept
{
    Mat4 result;
    result.m[0][0] = 1.0f / halfExtent;
    result.m[1][1] = 1.0f / halfExtent;
    result.m[2][2] = 1.0f / (nearZ - farZ);
    result.m[3][2] = nearZ / (nearZ - farZ);
    return result;
}

// The cofactor matrix of the model transform's rotation-scale block, so a
// non-uniformly scaled mesh lights correctly. The same construction the glTF
// importer uses for baking, and for the same reason: it is det(M) * M^-T, so
// nothing divides by a determinant a degenerate transform makes zero.
[[nodiscard]] Mat4 normalMatrixOf(const Mat4& model) noexcept
{
    const Vec3 a{model.m[0][0], model.m[0][1], model.m[0][2]};
    const Vec3 b{model.m[1][0], model.m[1][1], model.m[1][2]};
    const Vec3 c{model.m[2][0], model.m[2][1], model.m[2][2]};

    const Vec3 cofactor0 = core::cross(b, c);
    const Vec3 cofactor1 = core::cross(c, a);
    const Vec3 cofactor2 = core::cross(a, b);

    Mat4 result;
    result.m[0][0] = cofactor0.x;
    result.m[0][1] = cofactor0.y;
    result.m[0][2] = cofactor0.z;
    result.m[1][0] = cofactor1.x;
    result.m[1][1] = cofactor1.y;
    result.m[1][2] = cofactor1.z;
    result.m[2][0] = cofactor2.x;
    result.m[2][1] = cofactor2.y;
    result.m[2][2] = cofactor2.z;
    return result;
}

[[nodiscard]] std::span<const std::byte> asBytes(const void* data, std::size_t size) noexcept
{
    return std::span<const std::byte>(static_cast<const std::byte*>(data), size);
}

class DefaultRenderer final : public IRenderer
{
public:
    std::optional<core::EngineError> create(rhi::IDevice& device, const ShaderLibrary& shaders,
                                            rhi::TextureFormat colorFormat) override;
    void destroy(rhi::IDevice& device) override;
    void render(rhi::IDevice& device, rhi::ICmdList& cmd, const RenderTarget& target, const RenderWorld& world,
                const MeshCache& meshes) override;
    [[nodiscard]] bool valid() const noexcept override { return valid_; }
    [[nodiscard]] f32 shadowRadius() const noexcept override { return kShadowRadius; }

private:
    [[nodiscard]] std::optional<core::EngineError> ensureTargets(rhi::IDevice& device, u32 width, u32 height);
    // Which draws one call submits. `Shadow` takes every item in the list --
    // a caster outside the view still casts into it -- while the two forward
    // selectors take only what the camera can see, each from its own pass.
    enum class Selection
    {
        Shadow,
        Opaque,
        Transparent,
    };

    void drawGeometry(rhi::ICmdList& cmd, const RenderWorld& world, const MeshCache& meshes, const Mat4& viewProjection,
                      Selection selection);

    bool valid_ = false;
    rhi::TextureFormat colorFormat_ = rhi::TextureFormat::Undefined;

    rhi::PipelineHandle shadowPipeline_{};
    rhi::PipelineHandle pbrPipeline_{};
    // The same shader as `pbrPipeline_`, differing only in state: source-alpha
    // blending and no depth write. A fragment's colour does not depend on which
    // pass drew it; only the order and the state do.
    rhi::PipelineHandle pbrBlendPipeline_{};
    rhi::PipelineHandle skyPipeline_{};
    rhi::PipelineHandle tonemapPipeline_{};

    rhi::ShaderHandle shaders_[8]{};
    core::usize shaderCount_ = 0;

    rhi::TextureHandle hdr_{};
    rhi::TextureHandle depth_{};
    rhi::TextureHandle shadowMap_{};
    // 1x1 stand-ins for a material that has no map. `textureFlags` are
    // multipliers rather than branches, so the shader samples every slot
    // whatever the flag says -- and an unbound descriptor read is not a black
    // pixel, it is whatever the backend last left in that slot.
    rhi::TextureHandle whitePixel_{};
    rhi::TextureHandle flatNormalPixel_{};
    rhi::TextureHandle blackPixel_{};
    rhi::SamplerHandle linearSampler_{};
    rhi::SamplerHandle shadowSampler_{};

    // The size the offscreen targets were built for. A window resize rebuilds
    // them rather than stretching, because a stretched HDR target is a bug that
    // looks like a driver problem.
    u32 width_ = 0;
    u32 height_ = 0;
    bool defaultsUploaded_ = false;
};

} // namespace

Mat4 sunViewProjection(Vec3 sunDirection, core::DVec3 origin) noexcept
{
    const Vec3 direction = core::normalize(sunDirection);
    // A sun exactly overhead makes the obvious up vector parallel to the view,
    // which produces a NaN basis. `lookAt` already falls back to the identity
    // there, and picking the alternate up here is cheaper than a scene that
    // flickers when the clock passes noon.
    const Vec3 up = std::fabs(direction.y) > 0.99f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 1.0f, 0.0f};
    const Vec3 eye = direction * (kShadowDepth * 0.5f);
    Mat4 view = core::lookAt(eye, Vec3{}, up);

    // In f64, because `origin` is a world coordinate and an open world's are
    // large (ADR 0014). What comes out is under half a texel and fits an f32
    // with room to spare, which is the whole reason the rounding happens here
    // rather than in a shader.
    const core::f64 lightX = static_cast<core::f64>(view.m[0][0]) * origin.x +
                             static_cast<core::f64>(view.m[1][0]) * origin.y +
                             static_cast<core::f64>(view.m[2][0]) * origin.z;
    const core::f64 lightY = static_cast<core::f64>(view.m[0][1]) * origin.x +
                             static_cast<core::f64>(view.m[1][1]) * origin.y +
                             static_cast<core::f64>(view.m[2][1]) * origin.z;

    const auto texel = static_cast<core::f64>(kShadowTexel);
    const core::f64 residualX = lightX - std::round(lightX / texel) * texel;
    const core::f64 residualY = lightY - std::round(lightY / texel) * texel;

    view.m[3][0] += static_cast<f32>(residualX);
    view.m[3][1] += static_cast<f32>(residualY);

    return orthographic(kShadowExtent, 0.1f, kShadowDepth) * view;
}

std::optional<core::EngineError> DefaultRenderer::create(rhi::IDevice& device, const ShaderLibrary& shaders,
                                                         rhi::TextureFormat colorFormat)
{
    colorFormat_ = colorFormat;

    core::EngineError error;
    const auto load = [&](std::string_view name, rhi::ShaderStage stage) -> rhi::ShaderHandle {
        const rhi::ShaderHandle handle = shaders.create(device, name, stage, &error);
        if (handle.valid() && shaderCount_ < std::size(shaders_))
            shaders_[shaderCount_++] = handle;
        return handle;
    };

    const rhi::ShaderHandle shadowVertex = load("shadow_depth", rhi::ShaderStage::Vertex);
    const rhi::ShaderHandle shadowFragment = load("shadow_depth", rhi::ShaderStage::Fragment);
    const rhi::ShaderHandle pbrVertex = load("pbr", rhi::ShaderStage::Vertex);
    const rhi::ShaderHandle pbrFragment = load("pbr", rhi::ShaderStage::Fragment);
    const rhi::ShaderHandle skyVertex = load("sky", rhi::ShaderStage::Vertex);
    const rhi::ShaderHandle skyFragment = load("sky", rhi::ShaderStage::Fragment);
    const rhi::ShaderHandle tonemapVertex = load("tonemap", rhi::ShaderStage::Vertex);
    const rhi::ShaderHandle tonemapFragment = load("tonemap", rhi::ShaderStage::Fragment);

    if (!shadowVertex.valid() || !pbrVertex.valid() || !pbrFragment.valid() || !skyVertex.valid() ||
        !skyFragment.valid() || !tonemapVertex.valid() || !tonemapFragment.valid()) {
        destroy(device);
        return error.key.hash != 0 ? error : core::makeError(LUAUG_TR("render.err.shader_format_unknown"));
    }

    // The one static-mesh vertex layout, matching `asset::Vertex` exactly. The
    // 48 there and the 48 here are the same number for the same reason, and the
    // static_assert in model.h is what says so.
    const std::array<rhi::VertexAttribute, 4> attributes{
        rhi::VertexAttribute{.location = 0, .bufferSlot = 0, .format = rhi::VertexFormat::Float3, .offsetBytes = 0},
        rhi::VertexAttribute{.location = 1, .bufferSlot = 0, .format = rhi::VertexFormat::Float3, .offsetBytes = 12},
        rhi::VertexAttribute{.location = 2, .bufferSlot = 0, .format = rhi::VertexFormat::Float4, .offsetBytes = 24},
        rhi::VertexAttribute{.location = 3, .bufferSlot = 0, .format = rhi::VertexFormat::Float2, .offsetBytes = 40},
    };
    const std::array<rhi::VertexBufferLayout, 1> buffers{
        rhi::VertexBufferLayout{.slot = 0, .strideBytes = 48},
    };

    const std::array<rhi::ColorTargetDesc, 1> hdrTarget{rhi::ColorTargetDesc{.format = kHdrFormat}};
    const std::array<rhi::ColorTargetDesc, 1> swapTarget{rhi::ColorTargetDesc{.format = colorFormat}};

    shadowPipeline_ = device.createGraphicsPipeline({
        .vertexShader = shadowVertex,
        .fragmentShader = shadowFragment,
        .vertexBuffers = buffers,
        .vertexAttributes = attributes,
        // Front faces are culled in the shadow pass rather than back faces: it
        // moves the depth samples to the far side of a solid object, which is
        // the cheapest form of peter-panning control and costs nothing here.
        .rasterizer = {.cullMode = rhi::CullMode::Front},
        .depthStencil = {.depthTest = true, .depthWrite = true, .depthCompare = rhi::CompareOp::LessOrEqual},
        .colorTargets = {},
        .depthStencilFormat = kShadowFormat,
        .debugName = "shadow",
    });

    pbrPipeline_ = device.createGraphicsPipeline({
        .vertexShader = pbrVertex,
        .fragmentShader = pbrFragment,
        .vertexBuffers = buffers,
        .vertexAttributes = attributes,
        .rasterizer = {.cullMode = rhi::CullMode::Back},
        .depthStencil = {.depthTest = true, .depthWrite = true, .depthCompare = rhi::CompareOp::LessOrEqual},
        .colorTargets = hdrTarget,
        .depthStencilFormat = kDepthFormat,
        .debugName = "pbr",
    });

    // The blended pass. Depth-tested against what the opaque pass wrote, and
    // depth-write OFF -- two transparent surfaces must both contribute, so
    // neither may occlude the other. Source-alpha over, which is
    // `BlendState`'s own default and is why nothing in `rhi/descs.h` had to
    // change for this (ADR 0037's freeze holds).
    const std::array<rhi::ColorTargetDesc, 1> hdrBlendTarget{rhi::ColorTargetDesc{
        .format = kHdrFormat,
        .blend = {.enabled = true},
    }};
    pbrBlendPipeline_ = device.createGraphicsPipeline({
        .vertexShader = pbrVertex,
        .fragmentShader = pbrFragment,
        .vertexBuffers = buffers,
        .vertexAttributes = attributes,
        .rasterizer = {.cullMode = rhi::CullMode::Back},
        .depthStencil = {.depthTest = true, .depthWrite = false, .depthCompare = rhi::CompareOp::LessOrEqual},
        .colorTargets = hdrBlendTarget,
        .depthStencilFormat = kDepthFormat,
        .debugName = "pbr_blend",
    });

    // The sky writes no depth and tests none: it is drawn first and everything
    // else covers it. Testing would need a depth value for a triangle that has
    // no position in the world.
    skyPipeline_ = device.createGraphicsPipeline({
        .vertexShader = skyVertex,
        .fragmentShader = skyFragment,
        .primitive = rhi::PrimitiveType::TriangleList,
        .rasterizer = {.cullMode = rhi::CullMode::None},
        .colorTargets = hdrTarget,
        .debugName = "sky",
    });

    tonemapPipeline_ = device.createGraphicsPipeline({
        .vertexShader = tonemapVertex,
        .fragmentShader = tonemapFragment,
        .primitive = rhi::PrimitiveType::TriangleList,
        .rasterizer = {.cullMode = rhi::CullMode::None},
        .colorTargets = swapTarget,
        .debugName = "tonemap",
    });

    if (!shadowPipeline_.valid() || !pbrPipeline_.valid() || !pbrBlendPipeline_.valid() || !skyPipeline_.valid() ||
        !tonemapPipeline_.valid()) {
        destroy(device);
        return core::makeError(LUAUG_TR("render.err.pipeline_create_failed"));
    }

    linearSampler_ = device.createSampler({.debugName = "material"});
    shadowSampler_ = device.createSampler({
        .addressU = rhi::AddressMode::ClampToEdge,
        .addressV = rhi::AddressMode::ClampToEdge,
        .addressW = rhi::AddressMode::ClampToEdge,
        .debugName = "shadow",
    });

    // The three defaults, in the values that make a missing map a no-op rather
    // than a change: white multiplies to itself, (0.5, 0.5, 1) is the tangent-
    // space normal pointing straight out, and black adds nothing.
    const auto onePixel = [&](const char* name, core::u8 r, core::u8 g, core::u8 b) -> rhi::TextureHandle {
        const rhi::TextureHandle handle = device.createTexture({
            .format = rhi::TextureFormat::Rgba8Unorm,
            .usage = rhi::TextureUsage::Sampled,
            .width = 1,
            .height = 1,
            .debugName = name,
        });
        (void)r;
        (void)g;
        (void)b;
        // The pixels are written on the first frame rather than here: `create`
        // runs outside a frame and has no command list, and an RHI call for
        // "upload without one" would be a call added on the eve of the interface
        // freeze that `render` already has a way to avoid.
        return handle;
    };
    whitePixel_ = onePixel("default-white", 0xFF, 0xFF, 0xFF);
    flatNormalPixel_ = onePixel("default-normal", 0x80, 0x80, 0xFF);
    blackPixel_ = onePixel("default-black", 0x00, 0x00, 0x00);
    if (!whitePixel_.valid() || !flatNormalPixel_.valid() || !blackPixel_.valid()) {
        destroy(device);
        return core::makeError(LUAUG_TR("render.err.target_create_failed"));
    }

    shadowMap_ = device.createTexture({
        .format = kShadowFormat,
        .usage = rhi::TextureUsage::DepthStencilTarget | rhi::TextureUsage::Sampled,
        .width = kShadowResolution,
        .height = kShadowResolution,
        .debugName = "shadow-map",
    });
    if (!shadowMap_.valid()) {
        destroy(device);
        return core::makeError(LUAUG_TR("render.err.target_create_failed"));
    }

    valid_ = true;
    return std::nullopt;
}

std::optional<core::EngineError> DefaultRenderer::ensureTargets(rhi::IDevice& device, u32 width, u32 height)
{
    if (hdr_.valid() && width == width_ && height == height_)
        return std::nullopt;

    if (hdr_.valid())
        device.destroy(hdr_);
    if (depth_.valid())
        device.destroy(depth_);

    hdr_ = device.createTexture({
        .format = kHdrFormat,
        .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled,
        .width = width,
        .height = height,
        .debugName = "hdr",
    });
    depth_ = device.createTexture({
        .format = kDepthFormat,
        .usage = rhi::TextureUsage::DepthStencilTarget,
        .width = width,
        .height = height,
        .debugName = "depth",
    });
    if (!hdr_.valid() || !depth_.valid())
        return core::makeError(LUAUG_TR("render.err.target_create_failed"));

    width_ = width;
    height_ = height;
    return std::nullopt;
}

void DefaultRenderer::destroy(rhi::IDevice& device)
{
    for (core::usize index = 0; index < shaderCount_; ++index)
        device.destroy(shaders_[index]);
    shaderCount_ = 0;

    for (rhi::PipelineHandle* pipeline :
         {&shadowPipeline_, &pbrPipeline_, &pbrBlendPipeline_, &skyPipeline_, &tonemapPipeline_}) {
        if (pipeline->valid())
            device.destroy(*pipeline);
        *pipeline = {};
    }
    for (rhi::TextureHandle* texture : {&hdr_, &depth_, &shadowMap_, &whitePixel_, &flatNormalPixel_, &blackPixel_}) {
        if (texture->valid())
            device.destroy(*texture);
        *texture = {};
    }
    for (rhi::SamplerHandle* sampler : {&linearSampler_, &shadowSampler_}) {
        if (sampler->valid())
            device.destroy(*sampler);
        *sampler = {};
    }

    width_ = 0;
    height_ = 0;
    defaultsUploaded_ = false;
    valid_ = false;
}

void DefaultRenderer::drawGeometry(rhi::ICmdList& cmd, const RenderWorld& world, const MeshCache& meshes,
                                   const Mat4& viewProjection, Selection selection)
{
    const bool shadowPass = selection == Selection::Shadow;
    // The draws arrive sorted (Decision 7), so this walks them in order and
    // never reorders. Grouping is `extract`'s job and re-deriving it here would
    // be the backend doing work bgfx would have to repeat.
    u32 boundMaterial = 0xFFFFFFFFu;

    for (const DrawItem& draw : world.draws) {
        // The shadow pass takes everything; the forward passes take only what
        // the camera can see. A caster behind the camera still casts into the
        // frame -- including a half-transparent one, which still occludes. The
        // roadmap leaves whether it *should* as a separate question, and this
        // milestone does not open it.
        if (!shadowPass && !draw.inCameraFrustum)
            continue;
        if (selection == Selection::Opaque && draw.transparent)
            continue;
        if (selection == Selection::Transparent && !draw.transparent)
            continue;

        const MeshCache::Resolved* resolved = meshes.resolve(draw.mesh);
        if (resolved == nullptr || draw.section >= resolved->sections.size())
            continue;
        const MeshSection& section = resolved->sections[draw.section];
        if (section.indexCount == 0)
            continue;

        if (shadowPass) {
            const GpuShadowUniforms uniforms{viewProjection, draw.transform};
            cmd.bindUniforms(rhi::ShaderStage::Vertex, 0, asBytes(&uniforms, sizeof(uniforms)));
        }
        else {
            GpuObjectUniforms uniforms{viewProjection, draw.transform, normalMatrixOf(draw.transform)};
            uniforms.instanceAlphaUnused[0] = draw.alpha;
            cmd.bindUniforms(rhi::ShaderStage::Vertex, 0, asBytes(&uniforms, sizeof(uniforms)));

            if (draw.material != boundMaterial && draw.material < world.materials.size()) {
                const RenderMaterial& material = world.materials[draw.material];
                cmd.bindUniforms(rhi::ShaderStage::Fragment, 1, asBytes(&material.uniforms, sizeof(material.uniforms)));

                // Every slot is bound every time, with the shadow map last.
                // A slot left over from the previous material is the classic
                // way one mesh ends up wearing another's texture.
                const auto orDefault = [](rhi::TextureHandle handle, rhi::TextureHandle fallback) {
                    return handle.valid() ? handle : fallback;
                };
                const std::array<rhi::TextureBinding, 5> textures{
                    rhi::TextureBinding{orDefault(material.baseColor, whitePixel_), linearSampler_},
                    rhi::TextureBinding{orDefault(material.normal, flatNormalPixel_), linearSampler_},
                    rhi::TextureBinding{orDefault(material.metallicRoughness, whitePixel_), linearSampler_},
                    rhi::TextureBinding{orDefault(material.emissive, blackPixel_), linearSampler_},
                    rhi::TextureBinding{shadowMap_, shadowSampler_},
                };
                cmd.bindTextures(rhi::ShaderStage::Fragment, 0, textures);
                boundMaterial = draw.material;
            }
        }

        const std::array<rhi::BufferHandle, 1> vertexBuffers{resolved->vertices};
        cmd.bindVertexBuffers(0, vertexBuffers);
        cmd.bindIndexBuffer(resolved->indices, rhi::IndexType::U32);
        cmd.drawIndexed(section.indexCount, 1, resolved->firstIndex + section.firstIndex, resolved->vertexOffset, 0);
    }
}

void DefaultRenderer::render(rhi::IDevice& device, rhi::ICmdList& cmd, const RenderTarget& target,
                             const RenderWorld& world, const MeshCache& meshes)
{
    if (!valid_ || !target.color.valid() || target.width == 0 || target.height == 0)
        return;
    if (ensureTargets(device, target.width, target.height).has_value())
        return;

    if (!defaultsUploaded_) {
        // White multiplies to itself, (0.5, 0.5, 1) is the tangent-space normal
        // pointing straight out, and black adds nothing -- so a material with no
        // map gets a sample that changes nothing rather than an unbound read.
        const std::array<std::byte, 4> white{std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};
        const std::array<std::byte, 4> flat{std::byte{0x80}, std::byte{0x80}, std::byte{0xFF}, std::byte{0xFF}};
        const std::array<std::byte, 4> black{std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}};
        cmd.uploadTexture(whitePixel_, white, 0);
        cmd.uploadTexture(flatNormalPixel_, flat, 0);
        cmd.uploadTexture(blackPixel_, black, 0);
        defaultsUploaded_ = true;
    }

    const Mat4 sunMatrix = sunViewProjection(world.environment.sunDirection, world.camera.origin);

    // --- Shadow pass --------------------------------------------------------
    //
    // Runs even with no draws, so the map is cleared rather than carrying last
    // frame's depths into a frame that samples it.
    cmd.pushDebugGroup("shadow");
    cmd.beginRenderPass({
        .colorAttachments = {},
        .depthStencil = {.texture = shadowMap_, .loadOp = rhi::LoadOp::Clear, .storeOp = rhi::StoreOp::Store},
        .debugName = "shadow",
    });
    cmd.setPipeline(shadowPipeline_);
    cmd.setViewport({.width = static_cast<f32>(kShadowResolution), .height = static_cast<f32>(kShadowResolution)});
    cmd.setScissor(
        {.width = static_cast<core::i32>(kShadowResolution), .height = static_cast<core::i32>(kShadowResolution)});
    if (world.camera.valid)
        drawGeometry(cmd, world, meshes, sunMatrix, Selection::Shadow);
    cmd.endRenderPass();
    cmd.popDebugGroup();

    // --- Sky and forward PBR ------------------------------------------------

    const std::array<rhi::ColorAttachment, 1> hdrAttachment{rhi::ColorAttachment{
        .texture = hdr_,
        .loadOp = rhi::LoadOp::Clear,
        .storeOp = rhi::StoreOp::Store,
    }};

    cmd.pushDebugGroup("forward");
    cmd.beginRenderPass({
        .colorAttachments = hdrAttachment,
        .depthStencil = {.texture = depth_, .loadOp = rhi::LoadOp::Clear, .storeOp = rhi::StoreOp::DontCare},
        .debugName = "forward",
    });
    cmd.setViewport({.width = static_cast<f32>(target.width), .height = static_cast<f32>(target.height)});
    cmd.setScissor({.width = static_cast<core::i32>(target.width), .height = static_cast<core::i32>(target.height)});

    if (world.camera.valid) {
        GpuSkyUniforms sky;
        // The sky shader turns a screen position back into a world direction,
        // so it needs the inverse. Computed once per frame rather than per
        // pixel, which is the only reason it is a uniform rather than a
        // derivation.
        sky.inverseViewProjection = core::inverse(world.camera.viewProjection);
        sky.sunDirectionSize[0] = world.environment.sunDirection.x;
        sky.sunDirectionSize[1] = world.environment.sunDirection.y;
        sky.sunDirectionSize[2] = world.environment.sunDirection.z;
        sky.horizonColor[0] = world.environment.fogColor.r;
        sky.horizonColor[1] = world.environment.fogColor.g;
        sky.horizonColor[2] = world.environment.fogColor.b;
        cmd.setPipeline(skyPipeline_);
        cmd.bindUniforms(rhi::ShaderStage::Fragment, 0, asBytes(&sky, sizeof(sky)));
        cmd.draw(3, 1, 0, 0);

        GpuFrameUniforms frame;
        frame.sunDirectionBrightness[0] = world.environment.sunDirection.x;
        frame.sunDirectionBrightness[1] = world.environment.sunDirection.y;
        frame.sunDirectionBrightness[2] = world.environment.sunDirection.z;
        frame.sunDirectionBrightness[3] = world.environment.sunBrightness;
        frame.ambient[0] = world.environment.ambient.r;
        frame.ambient[1] = world.environment.ambient.g;
        frame.ambient[2] = world.environment.ambient.b;
        frame.fogColor[0] = world.environment.fogColor.r;
        frame.fogColor[1] = world.environment.fogColor.g;
        frame.fogColor[2] = world.environment.fogColor.b;
        frame.fogRange[0] = world.environment.fogStart;
        frame.fogRange[1] = world.environment.fogEnd;
        // Precomputed here so a fragment shader does not divide per pixel, and
        // zero when fog is off -- which makes the fog factor zero without the
        // shader needing to know that `end <= start` means anything.
        frame.fogRange[2] = world.environment.fogEnd > world.environment.fogStart
                                ? 1.0f / (world.environment.fogEnd - world.environment.fogStart)
                                : 0.0f;
        frame.sunViewProjection = sunMatrix;

        const auto lightCount = static_cast<u32>(std::min<core::usize>(world.lights.size(), kMaxForwardLights));
        frame.lightCountUnused[0] = static_cast<f32>(lightCount);
        for (u32 index = 0; index < lightCount; ++index) {
            const RenderLight& light = world.lights[index];
            GpuLight& gpu = frame.lights[index];
            gpu.positionRange[0] = light.position.x;
            gpu.positionRange[1] = light.position.y;
            gpu.positionRange[2] = light.position.z;
            gpu.positionRange[3] = light.range;
            gpu.color[0] = light.color.r * light.brightness;
            gpu.color[1] = light.color.g * light.brightness;
            gpu.color[2] = light.color.b * light.brightness;
            gpu.directionCosAngle[0] = light.direction.x;
            gpu.directionCosAngle[1] = light.direction.y;
            gpu.directionCosAngle[2] = light.direction.z;
            gpu.directionCosAngle[3] = light.kind == LightKind::Spot ? light.spotCosHalfAngle : -1.0f;
        }

        cmd.setPipeline(pbrPipeline_);
        cmd.bindUniforms(rhi::ShaderStage::Fragment, 0, asBytes(&frame, sizeof(frame)));
        drawGeometry(cmd, world, meshes, world.camera.viewProjection, Selection::Opaque);

        // Blended, after the opaque pass has filled depth, back to front. The
        // frame uniforms are still bound -- same block, same slot, same values
        // -- so only the pipeline changes.
        //
        // What this does NOT buy, and the deliverable should not imply
        // otherwise: sorting is per draw, so two transparent surfaces that
        // intersect each other sort wrongly at the pixels where they cross.
        // Order-independent transparency is not on the v1 list.
        cmd.setPipeline(pbrBlendPipeline_);
        drawGeometry(cmd, world, meshes, world.camera.viewProjection, Selection::Transparent);
    }

    cmd.endRenderPass();
    cmd.popDebugGroup();

    // --- Tonemap ------------------------------------------------------------
    //
    // A separate pass rather than writing the swapchain directly from the
    // forward one: the HDR target has to be complete before it can be sampled,
    // and a backend is entitled to enforce that.
    const std::array<rhi::ColorAttachment, 1> finalAttachment{rhi::ColorAttachment{
        .texture = target.color,
        .loadOp = rhi::LoadOp::Clear,
        .storeOp = rhi::StoreOp::Store,
    }};

    cmd.pushDebugGroup("tonemap");
    cmd.beginRenderPass({.colorAttachments = finalAttachment, .debugName = "tonemap"});
    cmd.setPipeline(tonemapPipeline_);
    cmd.setViewport({.width = static_cast<f32>(target.width), .height = static_cast<f32>(target.height)});
    cmd.setScissor({.width = static_cast<core::i32>(target.width), .height = static_cast<core::i32>(target.height)});

    const GpuTonemapUniforms tonemap;
    cmd.bindUniforms(rhi::ShaderStage::Fragment, 0, asBytes(&tonemap, sizeof(tonemap)));
    const std::array<rhi::TextureBinding, 1> hdrBinding{rhi::TextureBinding{hdr_, linearSampler_}};
    cmd.bindTextures(rhi::ShaderStage::Fragment, 0, hdrBinding);
    cmd.draw(3, 1, 0, 0);
    cmd.endRenderPass();
    cmd.popDebugGroup();
}

std::unique_ptr<IRenderer> createDefaultRenderer()
{
    return std::make_unique<DefaultRenderer>();
}

} // namespace luaug::render
