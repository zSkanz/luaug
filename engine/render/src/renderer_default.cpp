#include "luaug/core/i18n.h"
#include "luaug/core/text_key.h"
#include "luaug/render/environment.h"
#include "luaug/render/renderer.h"
#include "luaug/render/shader_types.h"
#include "luaug/render/shadow.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace luaug::render {
namespace {

using core::f32;
using core::Mat4;
using core::u32;
using core::Vec3;

constexpr rhi::TextureFormat kHdrFormat = rhi::TextureFormat::Rgba16Float;
constexpr rhi::TextureFormat kDepthFormat = rhi::TextureFormat::D32Float;
constexpr rhi::TextureFormat kShadowFormat = rhi::TextureFormat::D32Float;

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

[[nodiscard]] u32 environmentLevelSize(u32 level) noexcept
{
    const u32 size = kEnvironmentBaseSize >> level;
    return size > 0 ? size : 1u;
}

// The prefiltered environment's freshness, and the policy that keeps a
// day/night cycle from putting a CPU prefilter in every frame.
//
// **A frame uploads exactly one level, always, whether or not anything
// changed.** That is not the cheapest arrangement and it is the correct one:
// `clock_differential` requires two frames that differ only in `ClockTime` to
// issue the same NUMBER of commands, because what a clock changes is the values
// a frame carries and not its shape. An upload that appeared only when the sky
// had moved made a frame's shape depend on its history, and the gate said so
// the first time it ran. One level per frame is about thirty kilobytes averaged
// over the chain.
//
// Baking is what is conditional. A change marks every level dirty and each is
// rebaked when the cursor reaches it, finest first from wherever the cursor
// happens to be -- so a reflection sharpens over a few frames rather than
// stalling one.
//
// The FIRST bake is whole, because an environment that arrived one level per
// frame would light the first six frames of every run differently from the
// seventh, and that is a difference a golden recorded at frame two and a
// screenshot taken at frame thirty would disagree about.
struct EnvironmentCache
{
    SkyParams target{};
    bool everBaked = false;
    bool dirty[kEnvironmentMipCount]{};
    // Which level this frame uploads. Advances every frame regardless of
    // anything, which is the whole point.
    u32 cursor = 0;
    Vec3 irradiance[9]{};
    // Kept resident rather than rebuilt into one scratch buffer: the upload
    // happens every frame and the bake does not, so the pixels have to outlive
    // the bake that made them. About 171 KiB for the whole chain.
    std::vector<core::u16> levels[kEnvironmentMipCount];
    std::vector<core::u16> lut;

    // True when `params` differs from what the chain was baked from by enough
    // to be worth the work. The sun moving is the common case; the horizon
    // colour changing is a script writing `Lighting.FogColor` and is rare, so it
    // is tested exactly rather than with a threshold.
    [[nodiscard]] bool stale(const SkyParams& params) const noexcept
    {
        if (!everBaked)
            return true;
        if (core::dot(params.sunDirection, target.sunDirection) < kEnvironmentRebuildCosine)
            return true;
        return !(params.horizonColor == target.horizonColor && params.zenithColor == target.zenithColor &&
                 params.sunColor == target.sunColor);
    }
};

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

    // `skinned` is the pipeline a draw with a joint palette switches to. The
    // caller sets the static one and this switches at most once per pass,
    // because `extract` sorts by pipeline -- so a world with no skinned draw
    // makes no extra call at all, which is what keeps M4's goldens byte-exact.
    // A cascade's own bounds, so the shadow pass draws into cascade zero only
    // what cascade zero covers. Without it every cascade draws every caster and
    // four cascades cost four times the submission -- which is the exact cost
    // this milestone is also spending instancing to remove.
    struct CullSphere
    {
        Vec3 centre;
        f32 radius = 0.0f;
    };

    void drawGeometry(rhi::ICmdList& cmd, const RenderWorld& world, const MeshCache& meshes, const Mat4& viewProjection,
                      rhi::PipelineHandle staticPipeline, rhi::PipelineHandle skinnedPipeline, Selection selection,
                      const CullSphere* cull = nullptr);

    // Bakes whatever the environment owes this frame and uploads it. Called
    // once per frame, inside the frame, because `uploadTexture` needs a command
    // list and `create` has none.
    void updateEnvironment(rhi::ICmdList& cmd, const SkyParams& params);

