#include "luaug/core/i18n.h"
#include "luaug/core/text_key.h"
#include "luaug/render/clusters.h"
#include "luaug/render/environment.h"
#include "luaug/render/renderer.h"
#include "luaug/render/settings.h"
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

// Further than any camera in this engine can see, so a cascade boundary set to
// it is never the one a fragment selects.
constexpr f32 kUnreachableDistance = 1.0e9f;

constexpr rhi::TextureFormat kHdrFormat = rhi::TextureFormat::Rgba16Float;
constexpr rhi::TextureFormat kDepthFormat = rhi::TextureFormat::D32Float;
constexpr rhi::TextureFormat kShadowFormat = rhi::TextureFormat::D32Float;
// Tonemapped, sRGB-encoded bytes: the input the anti-aliasing resolve wants.
// `Rgba8Unorm` rather than the sRGB variant because `tonemap.hlsl` has already
// applied the transfer function, and a format that applied it again would be the
// classic double-encode.
constexpr rhi::TextureFormat kLdrFormat = rhi::TextureFormat::Rgba8Unorm;
constexpr rhi::TextureFormat kOcclusionFormat = rhi::TextureFormat::R8Unorm;
constexpr rhi::TextureFormat kLuminanceFormat = rhi::TextureFormat::R32Float;

// Five levels, halving from half resolution: the coarsest is a thirty-second of
// the frame, which is where a bloom's tail stops being distinguishable from a
// flat lift.
constexpr u32 kBloomLevels = 5;

// How far towards the frame's measured brightness one FRAME moves. Per frame
// rather than per second, deliberately: a rate driven by elapsed wall-clock time
// would make a screenshot at frame thirty a different picture on a fast machine
// and a slow one.
constexpr f32 kExposureAdaptationRate = 0.05f;

// The bloom threshold, in scene-referred luminance, with a soft knee around it.
// Applied once, on the way into the chain.
constexpr f32 kBloomThreshold = 1.1f;
constexpr f32 kBloomKnee = 0.6f;

// How much of the bloom chain is mixed back in. Small, and it should be: bloom
// that reads as a glow rather than as a haze is mostly threshold and radius, and
// the intensity is what stops it from becoming fog.
constexpr f32 kBloomIntensity = 0.05f;

// The most instances one frame may draw through the instanced path, and the one
// vertex buffer they all live in. Five megabytes, allocated once: the alternative
// is a buffer resized mid-frame, which is a stall.
constexpr u32 kMaxInstances = 65536;

// Below this a run is not worth batching: one instanced call costs a uniform
// push, a vertex-buffer bind and a draw, which is what two ordinary draws cost
// anyway.
constexpr u32 kMinInstanceBatch = 3;

constexpr u32 kNoBatch = 0xFFFFFFFFu;

// The occlusion pass's sampling radius in world metres, its self-occlusion bias,
// and how strongly it darkens.
constexpr f32 kOcclusionRadius = 0.6f;
constexpr f32 kOcclusionBias = 0.025f;
constexpr f32 kOcclusionStrength = 1.0f;

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

// A bloom level's size. Level zero is HALF the frame, so the chain starts one
// halving in -- a full-resolution first level would be the frame's cost again
// for a term that is about to be blurred.
[[nodiscard]] u32 bloomLevelSize(u32 base, u32 level) noexcept
{
    const u32 size = base >> (level + 1);
    return size > 0 ? size : 1u;
}

// One fullscreen triangle into one target: the shape every pass in the post
// chain has. Written once because eleven copies of it would be eleven places to
// forget the scissor, and a missing scissor is a pass that draws nothing on a
// backend that requires one.
void fullscreenPass(rhi::ICmdList& cmd, rhi::PipelineHandle pipeline, rhi::TextureHandle target, u32 width, u32 height,
                    std::string_view name, std::span<const rhi::TextureBinding> textures,
                    std::span<const std::byte> uniforms, rhi::LoadOp loadOp = rhi::LoadOp::Clear)
{
    const std::array<rhi::ColorAttachment, 1> attachment{rhi::ColorAttachment{
        .texture = target,
        .loadOp = loadOp,
        .storeOp = rhi::StoreOp::Store,
    }};
    cmd.beginRenderPass({.colorAttachments = attachment, .debugName = name});
    cmd.setPipeline(pipeline);
    cmd.setViewport({.width = static_cast<f32>(width), .height = static_cast<f32>(height)});
    cmd.setScissor({.width = static_cast<core::i32>(width), .height = static_cast<core::i32>(height)});
    if (!uniforms.empty())
        cmd.bindUniforms(rhi::ShaderStage::Fragment, 0, uniforms);
    if (!textures.empty())
        cmd.bindTextures(rhi::ShaderStage::Fragment, 0, textures);
    cmd.draw(3, 1, 0, 0);
    cmd.endRenderPass();
}

// A pass that clears a target and draws nothing.
//
// What it is for: a post pass that a setting switched off still has a texture
// downstream of it, and a texture the renderer samples must never hold whatever
// the allocator handed back. Clearing is both cheaper than the pass it replaces
// and the only answer that does not depend on the contents of memory -- the
// alternative, binding it anyway and multiplying by zero, turns an uninitialised
// NaN into a black frame.
void clearPass(rhi::ICmdList& cmd, rhi::TextureHandle target, u32 width, u32 height, std::string_view name,
               rhi::ColorRgba color)
{
    const std::array<rhi::ColorAttachment, 1> attachment{rhi::ColorAttachment{
        .texture = target,
        .loadOp = rhi::LoadOp::Clear,
        .storeOp = rhi::StoreOp::Store,
        .clearColor = color,
    }};
    cmd.beginRenderPass({.colorAttachments = attachment, .debugName = name});
    cmd.setViewport({.width = static_cast<f32>(width), .height = static_cast<f32>(height)});
    cmd.setScissor({.width = static_cast<core::i32>(width), .height = static_cast<core::i32>(height)});
    cmd.endRenderPass();
}

[[nodiscard]] u32 environmentLevelSize(u32 level) noexcept
{
    const u32 size = kEnvironmentBaseSize >> level;
    return size > 0 ? size : 1u;
}

// A run of draws that share a mesh, a section, a material and a level of
// detail, collapsed into one call (ADR 0043).
//
// **Culled as a WHOLE**, and that is the trade this design makes. Building a
// separate instance list per pass would let each cascade reject each object, at
// the cost of six lists per frame; culling whole batches instead keeps one list
// and draws a batch into any pass that any of it reaches. What that spends is
// vertex work on instances that are clipped -- which is the cheap side of a
// frame that was measured to be CPU-bound on submission.
struct InstanceBatch
{
    // The first draw of the run, which is the one that carries its mesh,
    // section and material.
    u32 firstDraw = 0;
    u32 firstInstance = 0;
    u32 count = 0;
    u32 lod = 0;
    // The union of the run's bounds, for the per-pass tests.
    Vec3 boundsCenter;
    f32 boundsRadius = 0.0f;
    // Whether ANY of the run is in the camera's frustum. The forward passes draw
    // the batch if this holds, because a batch is one call and cannot be drawn
    // in pieces.
    bool anyVisible = false;
};

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

    // Scaled with the shadow distance, because that is what it describes: the
    // fit's own radius is derived from how far the sun casts, and a setting that
    // halved the distance while extraction kept every caster within 220 metres
    // would be paying for casters no cascade covers.
    //
    // Zero when the sun casts into no cascades at all, which is what turns the
    // whole caster-retention rule off rather than leaving it running for a
    // shadow map nothing writes.
    [[nodiscard]] f32 shadowRadius() const noexcept override
    {
        if (settings_.shadowCascades == 0)
            return 0.0f;
        return kShadowRadius * (settings_.shadowDistance / kShadowDistance);
    }

    [[nodiscard]] RendererStats stats() const noexcept override { return stats_; }

    void setSettings(const GraphicsSettings& settings) override;
    [[nodiscard]] const GraphicsSettings& settings() const noexcept override { return settings_; }