    bool valid_ = false;
    rhi::TextureFormat colorFormat_ = rhi::TextureFormat::Undefined;

    rhi::PipelineHandle shadowPipeline_{};
    rhi::PipelineHandle pbrPipeline_{};
    // The skinned variants. Same shading, same state; what differs is the vertex
    // input layout and one more uniform block, both of which are pipeline
    // description rather than code (M6 brief, Decision 11).
    rhi::PipelineHandle shadowSkinnedPipeline_{};
    rhi::PipelineHandle pbrSkinnedPipeline_{};
    rhi::PipelineHandle pbrSkinnedBlendPipeline_{};
    // The same shader as `pbrPipeline_`, differing only in state: source-alpha
    // blending and no depth write. A fragment's colour does not depend on which
    // pass drew it; only the order and the state do.
    rhi::PipelineHandle pbrBlendPipeline_{};
    rhi::PipelineHandle skyPipeline_{};
    rhi::PipelineHandle tonemapPipeline_{};

    rhi::ShaderHandle shaders_[12]{};
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
    // The prefiltered environment and the split-sum BRDF table: image-based
    // lighting's two textures (ADR 0038, environment.h). Octahedral rather than
    // a cubemap because the frozen RHI has no cube type, and CPU-prefiltered
    // because it has no compute -- ADR 0043 records what that bought.
    rhi::TextureHandle environmentMap_{};
    rhi::TextureHandle brdfLut_{};
    rhi::SamplerHandle linearSampler_{};
    rhi::SamplerHandle shadowSampler_{};
    // Trilinear and clamped: the mip index IS the roughness, so filtering
    // between levels is the interpolation the split sum asks for rather than a
    // quality setting.
    rhi::SamplerHandle environmentSampler_{};
    EnvironmentCache environment_;

    // The size the offscreen targets were built for. A window resize rebuilds
    // them rather than stretching, because a stretched HDR target is a bug that
    // looks like a driver problem.
    u32 width_ = 0;
    u32 height_ = 0;
    bool defaultsUploaded_ = false;
    bool brdfUploaded_ = false;
};

} // namespace

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
    const rhi::ShaderHandle pbrSkinnedVertex = load("pbr_skinned", rhi::ShaderStage::Vertex);
    const rhi::ShaderHandle pbrSkinnedFragment = load("pbr_skinned", rhi::ShaderStage::Fragment);
    const rhi::ShaderHandle shadowSkinnedVertex = load("shadow_skinned", rhi::ShaderStage::Vertex);
    const rhi::ShaderHandle shadowSkinnedFragment = load("shadow_skinned", rhi::ShaderStage::Fragment);
    const rhi::ShaderHandle skyVertex = load("sky", rhi::ShaderStage::Vertex);
    const rhi::ShaderHandle skyFragment = load("sky", rhi::ShaderStage::Fragment);
    const rhi::ShaderHandle tonemapVertex = load("tonemap", rhi::ShaderStage::Vertex);
    const rhi::ShaderHandle tonemapFragment = load("tonemap", rhi::ShaderStage::Fragment);

    if (!shadowSkinnedVertex.valid() || !shadowSkinnedFragment.valid() || !pbrSkinnedVertex.valid() ||
        !pbrSkinnedFragment.valid())
        return core::makeError(LUAUG_TR("render.err.shader_format_unknown"));
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

    // The skinned layouts: the same stream at slot 0 plus `asset::SkinVertex` at
    // slot 1. The joint indices are `Float4` and not an integer format because
    // `rhi::VertexFormat` has none and that enumeration is frozen (ADR 0037);
    // model.h records what it costs.
    const std::array<rhi::VertexBufferLayout, 2> skinnedBuffers{
        rhi::VertexBufferLayout{.slot = 0, .strideBytes = 48},
        rhi::VertexBufferLayout{.slot = 1, .strideBytes = 32},
    };
    const std::array<rhi::VertexAttribute, 6> skinnedAttributes{
        rhi::VertexAttribute{.location = 0, .bufferSlot = 0, .format = rhi::VertexFormat::Float3, .offsetBytes = 0},
        rhi::VertexAttribute{.location = 1, .bufferSlot = 0, .format = rhi::VertexFormat::Float3, .offsetBytes = 12},
        rhi::VertexAttribute{.location = 2, .bufferSlot = 0, .format = rhi::VertexFormat::Float4, .offsetBytes = 24},
        rhi::VertexAttribute{.location = 3, .bufferSlot = 0, .format = rhi::VertexFormat::Float2, .offsetBytes = 40},
        rhi::VertexAttribute{.location = 4, .bufferSlot = 1, .format = rhi::VertexFormat::Float4, .offsetBytes = 0},
        rhi::VertexAttribute{.location = 5, .bufferSlot = 1, .format = rhi::VertexFormat::Float4, .offsetBytes = 16},
    };
    // The shadow pass reads position and the skin stream and nothing else, so
    // its joint and weight attributes are at locations 1 and 2 rather than 4 and
    // 5 -- the numbers are the shader's declaration order, not the vertex's.
    const std::array<rhi::VertexAttribute, 3> shadowSkinnedAttributes{
        rhi::VertexAttribute{.location = 0, .bufferSlot = 0, .format = rhi::VertexFormat::Float3, .offsetBytes = 0},
        rhi::VertexAttribute{.location = 1, .bufferSlot = 1, .format = rhi::VertexFormat::Float4, .offsetBytes = 0},
        rhi::VertexAttribute{.location = 2, .bufferSlot = 1, .format = rhi::VertexFormat::Float4, .offsetBytes = 16},
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

    shadowSkinnedPipeline_ = device.createGraphicsPipeline({
        .vertexShader = shadowSkinnedVertex,
        .fragmentShader = shadowSkinnedFragment,
        .vertexBuffers = skinnedBuffers,
        .vertexAttributes = shadowSkinnedAttributes,
        .rasterizer = {.cullMode = rhi::CullMode::Front},
        .depthStencil = {.depthTest = true, .depthWrite = true, .depthCompare = rhi::CompareOp::LessOrEqual},
        .colorTargets = {},
        .depthStencilFormat = kShadowFormat,
        .debugName = "shadow_skinned",
    });

    pbrSkinnedPipeline_ = device.createGraphicsPipeline({
        .vertexShader = pbrSkinnedVertex,
        .fragmentShader = pbrSkinnedFragment,
        .vertexBuffers = skinnedBuffers,
        .vertexAttributes = skinnedAttributes,
        .rasterizer = {.cullMode = rhi::CullMode::Back},
        .depthStencil = {.depthTest = true, .depthWrite = true, .depthCompare = rhi::CompareOp::LessOrEqual},
        .colorTargets = hdrTarget,
        .depthStencilFormat = kDepthFormat,
        .debugName = "pbr_skinned",
    });

    pbrSkinnedBlendPipeline_ = device.createGraphicsPipeline({
        .vertexShader = pbrSkinnedVertex,
        .fragmentShader = pbrSkinnedFragment,
        .vertexBuffers = skinnedBuffers,
        .vertexAttributes = skinnedAttributes,
        .rasterizer = {.cullMode = rhi::CullMode::Back},
        .depthStencil = {.depthTest = true, .depthWrite = false, .depthCompare = rhi::CompareOp::LessOrEqual},
        .colorTargets = hdrBlendTarget,
        .depthStencilFormat = kDepthFormat,
        .debugName = "pbr_skinned_blend",
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
        !tonemapPipeline_.valid() || !shadowSkinnedPipeline_.valid() || !pbrSkinnedPipeline_.valid() ||
        !pbrSkinnedBlendPipeline_.valid()) {
        destroy(device);
        return core::makeError(LUAUG_TR("render.err.pipeline_create_failed"));
    }

    linearSampler_ = device.createSampler({.debugName = "material"});
    environmentSampler_ = device.createSampler({
        .addressU = rhi::AddressMode::ClampToEdge,
        .addressV = rhi::AddressMode::ClampToEdge,
        .addressW = rhi::AddressMode::ClampToEdge,
        .debugName = "environment",
    });
    // Point, not linear: `Gather` fetches the four texels itself and the shader
    // does the bilinear comparison, which is what makes a hardware comparison
    // sampler unnecessary (ADR 0043).
    shadowSampler_ = device.createSampler({
        .minFilter = rhi::Filter::Nearest,
        .magFilter = rhi::Filter::Nearest,
        .mipmapMode = rhi::MipmapMode::Nearest,
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
        .width = kShadowAtlasResolution,
        .height = kShadowAtlasResolution,
        .debugName = "shadow-atlas",
    });
    if (!shadowMap_.valid()) {
        destroy(device);
        return core::makeError(LUAUG_TR("render.err.target_create_failed"));
    }

    // The environment's mip chain is the roughness chain, so `mipLevels` is the
    // number of roughness steps and not a filtering nicety. Written entirely by
    // `uploadTexture`, which is the one frozen call that takes a level.
    environmentMap_ = device.createTexture({
        .format = kHdrFormat,
        .usage = rhi::TextureUsage::Sampled,
        .width = kEnvironmentBaseSize,
        .height = kEnvironmentBaseSize,
        .mipLevels = kEnvironmentMipCount,
        .debugName = "environment",
    });
    brdfLut_ = device.createTexture({
        .format = kHdrFormat,
        .usage = rhi::TextureUsage::Sampled,
        .width = kBrdfLutSize,
        .height = kBrdfLutSize,
        .debugName = "brdf-lut",
    });
    if (!environmentMap_.valid() || !brdfLut_.valid()) {
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
         {&shadowPipeline_, &pbrPipeline_, &pbrBlendPipeline_, &skyPipeline_, &tonemapPipeline_,
          &shadowSkinnedPipeline_, &pbrSkinnedPipeline_, &pbrSkinnedBlendPipeline_}) {
        if (pipeline->valid())
            device.destroy(*pipeline);
        *pipeline = {};
    }
    for (rhi::TextureHandle* texture :
         {&hdr_, &depth_, &shadowMap_, &whitePixel_, &flatNormalPixel_, &blackPixel_, &environmentMap_, &brdfLut_}) {
        if (texture->valid())
            device.destroy(*texture);
        *texture = {};
    }
    for (rhi::SamplerHandle* sampler : {&linearSampler_, &shadowSampler_, &environmentSampler_}) {
        if (sampler->valid())
            device.destroy(*sampler);
        *sampler = {};
    }

    width_ = 0;
    height_ = 0;
    defaultsUploaded_ = false;
    brdfUploaded_ = false;
    environment_ = EnvironmentCache{};
    valid_ = false;
}

void DefaultRenderer::updateEnvironment(rhi::ICmdList& cmd, const SkyParams& params)
{
    const auto uploadLevel = [&](u32 level) {
        const std::vector<core::u16>& pixels = environment_.levels[level];
        if (!pixels.empty())
            cmd.uploadTexture(environmentMap_, asBytes(pixels.data(), pixels.size() * sizeof(core::u16)), level);
    };
    const auto bakeLevel = [&](u32 level) {
        const u32 size = environmentLevelSize(level);
        const f32 roughness =
            kEnvironmentMipCount > 1 ? static_cast<f32>(level) / static_cast<f32>(kEnvironmentMipCount - 1) : 0.0f;
        environment_.levels[level].assign(static_cast<core::usize>(size) * size * 4, 0);
        bakeEnvironmentLevel(environment_.target, size, roughness, environmentSampleCount(level),
                             environment_.levels[level]);
        environment_.dirty[level] = false;
        // Irradiance rides on level zero because it is the same projection of
        // the same sky, and because a diffuse ambient lagging the specular one
        // would read as a colour shift on every matte surface while the sun
        // moves.
        if (level == 0)
            bakeIrradianceSh(environment_.target, environment_.irradiance);
    };

    if (!brdfUploaded_) {
        // Independent of the environment -- it is the BRDF integrated against
        // itself -- so it is baked once and never again.
        environment_.lut.assign(static_cast<core::usize>(kBrdfLutSize) * kBrdfLutSize * 4, 0);
        bakeBrdfLut(kBrdfLutSize, environment_.lut);
        cmd.uploadTexture(brdfLut_, asBytes(environment_.lut.data(), environment_.lut.size() * sizeof(core::u16)), 0);
        brdfUploaded_ = true;
    }

    if (environment_.stale(params)) {
        environment_.target = params;
        for (bool& level : environment_.dirty)
            level = true;
    }

    if (!environment_.everBaked) {
        for (u32 level = 0; level < kEnvironmentMipCount; ++level) {
            bakeLevel(level);
            uploadLevel(level);
        }
        environment_.everBaked = true;
        environment_.cursor = 0;
        return;
    }

    const u32 level = environment_.cursor;
    environment_.cursor = (environment_.cursor + 1) % kEnvironmentMipCount;
    if (environment_.dirty[level])
        bakeLevel(level);
    uploadLevel(level);
}

void DefaultRenderer::drawGeometry(rhi::ICmdList& cmd, const RenderWorld& world, const MeshCache& meshes,
                                   const Mat4& viewProjection, rhi::PipelineHandle staticPipeline,
                                   rhi::PipelineHandle skinnedPipeline, Selection selection, const CullSphere* cull)
{
    const bool shadowPass = selection == Selection::Shadow;
    bool onSkinnedPipeline = false;

    // Pixels per world unit at one metre, from the projection itself rather
    // than from a field-of-view nobody stored: `projection[1][1]` IS
    // `1 / tan(fovY / 2)` for `core::perspective`, so half the target height
    // times that is the number a metre subtends at a metre away.
    //
    // Taken from the CAMERA even in the shadow pass, deliberately. A level
    // chosen by how big a thing looks to the LIGHT would change with the sun,
    // so a shadow could be cast by different geometry than the object drawn --
    // which is a shadow that does not match its caster. Choosing once, from the
    // camera, keeps the two the same mesh.
    const f32 pixelsPerUnit =
        world.camera.valid && height_ > 0 ? 0.5f * static_cast<f32>(height_) * world.camera.projection.m[1][1] : 0.0f;
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
        if (cull != nullptr) {
            const Vec3 offset = draw.boundsCenter - cull->centre;
            const f32 reach = cull->radius + draw.boundsRadius;
            if (core::dot(offset, offset) > reach * reach)
                continue;
        }

        const MeshCache::Resolved* resolved = meshes.resolve(draw.mesh);
        if (resolved == nullptr || resolved->lods.empty())
            continue;

        const MeshLodRange& level = resolved->lods[selectMeshLod(*resolved, draw.transform, pixelsPerUnit)];
        if (draw.section >= level.sectionCount || level.firstSection + draw.section >= resolved->sections.size())
            continue;
        const MeshSection& section = resolved->sections[level.firstSection + draw.section];
        if (section.indexCount == 0)
            continue;

        // A draw is skinned only if it has a palette AND the mesh carries the
        // second stream. The two can disagree for exactly one frame -- a mesh
        // whose file failed to load has no skin buffer while a track already
        // exists -- and drawing that through the skinned pipeline would read an
        // unbound vertex buffer.
        const bool skinnedDraw = draw.boneCount > 0 && resolved->skin.valid() && skinnedPipeline.valid();
        if (skinnedDraw != onSkinnedPipeline) {
            cmd.setPipeline(skinnedDraw ? skinnedPipeline : staticPipeline);
            onSkinnedPipeline = skinnedDraw;
        }

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
                const std::array<rhi::TextureBinding, 7> textures{
                    rhi::TextureBinding{orDefault(material.baseColor, whitePixel_), linearSampler_},
                    rhi::TextureBinding{orDefault(material.normal, flatNormalPixel_), linearSampler_},
                    rhi::TextureBinding{orDefault(material.metallicRoughness, whitePixel_), linearSampler_},
                    rhi::TextureBinding{orDefault(material.emissive, blackPixel_), linearSampler_},
                    rhi::TextureBinding{shadowMap_, shadowSampler_},
                    rhi::TextureBinding{environmentMap_, environmentSampler_},
                    rhi::TextureBinding{brdfLut_, environmentSampler_},
                };
                cmd.bindTextures(rhi::ShaderStage::Fragment, 0, textures);
                boundMaterial = draw.material;
            }
        }

        if (skinnedDraw) {
            // The palette, one upload per draw. Per draw rather than per
            // skeleton because `bindUniforms` is the only route the frozen RHI
            // gives (ADR 0037) and it is scoped to the next draw -- which is why
            // `kMaxSkinJoints` is a budget worth keeping small.
            GpuSkinUniforms skin;
            const u32 count = draw.boneCount < kMaxSkinJoints ? draw.boneCount : kMaxSkinJoints;
            for (u32 index = 0; index < count; ++index)
                skin.jointMatrices[index] = world.bones[draw.firstBone + index];
            cmd.bindUniforms(rhi::ShaderStage::Vertex, 1, asBytes(&skin, sizeof(skin)));

            const std::array<rhi::BufferHandle, 2> vertexBuffers{resolved->vertices, resolved->skin};
            cmd.bindVertexBuffers(0, vertexBuffers);
        }
        else {
            const std::array<rhi::BufferHandle, 1> vertexBuffers{resolved->vertices};
            cmd.bindVertexBuffers(0, vertexBuffers);
        }
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

    // The sky, resolved once, and the one place its derived colours come from.
    // Both the sky pass and the prefiltered environment read this struct, which
    // is what stops a reflection from disagreeing with what it reflects.
    const SkyParams sky = skyParamsFor(world.environment.sunDirection, world.environment.fogColor);
    updateEnvironment(cmd, sky);

    // The cascade fit, from the camera basis. `inverse(view)` is the camera's own
    // frame; the camera sits at the origin of this space, so only its axes are
    // read out of it.
    const Mat4 cameraFrame = core::inverse(world.camera.view);
    ShadowFit fit;
    fit.sunDirection = world.environment.sunDirection;
    fit.right = Vec3{cameraFrame.m[0][0], cameraFrame.m[0][1], cameraFrame.m[0][2]};
    fit.up = Vec3{cameraFrame.m[1][0], cameraFrame.m[1][1], cameraFrame.m[1][2]};
    // The camera looks down -Z, so its forward is the negated third axis.
    fit.forward = Vec3{-cameraFrame.m[2][0], -cameraFrame.m[2][1], -cameraFrame.m[2][2]};
    fit.tanHalfFovX = world.camera.projection.m[0][0] != 0.0f ? 1.0f / world.camera.projection.m[0][0] : 0.5f;
    fit.tanHalfFovY = world.camera.projection.m[1][1] != 0.0f ? 1.0f / world.camera.projection.m[1][1] : 0.3f;
    fit.nearPlane = world.camera.nearPlane;
    fit.origin = world.camera.origin;
    const ShadowCascades cascades = fitShadowCascades(fit);

    // --- Shadow pass --------------------------------------------------------
    //
    // One pass, four viewports into one 2x2 atlas -- `shadow.h` says why an
    // atlas rather than an array. Runs even with no draws, so the map is cleared
    // rather than carrying last frame's depths into a frame that samples it.
    //
    // Each cascade also culls against its OWN sphere. Without that, four
    // cascades cost four times the submission, which is the exact price the
    // instanced path elsewhere in this milestone exists to remove.
    cmd.pushDebugGroup("shadow");
    cmd.beginRenderPass({
        .colorAttachments = {},
        .depthStencil = {.texture = shadowMap_, .loadOp = rhi::LoadOp::Clear, .storeOp = rhi::StoreOp::Store},
        .debugName = "shadow",
    });
    if (world.camera.valid) {
        f32 splits[kShadowCascadeCount + 1]{};
        shadowSplits(world.camera.nearPlane, kShadowDistance, kShadowSplitLambda, splits);

        for (u32 index = 0; index < kShadowCascadeCount; ++index) {
            const auto tile = static_cast<f32>(kShadowTileResolution);
            const f32 x = static_cast<f32>(index & 1u) * tile;
            const f32 y = static_cast<f32>(index >> 1u) * tile;
            cmd.setPipeline(shadowPipeline_);
            cmd.setViewport({.x = x, .y = y, .width = tile, .height = tile});
            cmd.setScissor({.x = static_cast<core::i32>(x),
                            .y = static_cast<core::i32>(y),
                            .width = static_cast<core::i32>(kShadowTileResolution),
                            .height = static_cast<core::i32>(kShadowTileResolution)});

            // The cascade's sphere, in the same camera-relative space the fit
            // used and the draws are in. The radius is recovered from the texel
            // size rather than returned separately: it is the same number the
            // fit divided by the tile resolution.
            const CullSphere cull{
                fit.forward * ((splits[index] + splits[index + 1]) * 0.5f),
                cascades.texelWorld[index] * 0.5f * tile,
            };
            drawGeometry(cmd, world, meshes, cascades.viewProjection[index], shadowPipeline_, shadowSkinnedPipeline_,
                         Selection::Shadow, &cull);
        }
    }
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
        GpuSkyUniforms skyUniforms;
        // The sky shader turns a screen position back into a world direction,
        // so it needs the inverse. Computed once per frame rather than per
        // pixel, which is the only reason it is a uniform rather than a
        // derivation.
        skyUniforms.inverseViewProjection = core::inverse(world.camera.viewProjection);
        skyUniforms.sunDirectionSize[0] = sky.sunDirection.x;
        skyUniforms.sunDirectionSize[1] = sky.sunDirection.y;
        skyUniforms.sunDirectionSize[2] = sky.sunDirection.z;
        skyUniforms.sunDirectionSize[3] = sky.sunAngularRadius;
        skyUniforms.horizonColor[0] = sky.horizonColor.r;
        skyUniforms.horizonColor[1] = sky.horizonColor.g;
        skyUniforms.horizonColor[2] = sky.horizonColor.b;
        skyUniforms.zenithColor[0] = sky.zenithColor.r;
        skyUniforms.zenithColor[1] = sky.zenithColor.g;
        skyUniforms.zenithColor[2] = sky.zenithColor.b;
        skyUniforms.sunColor[0] = sky.sunColor.r;
        skyUniforms.sunColor[1] = sky.sunColor.g;
        skyUniforms.sunColor[2] = sky.sunColor.b;
        // The disc's brightness relative to the sky around it, scaled by the day
        // factor so a sun below the horizon leaves no disc behind.
        skyUniforms.sunColor[3] = kSunDiscIntensity * sky.dayFactor;
        cmd.setPipeline(skyPipeline_);
        cmd.bindUniforms(rhi::ShaderStage::Fragment, 0, asBytes(&skyUniforms, sizeof(skyUniforms)));
        cmd.draw(3, 1, 0, 0);

        GpuFrameUniforms frame;
        frame.sunDirectionBrightness[0] = world.environment.sunDirection.x;
        frame.sunDirectionBrightness[1] = world.environment.sunDirection.y;
        frame.sunDirectionBrightness[2] = world.environment.sunDirection.z;
        // The day factor is folded in here rather than tested in the shader: a
        // sun below the horizon is a sun that lights nothing, and before M7.5 it
        // went on lighting every upward-facing surface from underneath.
        frame.sunDirectionBrightness[3] = world.environment.sunBrightness * sky.dayFactor;
        frame.sunColorUnused[0] = sky.sunColor.r;
        frame.sunColorUnused[1] = sky.sunColor.g;
        frame.sunColorUnused[2] = sky.sunColor.b;
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
        for (u32 index = 0; index < kShadowCascadeCount; ++index) {
            frame.cascadeViewProjection[index] = cascades.viewProjection[index];
            frame.cascadeFar[index] = cascades.farDistance[index];
            frame.cascadeTexelWorld[index] = cascades.texelWorld[index];
            frame.cascadeDepthRange[index] = cascades.depthRange[index];
        }
        frame.shadowParams[0] = kShadowFilterWorldRadius;
        frame.shadowParams[1] = kShadowNormalOffsetTexels;
        frame.shadowParams[2] = kShadowCascadeBlend;
        frame.shadowParams[3] = kShadowDepthBiasMetres;

        frame.environmentParams[0] = static_cast<f32>(kEnvironmentMipCount);
        frame.environmentParams[1] = 1.0f;
        for (u32 index = 0; index < 9; ++index) {
            frame.irradianceSh[index][0] = environment_.irradiance[index].x;
            frame.irradianceSh[index][1] = environment_.irradiance[index].y;
            frame.irradianceSh[index][2] = environment_.irradiance[index].z;
            frame.irradianceSh[index][3] = 0.0f;
        }

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
        drawGeometry(cmd, world, meshes, world.camera.viewProjection, pbrPipeline_, pbrSkinnedPipeline_,
                     Selection::Opaque);

        // Blended, after the opaque pass has filled depth, back to front. The
        // frame uniforms are still bound -- same block, same slot, same values
        // -- so only the pipeline changes.
        //
        // What this does NOT buy, and the deliverable should not imply
        // otherwise: sorting is per draw, so two transparent surfaces that
        // intersect each other sort wrongly at the pixels where they cross.
        // Order-independent transparency is not on the v1 list.
        cmd.setPipeline(pbrBlendPipeline_);
        drawGeometry(cmd, world, meshes, world.camera.viewProjection, pbrBlendPipeline_, pbrSkinnedBlendPipeline_,
                     Selection::Transparent);
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