private:
    [[nodiscard]] std::optional<core::EngineError> ensureTargets(rhi::IDevice& device, u32 width, u32 height);
    [[nodiscard]] std::optional<core::EngineError> ensureShadowMap(rhi::IDevice& device);
    // Which draws one call submits. `Shadow` takes every item in the list --
    // a caster outside the view still casts into it -- while the two forward
    // selectors take only what the camera can see, each from its own pass.
    enum class Selection
    {
        Shadow,
        // Depth only, but filtered like the forward pass: a caster outside the
        // view still casts into it, and a caster outside the view still must not
        // fill the depth buffer the camera reads.
        Prepass,
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

    // Groups the sorted draw list into instanced runs and fills the staging
    // buffer. Runs before any render pass, because the upload has to.
    void buildInstanceBatches(const RenderWorld& world, const MeshCache& meshes);

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
    // Depth only, like the shadow pass, but culling BACK faces so the depth it
    // writes is the depth the forward pass will test against. The shadow pass
    // culls front faces on purpose and that would put every surface half a
    // thickness away here.
    rhi::PipelineHandle depthPrepassPipeline_{};
    rhi::PipelineHandle depthPrepassSkinnedPipeline_{};
    // The instanced variants: same shading, same state, a second vertex stream
    // that steps per instance.
    rhi::PipelineHandle shadowInstancedPipeline_{};
    rhi::PipelineHandle depthPrepassInstancedPipeline_{};
    rhi::PipelineHandle pbrInstancedPipeline_{};
    rhi::PipelineHandle skyPipeline_{};
    rhi::PipelineHandle ssaoPipeline_{};
    rhi::PipelineHandle ssaoBlurPipeline_{};
    rhi::PipelineHandle bloomDownPipeline_{};
    rhi::PipelineHandle bloomUpPipeline_{};
    rhi::PipelineHandle luminanceDownPipeline_{};
    rhi::PipelineHandle luminanceReducePipeline_{};
    rhi::PipelineHandle luminanceAdaptPipeline_{};
    rhi::PipelineHandle tonemapPipeline_{};
    rhi::PipelineHandle fxaaPipeline_{};

    rhi::ShaderHandle shaders_[32]{};
    core::usize shaderCount_ = 0;

    rhi::TextureHandle hdr_{};
    rhi::TextureHandle depth_{};
    // Tonemapped and sRGB-encoded, so the anti-aliasing resolve has an image to
    // find edges in. FXAA works on perceptual luminance, which is what makes it
    // a post-tonemap pass rather than a pre-tonemap one.
    rhi::TextureHandle ldr_{};
    // Half resolution, and the blur's ping-pong partner. Half because sixteen
    // taps at full resolution is four times the cost for a term the blur is
    // about to spread anyway (R16).
    rhi::TextureHandle occlusion_{};
    rhi::TextureHandle occlusionBlur_{};
    // The bloom chain, each level its own texture because a `ColorAttachment`
    // names a texture and not a mip level.
    rhi::TextureHandle bloom_[kBloomLevels]{};
    // The automatic exposure's measurement chain, and the two 1x1 targets it
    // ping-pongs between: one frame reads what the last one wrote.
    rhi::TextureHandle luminance64_{};
    rhi::TextureHandle luminance8_{};
    rhi::TextureHandle exposure_[2]{};
    u32 exposureIndex_ = 0;
    bool exposureInitialised_ = false;
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
    // The clustered light tables (clusters.h). Uploaded whole every frame --
    // ninety kilobytes between them -- because `uploadTexture` writes a whole
    // mip and because a frame whose command shape depended on whether the lights
    // moved is exactly what `clock_differential` refuses.
    rhi::TextureHandle clusterGrid_{};
    rhi::TextureHandle lightIndices_{};
    rhi::TextureHandle lightData_{};
    ClusterGrid clusters_;
    // The per-instance vertex stream, and the plan that indexes it. Rebuilt
    // every frame, uploaded once before any render pass -- uploading inside one
    // is what `rhi.err.upload_inside_pass` refuses, and rightly.
    // Scratch for the shadow fit, kept across frames so a frame allocates
    // nothing for it.
    std::vector<ShadowCasterBounds> casterBounds_;

    rhi::BufferHandle instanceBuffer_{};
    std::vector<GpuInstance> instanceStaging_;
    std::vector<InstanceBatch> batches_;
    // Per draw: which batch covers it, or `kNoBatch`.
    std::vector<u32> batchOf_;
    // What the frame actually submitted, for the stat that says whether any of
    // this did anything.
    RendererStats stats_;
    rhi::SamplerHandle linearSampler_{};
    rhi::SamplerHandle shadowSampler_{};
    // Trilinear and clamped: the mip index IS the roughness, so filtering
    // between levels is the interpolation the split sum asks for rather than a
    // quality setting.
    rhi::SamplerHandle environmentSampler_{};
    // Point and clamped: the cluster tables are looked up by exact texel, and
    // filtering between two light offsets would be a light index that does not
    // exist.
    rhi::SamplerHandle pointSampler_{};
    EnvironmentCache environment_;

    // The size the offscreen targets were built for. A window resize rebuilds
    // them rather than stretching, because a stretched HDR target is a bug that
    // looks like a driver problem.
    u32 width_ = 0;
    u32 height_ = 0;

    // What the settings resolve to for this frame: the world is rendered at a
    // fraction of the output and the final resolve upscales it. `width_` above
    // is what the internal chain was BUILT for, and these two are what it is
    // built for now -- the same number until a render scale is set.
    u32 renderWidth_ = 0;
    u32 renderHeight_ = 0;

    // The previous frame's cascade fit, so this frame can keep it (D048).
    ShadowCascades shadowFit_{};
    bool shadowFitted_ = false;

    GraphicsSettings settings_;
    // The tile resolution `shadowMap_` was created for, so a settings change
    // rebuilds it and a repeated one does not.
    u32 shadowTile_ = 0;

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
    const rhi::ShaderHandle shadowInstancedVertex = load("shadow_instanced", rhi::ShaderStage::Vertex);
    const rhi::ShaderHandle shadowInstancedFragment = load("shadow_instanced", rhi::ShaderStage::Fragment);
    const rhi::ShaderHandle pbrInstancedVertex = load("pbr_instanced", rhi::ShaderStage::Vertex);
    const rhi::ShaderHandle pbrInstancedFragment = load("pbr_instanced", rhi::ShaderStage::Fragment);
    const rhi::ShaderHandle ssaoVertex = load("ssao", rhi::ShaderStage::Vertex);
    const rhi::ShaderHandle ssaoFragment = load("ssao", rhi::ShaderStage::Fragment);
    const rhi::ShaderHandle ssaoBlurVertex = load("ssao_blur", rhi::ShaderStage::Vertex);
    const rhi::ShaderHandle ssaoBlurFragment = load("ssao_blur", rhi::ShaderStage::Fragment);
    const rhi::ShaderHandle bloomDownVertex = load("bloom_down", rhi::ShaderStage::Vertex);
    const rhi::ShaderHandle bloomDownFragment = load("bloom_down", rhi::ShaderStage::Fragment);
    const rhi::ShaderHandle bloomUpVertex = load("bloom_up", rhi::ShaderStage::Vertex);
    const rhi::ShaderHandle bloomUpFragment = load("bloom_up", rhi::ShaderStage::Fragment);
    const rhi::ShaderHandle luminanceDownVertex = load("luminance_down", rhi::ShaderStage::Vertex);
    const rhi::ShaderHandle luminanceDownFragment = load("luminance_down", rhi::ShaderStage::Fragment);
    const rhi::ShaderHandle luminanceReduceVertex = load("luminance_reduce", rhi::ShaderStage::Vertex);
    const rhi::ShaderHandle luminanceReduceFragment = load("luminance_reduce", rhi::ShaderStage::Fragment);
    const rhi::ShaderHandle luminanceAdaptVertex = load("luminance_adapt", rhi::ShaderStage::Vertex);
    const rhi::ShaderHandle luminanceAdaptFragment = load("luminance_adapt", rhi::ShaderStage::Fragment);
    const rhi::ShaderHandle fxaaVertex = load("fxaa", rhi::ShaderStage::Vertex);
    const rhi::ShaderHandle fxaaFragment = load("fxaa", rhi::ShaderStage::Fragment);

    for (const rhi::ShaderHandle handle : {shadowInstancedVertex,
                                           shadowInstancedFragment,
                                           pbrInstancedVertex,
                                           pbrInstancedFragment,
                                           ssaoVertex,
                                           ssaoFragment,
                                           ssaoBlurVertex,
                                           ssaoBlurFragment,
                                           bloomDownVertex,
                                           bloomDownFragment,
                                           bloomUpVertex,
                                           bloomUpFragment,
                                           luminanceDownVertex,
                                           luminanceDownFragment,
                                           luminanceReduceVertex,
                                           luminanceReduceFragment,
                                           luminanceAdaptVertex,
                                           luminanceAdaptFragment,
                                           fxaaVertex,
                                           fxaaFragment}) {
        if (!handle.valid()) {
            destroy(device);
            return error.key.hash != 0 ? error : core::makeError(LUAUG_TR("render.err.shader_format_unknown"));
        }
    }

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
        // Front faces are culled in the shadow pass rather than back faces, and
        // the measurement behind that is in D051: switching to back faces puts a
        // regular hatched acne across every lit floor in `examples/02-meshes`.
        // What it COSTS is the contact -- the stored depth is the far side of a
        // solid object -- and the answer to that is not the cull mode, it is the
        // two biases below it, which exist to fight an acne this already
        // prevents.
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

    // The DEPTH PREPASS, and it is `shadow_depth` compiled into a different
    // pipeline rather than a new shader: `GpuShadowUniforms` is already a
    // view-projection and a model matrix, which is exactly what a depth-only
    // pass of the camera needs. What differs is state -- back-face culling, so
    // the depth it writes is the depth the forward pass will test against.
    //
    // What it does NOT do, said out loud: an alpha-masked material writes depth
    // where its own fragments would have been discarded. The shadow pass has
    // always had the same gap, and closing it means a second fragment shader
    // that samples base colour in a pass whose whole point is not to.
    depthPrepassPipeline_ = device.createGraphicsPipeline({
        .vertexShader = shadowVertex,
        .fragmentShader = shadowFragment,
        .vertexBuffers = buffers,
        .vertexAttributes = attributes,
        .rasterizer = {.cullMode = rhi::CullMode::Back},
        .depthStencil = {.depthTest = true, .depthWrite = true, .depthCompare = rhi::CompareOp::LessOrEqual},
        .colorTargets = {},
        .depthStencilFormat = kDepthFormat,
        .debugName = "depth_prepass",
    });
    depthPrepassSkinnedPipeline_ = device.createGraphicsPipeline({
        .vertexShader = shadowSkinnedVertex,
        .fragmentShader = shadowSkinnedFragment,
        .vertexBuffers = skinnedBuffers,
        .vertexAttributes = shadowSkinnedAttributes,
        .rasterizer = {.cullMode = rhi::CullMode::Back},
        .depthStencil = {.depthTest = true, .depthWrite = true, .depthCompare = rhi::CompareOp::LessOrEqual},
        .colorTargets = {},
        .depthStencilFormat = kDepthFormat,
        .debugName = "depth_prepass_skinned",
    });

    // The sky writes no depth and tests none: it is drawn first and everything
    // else covers it. It declares the depth FORMAT anyway, because the forward
    // pass has a depth attachment and every pipeline used inside it must agree
    // about that format even when it neither reads nor writes one.
    skyPipeline_ = device.createGraphicsPipeline({
        .vertexShader = skyVertex,
        .fragmentShader = skyFragment,
        .primitive = rhi::PrimitiveType::TriangleList,
        .rasterizer = {.cullMode = rhi::CullMode::None},
        .depthStencil = {.depthTest = false, .depthWrite = false},
        .colorTargets = hdrTarget,
        .depthStencilFormat = kDepthFormat,
        .debugName = "sky",
    });

    const std::array<rhi::ColorTargetDesc, 1> occlusionTarget{rhi::ColorTargetDesc{.format = kOcclusionFormat}};
    const std::array<rhi::ColorTargetDesc, 1> luminanceTarget{rhi::ColorTargetDesc{.format = kLuminanceFormat}};
    const std::array<rhi::ColorTargetDesc, 1> ldrTarget{rhi::ColorTargetDesc{.format = kLdrFormat}};
    // Additive, which is what lets the upsample ADD into the level below rather
    // than read a target it is also writing -- something every backend refuses
    // and which the frozen `BlendState` already makes unnecessary.
    const std::array<rhi::ColorTargetDesc, 1> bloomAddTarget{rhi::ColorTargetDesc{
        .format = kHdrFormat,
        .blend = {.enabled = true,
                  .srcColor = rhi::BlendFactor::One,
                  .dstColor = rhi::BlendFactor::One,
                  .srcAlpha = rhi::BlendFactor::One,
                  .dstAlpha = rhi::BlendFactor::One},
    }};

    const auto fullscreen = [&](rhi::ShaderHandle vertex, rhi::ShaderHandle fragment,
                                std::span<const rhi::ColorTargetDesc> targets, const char* name) {
        return device.createGraphicsPipeline({
            .vertexShader = vertex,
            .fragmentShader = fragment,
            .primitive = rhi::PrimitiveType::TriangleList,
            .rasterizer = {.cullMode = rhi::CullMode::None},
            .colorTargets = targets,
            .debugName = name,
        });
    };

    // The per-INSTANCE stream, at slot 1: the model matrix as four columns and
    // the instance's alpha. `perInstance` is the one field ADR 0043 added to the
    // frozen RHI, and this is its only caller.
    const std::array<rhi::VertexBufferLayout, 2> instancedBuffers{
        rhi::VertexBufferLayout{.slot = 0, .strideBytes = 48},
        rhi::VertexBufferLayout{.slot = 1, .strideBytes = sizeof(GpuInstance), .perInstance = true},
    };
    const std::array<rhi::VertexAttribute, 9> instancedAttributes{
        rhi::VertexAttribute{.location = 0, .bufferSlot = 0, .format = rhi::VertexFormat::Float3, .offsetBytes = 0},
        rhi::VertexAttribute{.location = 1, .bufferSlot = 0, .format = rhi::VertexFormat::Float3, .offsetBytes = 12},
        rhi::VertexAttribute{.location = 2, .bufferSlot = 0, .format = rhi::VertexFormat::Float4, .offsetBytes = 24},
        rhi::VertexAttribute{.location = 3, .bufferSlot = 0, .format = rhi::VertexFormat::Float2, .offsetBytes = 40},
        rhi::VertexAttribute{.location = 4, .bufferSlot = 1, .format = rhi::VertexFormat::Float4, .offsetBytes = 0},
        rhi::VertexAttribute{.location = 5, .bufferSlot = 1, .format = rhi::VertexFormat::Float4, .offsetBytes = 16},
        rhi::VertexAttribute{.location = 6, .bufferSlot = 1, .format = rhi::VertexFormat::Float4, .offsetBytes = 32},
        rhi::VertexAttribute{.location = 7, .bufferSlot = 1, .format = rhi::VertexFormat::Float4, .offsetBytes = 48},
        rhi::VertexAttribute{.location = 8, .bufferSlot = 1, .format = rhi::VertexFormat::Float4, .offsetBytes = 64},
    };
    // The depth-only instanced pass reads position and the four model columns
    // and nothing else, so its locations are 0 through 4 -- the numbers are the
    // shader's declaration order, not the vertex's.
    const std::array<rhi::VertexAttribute, 5> shadowInstancedAttributes{
        rhi::VertexAttribute{.location = 0, .bufferSlot = 0, .format = rhi::VertexFormat::Float3, .offsetBytes = 0},
        rhi::VertexAttribute{.location = 1, .bufferSlot = 1, .format = rhi::VertexFormat::Float4, .offsetBytes = 0},
        rhi::VertexAttribute{.location = 2, .bufferSlot = 1, .format = rhi::VertexFormat::Float4, .offsetBytes = 16},
        rhi::VertexAttribute{.location = 3, .bufferSlot = 1, .format = rhi::VertexFormat::Float4, .offsetBytes = 32},
        rhi::VertexAttribute{.location = 4, .bufferSlot = 1, .format = rhi::VertexFormat::Float4, .offsetBytes = 48},
    };

    shadowInstancedPipeline_ = device.createGraphicsPipeline({
        .vertexShader = shadowInstancedVertex,
        .fragmentShader = shadowInstancedFragment,
        .vertexBuffers = instancedBuffers,
        .vertexAttributes = shadowInstancedAttributes,
        .rasterizer = {.cullMode = rhi::CullMode::Front},
        .depthStencil = {.depthTest = true, .depthWrite = true, .depthCompare = rhi::CompareOp::LessOrEqual},
        .colorTargets = {},
        .depthStencilFormat = kShadowFormat,
        .debugName = "shadow_instanced",
    });
    depthPrepassInstancedPipeline_ = device.createGraphicsPipeline({
        .vertexShader = shadowInstancedVertex,
        .fragmentShader = shadowInstancedFragment,
        .vertexBuffers = instancedBuffers,
        .vertexAttributes = shadowInstancedAttributes,
        .rasterizer = {.cullMode = rhi::CullMode::Back},
        .depthStencil = {.depthTest = true, .depthWrite = true, .depthCompare = rhi::CompareOp::LessOrEqual},
        .colorTargets = {},
        .depthStencilFormat = kDepthFormat,
        .debugName = "depth_prepass_instanced",
    });
    pbrInstancedPipeline_ = device.createGraphicsPipeline({
        .vertexShader = pbrInstancedVertex,
        .fragmentShader = pbrInstancedFragment,
        .vertexBuffers = instancedBuffers,
        .vertexAttributes = instancedAttributes,
        .rasterizer = {.cullMode = rhi::CullMode::Back},
        .depthStencil = {.depthTest = true, .depthWrite = true, .depthCompare = rhi::CompareOp::LessOrEqual},
        .colorTargets = hdrTarget,
        .depthStencilFormat = kDepthFormat,
        .debugName = "pbr_instanced",
    });

    ssaoPipeline_ = fullscreen(ssaoVertex, ssaoFragment, occlusionTarget, "ssao");
    ssaoBlurPipeline_ = fullscreen(ssaoBlurVertex, ssaoBlurFragment, occlusionTarget, "ssao_blur");
    bloomDownPipeline_ = fullscreen(bloomDownVertex, bloomDownFragment, hdrTarget, "bloom_down");
    bloomUpPipeline_ = fullscreen(bloomUpVertex, bloomUpFragment, bloomAddTarget, "bloom_up");
    luminanceDownPipeline_ = fullscreen(luminanceDownVertex, luminanceDownFragment, luminanceTarget, "luminance_down");
    luminanceReducePipeline_ =
        fullscreen(luminanceReduceVertex, luminanceReduceFragment, luminanceTarget, "luminance_reduce");
    luminanceAdaptPipeline_ =
        fullscreen(luminanceAdaptVertex, luminanceAdaptFragment, luminanceTarget, "luminance_adapt");
    tonemapPipeline_ = fullscreen(tonemapVertex, tonemapFragment, ldrTarget, "tonemap");
    fxaaPipeline_ = fullscreen(fxaaVertex, fxaaFragment, swapTarget, "fxaa");

    if (!shadowPipeline_.valid() || !pbrPipeline_.valid() || !pbrBlendPipeline_.valid() || !skyPipeline_.valid() ||
        !tonemapPipeline_.valid() || !shadowSkinnedPipeline_.valid() || !pbrSkinnedPipeline_.valid() ||
        !pbrSkinnedBlendPipeline_.valid() || !depthPrepassPipeline_.valid() || !depthPrepassSkinnedPipeline_.valid() ||
        !ssaoPipeline_.valid() || !ssaoBlurPipeline_.valid() || !bloomDownPipeline_.valid() ||
        !bloomUpPipeline_.valid() || !luminanceDownPipeline_.valid() || !luminanceReducePipeline_.valid() ||
        !luminanceAdaptPipeline_.valid() || !fxaaPipeline_.valid() || !shadowInstancedPipeline_.valid() ||
        !depthPrepassInstancedPipeline_.valid() || !pbrInstancedPipeline_.valid()) {
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
    pointSampler_ = device.createSampler({
        .minFilter = rhi::Filter::Nearest,
        .magFilter = rhi::Filter::Nearest,
        .mipmapMode = rhi::MipmapMode::Nearest,
        .addressU = rhi::AddressMode::ClampToEdge,
        .addressV = rhi::AddressMode::ClampToEdge,
        .addressW = rhi::AddressMode::ClampToEdge,
        .debugName = "cluster",
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

    if (ensureShadowMap(device).has_value()) {
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
    instanceBuffer_ = device.createBuffer({
        .usage = rhi::BufferUsage::Vertex,
        .sizeBytes = kMaxInstances * static_cast<u32>(sizeof(GpuInstance)),
        .debugName = "instances",
    });
    if (!instanceBuffer_.valid()) {
        destroy(device);
        return core::makeError(LUAUG_TR("render.err.target_create_failed"));
    }

    clusterGrid_ = device.createTexture({
        .format = rhi::TextureFormat::R32Float,
        .usage = rhi::TextureUsage::Sampled,
        .width = kClusterGridWidth,
        .height = kClusterGridHeight,
        .debugName = "cluster-grid",
    });
    lightIndices_ = device.createTexture({
        .format = rhi::TextureFormat::R32Float,
        .usage = rhi::TextureUsage::Sampled,
        .width = kLightIndexTextureWidth,
        .height = kLightIndexTextureHeight,
        .debugName = "cluster-light-indices",
    });
    lightData_ = device.createTexture({
        .format = rhi::TextureFormat::Rgba32Float,
        .usage = rhi::TextureUsage::Sampled,
        .width = 3,
        .height = kMaxClusteredLights,
        .debugName = "cluster-light-data",
    });
    if (!environmentMap_.valid() || !brdfLut_.valid() || !clusterGrid_.valid() || !lightIndices_.valid() ||
        !lightData_.valid()) {
        destroy(device);
        return core::makeError(LUAUG_TR("render.err.target_create_failed"));
    }

    valid_ = true;
    return std::nullopt;
}

// The atlas is always two tiles by two, whatever the cascade count: fewer
// cascades buy submission rather than memory, and `shadowTileResolution` is the
// dial that buys memory (settings.h).
std::optional<core::EngineError> DefaultRenderer::ensureShadowMap(rhi::IDevice& device)
{
    if (shadowMap_.valid() && shadowTile_ == settings_.shadowTileResolution)
        return std::nullopt;

    if (shadowMap_.valid())
        device.destroy(shadowMap_);

    const u32 atlas = settings_.shadowTileResolution * 2;
    shadowMap_ = device.createTexture({
        .format = kShadowFormat,
        .usage = rhi::TextureUsage::DepthStencilTarget | rhi::TextureUsage::Sampled,
        .width = atlas,
        .height = atlas,
        .debugName = "shadow-atlas",
    });
    if (!shadowMap_.valid())
        return core::makeError(LUAUG_TR("render.err.target_create_failed"));

    shadowTile_ = settings_.shadowTileResolution;
    // A new atlas is a new texel size, so last frame's box would be remembered
    // against a lattice that no longer exists.
    shadowFitted_ = false;
    return std::nullopt;
}

void DefaultRenderer::setSettings(const GraphicsSettings& settings)
{
    // Clamped here as well as at every source, because this is the last door: a
    // caller that builds a `GraphicsSettings` by hand should not be able to ask
    // for a render scale of zero and get a target of no pixels.
    settings_ = clampSettings(settings);
}

std::optional<core::EngineError> DefaultRenderer::ensureTargets(rhi::IDevice& device, u32 width, u32 height)
{
    if (hdr_.valid() && width == width_ && height == height_)
        return std::nullopt;

    // `width`/`height` arrive already scaled -- see `render`, which is the one
    // place the settings' render scale is applied, so that nothing downstream
    // has to remember to.

    for (rhi::TextureHandle* texture : {&hdr_, &depth_, &ldr_, &occlusion_, &occlusionBlur_, &luminance64_,
                                        &luminance8_, &exposure_[0], &exposure_[1]}) {
        if (texture->valid())
            device.destroy(*texture);
        *texture = {};
    }
    for (rhi::TextureHandle& level : bloom_) {
        if (level.valid())
            device.destroy(level);
        level = {};
    }

    const auto half = [](u32 value) { return value > 1 ? value / 2 : 1u; };

    hdr_ = device.createTexture({
        .format = kHdrFormat,
        .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled,
        .width = width,
        .height = height,
        .debugName = "hdr",
    });
    // **Sampled as well as an attachment**, which is the whole of the roadmap's
    // "the scene depth must be samplable by a later pass". It is written by the
    // prepass, read by ambient occlusion, and then attached again by the forward
    // pass -- never both at once, which is a rule every backend enforces and
    // none has to be asked about.
    depth_ = device.createTexture({
        .format = kDepthFormat,
        .usage = rhi::TextureUsage::DepthStencilTarget | rhi::TextureUsage::Sampled,
        .width = width,
        .height = height,
        .debugName = "depth",
    });
    ldr_ = device.createTexture({
        .format = kLdrFormat,
        .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled,
        .width = width,
        .height = height,
        .debugName = "ldr",
    });
    occlusion_ = device.createTexture({
        .format = kOcclusionFormat,
        .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled,
        .width = half(width),
        .height = half(height),
        .debugName = "occlusion",
    });
    occlusionBlur_ = device.createTexture({
        .format = kOcclusionFormat,
        .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled,
        .width = half(width),
        .height = half(height),
        .debugName = "occlusion-blur",
    });

    u32 levelWidth = half(width);
    u32 levelHeight = half(height);
    for (u32 level = 0; level < kBloomLevels; ++level) {
        bloom_[level] = device.createTexture({
            .format = kHdrFormat,
            .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled,
            .width = levelWidth,
            .height = levelHeight,
            .debugName = "bloom",
        });
        levelWidth = half(levelWidth);
        levelHeight = half(levelHeight);
    }

    luminance64_ = device.createTexture({
        .format = kLuminanceFormat,
        .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled,
        .width = 64,
        .height = 64,
        .debugName = "luminance-64",
    });
    luminance8_ = device.createTexture({
        .format = kLuminanceFormat,
        .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled,
        .width = 8,
        .height = 8,
        .debugName = "luminance-8",
    });
    for (rhi::TextureHandle& target : exposure_) {
        target = device.createTexture({
            .format = kLuminanceFormat,
            .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled,
            .width = 1,
            .height = 1,
            .debugName = "exposure",
        });
    }
    exposureInitialised_ = false;

    if (!hdr_.valid() || !depth_.valid() || !ldr_.valid() || !occlusion_.valid() || !occlusionBlur_.valid() ||
        !luminance64_.valid() || !luminance8_.valid() || !exposure_[0].valid() || !exposure_[1].valid())
        return core::makeError(LUAUG_TR("render.err.target_create_failed"));
    for (const rhi::TextureHandle& level : bloom_) {
        if (!level.valid())
            return core::makeError(LUAUG_TR("render.err.target_create_failed"));
    }

    width_ = width;
    height_ = height;
    return std::nullopt;
}

void DefaultRenderer::destroy(rhi::IDevice& device)
{
    for (core::usize index = 0; index < shaderCount_; ++index)
        device.destroy(shaders_[index]);
    shaderCount_ = 0;

    for (rhi::PipelineHandle* pipeline : {&shadowPipeline_,
                                          &pbrPipeline_,
                                          &pbrBlendPipeline_,
                                          &skyPipeline_,
                                          &tonemapPipeline_,
                                          &shadowSkinnedPipeline_,
                                          &pbrSkinnedPipeline_,
                                          &pbrSkinnedBlendPipeline_,
                                          &depthPrepassPipeline_,
                                          &depthPrepassSkinnedPipeline_,
                                          &ssaoPipeline_,
                                          &ssaoBlurPipeline_,
                                          &bloomDownPipeline_,
                                          &bloomUpPipeline_,
                                          &luminanceDownPipeline_,
                                          &luminanceReducePipeline_,
                                          &luminanceAdaptPipeline_,
                                          &fxaaPipeline_,
                                          &shadowInstancedPipeline_,
                                          &depthPrepassInstancedPipeline_,
                                          &pbrInstancedPipeline_}) {
        if (pipeline->valid())
            device.destroy(*pipeline);
        *pipeline = {};
    }
    for (rhi::TextureHandle* texture :
         {&hdr_, &depth_, &ldr_, &occlusion_, &occlusionBlur_, &luminance64_, &luminance8_, &exposure_[0],
          &exposure_[1], &shadowMap_, &whitePixel_, &flatNormalPixel_, &blackPixel_, &environmentMap_, &brdfLut_,
          &clusterGrid_, &lightIndices_, &lightData_}) {
        if (texture->valid())
            device.destroy(*texture);
        *texture = {};
    }
    for (rhi::TextureHandle& level : bloom_) {
        if (level.valid())
            device.destroy(level);
        level = {};
    }
    if (instanceBuffer_.valid())
        device.destroy(instanceBuffer_);
    instanceBuffer_ = {};

    for (rhi::SamplerHandle* sampler : {&linearSampler_, &shadowSampler_, &environmentSampler_, &pointSampler_}) {
        if (sampler->valid())
            device.destroy(*sampler);
        *sampler = {};
    }

    width_ = 0;
    height_ = 0;
    defaultsUploaded_ = false;
    brdfUploaded_ = false;
    exposureInitialised_ = false;
    exposureIndex_ = 0;
    environment_ = EnvironmentCache{};
    valid_ = false;
}

void DefaultRenderer::buildInstanceBatches(const RenderWorld& world, const MeshCache& meshes)
{
    batches_.clear();
    instanceStaging_.clear();
    batchOf_.assign(world.draws.size(), kNoBatch);
    if (!world.camera.valid)
        return;

    // The SAME level the submission will choose, from the same function and the
    // same camera -- a batch whose members disagreed about their level of detail
    // would draw one mesh with another's index range.
    const f32 pixelsPerUnit = height_ > 0 ? 0.5f * static_cast<f32>(height_) * world.camera.projection.m[1][1] : 0.0f;

    const auto instanceable = [&](const DrawItem& draw) {
        // Transparent draws are never batched: their ORDER is their
        // correctness, and `drawSortKey` zeroes their mesh field for exactly
        // that reason. Skinned draws are not batched either -- a joint palette
        // is per draw and there is no room for one in a vertex stream.
        return !draw.transparent && draw.boneCount == 0;
    };

    for (core::usize index = 0; index < world.draws.size();) {
        const DrawItem& first = world.draws[index];
        if (!instanceable(first)) {
            ++index;
            continue;
        }
        const MeshCache::Resolved* resolved = meshes.resolve(first.mesh);
        if (resolved == nullptr || resolved->lods.empty()) {
            ++index;
            continue;
        }
        const u32 lod = selectMeshLod(*resolved, first.transform, pixelsPerUnit);

        core::usize last = index + 1;
        while (last < world.draws.size()) {
            const DrawItem& next = world.draws[last];
            if (!instanceable(next) || !(next.mesh == first.mesh) || next.section != first.section ||
                next.material != first.material)
                break;
            if (selectMeshLod(*resolved, next.transform, pixelsPerUnit) != lod)
                break;
            ++last;
        }

        const auto count = static_cast<u32>(last - index);
        if (count < kMinInstanceBatch || instanceStaging_.size() + count > static_cast<core::usize>(kMaxInstances)) {
            index = last;
            continue;
        }

        InstanceBatch batch;
        batch.firstDraw = static_cast<u32>(index);
        batch.firstInstance = static_cast<u32>(instanceStaging_.size());
        batch.count = count;
        batch.lod = lod;

        // The union sphere, grown one member at a time. Conservative in the
        // direction that never drops geometry, which is the only direction a
        // cull may be wrong in.
        batch.boundsCenter = world.draws[index].boundsCenter;
        batch.boundsRadius = world.draws[index].boundsRadius;
        for (core::usize member = index; member < last; ++member) {
            const DrawItem& draw = world.draws[member];
            batchOf_[member] = static_cast<u32>(batches_.size());
            batch.anyVisible = batch.anyVisible || draw.inCameraFrustum;

            GpuInstance instance;
            instance.model = draw.transform;
            instance.alphaUnused[0] = draw.alpha;
            instanceStaging_.push_back(instance);

            const Vec3 offset = draw.boundsCenter - batch.boundsCenter;
            const f32 distance = core::length(offset);
            if (distance + draw.boundsRadius > batch.boundsRadius) {
                const f32 grown = 0.5f * (batch.boundsRadius + distance + draw.boundsRadius);
                if (distance > 1e-6f)
                    batch.boundsCenter = batch.boundsCenter + offset * ((grown - batch.boundsRadius) / distance);
                batch.boundsRadius = grown;
            }
        }

        batches_.push_back(batch);
        index = last;
    }
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
    const bool depthOnly = selection == Selection::Shadow || selection == Selection::Prepass;
    // Which pipeline is currently set. Three variants now rather than two, so a
    // handle is clearer than a bool -- and `extract`'s sort keeps runs of each
    // together, so this switches a handful of times per pass whatever the scene.
    rhi::PipelineHandle currentPipeline = staticPipeline;
    const rhi::PipelineHandle instancedPipeline = selection == Selection::Shadow    ? shadowInstancedPipeline_
                                                  : selection == Selection::Prepass ? depthPrepassInstancedPipeline_
                                                                                    : pbrInstancedPipeline_;

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

    for (core::usize drawIndex = 0; drawIndex < world.draws.size(); ++drawIndex) {
        const DrawItem& draw = world.draws[drawIndex];

        // A batched run is drawn once, by its first member, and every other
        // member is skipped -- one call cannot be issued in pieces, which is
        // also why a batch is culled as a whole.
        const u32 batchIndex = drawIndex < batchOf_.size() ? batchOf_[drawIndex] : kNoBatch;
        const InstanceBatch* batch = batchIndex == kNoBatch ? nullptr : &batches_[batchIndex];
        if (batch != nullptr && batch->firstDraw != drawIndex)
            continue;

        // The shadow pass takes everything; the forward passes take only what
        // the camera can see. A caster behind the camera still casts into the
        // frame -- including a half-transparent one, which still occludes. The
        // roadmap leaves whether it *should* as a separate question, and this
        // milestone does not open it.
        const bool visible = batch != nullptr ? batch->anyVisible : draw.inCameraFrustum;
        if (selection != Selection::Shadow && !visible)
            continue;
        if ((selection == Selection::Opaque || selection == Selection::Prepass) && draw.transparent)
            continue;
        if (selection == Selection::Transparent && !draw.transparent)
            continue;
        if (cull != nullptr) {
            const Vec3 centre = batch != nullptr ? batch->boundsCenter : draw.boundsCenter;
            const f32 radius = batch != nullptr ? batch->boundsRadius : draw.boundsRadius;
            const Vec3 offset = centre - cull->centre;
            const f32 reach = cull->radius + radius;
            if (core::dot(offset, offset) > reach * reach)
                continue;
        }

        const MeshCache::Resolved* resolved = meshes.resolve(draw.mesh);
        if (resolved == nullptr || resolved->lods.empty())
            continue;

        const u32 lod = batch != nullptr ? batch->lod : selectMeshLod(*resolved, draw.transform, pixelsPerUnit);
        const MeshLodRange& level = resolved->lods[lod];
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
        const bool skinnedDraw =
            batch == nullptr && draw.boneCount > 0 && resolved->skin.valid() && skinnedPipeline.valid();
        const rhi::PipelineHandle wanted = batch != nullptr ? instancedPipeline
                                           : skinnedDraw    ? skinnedPipeline
                                                            : staticPipeline;
        if (!(wanted == currentPipeline)) {
            cmd.setPipeline(wanted);
            currentPipeline = wanted;
        }

        if (depthOnly) {
            // The instanced path reads its model matrix from the vertex stream,
            // so the block carries only the view-projection -- but it is pushed
            // per batch rather than per pass, because an ordinary draw between
            // two batches overwrites the same slot.
            const GpuShadowUniforms uniforms{viewProjection, batch != nullptr ? Mat4{} : draw.transform};
            cmd.bindUniforms(rhi::ShaderStage::Vertex, 0, asBytes(&uniforms, sizeof(uniforms)));
        }
        else {
            GpuObjectUniforms uniforms{viewProjection, batch != nullptr ? Mat4{} : draw.transform,
                                       batch != nullptr ? Mat4{} : normalMatrixOf(draw.transform)};
            uniforms.instanceAlphaUnused[0] = batch != nullptr ? 1.0f : draw.alpha;
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
                const std::array<rhi::TextureBinding, 11> textures{
                    rhi::TextureBinding{orDefault(material.baseColor, whitePixel_), linearSampler_},
                    rhi::TextureBinding{orDefault(material.normal, flatNormalPixel_), linearSampler_},
                    rhi::TextureBinding{orDefault(material.metallicRoughness, whitePixel_), linearSampler_},
                    rhi::TextureBinding{orDefault(material.emissive, blackPixel_), linearSampler_},
                    rhi::TextureBinding{shadowMap_, shadowSampler_},
                    rhi::TextureBinding{environmentMap_, environmentSampler_},
                    rhi::TextureBinding{brdfLut_, environmentSampler_},
                    rhi::TextureBinding{clusterGrid_, pointSampler_},
                    rhi::TextureBinding{lightIndices_, pointSampler_},
                    rhi::TextureBinding{lightData_, pointSampler_},
                    rhi::TextureBinding{occlusion_, linearSampler_},
                };
                cmd.bindTextures(rhi::ShaderStage::Fragment, 0, textures);
                boundMaterial = draw.material;
            }
        }

        if (batch != nullptr) {
            const std::array<rhi::BufferHandle, 2> vertexBuffers{resolved->vertices, instanceBuffer_};
            cmd.bindVertexBuffers(0, vertexBuffers);
        }
        else if (skinnedDraw) {
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
        cmd.drawIndexed(section.indexCount, batch != nullptr ? batch->count : 1,
                        resolved->firstIndex + section.firstIndex, resolved->vertexOffset,
                        batch != nullptr ? batch->firstInstance : 0);

        ++stats_.drawCalls;
        if (batch != nullptr) {
            ++stats_.instancedDraws;
            stats_.instances += batch->count;
        }
    }
}

void DefaultRenderer::render(rhi::IDevice& device, rhi::ICmdList& cmd, const RenderTarget& target,
                             const RenderWorld& world, const MeshCache& meshes)
{
    if (!valid_ || !target.color.valid() || target.width == 0 || target.height == 0)
        return;

    // **The one place the render scale is applied.** Everything below draws the
    // WORLD at `renderWidth_` by `renderHeight_` and only the final resolve
    // writes `target`, so a reduced scale costs every per-pixel pass at once
    // and costs the 2D pass -- which the host draws afterwards, at the target's
    // own size -- nothing at all.
    const auto scaled = [&](u32 value) {
        const auto result = static_cast<u32>(static_cast<f32>(value) * settings_.renderScale + 0.5f);
        return result > 0 ? result : 1u;
    };
    renderWidth_ = scaled(target.width);
    renderHeight_ = scaled(target.height);

    if (ensureShadowMap(device).has_value())
        return;
    if (ensureTargets(device, renderWidth_, renderHeight_).has_value())
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

    // The clustered light assignment, and its three tables. Built on the CPU
    // because the frozen RHI has no compute, and uploaded unconditionally
    // because a frame's command shape must not depend on whether the lights
    // moved -- the same rule the environment upload obeys, for the same reason.
    // Grouped and uploaded BEFORE any render pass begins, because
    // `rhi.err.upload_inside_pass` refuses the alternative and is right to: the
    // GPU is rasterizing into the target by then.
    stats_ = RendererStats{};
    buildInstanceBatches(world, meshes);
    if (!instanceStaging_.empty()) {
        cmd.upload(instanceBuffer_, asBytes(instanceStaging_.data(), instanceStaging_.size() * sizeof(GpuInstance)), 0);
    }

    // The light budget, applied where the lights enter the frame. Truncation
    // rather than selection: extraction order is deterministic (R10), so which
    // lights survive a budget is the same answer on every machine and in every
    // replay of the same world.
    const std::span<const RenderLight> budgetedLights =
        world.lights.size() > settings_.lightBudget
            ? std::span<const RenderLight>(world.lights.data(), settings_.lightBudget)
            : std::span<const RenderLight>(world.lights);
    buildClusters(world.camera, budgetedLights, clusters_);
    cmd.uploadTexture(clusterGrid_, asBytes(clusters_.grid.data(), clusters_.grid.size() * sizeof(f32)), 0);
    cmd.uploadTexture(lightIndices_, asBytes(clusters_.indices.data(), clusters_.indices.size() * sizeof(f32)), 0);
    cmd.uploadTexture(lightData_, asBytes(clusters_.lightData.data(), clusters_.lightData.size() * sizeof(f32)), 0);

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

    // What can cast, handed to the fit so a cascade can be sized to it. The two
    // numbers are already on every `DrawItem`; nothing is computed here that the
    // extraction did not compute.
    casterBounds_.clear();
    casterBounds_.reserve(world.draws.size());
    for (const DrawItem& draw : world.draws)
        casterBounds_.push_back(ShadowCasterBounds{draw.boundsCenter, draw.boundsRadius});
    fit.casters = casterBounds_;
    fit.distance = settings_.shadowDistance;
    fit.tileResolution = settings_.shadowTileResolution;

    // Last frame's fit is handed back in, which is what lets a cascade keep the
    // same box -- and therefore the same texel lattice -- while nothing has left
    // it (D048). Held per renderer rather than globally, for the reason the
    // exposure and the environment chain are: this is the history of one view,
    // and two views in one process are two histories.
    const ShadowCascades cascades = fitShadowCascades(fit, shadowFitted_ ? &shadowFit_ : nullptr);
    shadowFit_ = cascades;
    shadowFitted_ = true;

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
        shadowSplits(world.camera.nearPlane, settings_.shadowDistance, kShadowSplitLambda, splits);

        // **A cascade nothing renders into is a cascade that is cleared, and a
        // cleared depth of 1 reads as lit.** That is the whole mechanism behind
        // `shadowCascades` being a setting: the sampler needs no idea how many
        // there are, because a fragment that selects a tile nobody drew into
        // gets the same answer as one that falls outside a cascade entirely.
        for (u32 index = 0; index < settings_.shadowCascades; ++index) {
            const auto tile = static_cast<f32>(settings_.shadowTileResolution);
            const f32 x = static_cast<f32>(index & 1u) * tile;
            const f32 y = static_cast<f32>(index >> 1u) * tile;
            cmd.setPipeline(shadowPipeline_);
            cmd.setViewport({.x = x, .y = y, .width = tile, .height = tile});
            cmd.setScissor({.x = static_cast<core::i32>(x),
                            .y = static_cast<core::i32>(y),
                            .width = static_cast<core::i32>(settings_.shadowTileResolution),
                            .height = static_cast<core::i32>(settings_.shadowTileResolution)});

            // The cascade's sphere, in the same camera-relative space the fit
            // used and the draws are in. It comes BACK from the fit now: the
            // box is sized to the casters and the sphere is sized to what the
            // camera can see, and those stopped being the same number when the
            // box learned to be smaller than the slice.
            // Shrunk by the filter's reach, the way Unity's
            // `cullingSphere.w -= filterSize` is (U-58): a fragment at the very
            // edge of a cascade has taps that step outside it, and a caster
            // culled because its centre was outside would leave those taps
            // reading empty depth.
            const f32 filterReach = cascades.texelWorld[index] * kShadowFilterMaxTexels;
            const CullSphere cull{cascades.cullCentre[index], cascades.cullRadius[index] + filterReach};
            drawGeometry(cmd, world, meshes, cascades.viewProjection[index], shadowPipeline_, shadowSkinnedPipeline_,
                         Selection::Shadow, &cull);
        }
    }
    cmd.endRenderPass();
    cmd.popDebugGroup();

    // --- Depth prepass -------------------------------------------------------
    //
    // **This is the roadmap's design constraint, answered.** The scene's depth
    // has to be samplable by a later pass, and a prepass is what makes that
    // possible without asking the frozen RHI for a read-only depth state: depth
    // is written here, sampled by the occlusion pass, and attached again by the
    // forward pass -- never a texture and an attachment at the same time.
    //
    // What it costs is a second geometry submission, which is CPU work in the
    // exact place the instanced path exists to reduce. What it buys, besides the
    // constraint, is early-Z rejection for the forward pass.
    cmd.pushDebugGroup("depth-prepass");
    cmd.beginRenderPass({
        .colorAttachments = {},
        .depthStencil = {.texture = depth_, .loadOp = rhi::LoadOp::Clear, .storeOp = rhi::StoreOp::Store},
        .debugName = "depth-prepass",
    });
    cmd.setViewport({.width = static_cast<f32>(renderWidth_), .height = static_cast<f32>(renderHeight_)});
    cmd.setScissor({.width = static_cast<core::i32>(renderWidth_), .height = static_cast<core::i32>(renderHeight_)});
    if (world.camera.valid) {
        cmd.setPipeline(depthPrepassPipeline_);
        drawGeometry(cmd, world, meshes, world.camera.viewProjection, depthPrepassPipeline_,
                     depthPrepassSkinnedPipeline_, Selection::Prepass);
    }
    cmd.endRenderPass();
    cmd.popDebugGroup();

    // --- Ambient occlusion ---------------------------------------------------
    //
    // Half resolution, sixteen taps, then a depth-aware blur in two separable
    // passes. The result multiplies the environment and the ambient and nothing
    // else -- the sun has a shadow map that answers whether IT reaches a surface
    // (brief, Decision 13).
    cmd.pushDebugGroup("occlusion");
    const u32 occlusionWidth = renderWidth_ > 1 ? renderWidth_ / 2 : 1;
    const u32 occlusionHeight = renderHeight_ > 1 ? renderHeight_ / 2 : 1;
    if (!settings_.ambientOcclusion) {
        // White is "nothing is occluded", which is what the forward pass
        // multiplies its ambient term by when this one is switched off.
        clearPass(cmd, occlusion_, occlusionWidth, occlusionHeight, "occlusion-off",
                  rhi::ColorRgba{1.0f, 1.0f, 1.0f, 1.0f});
    }
    else {
        GpuSsaoUniforms ssao;
        ssao.projection[0] = world.camera.projection.m[0][0] != 0.0f ? 1.0f / world.camera.projection.m[0][0] : 1.0f;
        ssao.projection[1] = world.camera.projection.m[1][1] != 0.0f ? 1.0f / world.camera.projection.m[1][1] : 1.0f;
        ssao.projection[2] = world.camera.nearPlane;
        ssao.projection[3] = world.camera.farPlane;
        ssao.viewport[0] = static_cast<f32>(occlusionWidth);
        ssao.viewport[1] = static_cast<f32>(occlusionHeight);
        ssao.viewport[2] = 1.0f / static_cast<f32>(occlusionWidth);
        ssao.viewport[3] = 1.0f / static_cast<f32>(occlusionHeight);
        ssao.params[0] = kOcclusionRadius;
        ssao.params[1] = kOcclusionBias;
        ssao.params[2] = kOcclusionStrength;

        const std::array<rhi::TextureBinding, 1> depthBinding{rhi::TextureBinding{depth_, pointSampler_}};
        fullscreenPass(cmd, ssaoPipeline_, occlusion_, occlusionWidth, occlusionHeight, "ssao", depthBinding,
                       asBytes(&ssao, sizeof(ssao)));

        GpuBlurUniforms blur;
        blur.texelDirection[0] = 1.0f / static_cast<f32>(occlusionWidth);
        blur.texelDirection[1] = 1.0f / static_cast<f32>(occlusionHeight);
        blur.texelDirection[2] = 1.0f;
        blur.texelDirection[3] = 0.0f;
        const std::array<rhi::TextureBinding, 2> horizontal{rhi::TextureBinding{occlusion_, linearSampler_},
                                                            rhi::TextureBinding{depth_, pointSampler_}};
        fullscreenPass(cmd, ssaoBlurPipeline_, occlusionBlur_, occlusionWidth, occlusionHeight, "ssao-blur-x",
                       horizontal, asBytes(&blur, sizeof(blur)));

        blur.texelDirection[2] = 0.0f;
        blur.texelDirection[3] = 1.0f;
        const std::array<rhi::TextureBinding, 2> vertical{rhi::TextureBinding{occlusionBlur_, linearSampler_},
                                                          rhi::TextureBinding{depth_, pointSampler_}};
        fullscreenPass(cmd, ssaoBlurPipeline_, occlusion_, occlusionWidth, occlusionHeight, "ssao-blur-y", vertical,
                       asBytes(&blur, sizeof(blur)));
    }
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
        // LOADED, not cleared: the prepass wrote this depth and the occlusion
        // pass has already read it. Stored, because the blended pass tests
        // against it.
        .depthStencil = {.texture = depth_, .loadOp = rhi::LoadOp::Load, .storeOp = rhi::StoreOp::Store},
        .debugName = "forward",
    });
    cmd.setViewport({.width = static_cast<f32>(renderWidth_), .height = static_cast<f32>(renderHeight_)});
    cmd.setScissor({.width = static_cast<core::i32>(renderWidth_), .height = static_cast<core::i32>(renderHeight_)});

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
            // A cascade past the setting's count was never rendered into, so its
            // boundary is pushed past anything a frame can hold: the selection
            // loop then stops at the last cascade that WAS rendered, and a
            // fragment beyond it selects the empty tile and comes back lit. The
            // blend band makes that a fade rather than a plane, which is what a
            // shadow distance ending should look like anyway.
            frame.cascadeFar[index] =
                index < settings_.shadowCascades ? cascades.farDistance[index] : kUnreachableDistance;
            frame.cascadeTexelWorld[index] = cascades.texelWorld[index];
            frame.cascadeDepthRange[index] = cascades.depthRange[index];
        }
        frame.shadowParams[0] = kShadowFilterWorldRadius;
        frame.shadowParams[1] = kShadowNormalOffsetFilters;
        frame.shadowParams[2] = kShadowCascadeBlend;
        frame.shadowParams[3] = kShadowDepthBiasMetres;

        frame.environmentParams[0] = static_cast<f32>(kEnvironmentMipCount);
        frame.environmentParams[1] = 1.0f;
        frame.environmentParams[2] = 1.0f;
        for (u32 index = 0; index < 9; ++index) {
            frame.irradianceSh[index][0] = environment_.irradiance[index].x;
            frame.irradianceSh[index][1] = environment_.irradiance[index].y;
            frame.irradianceSh[index][2] = environment_.irradiance[index].z;
            frame.irradianceSh[index][3] = 0.0f;
        }

        // The lights themselves are in the tables uploaded above; what the block
        // carries is how to find them.
        frame.lightCountUnused[0] = static_cast<f32>(clusters_.lightCount);
        frame.clusterParams[0] = clusters_.sliceScale;
        frame.clusterParams[1] = clusters_.sliceBias;
        frame.viewportParams[0] = static_cast<f32>(renderWidth_);
        frame.viewportParams[1] = static_cast<f32>(renderHeight_);
        frame.viewportParams[2] = 1.0f / static_cast<f32>(renderWidth_);
        frame.viewportParams[3] = 1.0f / static_cast<f32>(renderHeight_);

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

    // --- Automatic exposure -------------------------------------------------
    //
    // Three passes down to one texel, and the last of them carries state: it
    // reads the exposure the LAST frame wrote and writes this frame's into the
    // other of two 1x1 targets. That ping-pong is how a value survives a frame
    // in a renderer with no compute and no readback a frame could afford.
    const u32 previousExposure = exposureIndex_;
    const u32 nextExposure = 1u - exposureIndex_;
    exposureIndex_ = nextExposure;

    cmd.pushDebugGroup("exposure");
    if (!settings_.autoExposure) {
        // A neutral gain, written directly. Skipping the three passes is the
        // point of the setting -- what is left is `ExposureCompensation` and
        // `Lighting.Brightness`, which is exactly the fixed exposure the engine
        // had before M7.5 metered anything.
        clearPass(cmd, exposure_[nextExposure], 1, 1, "exposure-fixed", rhi::ColorRgba{1.0f, 1.0f, 1.0f, 1.0f});
        // So that switching metering back on adapts instantly from the frame it
        // measures rather than from the neutral value it finds.
        exposureInitialised_ = false;
    }
    else {
        GpuLuminanceUniforms luminance;
        luminance.texelRate[0] = 1.0f / static_cast<f32>(renderWidth_);
        luminance.texelRate[1] = 1.0f / static_cast<f32>(renderHeight_);
        const std::array<rhi::TextureBinding, 1> hdrBinding{rhi::TextureBinding{hdr_, linearSampler_}};
        fullscreenPass(cmd, luminanceDownPipeline_, luminance64_, 64, 64, "luminance-down", hdrBinding,
                       asBytes(&luminance, sizeof(luminance)));

        luminance.texelRate[0] = 1.0f / 64.0f;
        luminance.texelRate[1] = 1.0f / 64.0f;
        const std::array<rhi::TextureBinding, 1> coarse{rhi::TextureBinding{luminance64_, linearSampler_}};
        fullscreenPass(cmd, luminanceReducePipeline_, luminance8_, 8, 8, "luminance-reduce", coarse,
                       asBytes(&luminance, sizeof(luminance)));

        luminance.texelRate[0] = 1.0f / 8.0f;
        luminance.texelRate[1] = 1.0f / 8.0f;
        // The first frame after a resize adapts instantly rather than from
        // whatever the freshly created target happens to hold: a run whose first
        // frames faded in from black is a run whose golden at frame two and
        // screenshot at frame thirty disagree.
        luminance.texelRate[2] = exposureInitialised_ ? kExposureAdaptationRate : 1.0f;
        const std::array<rhi::TextureBinding, 2> adapt{
            rhi::TextureBinding{luminance8_, linearSampler_},
            rhi::TextureBinding{exposure_[previousExposure], linearSampler_}};
        fullscreenPass(cmd, luminanceAdaptPipeline_, exposure_[nextExposure], 1, 1, "luminance-adapt", adapt,
                       asBytes(&luminance, sizeof(luminance)));
        exposureInitialised_ = true;
    }
    cmd.popDebugGroup();

    // --- Bloom ---------------------------------------------------------------
    //
    // Down with a thirteen-tap box, up with a tent, each level its own texture
    // because a `ColorAttachment` names a texture and not a mip level. The
    // threshold is applied once, on the way in.
    cmd.pushDebugGroup("bloom");
    if (!settings_.bloom) {
        // Black adds nothing, and the tonemap adds `bloom_[0]` unconditionally.
        // Cheaper than the eight passes it replaces and, unlike leaving the
        // chain's textures alone, does not depend on what was in them.
        clearPass(cmd, bloom_[0], bloomLevelSize(renderWidth_, 0), bloomLevelSize(renderHeight_, 0), "bloom-off",
                  rhi::ColorRgba{0.0f, 0.0f, 0.0f, 1.0f});
    }
    else {
        u32 sourceWidth = renderWidth_;
        u32 sourceHeight = renderHeight_;
        for (u32 level = 0; level < kBloomLevels; ++level) {
            GpuBloomUniforms bloom;
            bloom.texelRadius[0] = 1.0f / static_cast<f32>(sourceWidth);
            bloom.texelRadius[1] = 1.0f / static_cast<f32>(sourceHeight);
            bloom.texelRadius[2] = 1.0f;
            if (level == 0) {
                bloom.threshold[0] = kBloomThreshold;
                bloom.threshold[1] = kBloomKnee;
            }
            const std::array<rhi::TextureBinding, 1> source{
                rhi::TextureBinding{level == 0 ? hdr_ : bloom_[level - 1], linearSampler_}};
            sourceWidth = bloomLevelSize(renderWidth_, level);
            sourceHeight = bloomLevelSize(renderHeight_, level);
            fullscreenPass(cmd, bloomDownPipeline_, bloom_[level], sourceWidth, sourceHeight, "bloom-down", source,
                           asBytes(&bloom, sizeof(bloom)));
        }

        for (u32 level = kBloomLevels - 1; level > 0; --level) {
            GpuBloomUniforms bloom;
            bloom.texelRadius[0] = 1.0f / static_cast<f32>(bloomLevelSize(renderWidth_, level));
            bloom.texelRadius[1] = 1.0f / static_cast<f32>(bloomLevelSize(renderHeight_, level));
            bloom.texelRadius[2] = 1.0f;
            const std::array<rhi::TextureBinding, 1> source{rhi::TextureBinding{bloom_[level], linearSampler_}};
            // `LoadOp::Load`, because the pipeline blends ADDITIVELY into what
            // the downsample already put there -- reading and writing one target
            // in one pass is what every backend refuses.
            fullscreenPass(cmd, bloomUpPipeline_, bloom_[level - 1], bloomLevelSize(renderWidth_, level - 1),
                           bloomLevelSize(renderHeight_, level - 1), "bloom-up", source, asBytes(&bloom, sizeof(bloom)),
                           rhi::LoadOp::Load);
        }
    }
    cmd.popDebugGroup();

    // --- Tonemap ------------------------------------------------------------
    //
    // A separate pass rather than writing the swapchain directly from the
    // forward one: the HDR target has to be complete before it can be sampled,
    // and a backend is entitled to enforce that. It writes an LDR TEXTURE rather
    // than the swapchain now, because the anti-aliasing resolve needs a
    // tonemapped image to find edges in.
    GpuTonemapUniforms tonemap;
    tonemap.exposureBloom[0] = world.environment.exposureCompensation;
    tonemap.exposureBloom[1] = kBloomIntensity;
    const std::array<rhi::TextureBinding, 3> tonemapBindings{
        rhi::TextureBinding{hdr_, linearSampler_}, rhi::TextureBinding{bloom_[0], linearSampler_},
        rhi::TextureBinding{exposure_[nextExposure], linearSampler_}};

    // With anti-aliasing on, this writes the LDR texture the resolve reads and
    // the resolve is what reaches the target. With it off, this IS the resolve
    // -- and it is also where a reduced render scale is upscaled, because a
    // fullscreen pass into a larger target sampling a smaller source is exactly
    // a bilinear upscale.
    cmd.pushDebugGroup("tonemap");
    fullscreenPass(cmd, tonemapPipeline_, settings_.antiAliasing ? ldr_ : target.color,
                   settings_.antiAliasing ? renderWidth_ : target.width,
                   settings_.antiAliasing ? renderHeight_ : target.height, "tonemap", tonemapBindings,
                   asBytes(&tonemap, sizeof(tonemap)));
    cmd.popDebugGroup();

    // --- Anti-aliasing -------------------------------------------------------
    //
    // FXAA, on the tonemapped image, resolving to the swapchain. Spatial rather
    // than temporal on purpose (M7.5 brief, Decision 10), and nothing here
    // forecloses a temporal pass -- which would replace this one rather than
    // fight it.
    if (settings_.antiAliasing) {
        GpuFxaaUniforms fxaa;
        // The SOURCE's texel, not the target's. FXAA walks an edge in the image
        // it is reading, and at a reduced render scale that image is smaller
        // than what it writes -- a step sized in output texels would look for
        // edges at the wrong spacing and find none.
        fxaa.texel[0] = 1.0f / static_cast<f32>(renderWidth_);
        fxaa.texel[1] = 1.0f / static_cast<f32>(renderHeight_);
        const std::array<rhi::TextureBinding, 1> ldrBinding{rhi::TextureBinding{ldr_, linearSampler_}};
        cmd.pushDebugGroup("fxaa");
        fullscreenPass(cmd, fxaaPipeline_, target.color, target.width, target.height, "fxaa", ldrBinding,
                       asBytes(&fxaa, sizeof(fxaa)));
        cmd.popDebugGroup();
    }
}

std::unique_ptr<IRenderer> createDefaultRenderer()
{
    return std::make_unique<DefaultRenderer>();
}

} // namespace luaug::render
