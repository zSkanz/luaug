#include "luaug/asset/surface_shader.h"
#include "luaug/core/i18n.h"
#include "luaug/core/log.h"
#include "luaug/core/text_key.h"
#include "luaug/render/clusters.h"
#include "luaug/render/environment.h"
#include "luaug/render/particles.h"
#include "luaug/render/renderer.h"
#include "luaug/render/settings.h"
#include "luaug/render/shader_types.h"
#include "luaug/render/shadow.h"
#include "luaug/render/surface_source.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace luaug::render {
namespace {

using core::f32;
using core::Mat4;
using core::u32;
using core::Vec3;

// Pixels a metre covers at a metre's distance, for choosing a mesh's level of
// detail. Zero -- always the finest level -- under an orthographic camera, where
// how big a thing looks does not depend on how far away it is.
[[nodiscard]] f32 lodPixelsPerUnit(const render::RenderCamera& camera, u32 height) noexcept
{
    if (height == 0 || core::isOrthographic(camera.projection))
        return 0.0f;
    return 0.5f * static_cast<f32>(height) * camera.projection.m[1][1];
}

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

// Contact shadows: how far each pixel marches towards the sun, how thick a
// surface is assumed to be behind what the depth buffer shows, and where the
// effect has faded out. Sixty centimetres covers the gap a shadow map's biases
// leave at the base of anything standing on the ground with room to spare; a
// quarter of a metre of thickness stops a pole in front of the ray from
// shadowing the wall a metre behind it; and past sixty metres the gap is
// smaller than a pixel.
constexpr f32 kContactRayMetres = 0.6f;

// The value `drawGeometry` records as the bound material while caves are
// bound, so the next ordinary draw rebinds its own.
constexpr u32 kTerrainBinding = 0xFFFFFFFEu;
// The largest terrain draw the shadow fit takes as a caster, in metres: a
// full-detail node of a few chunks, and nothing coarser.
constexpr f32 kTerrainCasterRadius = 96.0f;
constexpr u32 kVoxelBinding = 0xFFFFFFFDu;

// The block atlas: sixty-four pixel tiles, sixteen to a row, 256 images. Enough
// for any block game's palette, and a quarter of a 2048 atlas's memory. RGBA16F
// because what is drawn into it is the image as the shader SAW it -- already
// linear -- and eight bits of linear crushes the darks of an sRGB image.
constexpr u32 kVoxelTileSize = 64;
constexpr u32 kVoxelTilesPerRow = 16;
constexpr u32 kVoxelAtlasSize = kVoxelTileSize * kVoxelTilesPerRow;
constexpr u32 kVoxelAtlasTiles = kVoxelTilesPerRow * kVoxelTilesPerRow;
constexpr rhi::TextureFormat kVoxelAtlasFormat = rhi::TextureFormat::Rgba16Float;
constexpr f32 kContactThicknessMetres = 0.25f;
constexpr f32 kContactFadeDistance = 60.0f;
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
// `BloomEffect.Size` at which the upsample's tent has its own radius of one
// source texel: the engine's own reach, and the class's default (ADR 0096).
constexpr f32 kBloomSize = 24.0f;
// For a `Sky`'s angular sizes, which are authored in degrees.
constexpr f32 kDegreesToRadians = 0.017453292519943295f;
// How many halvings the look's blur may go down before it runs its Gaussian:
// at five, the smallest level is a thirty-second of the frame, where a blur of
// the whole screen's height is still a handful of texels.
constexpr u32 kLookBlurLevels = 5;
// The Gaussian's width, in texels of the level it runs at, that the level is
// chosen to stay under: wide enough that a bilinear resample back to the frame
// shows no steps, narrow enough that the kernel is a dozen taps.
constexpr f32 kLookBlurLevelSigma = 4.0f;
// The widest circle of confusion `DepthOfFieldEffect` draws, in pixels of a
// 1080-line picture: what `NearIntensity` or `FarIntensity` of 1 reaches.
constexpr f32 kFocusWidestPixels = 16.0f;
// Sun rays: how many taps the gather takes towards the sun, how much each
// counts less than the one before, and how bright the shafts are at an
// `Intensity` of 1 against the sky they come from.
constexpr u32 kRaysTaps = 64;
constexpr f32 kRaysDecay = 0.975f;
constexpr f32 kRaysStrength = 3.0f;
// How far past the screen's edge, in the screen's half-widths, the sun may go
// before its rays have faded away entirely.
constexpr f32 kRaysEdgeFade = 0.6f;
// How far a ray into the open sky is taken to travel through the air, in
// metres. Far enough that a level ray is buried in any air there is, near
// enough that an air which never thins (`Decay` 0) still leaves the zenith a
// little of the sky's own colour at a low `Density`.
constexpr f32 kAirSkyReach = 40000.0f;
// The glare lobe's tightness about the sun, and its strength at `Glare` 1.
constexpr f32 kAirGlareExponent = 12.0f;
constexpr f32 kAirGlareStrength = 0.35f;
// A governed sky's night: how bright the moon's face is, how bright the
// brightest star, and the chance a cell of the star grid holds one.
constexpr f32 kMoonGlow = 1.6f;
constexpr f32 kStarGlow = 1.2f;
constexpr f32 kStarChance = 0.5f;
// The clouds' wind, in layer units a second of game time, and where its
// offset wraps: far enough that a game would have to run for days to see the
// layer jump, near enough that f32 keeps every step of it.
constexpr core::f64 kCloudWind = 0.004;
constexpr core::f64 kCloudWindWrap = 1024.0;
// The share of the air a ray into the open sky counts (`look_air.hlsl`): the
// gradient is already the air above, so the whole integral again drew a grey
// afternoon. The horizon, where the integral is largest, is buried either way.
constexpr f32 kAirSkyShare = 0.3f;

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
//
// `secondUniforms` is the fragment stage's slot 1, for a pass that keeps an
// existing block at slot 0 unchanged and adds its own beside it -- the graded
// tonemap, which must leave the plain one's block byte for byte as it was.
void fullscreenPass(rhi::ICmdList& cmd, rhi::PipelineHandle pipeline, rhi::TextureHandle target, u32 width, u32 height,
                    std::string_view name, std::span<const rhi::TextureBinding> textures,
                    std::span<const std::byte> uniforms, rhi::LoadOp loadOp = rhi::LoadOp::Clear,
                    std::span<const std::byte> secondUniforms = {})
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
    if (!secondUniforms.empty())
        cmd.bindUniforms(rhi::ShaderStage::Fragment, 1, secondUniforms);
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
// How far the clouds may drift, in layer units, before the reflections are
// rebuilt to show where they went: about every five seconds of game time.
constexpr f32 kCloudRebakeDrift = 0.02f;

struct EnvironmentCache
{
    SkyParams target{};
    bool everBaked = false;
    bool dirty[kEnvironmentMipCount]{};
    // Which level this frame uploads. Advances every frame regardless of
    // anything, which is the whole point.
    u32 cursor = 0;
    // What the shader is given, and what the last bake produced. They are two
    // fields because the second one STEPS: it is only recomputed when the sky
    // has drifted past `kEnvironmentRebuildCosine`, which is about once every
    // hundred frames under a fast day cycle, and a diffuse ambient that arrives
    // in steps is a world whose every matte surface pulses at once (D053).
    Vec3 irradiance[9]{};
    Vec3 irradianceTarget[9]{};
    // The sky the target was projected from, which is a different question from
    // the one `target` above answers -- see `irradianceStale`.
    SkyParams irradianceSky;
    // And what LAST FRAME's sky was, which is a third question again: it is how
    // a clock being scrubbed is told apart from a clock running.
    SkyParams previousSky;
    bool hasPreviousSky = false;
    // Kept resident rather than rebuilt into one scratch buffer: the upload
    // happens every frame and the bake does not, so the pixels have to outlive
    // the bake that made them. About 171 KiB for the whole chain.
    std::vector<core::u16> levels[kEnvironmentMipCount];
    std::vector<core::u16> lut;

    // True when `params` differs from what the chain was baked from by enough
    // to be worth the work. The sun moving is the common case; the horizon
    // colour changing is a script writing `Lighting.FogColor` and is rare, so it
    // is tested exactly rather than with a threshold.
    // The diffuse half's own test. Same shape as `stale`, four times tighter,
    // and cheap enough to be: see the note where it is called.
    [[nodiscard]] bool irradianceStale(const SkyParams& params) const noexcept
    {
        if (!everBaked)
            return true;
        if (core::dot(params.sunDirection, irradianceSky.sunDirection) < kIrradianceRebuildCosine)
            return true;
        return !(params.horizonColor == irradianceSky.horizonColor && params.zenithColor == irradianceSky.zenithColor &&
                 params.sunColor == irradianceSky.sunColor && params.skybox == irradianceSky.skybox &&
                 params.celestial == irradianceSky.celestial);
    }

    [[nodiscard]] bool stale(const SkyParams& params) const noexcept
    {
        if (!everBaked)
            return true;
        if (core::dot(params.sunDirection, target.sunDirection) < kEnvironmentRebuildCosine)
            return true;
        return !(params.horizonColor == target.horizonColor && params.zenithColor == target.zenithColor &&
                 params.sunColor == target.sunColor && params.specularScale == target.specularScale &&
                 params.skybox == target.skybox && params.celestial == target.celestial &&
                 params.sunAngularRadius == target.sunAngularRadius && params.cloudCover == target.cloudCover &&
                 params.cloudDensity == target.cloudDensity && params.cloudColor == target.cloudColor &&
                 std::abs(params.cloudDriftX - target.cloudDriftX) < kCloudRebakeDrift &&
                 std::abs(params.cloudDriftZ - target.cloudDriftZ) < kCloudRebakeDrift);
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
    void setSurfaceSource(ISurfaceSource* source) override { surfaceSource_ = source; }
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
        // Every draw a tool has selected, into a single-channel mask. Depth-only
        // in the sense that matters here -- it wants a position and nothing else
        // -- which is why it shares the shadow pass's vertex shaders.
        Outline,
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

    // --- Surface shaders (ADR 0091) -------------------------------------------
    //
    // **Built the first time a frame names one, and never before** -- the
    // terrain's rule, for the terrain's reason: a renderer that made them in
    // `create` would change the command stream, and every capture golden, of
    // every scene that has none. A surface that cannot be built is tried once,
    // reported once, and drawn as the built-in surface.
    struct SurfaceSet
    {
        std::string name;
        bool ready = false;
        // A URN surface's program revision, so a recompile rebuilds.
        core::u64 revision = 0;
        asset::SurfaceReflection reflection;
        std::array<rhi::ShaderHandle, 10> shaders{};
        rhi::PipelineHandle forward{};
        rhi::PipelineHandle blended{};
        rhi::PipelineHandle instanced{};
        rhi::PipelineHandle shadow{};
        rhi::PipelineHandle shadowInstanced{};
        rhi::PipelineHandle prepass{};
        rhi::PipelineHandle prepassInstanced{};
    };
    std::vector<SurfaceSet> surfaces_;
    ISurfaceSource* surfaceSource_ = nullptr;
    // Per material of the frame: whether its surface failed, and draws as the
    // error surface -- loud magenta, the colour no material means.
    std::vector<bool> materialError_;
    // Per material of the frame: its surface (index + 1, 0 for the built-in),
    // its packed block, and its surface textures in declaration order.
    std::vector<u32> materialSurface_;
    std::vector<std::vector<core::u8>> materialBlock_;
    std::vector<std::array<rhi::TextureBinding, asset::MaxSurfaceTextures>> materialSurfaceTextures_;
    [[nodiscard]] u32 surfaceFor(rhi::IDevice& device, std::string_view name, bool& failed);
    [[nodiscard]] bool buildSurfacePipelines(rhi::IDevice& device, SurfaceSet& set);
    static void releaseSurface(rhi::IDevice& device, SurfaceSet& set);
    void prepareSurfaces(rhi::IDevice& device, const RenderWorld& world);
    void bindSurface(rhi::ICmdList& cmd, u32 material, bool fragment, bool blended) const;
    // Before the blended pass: the scene's depth and, when asked, colour.
    void copySceneForSurfaces(rhi::IDevice& device, rhi::ICmdList& cmd, const RenderWorld& world,
                              const GpuFrameUniforms& frame);

    // --- Terrain (ADR 0082) --------------------------------------------------
    //
    // **Created the first time a frame has terrain, and never before.** A
    // renderer that made these in `create` would add two pipelines to the
    // command stream of every scene -- and to every capture golden of a scene
    // that has no terrain at all. Deferred, a project that never touches
    // terrain pays nothing and its goldens do not move.
    [[nodiscard]] bool ensureTerrain(rhi::IDevice& device);
    // The block shader's pipeline, on the same lazy terms as the terrain's.
    [[nodiscard]] bool ensureVoxel(rhi::IDevice& device);
    // The particle pipeline and its instance buffer, on the same lazy terms.
    [[nodiscard]] bool ensureParticles(rhi::IDevice& device);
    // The decal pipeline, on the same lazy terms.
    [[nodiscard]] bool ensureDecals(rhi::IDevice& device);
    // The world UI pipelines and buffer, on the same lazy terms.
    [[nodiscard]] bool ensureWorldUi(rhi::IDevice& device);
    // The sprite pipeline and its instance buffer (the 2D layer), on the same
    // lazy terms: a world with nothing on the plane builds neither.
    [[nodiscard]] bool ensureSprites(rhi::IDevice& device);

    // **A pipeline the look needs (ADR 0096), made the first frame it is
    // used** -- as the decals' and the particles' are, and for their reason: a
    // world with none of these instances builds none of them, so its command
    // stream is the one it always was, shader for shader.
    struct LookPipeline
    {
        rhi::PipelineHandle handle{};
        bool tried = false;
    };
    // How a look pipeline writes: over what is there, added to it, or laid over
    // it as air -- `dst = src + dst * srcAlpha`, the colour the air adds and
    // the fraction of what is behind it that survives.
    enum class LookBlend : core::u8
    {
        Replace,
        Add,
        Air,
    };
    [[nodiscard]] bool ensureLookPipeline(rhi::IDevice& device, LookPipeline& slot, const char* shader,
                                          rhi::TextureFormat format, LookBlend blend = LookBlend::Replace);

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
    rhi::PipelineHandle contactPipeline_{};
    rhi::PipelineHandle bloomDownPipeline_{};
    rhi::PipelineHandle bloomUpPipeline_{};
    rhi::PipelineHandle luminanceDownPipeline_{};
    rhi::PipelineHandle luminanceReducePipeline_{};
    rhi::PipelineHandle luminanceAdaptPipeline_{};
    rhi::PipelineHandle tonemapPipeline_{};
    rhi::PipelineHandle fxaaPipeline_{};
    // The editor's selection silhouette. Four pipelines because the mask draws
    // the same three geometry variants everything else does, plus the fullscreen
    // pass that turns the mask into a line.
    rhi::PipelineHandle outlinePipeline_{};
    rhi::PipelineHandle outlineSkinnedPipeline_{};
    rhi::PipelineHandle outlineCompositePipeline_{};

    // Every shader handle this renderer created, so `destroy` can release them
    // without a second list. Sized with room: `create` silently stops recording
    // once it is full and the overflow leaks at shutdown, which is a bug that
    // announces itself nowhere.
    rhi::ShaderHandle shaders_[128]{};
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
    // One channel: which pixels belong to something a tool has selected. Only
    // ever written when a draw carries `outlined`, which no game does.
    rhi::TextureHandle outlineMask_{};
    rhi::TextureHandle occlusionBlur_{};
    // Full resolution, one channel: the sun's contact shadows. Full rather than
    // half like the occlusion term, because what it carries is the sharp line
    // where a caster meets the ground.
    rhi::TextureHandle contact_{};
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
    // The local-light atlas and this frame's assignment of its tiles. Held
    // across frames only so the vector behind `localCandidates_` keeps its
    // capacity; nothing in either survives a frame.
    rhi::TextureHandle localShadowMap_{};
    LocalShadows localShadows_{};
    std::vector<LocalShadowCandidate> localCandidates_;
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

    // The terrain's pipelines, made by `ensureTerrain`: the forward pass with
    // the terrain's look, and the shadow pass with no culling and a push away
    // from the light. The depth prepass draws a terrain as the static mesh it
    // is.
    const ShaderLibrary* shaderLibrary_ = nullptr;
    bool terrainTried_ = false;
    bool terrainValid_ = false;
    rhi::PipelineHandle terrainPipeline_{};
    rhi::PipelineHandle terrainShadowPipeline_{};
    // How far the shadow pass being drawn pushes the terrain from the light,
    // in the light's clip depth. Set per cascade; zero for a local light.
    f32 terrainShadowPush_ = 0.0f;
    // The block world's forward pipeline, made the first frame a block is
    // drawn, and the palette it reads, filled each frame from the registry.
    rhi::PipelineHandle voxelPipeline_{};
    // The same shader, blended and not writing depth, for glass and water.
    rhi::PipelineHandle voxelBlendPipeline_{};
    // The cutout faces' shadow: depth only, with the forward pass's hole test,
    // so a leaf block casts the leaves rather than a square.
    rhi::PipelineHandle voxelShadowPipeline_{};
    bool voxelTried_ = false;
    GpuVoxelPalette voxelPalette_{};
    // The block atlas (V1): one tile per distinct block image, filled by
    // drawing each image into its square the first frame it is loaded -- which
    // is what lets it hold compiled images the CPU has no pixels for.
    rhi::TextureHandle voxelAtlas_{};
    rhi::PipelineHandle voxelTilePipeline_{};
    // Which texture is in which tile, by handle, in the order they arrived.
    std::vector<std::pair<u32, u32>> voxelTiles_;
    // Particles (F2): the pipeline, made the first frame one is drawn, and
    // the instance stream this frame uploads into.
    rhi::PipelineHandle particlePipeline_{};
    // Decals (F2), made the first frame one is drawn.
    rhi::PipelineHandle decalPipeline_{};
    bool decalTried_ = false;
    rhi::BufferHandle particleBuffer_{};
    // World-space UI (F3): one pipeline tested against depth and one that is
    // not, for `AlwaysOnTop`, and a vertex buffer of `MaxWorldUiVertices`.
    rhi::PipelineHandle worldUiPipeline_{};
    rhi::PipelineHandle worldUiOnTopPipeline_{};
    rhi::BufferHandle worldUiBuffer_{};
    bool worldUiTried_ = false;
    u32 worldUiVertexCount_ = 0;
    bool particleTried_ = false;
    std::vector<GpuParticle> particleStaging_;
    u32 particleCount_ = 0;
    // The 2D layer's sprites: one instance each, drawn in runs that share an
    // image and a filter.
    rhi::PipelineHandle spritePipeline_{};
    rhi::BufferHandle spriteBuffer_{};
    bool spriteTried_ = false;
    std::vector<GpuSprite> spriteStaging_;
    u32 spriteCount_ = 0;
    // The palette the terrain shader reads, filled once a frame.
    GpuTerrainSurfaceUniforms terrainSurface_{};

    // --- The look (ADR 0096) --------------------------------------------------
    //
    // `tonemap.hlsl` with every colour correction folded in, for a frame that
    // has one.
    LookPipeline gradedTonemap_;
    // One direction of a separable Gaussian, and a filtered copy from one size
    // to another.
    LookPipeline blur_;
    LookPipeline resample_;
    // Depth of field's three passes.
    LookPipeline focusPrepare_;
    LookPipeline focusGather_;
    LookPipeline focusComposite_;
    // Sun rays' two passes, and the resample again with an additive blend, to
    // lay the shafts over the frame without reading it.
    LookPipeline raysMask_;
    LookPipeline raysGather_;
    LookPipeline raysAdd_;
    // The air, laid over the opaque world and the sky.
    LookPipeline air_;
    // The scene behind blended surface shaders (ADR 0091): its depth as
    // distances, and its colour -- each made only in a frame that needs it.
    LookPipeline surfaceSceneDepth_;
    rhi::TextureHandle sceneDepthCopy_{};
    rhi::TextureHandle sceneColorCopy_{};
    // The sky a `Sky` governs. Made by `ensureSkyLook` rather than as a
    // fullscreen pass: it is drawn INSIDE the forward pass, so it declares
    // that pass's depth format as the plain sky's pipeline does.
    LookPipeline skyLook_;
    [[nodiscard]] bool ensureSkyLook(rhi::IDevice& device);

    // Every look pipeline, for `destroy`.
    [[nodiscard]] std::array<LookPipeline*, 12> lookPipelines() noexcept
    {
        return {&gradedTonemap_, &blur_,       &resample_, &focusPrepare_, &focusGather_,       &focusComposite_,
                &raysMask_,      &raysGather_, &raysAdd_,  &air_,          &surfaceSceneDepth_, &skyLook_};
    }

    // **The look's own images, made the first frame one is needed** and
    // remade when the render size changes -- never on a frame without the
    // effect that needs them. `lookColor_` is a second full-resolution HDR
    // image, for a pass that reads the frame and has to write a changed one
    // somewhere else; the blur's levels are its downsample chain and each
    // level's ping-pong partner.
    rhi::TextureHandle lookColor_{};
    rhi::TextureHandle blurLevels_[kLookBlurLevels]{};
    rhi::TextureHandle blurPong_[kLookBlurLevels]{};
    // Depth of field at half resolution: the frame with each texel's circle,
    // and what the gather made of it.
    rhi::TextureHandle focusPrepared_{};
    rhi::TextureHandle focusGathered_{};
    // Sun rays at half the frame: what can shine, and the shafts.
    rhi::TextureHandle raysMasked_{};
    rhi::TextureHandle raysGathered_{};
    u32 lookWidth_ = 0;
    u32 lookHeight_ = 0;
    [[nodiscard]] bool lookTexture(rhi::IDevice& device, rhi::TextureHandle& slot, u32 width, u32 height,
                                   const char* name);
    void releaseLookTextures(rhi::IDevice& device);
    // Blurs `image` in place by `size` pixels of a 1080-line picture.
    void blurImage(rhi::IDevice& device, rhi::ICmdList& cmd, rhi::TextureHandle image, f32 size);
    // Focuses `image` by distance into the other full-resolution image, and
    // returns that one -- or `image` itself when the passes cannot be made.
    [[nodiscard]] rhi::TextureHandle focusImage(rhi::IDevice& device, rhi::ICmdList& cmd, const RenderWorld& world,
                                                rhi::TextureHandle image);
    // Adds the sun's shafts onto `image`.
    void sunRaysOnto(rhi::IDevice& device, rhi::ICmdList& cmd, const RenderWorld& world, const SkyParams& sky,
                     rhi::TextureHandle image);
};

} // namespace

std::optional<core::EngineError> DefaultRenderer::create(rhi::IDevice& device, const ShaderLibrary& shaders,
                                                         rhi::TextureFormat colorFormat)
{
    colorFormat_ = colorFormat;
    shaderLibrary_ = &shaders;

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
    const rhi::ShaderHandle contactVertex = load("contact_shadow", rhi::ShaderStage::Vertex);
    const rhi::ShaderHandle contactFragment = load("contact_shadow", rhi::ShaderStage::Fragment);
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
    // The editor's selection silhouette. The mask's own vertex stage makes the
    // static pipeline; the skinned and instanced ones pair this fragment with
    // the shadow pass's vertex stages, because "position through a matrix" is
    // the same shader whether it writes depth or a one.
    const rhi::ShaderHandle outlineVertex = load("outline_mask", rhi::ShaderStage::Vertex);
    const rhi::ShaderHandle outlineFragment = load("outline_mask", rhi::ShaderStage::Fragment);
    const rhi::ShaderHandle outlineCompositeVertex = load("outline_composite", rhi::ShaderStage::Vertex);
    const rhi::ShaderHandle outlineCompositeFragment = load("outline_composite", rhi::ShaderStage::Fragment);

    for (const rhi::ShaderHandle handle : {shadowInstancedVertex,
                                           shadowInstancedFragment,
                                           pbrInstancedVertex,
                                           pbrInstancedFragment,
                                           ssaoVertex,
                                           ssaoFragment,
                                           ssaoBlurVertex,
                                           ssaoBlurFragment,
                                           contactVertex,
                                           contactFragment,
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
                                           fxaaFragment,
                                           outlineVertex,
                                           outlineFragment,
                                           outlineCompositeVertex,
                                           outlineCompositeFragment}) {
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
    contactPipeline_ = fullscreen(contactVertex, contactFragment, occlusionTarget, "contact_shadow");
    bloomDownPipeline_ = fullscreen(bloomDownVertex, bloomDownFragment, hdrTarget, "bloom_down");
    bloomUpPipeline_ = fullscreen(bloomUpVertex, bloomUpFragment, bloomAddTarget, "bloom_up");
    luminanceDownPipeline_ = fullscreen(luminanceDownVertex, luminanceDownFragment, luminanceTarget, "luminance_down");
    luminanceReducePipeline_ =
        fullscreen(luminanceReduceVertex, luminanceReduceFragment, luminanceTarget, "luminance_reduce");
    luminanceAdaptPipeline_ =
        fullscreen(luminanceAdaptVertex, luminanceAdaptFragment, luminanceTarget, "luminance_adapt");
    tonemapPipeline_ = fullscreen(tonemapVertex, tonemapFragment, ldrTarget, "tonemap");
    fxaaPipeline_ = fullscreen(fxaaVertex, fxaaFragment, swapTarget, "fxaa");

    // --- The editor's selection silhouette -----------------------------------
    //
    // The mask draws the same three geometry variants as everything else, with
    // the shadow pass's vertex stages: what a mask needs from a vertex is a
    // position through a matrix, which is what a depth-only pass needs too.
    //
    // **Front faces only and no depth at all.** The mask asks whether a pixel is
    // covered by the selected object, not by which part of it, so back-face
    // culling halves the fill for free -- and testing depth would be asking a
    // buffer this pass does not have. The consequence is deliberate and it is
    // the one an editor wants: the outline shows through what is in front of it,
    // so selecting something you cannot see still tells you where it is.
    const std::array<rhi::ColorTargetDesc, 1> maskTarget{rhi::ColorTargetDesc{.format = kOcclusionFormat}};
    outlinePipeline_ = device.createGraphicsPipeline({
        .vertexShader = outlineVertex,
        .fragmentShader = outlineFragment,
        .vertexBuffers = buffers,
        .vertexAttributes = attributes,
        .rasterizer = {.cullMode = rhi::CullMode::Back},
        .depthStencil = {.depthTest = false, .depthWrite = false},
        .colorTargets = maskTarget,
        .debugName = "outline_mask",
    });
    outlineSkinnedPipeline_ = device.createGraphicsPipeline({
        .vertexShader = shadowSkinnedVertex,
        .fragmentShader = outlineFragment,
        .vertexBuffers = skinnedBuffers,
        .vertexAttributes = shadowSkinnedAttributes,
        .rasterizer = {.cullMode = rhi::CullMode::Back},
        .depthStencil = {.depthTest = false, .depthWrite = false},
        .colorTargets = maskTarget,
        .debugName = "outline_mask_skinned",
    });
    // Straight alpha over whatever the frame already holds. The shader hands
    // back a premultiplied colour with its own coverage in alpha, so the source
    // factor is One.
    const std::array<rhi::ColorTargetDesc, 1> outlineTarget{rhi::ColorTargetDesc{
        .format = colorFormat,
        .blend = {.enabled = true,
                  .srcColor = rhi::BlendFactor::One,
                  .dstColor = rhi::BlendFactor::OneMinusSrcAlpha,
                  .srcAlpha = rhi::BlendFactor::One,
                  .dstAlpha = rhi::BlendFactor::OneMinusSrcAlpha},
    }};
    outlineCompositePipeline_ =
        fullscreen(outlineCompositeVertex, outlineCompositeFragment, outlineTarget, "outline_composite");

    if (!outlinePipeline_.valid() || !outlineSkinnedPipeline_.valid() || !outlineCompositePipeline_.valid()) {
        destroy(device);
        return core::makeError(LUAUG_TR("render.err.pipeline_create_failed"));
    }

    if (!shadowPipeline_.valid() || !pbrPipeline_.valid() || !pbrBlendPipeline_.valid() || !skyPipeline_.valid() ||
        !tonemapPipeline_.valid() || !shadowSkinnedPipeline_.valid() || !pbrSkinnedPipeline_.valid() ||
        !pbrSkinnedBlendPipeline_.valid() || !depthPrepassPipeline_.valid() || !depthPrepassSkinnedPipeline_.valid() ||
        !ssaoPipeline_.valid() || !ssaoBlurPipeline_.valid() || !contactPipeline_.valid() ||
        !bloomDownPipeline_.valid() || !bloomUpPipeline_.valid() || !luminanceDownPipeline_.valid() ||
        !luminanceReducePipeline_.valid() || !luminanceAdaptPipeline_.valid() || !fxaaPipeline_.valid() ||
        !shadowInstancedPipeline_.valid() || !depthPrepassInstancedPipeline_.valid() ||
        !pbrInstancedPipeline_.valid()) {
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

    // The LOCAL atlas, for spots and points (`shadow.h`). Fixed size rather than
    // scaled by the shadow settings: its tiles are per light and not per camera
    // slice, so the quality dial that sizes a cascade says nothing about it.
    if (localShadowMap_.valid())
        device.destroy(localShadowMap_);
    localShadowMap_ = device.createTexture({
        .format = kShadowFormat,
        .usage = rhi::TextureUsage::DepthStencilTarget | rhi::TextureUsage::Sampled,
        .width = kLocalShadowAtlasResolution,
        .height = kLocalShadowAtlasResolution,
        .debugName = "local-shadow-atlas",
    });
    if (!localShadowMap_.valid())
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

    for (rhi::TextureHandle* texture : {&hdr_, &depth_, &ldr_, &occlusion_, &occlusionBlur_, &contact_, &outlineMask_,
                                        &luminance64_, &luminance8_, &exposure_[0], &exposure_[1]}) {
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
    // Full resolution, unlike the occlusion term beside it: what this carries is
    // an EDGE, and half a pixel of it is the difference between an outline and a
    // suggestion.
    outlineMask_ = device.createTexture({
        .format = kOcclusionFormat,
        .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled,
        .width = width,
        .height = height,
        .debugName = "outline-mask",
    });
    contact_ = device.createTexture({
        .format = kOcclusionFormat,
        .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled,
        .width = width,
        .height = height,
        .debugName = "contact-shadow",
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
        !outlineMask_.valid() || !luminance64_.valid() || !luminance8_.valid() || !exposure_[0].valid() ||
        !exposure_[1].valid())
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

    for (SurfaceSet& surface : surfaces_)
        releaseSurface(device, surface);
    surfaces_.clear();

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
                                          &contactPipeline_,
                                          &bloomDownPipeline_,
                                          &bloomUpPipeline_,
                                          &luminanceDownPipeline_,
                                          &luminanceReducePipeline_,
                                          &luminanceAdaptPipeline_,
                                          &fxaaPipeline_,
                                          &shadowInstancedPipeline_,
                                          &depthPrepassInstancedPipeline_,
                                          &pbrInstancedPipeline_,
                                          &outlinePipeline_,
                                          &outlineSkinnedPipeline_,
                                          &outlineCompositePipeline_}) {
        if (pipeline->valid())
            device.destroy(*pipeline);
        *pipeline = {};
    }
    for (rhi::TextureHandle* texture : {&hdr_,
                                        &depth_,
                                        &ldr_,
                                        &occlusion_,
                                        &occlusionBlur_,
                                        &contact_,
                                        &outlineMask_,
                                        &luminance64_,
                                        &luminance8_,
                                        &exposure_[0],
                                        &exposure_[1],
                                        &shadowMap_,
                                        &localShadowMap_,
                                        &whitePixel_,
                                        &flatNormalPixel_,
                                        &blackPixel_,
                                        &environmentMap_,
                                        &brdfLut_,
                                        &clusterGrid_,
                                        &lightIndices_,
                                        &lightData_}) {
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

    for (rhi::PipelineHandle* pipeline :
         {&terrainPipeline_, &terrainShadowPipeline_, &voxelPipeline_, &particlePipeline_, &voxelTilePipeline_,
          &voxelBlendPipeline_, &voxelShadowPipeline_, &decalPipeline_, &worldUiPipeline_, &worldUiOnTopPipeline_,
          &spritePipeline_}) {
        if (pipeline->valid())
            device.destroy(*pipeline);
        *pipeline = {};
    }
    terrainTried_ = false;
    terrainValid_ = false;
    voxelTried_ = false;
    if (particleBuffer_.valid())
        device.destroy(particleBuffer_);
    particleBuffer_ = {};
    if (worldUiBuffer_.valid())
        device.destroy(worldUiBuffer_);
    worldUiBuffer_ = {};
    if (spriteBuffer_.valid())
        device.destroy(spriteBuffer_);
    spriteBuffer_ = {};
    spriteTried_ = false;
    if (voxelAtlas_.valid())
        device.destroy(voxelAtlas_);
    voxelAtlas_ = {};
    voxelTiles_.clear();
    particleTried_ = false;
    decalTried_ = false;
    worldUiTried_ = false;
    for (LookPipeline* look : lookPipelines()) {
        if (look->handle.valid())
            device.destroy(look->handle);
        *look = {};
    }
    releaseLookTextures(device);
    lookWidth_ = 0;
    lookHeight_ = 0;

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

void DefaultRenderer::releaseSurface(rhi::IDevice& device, SurfaceSet& set)
{
    for (rhi::PipelineHandle* pipeline : {&set.forward, &set.blended, &set.instanced, &set.shadow, &set.shadowInstanced,
                                          &set.prepass, &set.prepassInstanced}) {
        if (pipeline->valid())
            device.destroy(*pipeline);
        *pipeline = rhi::PipelineHandle{};
    }
    for (rhi::ShaderHandle& shader : set.shaders) {
        if (shader.valid())
            device.destroy(shader);
        shader = rhi::ShaderHandle{};
    }
    set.ready = false;
}

u32 DefaultRenderer::surfaceFor(rhi::IDevice& device, std::string_view name, bool& failed)
{
    failed = false;
    // **A user's surface, by URN**: asked for every frame, never waited on.
    if (name.find("://") != std::string_view::npos) {
        const SurfaceProgram* program = nullptr;
        const SurfaceStatus status = surfaceSource_ != nullptr
                                         ? surfaceSource_->find(name, shaderLibrary_->format(), program)
                                         : SurfaceStatus::Pending;
        if (status == SurfaceStatus::Failed || (status == SurfaceStatus::Ready && program == nullptr)) {
            failed = status == SurfaceStatus::Failed;
            return 0;
        }
        if (status == SurfaceStatus::Pending)
            return 0;
        core::usize found = surfaces_.size();
        for (core::usize index = 0; index < surfaces_.size(); ++index) {
            if (surfaces_[index].name == name)
                found = index;
        }
        if (found < surfaces_.size() && surfaces_[found].revision == program->revision)
            return surfaces_[found].ready ? static_cast<u32>(found) + 1u : 0u;
        if (found == surfaces_.size()) {
            surfaces_.emplace_back().name = std::string(name);
        }
        SurfaceSet& set = surfaces_[found];
        // A frame in flight may still hold the old pipelines; a recompile is
        // a save in the editor, rare enough to wait for the GPU.
        if (set.ready)
            device.waitIdle();
        releaseSurface(device, set);
        // Timed, because it is the one cost a surface adds on the frame that
        // first draws it: ten shaders and every pipeline, on the render thread.
        const auto started = std::chrono::steady_clock::now();
        set.revision = program->revision;
        set.reflection = program->reflection;
        for (core::usize index = 0; index < 5; ++index) {
            for (const bool fragment : {false, true}) {
                const asset::SurfaceResourceCounts counts = asset::surfaceResourceCounts(
                    set.reflection, static_cast<asset::SurfaceVariant>(index),
                    fragment ? asset::SurfaceStage::Fragment : asset::SurfaceStage::Vertex);
                const std::vector<std::byte>& code = program->code[index * 2 + (fragment ? 1 : 0)];
                set.shaders[index * 2 + (fragment ? 1 : 0)] = device.createShader({
                    .stage = fragment ? rhi::ShaderStage::Fragment : rhi::ShaderStage::Vertex,
                    .format = shaderLibrary_->format(),
                    .code = code,
                    .entryPoint = fragment ? "FragmentMain" : "VertexMain",
                    .samplerCount = counts.samplers,
                    .uniformBufferCount = counts.uniformBuffers,
                    .debugName = set.name,
                });
            }
        }
        if (!buildSurfacePipelines(device, set)) {
            failed = true;
            return 0;
        }
        const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started);
        const std::array<core::I18nArg, 2> args{
            core::I18nArg{"urn", name}, core::I18nArg{"milliseconds", std::lround(elapsed.count() * 10.0) / 10.0}};
        core::log(core::LogLevel::Info, LUAUG_TR("render.info.surface_pipelines"), args);
        return static_cast<u32>(found) + 1u;
    }

    for (core::usize index = 0; index < surfaces_.size(); ++index) {
        if (surfaces_[index].name == name)
            return surfaces_[index].ready ? static_cast<u32>(index) + 1u : 0u;
    }
    SurfaceSet& set = surfaces_.emplace_back();
    set.name = std::string(name);

    const std::optional<std::string> source =
        shaderLibrary_ != nullptr ? shaderLibrary_->surfaceSource(name) : std::nullopt;
    if (!source.has_value()) {
        const std::array<core::I18nArg, 1> args{core::I18nArg{"name", name}};
        core::log(core::LogLevel::Warn, LUAUG_TR("render.warn.surface_unavailable"), args);
        return 0;
    }
    set.reflection = asset::reflectSurface(*source);
    if (!set.reflection.ok()) {
        const std::array<core::I18nArg, 1> args{core::I18nArg{"name", name}};
        core::log(core::LogLevel::Warn, LUAUG_TR("render.warn.surface_unavailable"), args);
        return 0;
    }

    // Every variant's two stages, with the counts the layout decides.
    constexpr std::array<std::pair<asset::SurfaceVariant, std::string_view>, 5> variants{
        std::pair{asset::SurfaceVariant::Forward, std::string_view{"forward"}},
        std::pair{asset::SurfaceVariant::ForwardInstanced, std::string_view{"forward_instanced"}},
        std::pair{asset::SurfaceVariant::ForwardBlended, std::string_view{"forward_blended"}},
        std::pair{asset::SurfaceVariant::Depth, std::string_view{"depth"}},
        std::pair{asset::SurfaceVariant::DepthInstanced, std::string_view{"depth_instanced"}},
    };
    for (core::usize index = 0; index < variants.size(); ++index) {
        const std::string shaderName = "surface_" + set.name + "_" + std::string(variants[index].second);
        for (const auto& [stage, rhiStage] : {std::pair{asset::SurfaceStage::Vertex, rhi::ShaderStage::Vertex},
                                              std::pair{asset::SurfaceStage::Fragment, rhi::ShaderStage::Fragment}}) {
            const asset::SurfaceResourceCounts counts =
                asset::surfaceResourceCounts(set.reflection, variants[index].first, stage);
            set.shaders[index * 2 + (stage == asset::SurfaceStage::Vertex ? 0 : 1)] =
                shaderLibrary_->createCounted(device, shaderName, rhiStage, counts.samplers, counts.uniformBuffers);
        }
    }
    if (!buildSurfacePipelines(device, set)) {
        const std::array<core::I18nArg, 1> args{core::I18nArg{"name", name}};
        core::log(core::LogLevel::Warn, LUAUG_TR("render.warn.surface_unavailable"), args);
        return 0;
    }
    return static_cast<u32>(surfaces_.size());
}

bool DefaultRenderer::buildSurfacePipelines(rhi::IDevice& device, SurfaceSet& set)
{
    if (!std::all_of(set.shaders.begin(), set.shaders.end(), [](rhi::ShaderHandle shader) { return shader.valid(); }))
        return false;

    // The built-in surface's states, with the full vertex layout in every pass:
    // a displaced vertex casts a displaced shadow, and it may displace by its
    // normal or its uv.
    const std::array<rhi::VertexAttribute, 4> attributes{
        rhi::VertexAttribute{.location = 0, .bufferSlot = 0, .format = rhi::VertexFormat::Float3, .offsetBytes = 0},
        rhi::VertexAttribute{.location = 1, .bufferSlot = 0, .format = rhi::VertexFormat::Float3, .offsetBytes = 12},
        rhi::VertexAttribute{.location = 2, .bufferSlot = 0, .format = rhi::VertexFormat::Float4, .offsetBytes = 24},
        rhi::VertexAttribute{.location = 3, .bufferSlot = 0, .format = rhi::VertexFormat::Float2, .offsetBytes = 40},
    };
    const std::array<rhi::VertexBufferLayout, 1> buffers{rhi::VertexBufferLayout{.slot = 0, .strideBytes = 48}};
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
    const std::array<rhi::ColorTargetDesc, 1> hdrTarget{rhi::ColorTargetDesc{.format = kHdrFormat}};
    const std::array<rhi::ColorTargetDesc, 1> hdrBlendTarget{
        rhi::ColorTargetDesc{.format = kHdrFormat, .blend = {.enabled = true}}};
    const rhi::DepthStencilState depthWriting{
        .depthTest = true, .depthWrite = true, .depthCompare = rhi::CompareOp::LessOrEqual};
    const auto shader = [&](core::usize variant, bool fragment) {
        return set.shaders[variant * 2 + (fragment ? 1 : 0)];
    };

    set.forward = device.createGraphicsPipeline({
        .vertexShader = shader(0, false),
        .fragmentShader = shader(0, true),
        .vertexBuffers = buffers,
        .vertexAttributes = attributes,
        .rasterizer = {.cullMode = rhi::CullMode::Back},
        .depthStencil = depthWriting,
        .colorTargets = hdrTarget,
        .depthStencilFormat = kDepthFormat,
        .debugName = "surface_forward",
    });
    set.instanced = device.createGraphicsPipeline({
        .vertexShader = shader(1, false),
        .fragmentShader = shader(1, true),
        .vertexBuffers = instancedBuffers,
        .vertexAttributes = instancedAttributes,
        .rasterizer = {.cullMode = rhi::CullMode::Back},
        .depthStencil = depthWriting,
        .colorTargets = hdrTarget,
        .depthStencilFormat = kDepthFormat,
        .debugName = "surface_forward_instanced",
    });
    set.blended = device.createGraphicsPipeline({
        .vertexShader = shader(2, false),
        .fragmentShader = shader(2, true),
        .vertexBuffers = buffers,
        .vertexAttributes = attributes,
        .rasterizer = {.cullMode = rhi::CullMode::Back},
        .depthStencil = {.depthTest = true, .depthWrite = false, .depthCompare = rhi::CompareOp::LessOrEqual},
        .colorTargets = hdrBlendTarget,
        .depthStencilFormat = kDepthFormat,
        .debugName = "surface_forward_blended",
    });
    const auto depthPipeline = [&](core::usize variant, bool instanced, rhi::CullMode cull, rhi::TextureFormat format,
                                   const char* debugName) {
        return device.createGraphicsPipeline({
            .vertexShader = shader(variant, false),
            .fragmentShader = shader(variant, true),
            .vertexBuffers = instanced ? std::span<const rhi::VertexBufferLayout>(instancedBuffers)
                                       : std::span<const rhi::VertexBufferLayout>(buffers),
            .vertexAttributes = instanced ? std::span<const rhi::VertexAttribute>(instancedAttributes)
                                          : std::span<const rhi::VertexAttribute>(attributes),
            .rasterizer = {.cullMode = cull},
            .depthStencil = depthWriting,
            .colorTargets = {},
            .depthStencilFormat = format,
            .debugName = debugName,
        });
    };
    set.shadow = depthPipeline(3, false, rhi::CullMode::Front, kShadowFormat, "surface_shadow");
    set.shadowInstanced = depthPipeline(4, true, rhi::CullMode::Front, kShadowFormat, "surface_shadow_instanced");
    set.prepass = depthPipeline(3, false, rhi::CullMode::Back, kDepthFormat, "surface_prepass");
    set.prepassInstanced = depthPipeline(4, true, rhi::CullMode::Back, kDepthFormat, "surface_prepass_instanced");

    set.ready = set.forward.valid() && set.instanced.valid() && set.blended.valid() && set.shadow.valid() &&
                set.shadowInstanced.valid() && set.prepass.valid() && set.prepassInstanced.valid();
    return set.ready;
}

void DefaultRenderer::prepareSurfaces(rhi::IDevice& device, const RenderWorld& world)
{
    materialSurface_.assign(world.materials.size(), 0u);
    materialError_.assign(world.materials.size(), false);
    materialBlock_.resize(world.materials.size());
    materialSurfaceTextures_.resize(world.materials.size());

    // The clock and the camera, the same for every surface this frame. An f32
    // of seconds keeps a millisecond for about four and a half hours of play.
    const f32 clock[4] = {static_cast<f32>(world.environment.surfaceTime), static_cast<f32>(world.camera.origin.x),
                          static_cast<f32>(world.camera.origin.y), static_cast<f32>(world.camera.origin.z)};

    for (core::usize index = 0; index < world.materials.size(); ++index) {
        const RenderMaterial& material = world.materials[index];
        const std::string_view name =
            material.surface.empty() ? std::string_view{settings_.forcedSurface} : std::string_view{material.surface};
        if (name.empty())
            continue;
        bool failed = false;
        const u32 surface = surfaceFor(device, name, failed);
        materialError_[index] = failed;
        if (surface == 0)
            continue;
        materialSurface_[index] = surface;
        const asset::SurfaceReflection& reflection = surfaces_[surface - 1].reflection;

        // **The built-in fields first, under their own names**, then what the
        // material says for its shader: a surface that declares `Color` gets
        // the part's colour, and one that declares its own `Color` default
        // gets it only when nothing set one.
        const auto builtIn = [&](std::string_view field) -> std::optional<std::array<f32, 4>> {
            const GpuMaterialUniforms& u = material.uniforms;
            if (field == "Color")
                return std::array<f32, 4>{u.baseColor[0], u.baseColor[1], u.baseColor[2], u.baseColor[3]};
            if (field == "Metalness")
                return std::array<f32, 4>{u.metallicRoughnessNormalCutoff[0], 0.0f, 0.0f, 0.0f};
            if (field == "Roughness")
                return std::array<f32, 4>{u.metallicRoughnessNormalCutoff[1], 0.0f, 0.0f, 0.0f};
            if (field == "NormalScale")
                return std::array<f32, 4>{u.metallicRoughnessNormalCutoff[2], 0.0f, 0.0f, 0.0f};
            if (field == "AlphaCutoff")
                return std::array<f32, 4>{u.metallicRoughnessNormalCutoff[3], 0.0f, 0.0f, 0.0f};
            if (field == "Emissive")
                return std::array<f32, 4>{u.emissive[0], u.emissive[1], u.emissive[2], 0.0f};
            return std::nullopt;
        };
        const auto valueOf = [&](std::string_view field) -> const SurfaceValue* {
            for (const SurfaceValue& value : material.surfaceValues) {
                if (value.name == field)
                    return &value;
            }
            return nullptr;
        };

        std::vector<core::u8>& block = materialBlock_[index];
        block.assign(reflection.blockBytes, 0);
        std::memcpy(block.data(), clock, sizeof(clock));
        for (const asset::SurfaceParam& param : reflection.params) {
            if (const SurfaceValue* value = valueOf(param.name); value != nullptr && !value->isTexture) {
                asset::writeSurfaceParam(param, value->value, block);
            }
            else if (const std::optional<std::array<f32, 4>> fixed = builtIn(param.name); fixed.has_value()) {
                asset::writeSurfaceParam(param, *fixed, block);
            }
            else {
                asset::writeSurfaceParam(param, {}, block);
            }
        }

        core::u32 textureMask = 0;
        const auto builtInMap = [&](std::string_view field) -> rhi::TextureHandle {
            if (field == "ColorMap")
                return material.baseColor;
            if (field == "NormalMap")
                return material.normal;
            if (field == "MetallicRoughnessMap")
                return material.metallicRoughness;
            if (field == "EmissiveMap")
                return material.emissive;
            return rhi::TextureHandle{};
        };
        for (core::usize slot = 0; slot < reflection.textures.size(); ++slot) {
            const asset::SurfaceTexture& texture = reflection.textures[slot];
            rhi::TextureHandle handle{};
            if (const SurfaceValue* value = valueOf(texture.name); value != nullptr && value->isTexture)
                handle = value->texture;
            if (!handle.valid())
                handle = builtInMap(texture.name);
            if (handle.valid())
                textureMask |= 1u << slot;
            if (!handle.valid()) {
                handle = texture.fallback == asset::SurfaceTextureDefault::Black    ? blackPixel_
                         : texture.fallback == asset::SurfaceTextureDefault::Normal ? flatNormalPixel_
                                                                                    : whitePixel_;
            }
            materialSurfaceTextures_[index][slot] = rhi::TextureBinding{handle, linearSampler_};
        }
        std::memcpy(block.data() + 16, &textureMask, sizeof(textureMask));
    }
}

void DefaultRenderer::bindSurface(rhi::ICmdList& cmd, u32 material, bool fragment, bool blended) const
{
    const std::vector<core::u8>& block = materialBlock_[material];
    const asset::SurfaceReflection& reflection = surfaces_[materialSurface_[material] - 1].reflection;
    const std::span<const rhi::TextureBinding> textures(materialSurfaceTextures_[material].data(),
                                                        reflection.textures.size());
    cmd.bindUniforms(rhi::ShaderStage::Vertex, 1, std::as_bytes(std::span(block)));
    if (!textures.empty())
        cmd.bindTextures(rhi::ShaderStage::Vertex, 0, textures);
    if (fragment) {
        cmd.bindUniforms(rhi::ShaderStage::Fragment, 2, std::as_bytes(std::span(block)));
        const std::size_t low = std::min<std::size_t>(textures.size(), 4);
        if (low > 0)
            cmd.bindTextures(rhi::ShaderStage::Fragment, 0, textures.first(low));
        if (textures.size() > low)
            cmd.bindTextures(rhi::ShaderStage::Fragment, asset::EngineFragmentSamplers, textures.subspan(low));
        if (blended) {
            // A blended surface's stage declares every slot up to the scene's,
            // and SDL_GPU wants each bound: the fifth texture's, when there is
            // no fifth texture, is white.
            if (textures.size() <= 4) {
                const std::array<rhi::TextureBinding, 1> unused{rhi::TextureBinding{whitePixel_, linearSampler_}};
                cmd.bindTextures(rhi::ShaderStage::Fragment, asset::EngineFragmentSamplers, unused);
            }
            const std::array<rhi::TextureBinding, 2> scene{
                rhi::TextureBinding{sceneDepthCopy_.valid() ? sceneDepthCopy_ : whitePixel_, pointSampler_},
                rhi::TextureBinding{sceneColorCopy_.valid() ? sceneColorCopy_ : blackPixel_, linearSampler_},
            };
            cmd.bindTextures(rhi::ShaderStage::Fragment, asset::SceneDepthSlot, scene);
        }
    }
}

void DefaultRenderer::copySceneForSurfaces(rhi::IDevice& device, rhi::ICmdList& cmd, const RenderWorld& world,
                                           const GpuFrameUniforms& frame)
{
    // **Only a frame that has a blended surface shader in view pays for it** --
    // and every other frame's command stream is exactly what it was.
    bool depthWanted = false;
    bool colorWanted = false;
    for (const DrawItem& draw : world.draws) {
        if (!draw.transparent || !draw.inCameraFrustum || draw.material >= materialSurface_.size() ||
            materialSurface_[draw.material] == 0)
            continue;
        depthWanted = true;
        colorWanted = colorWanted || world.materials[draw.material].readsSceneColor;
    }
    if (!depthWanted)
        return;
    if (!ensureLookPipeline(device, surfaceSceneDepth_, "surface_scene_depth", rhi::TextureFormat::R32Float) ||
        (colorWanted && !ensureLookPipeline(device, resample_, "look_resample", kHdrFormat)))
        return;
    if (!sceneDepthCopy_.valid()) {
        sceneDepthCopy_ = device.createTexture({
            .format = rhi::TextureFormat::R32Float,
            .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled,
            .width = renderWidth_,
            .height = renderHeight_,
            .debugName = "surface-scene-depth",
        });
    }
    if (colorWanted && !lookTexture(device, sceneColorCopy_, renderWidth_, renderHeight_, "surface-scene-color"))
        colorWanted = false;
    if (!sceneDepthCopy_.valid())
        return;

    cmd.endRenderPass();
    const Mat4 inverseProjection = core::inverse(world.camera.projection);
    const std::array<rhi::TextureBinding, 1> depth{rhi::TextureBinding{depth_, pointSampler_}};
    fullscreenPass(cmd, surfaceSceneDepth_.handle, sceneDepthCopy_, renderWidth_, renderHeight_, "surface-scene-depth",
                   depth, asBytes(&inverseProjection, sizeof(inverseProjection)));
    if (colorWanted) {
        const std::array<rhi::TextureBinding, 1> color{rhi::TextureBinding{hdr_, linearSampler_}};
        fullscreenPass(cmd, resample_.handle, sceneColorCopy_, renderWidth_, renderHeight_, "surface-scene-color",
                       color, {});
    }

    const std::array<rhi::ColorAttachment, 1> resumeTarget{rhi::ColorAttachment{
        .texture = hdr_,
        .loadOp = rhi::LoadOp::Load,
        .storeOp = rhi::StoreOp::Store,
    }};
    cmd.beginRenderPass({
        .colorAttachments = resumeTarget,
        .depthStencil = {.texture = depth_, .loadOp = rhi::LoadOp::Load, .storeOp = rhi::StoreOp::Store},
        .debugName = "forward-after-scene-copy",
    });
    cmd.setViewport({.width = static_cast<f32>(renderWidth_), .height = static_cast<f32>(renderHeight_)});
    cmd.setScissor({.width = static_cast<core::i32>(renderWidth_), .height = static_cast<core::i32>(renderHeight_)});
    // The frame block again: the copies bound their own at the same slot.
    cmd.setPipeline(pbrBlendPipeline_);
    cmd.bindUniforms(rhi::ShaderStage::Fragment, 0, asBytes(&frame, sizeof(frame)));
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
    const f32 pixelsPerUnit = lodPixelsPerUnit(world.camera, height_);

    const auto instanceable = [&](const DrawItem& draw) {
        // Transparent draws are never batched: their ORDER is their
        // correctness, and `drawSortKey` zeroes their mesh field for exactly
        // that reason. Skinned draws are not batched either -- a joint palette
        // is per draw and there is no room for one in a vertex stream.
        //
        // **Selection is NOT a reason to leave a batch** (D073). The first cut
        // of the outline pass excluded an outlined draw here, which split the
        // run it was in -- and a run of identical meshes is how a whole forest
        // is drawn. Whatever that split disturbed downstream, it took every
        // boulder and every tree canopy in the flagship out of the frame the
        // moment one of them was selected. The batching the entire frame
        // depends on is not the place to solve a tool's problem: the outline
        // pass ignores batching instead, which is where the cost belongs and
        // where it is a handful of draws.
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
            // By FAMILY, not by material: a run of parts that differ only by
            // colour is one call, each colour in its instance (D184).
            if (!instanceable(next) || !(next.mesh == first.mesh) || next.section != first.section ||
                world.familyOf(next.material) != world.familyOf(first.material))
                break;
            // A surface's colour is in its block, not in the instance's tint:
            // one material per run.
            const bool surfaced = first.material < materialSurface_.size() && materialSurface_[first.material] != 0;
            if (surfaced && next.material != first.material)
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
            instance.alphaTint[0] = draw.alpha;
            const GpuMaterialUniforms& own = world.materials[draw.material].uniforms;
            instance.alphaTint[1] = own.baseColor[0];
            instance.alphaTint[2] = own.baseColor[1];
            instance.alphaTint[3] = own.baseColor[2];
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
        // `Lighting.EnvironmentSpecularScale`, applied where the chain is made
        // and skipped at its default, so a world that never sets it bakes the
        // same halves it always did.
        if (environment_.target.specularScale != 1.0f) {
            for (core::u16& half : environment_.levels[level])
                half = floatToHalf(halfToFloat(half) * environment_.target.specularScale);
        }
        environment_.dirty[level] = false;
    };

    if (!brdfUploaded_) {
        // Independent of the environment -- it is the BRDF integrated against
        // itself -- so it is baked once and never again.
        environment_.lut.assign(static_cast<core::usize>(kBrdfLutSize) * kBrdfLutSize * 4, 0);
        bakeBrdfLut(kBrdfLutSize, environment_.lut);
        cmd.uploadTexture(brdfLut_, asBytes(environment_.lut.data(), environment_.lut.size() * sizeof(core::u16)), 0);
        brdfUploaded_ = true;
    }

    // **The diffuse half has its own staleness, four times tighter than the
    // specular one (D053).** What the specular chain pays for a rebuild is six
    // texture bakes and six uploads; what this pays is one projection of the
    // sky onto nine numbers. They are different prices and they were sharing a
    // threshold, which set the diffuse one by what the expensive half could
    // afford -- and a light every matte surface receives is exactly the one that
    // must not arrive in steps.
    if (environment_.irradianceStale(params)) {
        environment_.irradianceSky = params;
        bakeIrradianceSh(params, environment_.irradianceTarget);
    }

    if (environment_.stale(params)) {
        environment_.target = params;
        for (bool& level : environment_.dirty)
            level = true;
    }

    // **A sun that jumps is a cut, not a motion, and it is taken whole.**
    //
    // The blend below is sized for a sky the clock walks across. A sky that
    // arrives somewhere else between one frame and the next is a script
    // scrubbing `ClockTime`, a scene loading, or a fixture that steps a quarter
    // of an hour per frame -- and easing into those over a fifth of a second is
    // a fade nobody asked for. The threshold is the specular chain's own: a sun
    // that moves further than that in ONE frame is moving faster than the
    // environment can track at all, so there is nothing to protect.
    const bool cut = !environment_.everBaked || !environment_.hasPreviousSky ||
                     core::dot(params.sunDirection, environment_.previousSky.sunDirection) < kEnvironmentRebuildCosine;
    environment_.previousSky = params;
    environment_.hasPreviousSky = true;

    if (!environment_.everBaked) {
        for (u32 level = 0; level < kEnvironmentMipCount; ++level) {
            bakeLevel(level);
            uploadLevel(level);
        }
        for (u32 index = 0; index < 9; ++index)
            environment_.irradiance[index] = environment_.irradianceTarget[index];
        environment_.everBaked = true;
        environment_.cursor = 0;
        return;
    }

    // **The diffuse ambient walks towards its target rather than arriving at it
    // (D053).** The bake above happens on one frame in about a hundred; before
    // this, its result was handed to the shader whole on that frame, so the
    // light every matte surface in the world receives held still and then
    // stepped by about a per cent. A human running the flagship reported the
    // ground pulsing, and said it started around eleven in the morning -- which
    // is the hour band where the sky's irradiance changes fastest with the sun
    // in this model, so the accumulated step is largest.
    //
    // A per-frame rate rather than a time constant, which is the same shape the
    // exposure adaptation two hundred lines below uses and keeps a headless run
    // reproducible. At a twelfth per frame a step is spread over about a fifth
    // of a second: slow enough that no frame carries a visible jump, fast
    // enough that the ambient is never more than one bake behind the sky.
    const f32 rate = cut ? 1.0f : kEnvironmentIrradianceRate;
    for (u32 index = 0; index < 9; ++index) {
        environment_.irradiance[index] = environment_.irradiance[index] +
                                         (environment_.irradianceTarget[index] - environment_.irradiance[index]) * rate;
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
    const bool depthOnly =
        selection == Selection::Shadow || selection == Selection::Prepass || selection == Selection::Outline;
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
    const f32 pixelsPerUnit = world.camera.valid ? lodPixelsPerUnit(world.camera, height_) : 0.0f;
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
        // **The outline pass ignores batching.** A batch is drawn or skipped as
        // a whole, and selecting one of five identical crates does not select
        // the other four -- so an outlined draw inside a batch would outline
        // all five or none. Treating every draw as its own here costs a handful
        // of calls, because only what is selected is drawn at all, and it
        // leaves the batching the rest of the frame is built on untouched.
        const InstanceBatch* batch =
            selection == Selection::Outline || batchIndex == kNoBatch ? nullptr : &batches_[batchIndex];
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
        if (selection == Selection::Prepass && draw.cutout)
            continue;
        if ((selection == Selection::Opaque || selection == Selection::Prepass) && draw.transparent)
            continue;
        // A transparent selected part still gets an outline: what is selected
        // is a fact about the tool, not about the material.
        if (selection == Selection::Outline && !draw.outlined)
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
        // A terrain mesh in the opaque pass is drawn with the terrain's look,
        // and in the shadow pass with no culling and a push from the light (see
        // the cascade loop); in the prepass it is an ordinary static mesh.
        const bool terrainDraw =
            selection == Selection::Opaque && batch == nullptr && draw.terrain && terrainPipeline_.valid();
        const bool terrainShadow =
            selection == Selection::Shadow && batch == nullptr && draw.terrain && terrainShadowPipeline_.valid();
        const bool voxelDraw = (selection == Selection::Opaque || selection == Selection::Transparent) &&
                               batch == nullptr && draw.voxelBlock && voxelPipeline_.valid() &&
                               voxelBlendPipeline_.valid();
        // A leaf's shadow has the leaf's holes: the cutout faces cast through
        // the hole test rather than as the solid squares their mesh is.
        const bool leafShadow = selection == Selection::Shadow && batch == nullptr && draw.voxelBlock && draw.cutout &&
                                voxelShadowPipeline_.valid();
        // A surface shader's own pipelines, for a plain or instanced mesh (ADR
        // 0091). Skinned, terrain and voxel geometry keep the built-in surface,
        // and so does the outline mask, which wants a position and nothing else.
        const u32 surfaceId = selection != Selection::Outline && !skinnedDraw && !draw.terrain && !draw.voxelBlock &&
                                      draw.material < materialSurface_.size()
                                  ? materialSurface_[draw.material]
                                  : 0u;
        const SurfaceSet* surface = surfaceId != 0 ? &surfaces_[surfaceId - 1] : nullptr;
        // A masked surface cuts itself in its fragment, which its depth pass
        // does not run: left in the prepass, its holes would show whatever the
        // prepass depth hid -- the sky -- instead of what is behind them.
        if (selection == Selection::Prepass && surface != nullptr && world.materials[draw.material].masked)
            continue;
        const rhi::PipelineHandle surfacePipeline =
            surface == nullptr                    ? rhi::PipelineHandle{}
            : selection == Selection::Shadow      ? (batch != nullptr ? surface->shadowInstanced : surface->shadow)
            : selection == Selection::Prepass     ? (batch != nullptr ? surface->prepassInstanced : surface->prepass)
            : selection == Selection::Transparent ? surface->blended
                                                  : (batch != nullptr ? surface->instanced : surface->forward);
        const rhi::PipelineHandle wanted =
            surfacePipeline.valid() ? surfacePipeline
            : batch != nullptr      ? instancedPipeline
            : skinnedDraw           ? skinnedPipeline
            : terrainDraw           ? terrainPipeline_
            : terrainShadow         ? terrainShadowPipeline_
            : voxelDraw             ? (selection == Selection::Transparent ? voxelBlendPipeline_ : voxelPipeline_)
            : leafShadow            ? voxelShadowPipeline_
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
            if (surfacePipeline.valid())
                bindSurface(cmd, draw.material, false, false);
            if (terrainShadow) {
                const f32 push[4] = {terrainShadowPush_, 0.0f, 0.0f, 0.0f};
                cmd.bindUniforms(rhi::ShaderStage::Vertex, 1, asBytes(push, sizeof(push)));
            }
            if (leafShadow && boundMaterial != kVoxelBinding) {
                cmd.bindUniforms(rhi::ShaderStage::Vertex, 1, asBytes(&voxelPalette_, sizeof(voxelPalette_)));
                const std::array<rhi::TextureBinding, 1> atlas{
                    voxelAtlas_.valid() ? rhi::TextureBinding{voxelAtlas_, pointSampler_}
                                        : rhi::TextureBinding{whitePixel_, pointSampler_},
                };
                cmd.bindTextures(rhi::ShaderStage::Fragment, 0, atlas);
                boundMaterial = kVoxelBinding;
            }
        }
        else {
            GpuObjectUniforms uniforms{viewProjection, batch != nullptr ? Mat4{} : draw.transform,
                                       batch != nullptr ? Mat4{} : normalMatrixOf(draw.transform)};
            uniforms.instanceAlphaUnused[0] = batch != nullptr ? 1.0f : draw.alpha;
            cmd.bindUniforms(rhi::ShaderStage::Vertex, 0, asBytes(&uniforms, sizeof(uniforms)));

            if (voxelDraw) {
                // The registry's colours at the vertex stage's second slot, and
                // the standard textures bound to their neutral stand-ins -- once
                // per run of chunks.
                if (boundMaterial != kVoxelBinding) {
                    cmd.bindUniforms(rhi::ShaderStage::Vertex, 1, asBytes(&voxelPalette_, sizeof(voxelPalette_)));
                    const std::array<rhi::TextureBinding, 13> textures{
                        // The block atlas in the base-colour slot, sampled
                        // without smoothing; white until the first image.
                        voxelAtlas_.valid() ? rhi::TextureBinding{voxelAtlas_, pointSampler_}
                                            : rhi::TextureBinding{whitePixel_, pointSampler_},
                        rhi::TextureBinding{flatNormalPixel_, linearSampler_},
                        rhi::TextureBinding{whitePixel_, linearSampler_},
                        rhi::TextureBinding{blackPixel_, linearSampler_},
                        rhi::TextureBinding{shadowMap_, shadowSampler_},
                        rhi::TextureBinding{environmentMap_, environmentSampler_},
                        rhi::TextureBinding{brdfLut_, environmentSampler_},
                        rhi::TextureBinding{clusterGrid_, pointSampler_},
                        rhi::TextureBinding{lightIndices_, pointSampler_},
                        rhi::TextureBinding{lightData_, pointSampler_},
                        rhi::TextureBinding{occlusion_, linearSampler_},
                        rhi::TextureBinding{localShadowMap_, shadowSampler_},
                        rhi::TextureBinding{contact_, pointSampler_},
                    };
                    cmd.bindTextures(rhi::ShaderStage::Fragment, 0, textures);
                    boundMaterial = kVoxelBinding;
                }
            }
            else if (terrainDraw) {
                // The palette at the material slot, and the standard textures
                // bound to their neutral stand-ins -- once per run of terrain.
                if (boundMaterial != kTerrainBinding) {
                    cmd.bindUniforms(rhi::ShaderStage::Fragment, 1, asBytes(&terrainSurface_, sizeof(terrainSurface_)));
                    cmd.bindUniforms(rhi::ShaderStage::Vertex, 1, asBytes(&terrainSurface_, sizeof(terrainSurface_)));
                    const std::array<rhi::TextureBinding, 13> textures{
                        rhi::TextureBinding{whitePixel_, linearSampler_},
                        rhi::TextureBinding{flatNormalPixel_, linearSampler_},
                        rhi::TextureBinding{whitePixel_, linearSampler_},
                        rhi::TextureBinding{blackPixel_, linearSampler_},
                        rhi::TextureBinding{shadowMap_, shadowSampler_},
                        rhi::TextureBinding{environmentMap_, environmentSampler_},
                        rhi::TextureBinding{brdfLut_, environmentSampler_},
                        rhi::TextureBinding{clusterGrid_, pointSampler_},
                        rhi::TextureBinding{lightIndices_, pointSampler_},
                        rhi::TextureBinding{lightData_, pointSampler_},
                        rhi::TextureBinding{occlusion_, linearSampler_},
                        rhi::TextureBinding{localShadowMap_, shadowSampler_},
                        rhi::TextureBinding{contact_, pointSampler_},
                    };
                    cmd.bindTextures(rhi::ShaderStage::Fragment, 0, textures);
                    boundMaterial = kTerrainBinding;
                }
            }
            else if (draw.material != boundMaterial && draw.material < world.materials.size()) {
                const RenderMaterial& material = world.materials[draw.material];
                if (draw.material < materialError_.size() && materialError_[draw.material]) {
                    // A surface shader that does not compile: the built-in
                    // surface in a colour nobody picks, so it is found.
                    GpuMaterialUniforms error = material.uniforms;
                    const f32 magenta[4] = {1.0f, 0.0f, 1.0f, 1.0f};
                    std::memcpy(error.baseColor, magenta, sizeof(magenta));
                    const f32 glow[4] = {0.6f, 0.0f, 0.6f, 0.0f};
                    std::memcpy(error.emissive, glow, sizeof(glow));
                    std::memset(error.textureFlags, 0, sizeof(error.textureFlags));
                    cmd.bindUniforms(rhi::ShaderStage::Fragment, 1, asBytes(&error, sizeof(error)));
                }
                else {
                    cmd.bindUniforms(rhi::ShaderStage::Fragment, 1,
                                     asBytes(&material.uniforms, sizeof(material.uniforms)));
                }

                // Every slot is bound every time, with the shadow map last.
                // A slot left over from the previous material is the classic
                // way one mesh ends up wearing another's texture.
                const auto orDefault = [](rhi::TextureHandle handle, rhi::TextureHandle fallback) {
                    return handle.valid() ? handle : fallback;
                };
                const std::array<rhi::TextureBinding, 13> textures{
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
                    rhi::TextureBinding{localShadowMap_, shadowSampler_},
                    rhi::TextureBinding{contact_, pointSampler_},
                };
                cmd.bindTextures(rhi::ShaderStage::Fragment, 0, textures);
                boundMaterial = draw.material;
            }
            // Every draw rather than on a change of material: vertex slot 1 is
            // also where a skinned draw's joints and the terrain's block go.
            if (surfacePipeline.valid()) {
                bindSurface(cmd, draw.material, true, selection == Selection::Transparent);
                // Its textures took the built-in maps' slots: whatever draws
                // next with this material binds them again.
                boundMaterial = 0xFFFFFFFFu;
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

// A frame's world UI stops here: ten thousand quads, far past any screen of
// name tags, and a fixed buffer rather than one that grows under a frame.
constexpr u32 MaxWorldUiVertices = 60000;

// A frame's sprites stop here (the 2D layer): four megabytes of instances, and
// a screen of 16-pixel tiles at 4K is a little over thirty thousand of them.
constexpr u32 MaxSprites = 65536;

bool DefaultRenderer::ensureWorldUi(rhi::IDevice& device)
{
    if (worldUiTried_)
        return worldUiPipeline_.valid() && worldUiOnTopPipeline_.valid() && worldUiBuffer_.valid();
    worldUiTried_ = true;
    if (shaderLibrary_ == nullptr)
        return false;
    core::EngineError error;
    const rhi::ShaderHandle vertex = shaderLibrary_->create(device, "ui_world", rhi::ShaderStage::Vertex, &error);
    const rhi::ShaderHandle fragment = shaderLibrary_->create(device, "ui_world", rhi::ShaderStage::Fragment, &error);
    for (const rhi::ShaderHandle handle : {vertex, fragment}) {
        if (handle.valid() && shaderCount_ < std::size(shaders_))
            shaders_[shaderCount_++] = handle;
    }
    if (!vertex.valid() || !fragment.valid()) {
        core::logText(core::LogLevel::Warn, error.message);
        return false;
    }

    const std::array<rhi::VertexAttribute, 5> attributes{
        rhi::VertexAttribute{.location = 0,
                             .bufferSlot = 0,
                             .format = rhi::VertexFormat::Float3,
                             .offsetBytes = offsetof(WorldUiVertex, x)},
        rhi::VertexAttribute{.location = 1,
                             .bufferSlot = 0,
                             .format = rhi::VertexFormat::Ubyte4Unorm,
                             .offsetBytes = offsetof(WorldUiVertex, r)},
        rhi::VertexAttribute{.location = 2,
                             .bufferSlot = 0,
                             .format = rhi::VertexFormat::Float4,
                             .offsetBytes = offsetof(WorldUiVertex, localX)},
        rhi::VertexAttribute{.location = 3,
                             .bufferSlot = 0,
                             .format = rhi::VertexFormat::Float1,
                             .offsetBytes = offsetof(WorldUiVertex, radius)},
        rhi::VertexAttribute{.location = 4,
                             .bufferSlot = 0,
                             .format = rhi::VertexFormat::Float2,
                             .offsetBytes = offsetof(WorldUiVertex, u)},
    };
    const std::array<rhi::VertexBufferLayout, 1> buffers{
        rhi::VertexBufferLayout{.slot = 0, .strideBytes = sizeof(WorldUiVertex)},
    };
    // Straight alpha, as the screen's UI blends: a panel's transparency is a
    // property, and a UI colour is not premultiplied anywhere upstream.
    const std::array<rhi::ColorTargetDesc, 1> hdrTarget{rhi::ColorTargetDesc{
        .format = kHdrFormat,
        .blend = {.enabled = true},
    }};
    const auto make = [&](bool tested, const char* name) {
        return device.createGraphicsPipeline({
            .vertexShader = vertex,
            .fragmentShader = fragment,
            .vertexBuffers = buffers,
            .vertexAttributes = attributes,
            // Both sides: a sign is read from in front, and seen edge-on or
            // from behind it is a sign seen from behind.
            .rasterizer = {.cullMode = rhi::CullMode::None},
            // Tested and never written, like every blended surface: hidden by
            // what is in front, and never hiding what is drawn after it.
            .depthStencil = {.depthTest = tested, .depthWrite = false, .depthCompare = rhi::CompareOp::LessOrEqual},
            .colorTargets = hdrTarget,
            .depthStencilFormat = kDepthFormat,
            .debugName = name,
        });
    };
    worldUiPipeline_ = make(true, "ui_world");
    worldUiOnTopPipeline_ = make(false, "ui_world.top");
    worldUiBuffer_ = device.createBuffer({
        .usage = rhi::BufferUsage::Vertex,
        .sizeBytes = static_cast<u32>(MaxWorldUiVertices * sizeof(WorldUiVertex)),
        .debugName = "ui_world",
    });
    return worldUiPipeline_.valid() && worldUiOnTopPipeline_.valid() && worldUiBuffer_.valid();
}

bool DefaultRenderer::ensureDecals(rhi::IDevice& device)
{
    if (decalTried_)
        return decalPipeline_.valid();
    decalTried_ = true;
    if (shaderLibrary_ == nullptr)
        return false;
    core::EngineError error;
    const rhi::ShaderHandle vertex = shaderLibrary_->create(device, "decal", rhi::ShaderStage::Vertex, &error);
    const rhi::ShaderHandle fragment = shaderLibrary_->create(device, "decal", rhi::ShaderStage::Fragment, &error);
    for (const rhi::ShaderHandle handle : {vertex, fragment}) {
        if (handle.valid() && shaderCount_ < std::size(shaders_))
            shaders_[shaderCount_++] = handle;
    }
    if (!vertex.valid() || !fragment.valid()) {
        core::logText(core::LogLevel::Warn, error.message);
        return false;
    }
    // **Multiplied into what is there**: the colour this writes is a factor,
    // one where nothing lands. Alpha is left as it was.
    const std::array<rhi::ColorTargetDesc, 1> multiplyTarget{rhi::ColorTargetDesc{
        .format = kHdrFormat,
        .blend = {.enabled = true,
                  .srcColor = rhi::BlendFactor::DstColor,
                  .dstColor = rhi::BlendFactor::Zero,
                  .srcAlpha = rhi::BlendFactor::Zero,
                  .dstAlpha = rhi::BlendFactor::One},
    }};
    decalPipeline_ = device.createGraphicsPipeline({
        .vertexShader = vertex,
        .fragmentShader = fragment,
        // Both sides reach the fragment stage, which keeps the far one: see
        // the shader. No depth attachment -- the pass reads depth instead.
        .rasterizer = {.cullMode = rhi::CullMode::None},
        .colorTargets = multiplyTarget,
        .debugName = "decal",
    });
    return decalPipeline_.valid();
}

bool DefaultRenderer::ensureLookPipeline(rhi::IDevice& device, LookPipeline& slot, const char* shader,
                                         rhi::TextureFormat format, LookBlend blend)
{
    if (slot.tried)
        return slot.handle.valid();
    slot.tried = true;
    if (shaderLibrary_ == nullptr)
        return false;
    core::EngineError error;
    const rhi::ShaderHandle vertex = shaderLibrary_->create(device, shader, rhi::ShaderStage::Vertex, &error);
    const rhi::ShaderHandle fragment = shaderLibrary_->create(device, shader, rhi::ShaderStage::Fragment, &error);
    for (const rhi::ShaderHandle handle : {vertex, fragment}) {
        if (handle.valid() && shaderCount_ < std::size(shaders_))
            shaders_[shaderCount_++] = handle;
    }
    if (!vertex.valid() || !fragment.valid()) {
        core::logText(core::LogLevel::Warn, error.message);
        return false;
    }
    rhi::ColorTargetDesc target{.format = format};
    if (blend == LookBlend::Add) {
        target.blend = {.enabled = true,
                        .srcColor = rhi::BlendFactor::One,
                        .dstColor = rhi::BlendFactor::One,
                        .srcAlpha = rhi::BlendFactor::Zero,
                        .dstAlpha = rhi::BlendFactor::One};
    }
    else if (blend == LookBlend::Air) {
        target.blend = {.enabled = true,
                        .srcColor = rhi::BlendFactor::One,
                        .dstColor = rhi::BlendFactor::SrcAlpha,
                        .srcAlpha = rhi::BlendFactor::Zero,
                        .dstAlpha = rhi::BlendFactor::One};
    }
    const std::array<rhi::ColorTargetDesc, 1> targets{target};
    slot.handle = device.createGraphicsPipeline({
        .vertexShader = vertex,
        .fragmentShader = fragment,
        .primitive = rhi::PrimitiveType::TriangleList,
        .rasterizer = {.cullMode = rhi::CullMode::None},
        .colorTargets = targets,
        .debugName = shader,
    });
    return slot.handle.valid();
}

bool DefaultRenderer::lookTexture(rhi::IDevice& device, rhi::TextureHandle& slot, u32 width, u32 height,
                                  const char* name)
{
    if (slot.valid())
        return true;
    slot = device.createTexture({
        .format = kHdrFormat,
        .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled,
        .width = width,
        .height = height,
        .debugName = name,
    });
    return slot.valid();
}

void DefaultRenderer::releaseLookTextures(rhi::IDevice& device)
{
    const auto release = [&device](rhi::TextureHandle& texture) {
        if (texture.valid())
            device.destroy(texture);
        texture = {};
    };
    release(lookColor_);
    release(sceneDepthCopy_);
    release(sceneColorCopy_);
    for (rhi::TextureHandle& level : blurLevels_)
        release(level);
    for (rhi::TextureHandle& level : blurPong_)
        release(level);
    release(focusPrepared_);
    release(focusGathered_);
    release(raysMasked_);
    release(raysGathered_);
}

void DefaultRenderer::sunRaysOnto(rhi::IDevice& device, rhi::ICmdList& cmd, const RenderWorld& world,
                                  const SkyParams& sky, rhi::TextureHandle image)
{
    // **Where the sun is on the screen**: its direction projected as a point at
    // infinity -- w of zero, so only the view's rotation and the lens act on it.
    // Behind the camera there is nothing to stream from.
    const Mat4& viewProjection = world.camera.viewProjection;
    const Vec3 sun = sky.sunDirection;
    f32 clip[4]{};
    for (u32 row = 0; row < 4; ++row) {
        clip[row] =
            viewProjection.m[0][row] * sun.x + viewProjection.m[1][row] * sun.y + viewProjection.m[2][row] * sun.z;
    }
    if (!(clip[3] > 1e-4f))
        return;
    const f32 ndcX = clip[0] / clip[3];
    const f32 ndcY = clip[1] / clip[3];

    // **Present only while the sun is.** Past the screen's edge the shafts fade
    // over `kRaysEdgeFade` half-widths, and below the horizon they go with the
    // day -- so a sunset takes its rays with it rather than cutting them off.
    const f32 outside = std::max(std::abs(ndcX), std::abs(ndcY)) - 1.0f;
    const f32 onScreen = std::clamp(1.0f - outside / kRaysEdgeFade, 0.0f, 1.0f);
    const f32 presence = onScreen * sky.dayFactor * world.look.sunRaysIntensity * kRaysStrength;
    if (!(presence > 0.0f))
        return;

    if (!ensureLookPipeline(device, raysMask_, "look_rays_mask", kHdrFormat) ||
        !ensureLookPipeline(device, raysGather_, "look_rays_gather", kHdrFormat) ||
        !ensureLookPipeline(device, raysAdd_, "look_resample", kHdrFormat, LookBlend::Add))
        return;
    // Half resolution: at a quarter, a post or a branch in front of the sun was
    // a texel or two of the mask, and its shaft drowned in the glow around it.
    const u32 raysWidth = std::max(renderWidth_ / 2, 1u);
    const u32 raysHeight = std::max(renderHeight_ / 2, 1u);
    if (!lookTexture(device, raysMasked_, raysWidth, raysHeight, "look-rays-mask") ||
        !lookTexture(device, raysGathered_, raysWidth, raysHeight, "look-rays"))
        return;

    GpuLookRaysUniforms rays;
    rays.sun[0] = ndcX * 0.5f + 0.5f;
    rays.sun[1] = 0.5f - ndcY * 0.5f;
    rays.sun[2] = presence;
    rays.sun[3] = static_cast<f32>(renderWidth_) / static_cast<f32>(renderHeight_);
    // `Spread` is how much of the way to the sun a texel looks: a quarter at
    // 0, a halo round the sun; all of it at 1, shafts across the screen.
    rays.gather[0] = 0.25f + 0.75f * world.look.sunRaysSpread;
    rays.gather[1] = static_cast<f32>(kRaysTaps);
    rays.gather[2] = kRaysDecay;

    cmd.pushDebugGroup("sun-rays");
    const std::array<rhi::TextureBinding, 2> mask{rhi::TextureBinding{image, environmentSampler_},
                                                  rhi::TextureBinding{depth_, pointSampler_}};
    fullscreenPass(cmd, raysMask_.handle, raysMasked_, raysWidth, raysHeight, "rays-mask", mask,
                   asBytes(&rays, sizeof(rays)));
    const std::array<rhi::TextureBinding, 1> gather{rhi::TextureBinding{raysMasked_, environmentSampler_}};
    fullscreenPass(cmd, raysGather_.handle, raysGathered_, raysWidth, raysHeight, "rays-gather", gather,
                   asBytes(&rays, sizeof(rays)));
    const std::array<rhi::TextureBinding, 1> shafts{rhi::TextureBinding{raysGathered_, environmentSampler_}};
    fullscreenPass(cmd, raysAdd_.handle, image, renderWidth_, renderHeight_, "rays-add", shafts, {}, rhi::LoadOp::Load);
    cmd.popDebugGroup();
}

rhi::TextureHandle DefaultRenderer::focusImage(rhi::IDevice& device, rhi::ICmdList& cmd, const RenderWorld& world,
                                               rhi::TextureHandle image)
{
    if (!ensureLookPipeline(device, focusPrepare_, "look_focus_prepare", kHdrFormat) ||
        !ensureLookPipeline(device, focusGather_, "look_focus_gather", kHdrFormat) ||
        !ensureLookPipeline(device, focusComposite_, "look_focus_composite", kHdrFormat))
        return image;
    const u32 halfWidth = renderWidth_ > 1 ? renderWidth_ / 2 : 1;
    const u32 halfHeight = renderHeight_ > 1 ? renderHeight_ / 2 : 1;
    rhi::TextureHandle& output = image == lookColor_ ? hdr_ : lookColor_;
    if (!lookTexture(device, focusPrepared_, halfWidth, halfHeight, "look-focus") ||
        !lookTexture(device, focusGathered_, halfWidth, halfHeight, "look-focus-gathered") ||
        !lookTexture(device, output, renderWidth_, renderHeight_, "look-color"))
        return image;

    const RenderLook& look = world.look;
    const f32 widest = kFocusWidestPixels * static_cast<f32>(renderHeight_) / 1080.0f;
    GpuLookFocusUniforms focus;
    focus.band[0] = look.focusDistance;
    focus.band[1] = look.inFocusRadius;
    focus.band[2] = look.nearIntensity;
    focus.band[3] = look.farIntensity;
    focus.lens[0] = world.camera.nearPlane;
    focus.lens[1] = world.camera.farPlane;
    focus.lens[2] = widest * 0.5f;
    focus.lens[3] = widest;
    focus.texel[0] = 1.0f / static_cast<f32>(renderWidth_);
    focus.texel[1] = 1.0f / static_cast<f32>(renderHeight_);
    focus.texel[2] = 1.0f / static_cast<f32>(halfWidth);
    focus.texel[3] = 1.0f / static_cast<f32>(halfHeight);

    cmd.pushDebugGroup("depth-of-field");
    const std::array<rhi::TextureBinding, 2> prepare{rhi::TextureBinding{image, environmentSampler_},
                                                     rhi::TextureBinding{depth_, pointSampler_}};
    fullscreenPass(cmd, focusPrepare_.handle, focusPrepared_, halfWidth, halfHeight, "focus-prepare", prepare,
                   asBytes(&focus, sizeof(focus)));
    // Point-sampled: a circle of confusion averaged with its neighbour's is a
    // circle nobody's depth has.
    const std::array<rhi::TextureBinding, 1> gather{rhi::TextureBinding{focusPrepared_, pointSampler_}};
    fullscreenPass(cmd, focusGather_.handle, focusGathered_, halfWidth, halfHeight, "focus-gather", gather,
                   asBytes(&focus, sizeof(focus)));
    const std::array<rhi::TextureBinding, 3> composite{rhi::TextureBinding{image, pointSampler_},
                                                       rhi::TextureBinding{focusGathered_, environmentSampler_},
                                                       rhi::TextureBinding{depth_, pointSampler_}};
    fullscreenPass(cmd, focusComposite_.handle, output, renderWidth_, renderHeight_, "focus-composite", composite,
                   asBytes(&focus, sizeof(focus)));
    cmd.popDebugGroup();
    return output;
}

void DefaultRenderer::blurImage(rhi::IDevice& device, rhi::ICmdList& cmd, rhi::TextureHandle image, f32 size)
{
    // `Size` is where most of a pixel's light lands, which for a Gaussian is two
    // standard deviations -- in pixels of a picture 1,080 lines tall, so a blur
    // looks the same at every window size.
    const f32 sigma = 0.5f * size * static_cast<f32>(renderHeight_) / 1080.0f;
    // Below a quarter of a pixel the kernel's side taps weigh nothing.
    if (!(sigma > 0.25f))
        return;
    if (!ensureLookPipeline(device, blur_, "look_blur", kHdrFormat) ||
        !ensureLookPipeline(device, resample_, "look_resample", kHdrFormat))
        return;

    // Down the chain until the Gaussian is a few texels wide there.
    u32 levels = 0;
    f32 levelSigma = sigma;
    while (levelSigma > kLookBlurLevelSigma && levels < kLookBlurLevels) {
        levelSigma *= 0.5f;
        ++levels;
    }

    cmd.pushDebugGroup("blur");
    rhi::TextureHandle current = image;
    u32 width = renderWidth_;
    u32 height = renderHeight_;
    for (u32 level = 0; level < levels; ++level) {
        const u32 levelWidth = bloomLevelSize(renderWidth_, level);
        const u32 levelHeight = bloomLevelSize(renderHeight_, level);
        if (!lookTexture(device, blurLevels_[level], levelWidth, levelHeight, "look-blur")) {
            cmd.popDebugGroup();
            return;
        }
        // The bloom chain's own downsample, with no threshold: a thirteen-tap
        // box that does not alias the way one bilinear read per texel would.
        GpuBloomUniforms down;
        down.texelRadius[0] = 1.0f / static_cast<f32>(width);
        down.texelRadius[1] = 1.0f / static_cast<f32>(height);
        down.texelRadius[2] = 1.0f;
        const std::array<rhi::TextureBinding, 1> source{rhi::TextureBinding{current, environmentSampler_}};
        fullscreenPass(cmd, bloomDownPipeline_, blurLevels_[level], levelWidth, levelHeight, "blur-down", source,
                       asBytes(&down, sizeof(down)));
        current = blurLevels_[level];
        width = levelWidth;
        height = levelHeight;
    }

    // The partner the two directions ping-pong through: the level's own, or at
    // full size whichever full-resolution image `image` is not.
    rhi::TextureHandle& partner = levels > 0 ? blurPong_[levels - 1] : (image == lookColor_ ? hdr_ : lookColor_);
    if (!lookTexture(device, partner, width, height, "look-blur-pong")) {
        cmd.popDebugGroup();
        return;
    }
    GpuLookBlurUniforms pass;
    pass.stepSigma[2] = levelSigma;
    pass.stepSigma[3] = std::min(std::ceil(3.0f * levelSigma), 16.0f);
    pass.stepSigma[0] = 1.0f / static_cast<f32>(width);
    const std::array<rhi::TextureBinding, 1> across{rhi::TextureBinding{current, environmentSampler_}};
    fullscreenPass(cmd, blur_.handle, partner, width, height, "blur-x", across, asBytes(&pass, sizeof(pass)));
    pass.stepSigma[0] = 0.0f;
    pass.stepSigma[1] = 1.0f / static_cast<f32>(height);
    const std::array<rhi::TextureBinding, 1> vertical{rhi::TextureBinding{partner, environmentSampler_}};
    fullscreenPass(cmd, blur_.handle, current, width, height, "blur-y", vertical, asBytes(&pass, sizeof(pass)));

    // And back to the frame's size, into the image it came from.
    if (levels > 0) {
        const std::array<rhi::TextureBinding, 1> result{rhi::TextureBinding{current, environmentSampler_}};
        fullscreenPass(cmd, resample_.handle, image, renderWidth_, renderHeight_, "blur-up", result, {});
    }
    cmd.popDebugGroup();
}

bool DefaultRenderer::ensureSkyLook(rhi::IDevice& device)
{
    if (skyLook_.tried)
        return skyLook_.handle.valid();
    skyLook_.tried = true;
    if (shaderLibrary_ == nullptr)
        return false;
    core::EngineError error;
    const rhi::ShaderHandle vertex = shaderLibrary_->create(device, "sky_look", rhi::ShaderStage::Vertex, &error);
    const rhi::ShaderHandle fragment = shaderLibrary_->create(device, "sky_look", rhi::ShaderStage::Fragment, &error);
    for (const rhi::ShaderHandle handle : {vertex, fragment}) {
        if (handle.valid() && shaderCount_ < std::size(shaders_))
            shaders_[shaderCount_++] = handle;
    }
    if (!vertex.valid() || !fragment.valid()) {
        core::logText(core::LogLevel::Warn, error.message);
        return false;
    }
    const std::array<rhi::ColorTargetDesc, 1> target{rhi::ColorTargetDesc{.format = kHdrFormat}};
    skyLook_.handle = device.createGraphicsPipeline({
        .vertexShader = vertex,
        .fragmentShader = fragment,
        .primitive = rhi::PrimitiveType::TriangleList,
        .rasterizer = {.cullMode = rhi::CullMode::None},
        .depthStencil = {.depthTest = false, .depthWrite = false},
        .colorTargets = target,
        .depthStencilFormat = kDepthFormat,
        .debugName = "sky_look",
    });
    return skyLook_.handle.valid();
}

bool DefaultRenderer::ensureParticles(rhi::IDevice& device)
{
    if (particleTried_)
        return particlePipeline_.valid() && particleBuffer_.valid();
    particleTried_ = true;
    if (shaderLibrary_ == nullptr)
        return false;

    core::EngineError error;
    const auto load = [&](std::string_view name, rhi::ShaderStage stage) -> rhi::ShaderHandle {
        const rhi::ShaderHandle handle = shaderLibrary_->create(device, name, stage, &error);
        if (handle.valid() && shaderCount_ < std::size(shaders_))
            shaders_[shaderCount_++] = handle;
        return handle;
    };
    const rhi::ShaderHandle vertex = load("particle", rhi::ShaderStage::Vertex);
    const rhi::ShaderHandle fragment = load("particle", rhi::ShaderStage::Fragment);
    if (!vertex.valid() || !fragment.valid()) {
        core::logText(core::LogLevel::Warn, error.message);
        return false;
    }

    // **Instances only**: the six corners come from the vertex index, so the
    // one stream is per instance and there is no per-vertex buffer at all.
    const std::array<rhi::VertexAttribute, 3> attributes{
        rhi::VertexAttribute{.location = 0, .bufferSlot = 0, .format = rhi::VertexFormat::Float4, .offsetBytes = 0},
        rhi::VertexAttribute{.location = 1, .bufferSlot = 0, .format = rhi::VertexFormat::Float4, .offsetBytes = 16},
        rhi::VertexAttribute{.location = 2, .bufferSlot = 0, .format = rhi::VertexFormat::Float4, .offsetBytes = 32},
    };
    const std::array<rhi::VertexBufferLayout, 1> buffers{
        rhi::VertexBufferLayout{.slot = 0, .strideBytes = sizeof(GpuParticle), .perInstance = true},
    };
    // Premultiplied, which is what lets one pipeline blend and add: see the
    // shader's header.
    const std::array<rhi::ColorTargetDesc, 1> hdrTarget{rhi::ColorTargetDesc{
        .format = kHdrFormat,
        .blend = {.enabled = true,
                  .srcColor = rhi::BlendFactor::One,
                  .dstColor = rhi::BlendFactor::OneMinusSrcAlpha,
                  .srcAlpha = rhi::BlendFactor::One,
                  .dstAlpha = rhi::BlendFactor::OneMinusSrcAlpha},
    }};
    particlePipeline_ = device.createGraphicsPipeline({
        .vertexShader = vertex,
        .fragmentShader = fragment,
        .vertexBuffers = buffers,
        .vertexAttributes = attributes,
        .rasterizer = {.cullMode = rhi::CullMode::None},
        // No depth attachment: the shader reads the scene's depth, tests
        // against it and fades near it (soft particles), and never writes it.
        .colorTargets = hdrTarget,
        .debugName = "particle",
    });
    particleBuffer_ = device.createBuffer({
        .usage = rhi::BufferUsage::Vertex,
        .sizeBytes = static_cast<u32>(ParticleSystem::MaxDrawn * sizeof(GpuParticle)),
        .debugName = "particles",
    });
    return particlePipeline_.valid() && particleBuffer_.valid();
}

bool DefaultRenderer::ensureSprites(rhi::IDevice& device)
{
    if (spriteTried_)
        return spritePipeline_.valid() && spriteBuffer_.valid();
    spriteTried_ = true;
    if (shaderLibrary_ == nullptr)
        return false;

    core::EngineError error;
    const rhi::ShaderHandle vertex = shaderLibrary_->create(device, "sprite", rhi::ShaderStage::Vertex, &error);
    const rhi::ShaderHandle fragment = shaderLibrary_->create(device, "sprite", rhi::ShaderStage::Fragment, &error);
    for (const rhi::ShaderHandle handle : {vertex, fragment}) {
        if (handle.valid() && shaderCount_ < std::size(shaders_))
            shaders_[shaderCount_++] = handle;
    }
    if (!vertex.valid() || !fragment.valid()) {
        core::logText(core::LogLevel::Warn, error.message);
        return false;
    }

    const std::array<rhi::VertexAttribute, 4> attributes{
        rhi::VertexAttribute{.location = 0, .bufferSlot = 0, .format = rhi::VertexFormat::Float4, .offsetBytes = 0},
        rhi::VertexAttribute{.location = 1, .bufferSlot = 0, .format = rhi::VertexFormat::Float4, .offsetBytes = 16},
        rhi::VertexAttribute{.location = 2, .bufferSlot = 0, .format = rhi::VertexFormat::Float4, .offsetBytes = 32},
        rhi::VertexAttribute{.location = 3, .bufferSlot = 0, .format = rhi::VertexFormat::Float4, .offsetBytes = 48},
    };
    const std::array<rhi::VertexBufferLayout, 1> buffers{
        rhi::VertexBufferLayout{.slot = 0, .strideBytes = sizeof(GpuSprite), .perInstance = true},
    };
    // Straight alpha, as a picture's transparent pixels are stored.
    const std::array<rhi::ColorTargetDesc, 1> hdrTarget{rhi::ColorTargetDesc{
        .format = kHdrFormat,
        .blend = {.enabled = true},
    }};
    spritePipeline_ = device.createGraphicsPipeline({
        .vertexShader = vertex,
        .fragmentShader = fragment,
        .vertexBuffers = buffers,
        .vertexAttributes = attributes,
        // A flipped sprite is still a sprite, and the plane is seen from
        // either side.
        .rasterizer = {.cullMode = rhi::CullMode::None},
        // Tested and never written, like every blended surface: a 3D part in
        // front of the plane hides a sprite, and sprites order among
        // themselves by `ZIndex`, which is the order they are drawn in.
        .depthStencil = {.depthTest = true, .depthWrite = false, .depthCompare = rhi::CompareOp::LessOrEqual},
        .colorTargets = hdrTarget,
        .depthStencilFormat = kDepthFormat,
        .debugName = "sprite",
    });
    spriteBuffer_ = device.createBuffer({
        .usage = rhi::BufferUsage::Vertex,
        .sizeBytes = static_cast<u32>(MaxSprites * sizeof(GpuSprite)),
        .debugName = "sprites",
    });
    return spritePipeline_.valid() && spriteBuffer_.valid();
}

bool DefaultRenderer::ensureVoxel(rhi::IDevice& device)
{
    if (voxelTried_)
        return voxelPipeline_.valid();
    voxelTried_ = true;
    if (shaderLibrary_ == nullptr)
        return false;

    core::EngineError error;
    const auto load = [&](std::string_view name, rhi::ShaderStage stage) -> rhi::ShaderHandle {
        const rhi::ShaderHandle handle = shaderLibrary_->create(device, name, stage, &error);
        if (handle.valid() && shaderCount_ < std::size(shaders_))
            shaders_[shaderCount_++] = handle;
        return handle;
    };
    const rhi::ShaderHandle vertex = load("voxel", rhi::ShaderStage::Vertex);
    const rhi::ShaderHandle fragment = load("voxel", rhi::ShaderStage::Fragment);
    if (!vertex.valid() || !fragment.valid()) {
        core::logText(core::LogLevel::Warn, error.message);
        return false;
    }
    // `asset::Vertex`, all four attributes: the block shader reads the UV.
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

    // The atlas and the pipeline that fills it, beside the block pipeline that
    // reads it: a block world with images needs all three and one without
    // pays for the atlas's memory only when the first image arrives.
    const rhi::ShaderHandle tileVertex = load("voxel_tile", rhi::ShaderStage::Vertex);
    const rhi::ShaderHandle tileFragment = load("voxel_tile", rhi::ShaderStage::Fragment);
    if (tileVertex.valid() && tileFragment.valid()) {
        const std::array<rhi::ColorTargetDesc, 1> atlasTarget{rhi::ColorTargetDesc{.format = kVoxelAtlasFormat}};
        voxelTilePipeline_ = device.createGraphicsPipeline({
            .vertexShader = tileVertex,
            .fragmentShader = tileFragment,
            .rasterizer = {.cullMode = rhi::CullMode::None},
            .colorTargets = atlasTarget,
            .debugName = "voxel-tile",
        });
    }

    voxelPipeline_ = device.createGraphicsPipeline({
        .vertexShader = vertex,
        .fragmentShader = fragment,
        .vertexBuffers = buffers,
        .vertexAttributes = attributes,
        .rasterizer = {.cullMode = rhi::CullMode::Back},
        .depthStencil = {.depthTest = true, .depthWrite = true, .depthCompare = rhi::CompareOp::LessOrEqual},
        .colorTargets = hdrTarget,
        .depthStencilFormat = kDepthFormat,
        .debugName = "voxel",
    });
    // **Both sides, blended, depth tested and not written**: from under water
    // the surface is seen from below, and a pane of glass is a pane from either
    // side.
    const std::array<rhi::ColorTargetDesc, 1> blendTarget{rhi::ColorTargetDesc{
        .format = kHdrFormat,
        .blend = {.enabled = true},
    }};
    voxelBlendPipeline_ = device.createGraphicsPipeline({
        .vertexShader = vertex,
        .fragmentShader = fragment,
        .vertexBuffers = buffers,
        .vertexAttributes = attributes,
        .rasterizer = {.cullMode = rhi::CullMode::None},
        .depthStencil = {.depthTest = true, .depthWrite = false, .depthCompare = rhi::CompareOp::LessOrEqual},
        .colorTargets = blendTarget,
        .depthStencilFormat = kDepthFormat,
        .debugName = "voxel-blend",
    });

    // The shadow pipeline's state is `shadow_depth`'s -- front faces culled,
    // for the reason D051 gives -- and only its fragment stage differs.
    const rhi::ShaderHandle shadowVertex = load("voxel_shadow", rhi::ShaderStage::Vertex);
    const rhi::ShaderHandle shadowFragment = load("voxel_shadow", rhi::ShaderStage::Fragment);
    if (shadowVertex.valid() && shadowFragment.valid()) {
        const std::span<const rhi::VertexAttribute> shadowAttributes{attributes.data(), 3};
        voxelShadowPipeline_ = device.createGraphicsPipeline({
            .vertexShader = shadowVertex,
            .fragmentShader = shadowFragment,
            .vertexBuffers = buffers,
            .vertexAttributes = shadowAttributes,
            .rasterizer = {.cullMode = rhi::CullMode::Front},
            .depthStencil = {.depthTest = true, .depthWrite = true, .depthCompare = rhi::CompareOp::LessOrEqual},
            .colorTargets = {},
            .depthStencilFormat = kShadowFormat,
            .debugName = "voxel-shadow",
        });
    }
    return voxelPipeline_.valid();
}

bool DefaultRenderer::ensureTerrain(rhi::IDevice& device)
{
    if (terrainTried_)
        return terrainValid_;
    terrainTried_ = true;
    if (shaderLibrary_ == nullptr)
        return false;

    core::EngineError error;
    const auto load = [&](std::string_view name, rhi::ShaderStage stage) -> rhi::ShaderHandle {
        const rhi::ShaderHandle handle = shaderLibrary_->create(device, name, stage, &error);
        if (handle.valid() && shaderCount_ < std::size(shaders_))
            shaders_[shaderCount_++] = handle;
        return handle;
    };
    const rhi::ShaderHandle vertex = load("terrain", rhi::ShaderStage::Vertex);
    const rhi::ShaderHandle fragment = load("terrain", rhi::ShaderStage::Fragment);
    const rhi::ShaderHandle depthVertex = load("terrain_depth", rhi::ShaderStage::Vertex);
    const rhi::ShaderHandle depthFragment = load("terrain_depth", rhi::ShaderStage::Fragment);
    if (!vertex.valid() || !fragment.valid() || !depthVertex.valid() || !depthFragment.valid()) {
        core::logText(core::LogLevel::Warn, error.message);
        return false;
    }

    // `asset::Vertex`, 48 bytes, the layout every static mesh has -- a terrain
    // node is filed in `MeshCache` like one. Three attributes, not four: the
    // terrain shader has no use for the UV, and a pipeline that declares an
    // input its vertex shader does not read is one D3D12 refuses to build.
    const std::array<rhi::VertexAttribute, 3> attributes{
        rhi::VertexAttribute{.location = 0, .bufferSlot = 0, .format = rhi::VertexFormat::Float3, .offsetBytes = 0},
        rhi::VertexAttribute{.location = 1, .bufferSlot = 0, .format = rhi::VertexFormat::Float3, .offsetBytes = 12},
        rhi::VertexAttribute{.location = 2, .bufferSlot = 0, .format = rhi::VertexFormat::Float4, .offsetBytes = 24},
    };
    const std::array<rhi::VertexBufferLayout, 1> buffers{
        rhi::VertexBufferLayout{.slot = 0, .strideBytes = 48},
    };
    const std::array<rhi::ColorTargetDesc, 1> hdrTarget{rhi::ColorTargetDesc{.format = kHdrFormat}};
    terrainPipeline_ = device.createGraphicsPipeline({
        .vertexShader = vertex,
        .fragmentShader = fragment,
        .vertexBuffers = buffers,
        .vertexAttributes = attributes,
        .rasterizer = {.cullMode = rhi::CullMode::Back},
        .depthStencil = {.depthTest = true, .depthWrite = true, .depthCompare = rhi::CompareOp::LessOrEqual},
        .colorTargets = hdrTarget,
        .depthStencilFormat = kDepthFormat,
        .debugName = "terrain",
    });
    // **No culling in the shadow pass**, unlike every mesh there. The meshes
    // cull FRONT faces so the depth stored is a solid's far side (D051), and
    // the ground is a surface with no far side: culling its front faces would
    // cull all of it, and the sun would shine through the hills.
    const std::span<const rhi::VertexAttribute> positionOnly{attributes.data(), 1};
    terrainShadowPipeline_ = device.createGraphicsPipeline({
        .vertexShader = depthVertex,
        .fragmentShader = depthFragment,
        .vertexBuffers = buffers,
        .vertexAttributes = positionOnly,
        .rasterizer = {.cullMode = rhi::CullMode::None},
        .depthStencil = {.depthTest = true, .depthWrite = true, .depthCompare = rhi::CompareOp::LessOrEqual},
        .colorTargets = {},
        .depthStencilFormat = kShadowFormat,
        .debugName = "terrain_shadow",
    });

    terrainValid_ = terrainPipeline_.valid() && terrainShadowPipeline_.valid();
    return terrainValid_;
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
    // The look's images are remade lazily at the new size, by whichever effect
    // next needs one -- a frame without one releases nothing it never made.
    if (lookWidth_ != renderWidth_ || lookHeight_ != renderHeight_) {
        releaseLookTextures(device);
        lookWidth_ = renderWidth_;
        lookHeight_ = renderHeight_;
    }

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
    //
    // **With an `Atmosphere`, the horizon is the air's colour** (ADR 0096): it
    // takes `FogColor`'s place in the derivation, so the air is tinted by the
    // hour exactly as the horizon was -- warm at dusk, dark at night -- and the
    // sky, the reflections and the air laid over the world all agree on it.
    const bool air = world.look.atmosphere.present;
    SkyParams sky =
        skyParamsFor(world.environment.sunDirection, air ? world.look.atmosphere.color : world.environment.fogColor);
    sky.specularScale = world.environment.environmentSpecularScale;
    // **A `Sky` governs the sun's look and, with pictures, the sky itself**
    // (ADR 0096) -- never where the sun is.
    const RenderSky& skyLook = world.look.sky;
    if (skyLook.present) {
        setSunAngularRadius(sky, skyLook.sunAngularSize * 0.5f * kDegreesToRadians);
        sky.celestial = skyLook.celestialBodiesShown;
        if (skyLook.image.valid())
            sky.skybox = skyLook.radiance;
        // The clouds, carried by a wind of about sixteen metres a second at
        // the layer's scale, on the game's clock, folded in f64 before it
        // becomes f32 so an hour-long game drifts as smoothly as a new one.
        sky.cloudCover = skyLook.cloudCover;
        sky.cloudDensity = skyLook.cloudDensity;
        sky.cloudColor = skyLook.cloudColor;
        const core::f64 drift = std::fmod(world.environment.simTime * kCloudWind, kCloudWindWrap);
        sky.cloudDriftX = static_cast<f32>(drift);
        sky.cloudDriftZ = static_cast<f32>(drift * 0.37);
    }
    updateEnvironment(cmd, sky);

    // The clustered light assignment, and its three tables. Built on the CPU
    // because the frozen RHI has no compute, and uploaded unconditionally
    // because a frame's command shape must not depend on whether the lights
    // moved -- the same rule the environment upload obeys, for the same reason.
    // Grouped and uploaded BEFORE any render pass begins, because
    // `rhi.err.upload_inside_pass` refuses the alternative and is right to: the
    // GPU is rasterizing into the target by then.
    stats_ = RendererStats{};
    // The air's pipeline the first frame there is air -- before any pass,
    // because the air is laid INSIDE the forward pass's span, and a pipeline
    // made mid-pass is not.
    const bool airDrawn =
        air && world.camera.valid && ensureLookPipeline(device, air_, "look_air", kHdrFormat, LookBlend::Air);
    // And the governed sky's, for the same reason.
    const bool skyGoverned = skyLook.present && world.camera.valid && ensureSkyLook(device);
    prepareSurfaces(device, world);
    buildInstanceBatches(world, meshes);
    if (!instanceStaging_.empty()) {
        cmd.upload(instanceBuffer_, asBytes(instanceStaging_.data(), instanceStaging_.size() * sizeof(GpuInstance)), 0);
    }

    // This frame's particles, up before any pass for the reason the instances
    // are. The pipeline is made the first frame there is one, so a world
    // without particles builds nothing and moves no golden.
    particleCount_ = 0;
    if (!world.particles.empty() && ensureParticles(device)) {
        particleStaging_.clear();
        const usize count = std::min(world.particles.size(), ParticleSystem::MaxDrawn);
        particleStaging_.reserve(count);
        for (usize at = 0; at < count; ++at) {
            const RenderParticle& particle = world.particles[at];
            GpuParticle gpu;
            gpu.positionSize[0] = particle.position.x;
            gpu.positionSize[1] = particle.position.y;
            gpu.positionSize[2] = particle.position.z;
            gpu.positionSize[3] = particle.size;
            for (usize channel = 0; channel < 4; ++channel)
                gpu.color[channel] = particle.color[channel];
            gpu.params[0] = particle.emission;
            gpu.params[1] = static_cast<f32>(particle.shape);
            particleStaging_.push_back(gpu);
        }
        cmd.upload(particleBuffer_, asBytes(particleStaging_.data(), particleStaging_.size() * sizeof(GpuParticle)), 0);
        particleCount_ = static_cast<u32>(count);
    }

    // This frame's sprites (the 2D layer), up before any pass for the same
    // reason. Past `MaxSprites` the rest are not drawn: the order is ZIndex
    // first, so what goes is the front-most, which is loud rather than subtle.
    spriteCount_ = 0;
    if (!world.sprites.empty() && ensureSprites(device)) {
        spriteStaging_.clear();
        const usize count = std::min<usize>(world.sprites.size(), MaxSprites);
        spriteStaging_.reserve(count);
        for (usize at = 0; at < count; ++at) {
            const RenderSprite& sprite = world.sprites[at];
            GpuSprite gpu;
            for (usize axis = 0; axis < 4; ++axis) {
                gpu.rect[axis] = sprite.rect[axis];
                gpu.uv[axis] = sprite.uv[axis];
                gpu.color[axis] = sprite.color[axis];
            }
            gpu.turn[0] = sprite.cosine;
            gpu.turn[1] = sprite.sine;
            gpu.turn[2] = sprite.z;
            gpu.turn[3] = static_cast<f32>(sprite.shape);
            spriteStaging_.push_back(gpu);
        }
        cmd.upload(spriteBuffer_, asBytes(spriteStaging_.data(), spriteStaging_.size() * sizeof(GpuSprite)), 0);
        spriteCount_ = static_cast<u32>(count);
    }

    // This frame's world UI, up before any pass for the same reason (F3).
    worldUiVertexCount_ = 0;
    if (!world.worldUiVertices.empty() && ensureWorldUi(device)) {
        worldUiVertexCount_ = static_cast<u32>(std::min<usize>(world.worldUiVertices.size(), MaxWorldUiVertices));
        cmd.upload(worldUiBuffer_, asBytes(world.worldUiVertices.data(), worldUiVertexCount_ * sizeof(WorldUiVertex)),
                   0);
    }

    // The block world's palette, and its pipeline the first time a block is
    // drawn -- before any pass, because a pipeline made mid-pass is not.
    if (!world.voxelColors.empty() ||
        std::any_of(world.draws.begin(), world.draws.end(), [](const DrawItem& draw) { return draw.voxelBlock; })) {
        const RenderWorld::VoxelColors missing{Color3{1.0f, 0.0f, 1.0f}, Color3{1.0f, 0.0f, 1.0f},
                                               Color3{1.0f, 0.0f, 1.0f}};
        for (u32 id = 0; id < kVoxelPaletteSize; ++id) {
            const RenderWorld::VoxelColors& colors = id < world.voxelColors.size() ? world.voxelColors[id] : missing;
            const auto put = [](f32(&slot)[4], const Color3& color) {
                slot[0] = color.r;
                slot[1] = color.g;
                slot[2] = color.b;
                slot[3] = 1.0f;
            };
            put(voxelPalette_.top[id], colors.top);
            put(voxelPalette_.side[id], colors.side);
            put(voxelPalette_.bottom[id], colors.bottom);
        }
        voxelPalette_.params[0] = world.voxelBlockSize;
        voxelPalette_.params[1] = static_cast<f32>(kVoxelTilesPerRow);
        voxelPalette_.params[2] = 1.0f / static_cast<f32>(kVoxelTilesPerRow);
        voxelPalette_.params[3] = 0.5f / static_cast<f32>(kVoxelTileSize);
        (void)ensureVoxel(device);

        // **Each image into its tile, the first frame it is loaded**, and never
        // again: a tile keeps its image for as long as the renderer lives.
        const auto tileOf = [&](rhi::TextureHandle texture) -> f32 {
            if (!texture.valid())
                return -1.0f;
            for (const auto& [handle, tile] : voxelTiles_) {
                if (handle == texture.id)
                    return static_cast<f32>(tile);
            }
            if (voxelTiles_.size() >= kVoxelAtlasTiles || !voxelTilePipeline_.valid())
                return -1.0f;
            if (!voxelAtlas_.valid()) {
                voxelAtlas_ = device.createTexture({
                    .format = kVoxelAtlasFormat,
                    .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled,
                    .width = kVoxelAtlasSize,
                    .height = kVoxelAtlasSize,
                    .debugName = "voxel-atlas",
                });
                if (!voxelAtlas_.valid())
                    return -1.0f;
            }
            const auto tile = static_cast<u32>(voxelTiles_.size());
            const std::array<rhi::ColorAttachment, 1> atlasAttachment{rhi::ColorAttachment{
                .texture = voxelAtlas_,
                // The first image clears the atlas; every later one draws over
                // its own square and leaves the rest as it was.
                .loadOp = tile == 0 ? rhi::LoadOp::Clear : rhi::LoadOp::Load,
                .storeOp = rhi::StoreOp::Store,
            }};
            cmd.beginRenderPass({.colorAttachments = atlasAttachment, .debugName = "voxel-tile"});
            const auto column = static_cast<f32>(tile % kVoxelTilesPerRow);
            const auto row = static_cast<f32>(tile / kVoxelTilesPerRow);
            const auto size = static_cast<f32>(kVoxelTileSize);
            cmd.setViewport({.x = column * size, .y = row * size, .width = size, .height = size});
            cmd.setScissor({.x = static_cast<core::i32>(column * size),
                            .y = static_cast<core::i32>(row * size),
                            .width = static_cast<core::i32>(kVoxelTileSize),
                            .height = static_cast<core::i32>(kVoxelTileSize)});
            cmd.setPipeline(voxelTilePipeline_);
            const std::array<rhi::TextureBinding, 1> source{rhi::TextureBinding{texture, pointSampler_}};
            cmd.bindTextures(rhi::ShaderStage::Fragment, 0, source);
            cmd.draw(3, 1, 0, 0);
            cmd.endRenderPass();
            voxelTiles_.emplace_back(texture.id, tile);
            return static_cast<f32>(tile);
        };
        for (u32 id = 0; id < kVoxelPaletteSize; ++id) {
            const RenderWorld::VoxelTextures images =
                id < world.voxelTextures.size() ? world.voxelTextures[id] : RenderWorld::VoxelTextures{};
            voxelPalette_.tiles[id][0] = tileOf(images.top);
            voxelPalette_.tiles[id][1] = tileOf(images.side);
            voxelPalette_.tiles[id][2] = tileOf(images.bottom);
            voxelPalette_.tiles[id][3] = id < world.voxelColors.size() ? world.voxelColors[id].alpha : 1.0f;
        }
        voxelPalette_.params[0] = world.voxelBlockSize;
        (void)ensureVoxel(device);
    }

    // The terrain's palette, and its pipelines the first frame it is drawn --
    // before any pass, because a pipeline made mid-pass is not.
    if (!world.terrains.empty()) {
        for (u32 id = 0; id < kTerrainPaletteSize; ++id) {
            for (u32 channel = 0; channel < 4; ++channel)
                terrainSurface_.palette[id][channel] = world.terrains.front().palette[id][channel];
        }
        (void)ensureTerrain(device);
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

    // --- Local shadows: which lights get a tile ------------------------------
    //
    // **This is `PointLight.Shadows` and `SpotLight.Shadows` finally meaning
    // something** (`shadow.h`, and decision 5 of the finish-line ledger). Done
    // here rather than in `buildClusters` because it is a question about the
    // ATLAS and not about the clusters, and because the answer has to be written
    // into the light table before that table is uploaded a few lines below.
    localCandidates_.clear();
    for (const RenderLight& light : budgetedLights) {
        LocalShadowCandidate candidate;
        if (light.shadows) {
            candidate.position = light.position;
            candidate.direction = light.direction;
            candidate.range = light.range;
            candidate.cosHalfAngle = light.kind == LightKind::Spot ? light.spotCosHalfAngle : -1.0f;
        }
        // A light that casts nothing still occupies an INDEX, because the fit
        // answers with candidate indices and those have to be the light table's
        // own. Its range is zero, which the fit refuses -- so it costs a slot in
        // a vector and nothing else.
        localCandidates_.push_back(candidate);
    }
    localShadows_ = fitLocalShadows(localCandidates_);

    // The tile goes in `Color.w`, which was genuinely unused. -1 means "casts
    // nothing", which is what every light starts as.
    for (u32 index = 0; index < clusters_.lightCount; ++index)
        clusters_.lightData[static_cast<usize>(index) * 12 + 7] = -1.0f;
    for (u32 entry = 0; entry < localShadows_.count; ++entry) {
        const LocalShadow& shadow = localShadows_.entries[entry];
        if (shadow.candidate < clusters_.lightCount)
            clusters_.lightData[static_cast<usize>(shadow.candidate) * 12 + 7] = static_cast<f32>(shadow.firstTile);
    }

    cmd.uploadTexture(clusterGrid_, asBytes(clusters_.grid.data(), clusters_.grid.size() * sizeof(f32)), 0);
    cmd.uploadTexture(lightIndices_, asBytes(clusters_.indices.data(), clusters_.indices.size() * sizeof(f32)), 0);
    cmd.uploadTexture(lightData_, asBytes(clusters_.lightData.data(), clusters_.lightData.size() * sizeof(f32)), 0);

    // The cascade fit, from the camera basis. `inverse(view)` is the camera's own
    // frame; the camera sits at the origin of this space, so only its axes are
    // read out of it.
    const Mat4 cameraFrame = core::inverse(world.camera.view);
    ShadowFit fit;
    // From where the light comes: the moon's shadows at night (see `SkyParams`).
    fit.sunDirection = sky.lightDirection;
    fit.right = Vec3{cameraFrame.m[0][0], cameraFrame.m[0][1], cameraFrame.m[0][2]};
    fit.up = Vec3{cameraFrame.m[1][0], cameraFrame.m[1][1], cameraFrame.m[1][2]};
    // The camera looks down -Z, so its forward is the negated third axis.
    fit.forward = Vec3{-cameraFrame.m[2][0], -cameraFrame.m[2][1], -cameraFrame.m[2][2]};
    fit.spread = core::viewSpread(world.camera.projection);
    fit.nearPlane = world.camera.nearPlane;
    fit.origin = world.camera.origin;

    // What can cast, handed to the fit so a cascade can be sized to it. The two
    // numbers are already on every `DrawItem`; nothing is computed here that the
    // extraction did not compute.
    casterBounds_.clear();
    casterBounds_.reserve(world.draws.size());
    for (const DrawItem& draw : world.draws) {
        // **A coarse terrain node is not a caster the fit can use.** It is
        // hundreds of metres across, and handing it to the fit inflated every
        // cascade's depth range to its size -- the depth bias is a fraction of
        // that range, and thin casters' shadows vanished into it. Ground that
        // near is in the finer nodes; the coarse ones still draw into the map.
        if (draw.terrain && draw.boundsRadius > kTerrainCasterRadius)
            continue;
        casterBounds_.push_back(ShadowCasterBounds{draw.boundsCenter, draw.boundsRadius});
    }
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
        // `Lighting.GlobalShadows` off (ADR 0096) is the same mechanism with
        // no cascade drawn: the atlas is cleared and every fragment reads lit.
        const u32 cascadesDrawn = world.environment.globalShadows ? settings_.shadowCascades : 0u;
        for (u32 index = 0; index < cascadesDrawn; ++index) {
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
            // **The terrain has no far side to store.** Every mesh here culls
            // its front faces, so the depth in the map is the back of a solid
            // and a lit surface never shadows itself (D051). The ground is one
            // surface: drawn as it is, the map holds exactly the depth the
            // ground is then compared against, and the whole terrain acnes into
            // a dark rectangle the size of the cascade. So it is pushed away
            // from the light -- in the light's clip depth, which for an
            // orthographic cascade is linear in metres -- by as many texels as
            // the filter can reach plus a margin for a low sun: the widest disc
            // reads depths six texels off, and on ground tilted from the light
            // those are deeper than the fragment by the slope. Two texels left
            // concentric rings of self-shadow on open ground at a low sun.
            const f32 push = cascades.depthRange[index] > 0.0f
                                 ? 6.0f * cascades.texelWorld[index] / cascades.depthRange[index]
                                 : 0.0f;
            terrainShadowPush_ = push;
            drawGeometry(cmd, world, meshes, cascades.viewProjection[index], shadowPipeline_, shadowSkinnedPipeline_,
                         Selection::Shadow, &cull);
        }
    }
    cmd.endRenderPass();
    cmd.popDebugGroup();

    // --- Local shadow pass ---------------------------------------------------
    //
    // The same shape as the pass above and a different fit: one target, one
    // viewport per tile. **It runs even with nothing to draw**, for the reason
    // the cascade pass does -- a cleared depth of 1 reads as lit, so a tile
    // nobody rendered into is a light that shadows nothing rather than a light
    // that shadows everything with last frame's depths.
    //
    // Each tile culls against its own light's sphere. Without that, a scene with
    // six casting lights would submit its whole geometry six times, which is the
    // cost that makes a tile budget necessary rather than nice.
    cmd.pushDebugGroup("local-shadow");
    cmd.beginRenderPass({
        .colorAttachments = {},
        .depthStencil = {.texture = localShadowMap_, .loadOp = rhi::LoadOp::Clear, .storeOp = rhi::StoreOp::Store},
        .debugName = "local-shadow",
    });
    for (u32 entry = 0; entry < localShadows_.count; ++entry) {
        const LocalShadow& shadow = localShadows_.entries[entry];
        const LocalShadowCandidate& candidate = localCandidates_[shadow.candidate];
        for (u32 face = 0; face < shadow.tileCount; ++face) {
            const LocalShadowTileRect rect = localShadowTileRect(shadow.firstTile + face);
            cmd.setPipeline(shadowPipeline_);
            cmd.setViewport({.x = static_cast<f32>(rect.x),
                             .y = static_cast<f32>(rect.y),
                             .width = static_cast<f32>(rect.width),
                             .height = static_cast<f32>(rect.height)});
            cmd.setScissor({.x = static_cast<core::i32>(rect.x),
                            .y = static_cast<core::i32>(rect.y),
                            .width = static_cast<core::i32>(rect.width),
                            .height = static_cast<core::i32>(rect.height)});
            const CullSphere cull{candidate.position, candidate.range};
            terrainShadowPush_ = 0.0f;
            drawGeometry(cmd, world, meshes, shadow.viewProjection[face], shadowPipeline_, shadowSkinnedPipeline_,
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
    // The screen-space passes rebuild a position from depth as a perspective
    // camera made it, and an orthographic one (the 2D layer) is not that --
    // so under one they are off rather than wrong.
    const bool orthographic = core::isOrthographic(world.camera.projection);
    if (!settings_.ambientOcclusion || orthographic) {
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

    // --- Contact shadows -----------------------------------------------------
    //
    // The sun's last few centimetres, from the prepass depth (contact_shadow.hlsl).
    // After the occlusion pass because it reads the same depth, and before the
    // forward pass because the forward pass samples what it writes. Off, it is
    // a white clear, which the forward pass's `min` then ignores.
    cmd.pushDebugGroup("contact-shadow");
    if (!settings_.contactShadows || !world.camera.valid || settings_.shadowCascades == 0 || orthographic ||
        !world.environment.globalShadows) {
        clearPass(cmd, contact_, renderWidth_, renderHeight_, "contact-off", rhi::ColorRgba{1.0f, 1.0f, 1.0f, 1.0f});
    }
    else {
        GpuContactUniforms contact;
        contact.projection[0] = world.camera.projection.m[0][0] != 0.0f ? 1.0f / world.camera.projection.m[0][0] : 1.0f;
        contact.projection[1] = world.camera.projection.m[1][1] != 0.0f ? 1.0f / world.camera.projection.m[1][1] : 1.0f;
        contact.projection[2] = world.camera.nearPlane;
        contact.projection[3] = world.camera.farPlane;
        // Towards the sun, turned into the camera's view space: the view
        // matrix's rotation, applied to a direction.
        const Vec3 sun = sky.lightDirection;
        const Mat4& view = world.camera.view;
        const Vec3 viewSun{view.m[0][0] * sun.x + view.m[1][0] * sun.y + view.m[2][0] * sun.z,
                           view.m[0][1] * sun.x + view.m[1][1] * sun.y + view.m[2][1] * sun.z,
                           view.m[0][2] * sun.x + view.m[1][2] * sun.y + view.m[2][2] * sun.z};
        const f32 sunLength = core::length(viewSun);
        contact.sun[0] = sunLength > 0.0f ? viewSun.x / sunLength : 0.0f;
        contact.sun[1] = sunLength > 0.0f ? viewSun.y / sunLength : 1.0f;
        contact.sun[2] = sunLength > 0.0f ? viewSun.z / sunLength : 0.0f;
        contact.sun[3] = kContactRayMetres;
        contact.params[0] = kContactThicknessMetres;
        // A light that lights nothing shadows nothing: a sun below the horizon
        // before the moon has come up.
        contact.params[1] = sky.lightPresence;
        contact.params[2] = kContactFadeDistance;
        contact.params[3] = 1.0f;
        const std::array<rhi::TextureBinding, 1> depthBinding{rhi::TextureBinding{depth_, pointSampler_}};
        fullscreenPass(cmd, contactPipeline_, contact_, renderWidth_, renderHeight_, "contact-shadow", depthBinding,
                       asBytes(&contact, sizeof(contact)));
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
        if (skyGoverned) {
            // **The sky a `Sky` governs** (ADR 0096): its pictures or the
            // gradient, and its sun, moon and stars, through its own pipeline.
            GpuLookSkyUniforms lookSky;
            lookSky.flags[0] = skyLook.image.valid() ? 1.0f : 0.0f;
            lookSky.flags[1] = skyLook.celestialBodiesShown ? 1.0f : 0.0f;
            lookSky.flags[2] = skyLook.sunImage.valid() ? 1.0f : 0.0f;
            lookSky.flags[3] = skyLook.moonImage.valid() ? 1.0f : 0.0f;
            // The moon stands opposite the sun, as the light model has it.
            lookSky.moon[0] = -sky.sunDirection.x;
            lookSky.moon[1] = -sky.sunDirection.y;
            lookSky.moon[2] = -sky.sunDirection.z;
            lookSky.moon[3] = skyLook.moonAngularSize * 0.5f * kDegreesToRadians;
            // Night is what shows the moon and the stars: none of either while
            // the day factor is up, all of them once it has gone.
            const f32 night = 1.0f - sky.dayFactor;
            lookSky.moonColor[0] = 0.78f * kMoonGlow;
            lookSky.moonColor[1] = 0.82f * kMoonGlow;
            lookSky.moonColor[2] = 0.9f * kMoonGlow;
            lookSky.moonColor[3] = night;
            // About StarCount stars over the whole sphere: six faces of cells,
            // half of them holding one.
            const f32 cells = std::sqrt(static_cast<f32>(skyLook.starCount) / (6.0f * kStarChance));
            lookSky.stars[0] = skyLook.starCount > 0 ? std::max(cells, 1.0f) : 0.0f;
            lookSky.stars[1] = kStarChance;
            lookSky.stars[2] = night * night * kStarGlow;
            lookSky.clouds[0] = sky.cloudCover;
            lookSky.clouds[1] = sky.cloudDensity;
            lookSky.clouds[2] = sky.cloudDriftX;
            lookSky.clouds[3] = sky.cloudDriftZ;
            lookSky.cloudColor[0] = sky.cloudColor.r;
            lookSky.cloudColor[1] = sky.cloudColor.g;
            lookSky.cloudColor[2] = sky.cloudColor.b;
            lookSky.cloudColor[3] = kCloudSunLight;
            cmd.setPipeline(skyLook_.handle);
            cmd.bindUniforms(rhi::ShaderStage::Fragment, 0, asBytes(&skyUniforms, sizeof(skyUniforms)));
            cmd.bindUniforms(rhi::ShaderStage::Fragment, 1, asBytes(&lookSky, sizeof(lookSky)));
            const std::array<rhi::TextureBinding, 3> skyTextures{
                rhi::TextureBinding{skyLook.image.valid() ? skyLook.image : whitePixel_, environmentSampler_},
                rhi::TextureBinding{skyLook.sunImage.valid() ? skyLook.sunImage : whitePixel_, environmentSampler_},
                rhi::TextureBinding{skyLook.moonImage.valid() ? skyLook.moonImage : whitePixel_, environmentSampler_}};
            cmd.bindTextures(rhi::ShaderStage::Fragment, 0, skyTextures);
            cmd.draw(3, 1, 0, 0);
        }
        else {
            cmd.setPipeline(skyPipeline_);
            cmd.bindUniforms(rhi::ShaderStage::Fragment, 0, asBytes(&skyUniforms, sizeof(skyUniforms)));
            cmd.draw(3, 1, 0, 0);
        }

        GpuFrameUniforms frame;
        frame.sunDirectionBrightness[0] = sky.lightDirection.x;
        frame.sunDirectionBrightness[1] = sky.lightDirection.y;
        frame.sunDirectionBrightness[2] = sky.lightDirection.z;
        // The day factor is folded in here rather than tested in the shader: a
        // sun below the horizon is a sun that lights nothing, and before M7.5 it
        // went on lighting every upward-facing surface from underneath.
        // The moon by night, at its own small fraction (see `SkyParams`).
        frame.sunDirectionBrightness[3] = world.environment.sunBrightness * sky.lightFactor;
        frame.sunColorUnused[0] = sky.lightColor.r;
        frame.sunColorUnused[1] = sky.lightColor.g;
        frame.sunColorUnused[2] = sky.lightColor.b;
        frame.ambient[0] = world.environment.ambient.r;
        frame.ambient[1] = world.environment.ambient.g;
        frame.ambient[2] = world.environment.ambient.b;
        frame.outdoorAmbient[0] = world.environment.outdoorAmbient.r;
        frame.outdoorAmbient[1] = world.environment.outdoorAmbient.g;
        frame.outdoorAmbient[2] = world.environment.outdoorAmbient.b;
        // **With an `Atmosphere` the linear fog is not the world's any more**
        // (ADR 0096): `FogStart`, `FogEnd` and `FogColor` are kept and not
        // used. The opaque world and the sky are covered by the air pass below;
        // what still reads these three -- the blended surfaces and the
        // particles, which the air pass cannot see behind -- gets a linear
        // stand-in for the same air at the camera's height: its colour, from
        // the camera out to where nineteen parts in twenty are hidden.
        Color3 fogColor = world.environment.fogColor;
        f32 fogStart = world.environment.fogStart;
        f32 fogEnd = world.environment.fogEnd;
        if (air) {
            const AirMedium medium = airMediumOf(world.look.atmosphere, world.camera.origin.y);
            const f32 hidden = airOpticalDepth(medium, 0.0f, 1.0f);
            fogColor = sky.horizonColor;
            fogStart = 0.0f;
            fogEnd = hidden > 0.0f ? 3.0f / hidden : 0.0f;
        }
        frame.fogColor[0] = fogColor.r;
        frame.fogColor[1] = fogColor.g;
        frame.fogColor[2] = fogColor.b;
        frame.fogRange[0] = fogStart;
        frame.fogRange[1] = fogEnd;
        // Precomputed here so a fragment shader does not divide per pixel, and
        // zero when fog is off -- which makes the fog factor zero without the
        // shader needing to know that `end <= start` means anything.
        frame.fogRange[2] = fogEnd > fogStart ? 1.0f / (fogEnd - fogStart) : 0.0f;
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
        // `Lighting.ShadowSoftness` (ADR 0096): a quarter of a metre at 1, so
        // its default of 0.2 is the engine's own radius to the bit -- 0.2f
        // times a power of two is exact.
        frame.shadowParams[0] = world.environment.shadowSoftness == 0.2f ? kShadowFilterWorldRadius
                                                                         : world.environment.shadowSoftness * 0.25f;
        frame.shadowParams[1] = kShadowNormalOffsetTexels;
        frame.shadowParams[2] = kShadowCascadeBlend;
        frame.shadowParams[3] = kShadowDepthBiasMetres;

        // The local atlas: one matrix per TILE, and how many of them are live.
        // Zero live tiles is what a scene with no casting spot or point gets,
        // and the shader skips the whole lookup on it.
        for (u32 entry = 0; entry < localShadows_.count; ++entry) {
            const LocalShadow& shadow = localShadows_.entries[entry];
            for (u32 face = 0; face < shadow.tileCount; ++face) {
                const u32 tile = shadow.firstTile + face;
                if (tile < kLocalShadowTileCount)
                    frame.localShadowViewProjection[tile] = shadow.viewProjection[face];
            }
        }
        frame.localShadowParams[0] = static_cast<f32>(localShadows_.tilesUsed);
        frame.localShadowParams[1] = 1.0f / static_cast<f32>(kLocalShadowAtlasResolution);
        frame.localShadowParams[2] = kLocalShadowDepthBias;
        frame.localShadowParams[3] = kLocalShadowFilterTexels;

        frame.environmentParams[0] = static_cast<f32>(kEnvironmentMipCount);
        frame.environmentParams[1] = 1.0f;
        frame.environmentParams[2] = 1.0f;
        // `Lighting.EnvironmentDiffuseScale` (ADR 0096) on the nine
        // coefficients -- linear in them, so the sky's diffuse light scales
        // with no shader knowing. One is one, and the bytes are unchanged.
        const f32 diffuseScale = world.environment.environmentDiffuseScale;
        for (u32 index = 0; index < 9; ++index) {
            frame.irradianceSh[index][0] = environment_.irradiance[index].x * diffuseScale;
            frame.irradianceSh[index][1] = environment_.irradiance[index].y * diffuseScale;
            frame.irradianceSh[index][2] = environment_.irradiance[index].z * diffuseScale;
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

        // **Decals, between the opaque surfaces and the transparent ones**
        // (F2). They read the depth the opaque surfaces wrote, which a pass
        // that has it attached cannot, so the forward pass is closed around
        // them and reopened after -- only on a frame that has any, so every
        // other frame's command stream is what it always was.
        if (!world.decals.empty() && ensureDecals(device)) {
            cmd.endRenderPass();
            const std::array<rhi::ColorAttachment, 1> decalTarget{rhi::ColorAttachment{
                .texture = hdr_,
                .loadOp = rhi::LoadOp::Load,
                .storeOp = rhi::StoreOp::Store,
            }};
            cmd.beginRenderPass({.colorAttachments = decalTarget, .debugName = "decals"});
            cmd.setViewport({.width = static_cast<f32>(renderWidth_), .height = static_cast<f32>(renderHeight_)});
            cmd.setScissor(
                {.width = static_cast<core::i32>(renderWidth_), .height = static_cast<core::i32>(renderHeight_)});
            cmd.setPipeline(decalPipeline_);
            const Mat4 inverseViewProjection = core::inverse(world.camera.viewProjection);
            for (const RenderDecal& decal : world.decals) {
                GpuDecalUniforms vertex;
                vertex.boxToWorld = decal.boxToWorld;
                vertex.viewProjection = world.camera.viewProjection;
                GpuDecalFragment fragment;
                fragment.worldToBox = decal.worldToBox;
                fragment.inverseViewProjection = inverseViewProjection;
                fragment.color[0] = decal.color.r;
                fragment.color[1] = decal.color.g;
                fragment.color[2] = decal.color.b;
                fragment.color[3] = decal.opacity;
                fragment.params[0] = 1.0f / static_cast<f32>(renderWidth_);
                fragment.params[1] = 1.0f / static_cast<f32>(renderHeight_);
                fragment.params[2] = decal.texture.valid() ? 1.0f : 0.0f;
                fragment.axis[0] = decal.axis.x;
                fragment.axis[1] = decal.axis.y;
                fragment.axis[2] = decal.axis.z;
                cmd.bindUniforms(rhi::ShaderStage::Vertex, 0, asBytes(&vertex, sizeof(vertex)));
                cmd.bindUniforms(rhi::ShaderStage::Fragment, 0, asBytes(&fragment, sizeof(fragment)));
                const std::array<rhi::TextureBinding, 2> textures{
                    rhi::TextureBinding{decal.texture.valid() ? decal.texture : whitePixel_, linearSampler_},
                    rhi::TextureBinding{depth_, pointSampler_},
                };
                cmd.bindTextures(rhi::ShaderStage::Fragment, 0, textures);
                cmd.draw(36, 1, 0, 0);
                stats_.drawCalls += 1;
            }
            cmd.endRenderPass();

            const std::array<rhi::ColorAttachment, 1> resumeTarget{rhi::ColorAttachment{
                .texture = hdr_,
                .loadOp = rhi::LoadOp::Load,
                .storeOp = rhi::StoreOp::Store,
            }};
            cmd.beginRenderPass({
                .colorAttachments = resumeTarget,
                .depthStencil = {.texture = depth_, .loadOp = rhi::LoadOp::Load, .storeOp = rhi::StoreOp::Store},
                .debugName = "forward-blended",
            });
            cmd.setViewport({.width = static_cast<f32>(renderWidth_), .height = static_cast<f32>(renderHeight_)});
            cmd.setScissor(
                {.width = static_cast<core::i32>(renderWidth_), .height = static_cast<core::i32>(renderHeight_)});
            // The frame block again: the decal pass bound its own at the same
            // slot, and the blended surfaces below read this one.
            cmd.setPipeline(pbrBlendPipeline_);
            cmd.bindUniforms(rhi::ShaderStage::Fragment, 0, asBytes(&frame, sizeof(frame)));
        }

        // **The 2D layer's sprites**, after everything opaque and before the
        // blended 3D surfaces: a picture on the plane is hidden by a solid part
        // in front of it, and a pane of glass in front of it is drawn over it.
        // In runs of one image and one filter, which a tilemap makes long.
        if (spriteCount_ > 0) {
            GpuWorldUiView spriteView;
            spriteView.viewProjection = world.camera.viewProjection;
            cmd.setPipeline(spritePipeline_);
            cmd.bindUniforms(rhi::ShaderStage::Vertex, 0, asBytes(&spriteView, sizeof(spriteView)));
            const std::array<rhi::BufferHandle, 1> spriteBuffers{spriteBuffer_};
            cmd.bindVertexBuffers(0, spriteBuffers);
            u32 first = 0;
            while (first < spriteCount_) {
                const RenderSprite& lead = world.sprites[first];
                u32 end = first + 1;
                while (end < spriteCount_ && world.sprites[end].texture == lead.texture &&
                       world.sprites[end].nearest == lead.nearest) {
                    ++end;
                }
                const std::array<rhi::TextureBinding, 1> texture{
                    rhi::TextureBinding{lead.texture.valid() ? lead.texture : whitePixel_,
                                        lead.nearest ? pointSampler_ : environmentSampler_}};
                cmd.bindTextures(rhi::ShaderStage::Fragment, 0, texture);
                cmd.draw(6, end - first, 0, first);
                stats_.drawCalls += 1;
                first = end;
            }
            // The frame block again, for the blended surfaces below.
            cmd.bindUniforms(rhi::ShaderStage::Fragment, 0, asBytes(&frame, sizeof(frame)));
        }

        // **The air** (`Atmosphere`, ADR 0096), over everything opaque and
        // over the sky, before anything blended: it reads the depth the forward
        // pass has attached, so the pass is closed around it and reopened after,
        // as it is for the decals -- only on a frame that has air.
        if (airDrawn) {
            cmd.endRenderPass();
            const AirMedium medium = airMediumOf(world.look.atmosphere, world.camera.origin.y);
            GpuLookAirUniforms airBlock;
            airBlock.inverseViewProjection = core::inverse(world.camera.viewProjection);
            airBlock.density[0] = medium.extinction;
            airBlock.density[1] = medium.falloff;
            airBlock.density[2] = medium.height;
            airBlock.density[3] = medium.haze;
            airBlock.light[0] = sky.horizonColor.r;
            airBlock.light[1] = sky.horizonColor.g;
            airBlock.light[2] = sky.horizonColor.b;
            airBlock.light[3] = kAirSkyReach;
            const f32 glare = world.look.atmosphere.glare * kAirGlareStrength * sky.dayFactor;
            airBlock.glare[0] = sky.sunColor.r * glare;
            airBlock.glare[1] = sky.sunColor.g * glare;
            airBlock.glare[2] = sky.sunColor.b * glare;
            airBlock.glare[3] = kAirSkyShare;
            airBlock.sun[0] = sky.sunDirection.x;
            airBlock.sun[1] = sky.sunDirection.y;
            airBlock.sun[2] = sky.sunDirection.z;
            airBlock.sun[3] = kAirGlareExponent;
            const std::array<rhi::TextureBinding, 1> sceneDepth{rhi::TextureBinding{depth_, pointSampler_}};
            fullscreenPass(cmd, air_.handle, hdr_, renderWidth_, renderHeight_, "atmosphere", sceneDepth,
                           asBytes(&airBlock, sizeof(airBlock)), rhi::LoadOp::Load);

            const std::array<rhi::ColorAttachment, 1> resumeTarget{rhi::ColorAttachment{
                .texture = hdr_,
                .loadOp = rhi::LoadOp::Load,
                .storeOp = rhi::StoreOp::Store,
            }};
            cmd.beginRenderPass({
                .colorAttachments = resumeTarget,
                .depthStencil = {.texture = depth_, .loadOp = rhi::LoadOp::Load, .storeOp = rhi::StoreOp::Store},
                .debugName = "forward-after-air",
            });
            cmd.setViewport({.width = static_cast<f32>(renderWidth_), .height = static_cast<f32>(renderHeight_)});
            cmd.setScissor(
                {.width = static_cast<core::i32>(renderWidth_), .height = static_cast<core::i32>(renderHeight_)});
            cmd.setPipeline(pbrBlendPipeline_);
            cmd.bindUniforms(rhi::ShaderStage::Fragment, 0, asBytes(&frame, sizeof(frame)));
        }

        // Blended, after the opaque pass has filled depth, back to front. The
        // frame uniforms are still bound -- same block, same slot, same values
        // -- so only the pipeline changes.
        //
        // What this does NOT buy, and the deliverable should not imply
        // otherwise: sorting is per draw, so two transparent surfaces that
        // intersect each other sort wrongly at the pixels where they cross.
        // Order-independent transparency is not on the v1 list.
        copySceneForSurfaces(device, cmd, world, frame);
        cmd.setPipeline(pbrBlendPipeline_);
        drawGeometry(cmd, world, meshes, world.camera.viewProjection, pbrBlendPipeline_, pbrSkinnedBlendPipeline_,
                     Selection::Transparent);

        // Particles last (F2): over every surface, transparent ones included,
        // which is the one ordering error this accepts -- a particle behind a
        // pane of glass draws over it. Sorting the two together would put a
        // per-particle draw into a per-draw sort, and the whole point of a
        // particle is that it is not a draw of its own.
        if (particleCount_ > 0) {
            GpuParticleUniforms particleUniforms;
            particleUniforms.viewProjection = world.camera.viewProjection;
            const Mat4 cameraToWorld = core::inverse(world.camera.view);
            const Vec3 right = core::transformDirection(cameraToWorld, Vec3{1.0f, 0.0f, 0.0f});
            const Vec3 up = core::transformDirection(cameraToWorld, Vec3{0.0f, 1.0f, 0.0f});
            particleUniforms.cameraRight[0] = right.x;
            particleUniforms.cameraRight[1] = right.y;
            particleUniforms.cameraRight[2] = right.z;
            particleUniforms.cameraUp[0] = up.x;
            particleUniforms.cameraUp[1] = up.y;
            particleUniforms.cameraUp[2] = up.z;

            // A particle carries no sky term, so it is lit as the open air is
            // (ADR 0084): `OutdoorAmbient`, not the enclosed `Ambient`.
            GpuParticleLighting lighting;
            lighting.ambient[0] = world.environment.outdoorAmbient.r;
            lighting.ambient[1] = world.environment.outdoorAmbient.g;
            lighting.ambient[2] = world.environment.outdoorAmbient.b;
            const f32 sun = world.environment.sunBrightness * sky.lightFactor;
            lighting.sunLight[0] = sky.lightColor.r * sun;
            lighting.sunLight[1] = sky.lightColor.g * sun;
            lighting.sunLight[2] = sky.lightColor.b * sun;
            lighting.fogColor[0] = frame.fogColor[0];
            lighting.fogColor[1] = frame.fogColor[1];
            lighting.fogColor[2] = frame.fogColor[2];
            lighting.fogRange[0] = frame.fogRange[0];
            lighting.fogRange[1] = frame.fogRange[1];
            lighting.fogRange[2] = frame.fogRange[2];
            // Negative for an orthographic camera: `particle.hlsl` reads the
            // sign to know its depth is linear.
            lighting.depth[0] =
                core::isOrthographic(world.camera.projection) ? -world.camera.nearPlane : world.camera.nearPlane;
            lighting.depth[1] = world.camera.farPlane;
            lighting.depth[2] = 1.0f / static_cast<f32>(renderWidth_);
            lighting.depth[3] = 1.0f / static_cast<f32>(renderHeight_);

            // **Soft particles read the depth the forward pass has attached**,
            // so it is closed around them and reopened after, as it is for the
            // decals -- only on a frame that has particles.
            cmd.endRenderPass();
            const std::array<rhi::ColorAttachment, 1> particleTarget{rhi::ColorAttachment{
                .texture = hdr_,
                .loadOp = rhi::LoadOp::Load,
                .storeOp = rhi::StoreOp::Store,
            }};
            cmd.beginRenderPass({.colorAttachments = particleTarget, .debugName = "particles"});
            cmd.setViewport({.width = static_cast<f32>(renderWidth_), .height = static_cast<f32>(renderHeight_)});
            cmd.setScissor(
                {.width = static_cast<core::i32>(renderWidth_), .height = static_cast<core::i32>(renderHeight_)});
            cmd.setPipeline(particlePipeline_);
            cmd.bindUniforms(rhi::ShaderStage::Vertex, 0, asBytes(&particleUniforms, sizeof(particleUniforms)));
            cmd.bindUniforms(rhi::ShaderStage::Fragment, 0, asBytes(&lighting, sizeof(lighting)));
            const std::array<rhi::TextureBinding, 1> sceneDepth{rhi::TextureBinding{depth_, pointSampler_}};
            cmd.bindTextures(rhi::ShaderStage::Fragment, 0, sceneDepth);
            const std::array<rhi::BufferHandle, 1> particleBuffers{particleBuffer_};
            cmd.bindVertexBuffers(0, particleBuffers);
            cmd.draw(6, particleCount_, 0, 0);
            stats_.drawCalls += 1;
            cmd.endRenderPass();

            const std::array<rhi::ColorAttachment, 1> resumeTarget{rhi::ColorAttachment{
                .texture = hdr_,
                .loadOp = rhi::LoadOp::Load,
                .storeOp = rhi::StoreOp::Store,
            }};
            cmd.beginRenderPass({
                .colorAttachments = resumeTarget,
                .depthStencil = {.texture = depth_, .loadOp = rhi::LoadOp::Load, .storeOp = rhi::StoreOp::Store},
                .debugName = "forward-after-particles",
            });
            cmd.setViewport({.width = static_cast<f32>(renderWidth_), .height = static_cast<f32>(renderHeight_)});
            cmd.setScissor(
                {.width = static_cast<core::i32>(renderWidth_), .height = static_cast<core::i32>(renderHeight_)});
        }

        // **World UI after the particles** (F3): the trees arrive back to
        // front, and the ones that are always on top come after all of them.
        if (worldUiVertexCount_ > 0) {
            GpuWorldUiView view;
            view.viewProjection = world.camera.viewProjection;
            const std::array<rhi::BufferHandle, 1> uiBuffers{worldUiBuffer_};
            for (const bool onTop : {false, true}) {
                cmd.setPipeline(onTop ? worldUiOnTopPipeline_ : worldUiPipeline_);
                cmd.bindUniforms(rhi::ShaderStage::Vertex, 0, asBytes(&view, sizeof(view)));
                cmd.bindVertexBuffers(0, uiBuffers);
                for (const WorldUiRun& run : world.worldUiRuns) {
                    if (run.alwaysOnTop != onTop || run.vertexCount == 0 ||
                        run.firstVertex + run.vertexCount > worldUiVertexCount_)
                        continue;
                    GpuWorldUiLook look;
                    look.params[0] = run.brightness;
                    cmd.bindUniforms(rhi::ShaderStage::Fragment, 0, asBytes(&look, sizeof(look)));
                    const std::array<rhi::TextureBinding, 1> texture{
                        rhi::TextureBinding{run.texture.valid() ? run.texture : whitePixel_, environmentSampler_}};
                    cmd.bindTextures(rhi::ShaderStage::Fragment, 0, texture);
                    cmd.draw(run.vertexCount, 1, run.firstVertex, 0);
                    stats_.drawCalls += 1;
                }
            }
        }
    }

    cmd.endRenderPass();
    cmd.popDebugGroup();

    // --- The look's scene passes (ADR 0096) ------------------------------------
    //
    // What every pass below reads as "the frame". `hdr_` on every frame without
    // one of these effects, which is what keeps that frame's command stream the
    // one it always was.
    const RenderLook& look = world.look;
    rhi::TextureHandle sceneColor = hdr_;

    // **Depth of field**, first: it reads the depth the opaque world wrote,
    // and a blur or a shaft of light added before it would be focused as though
    // it stood where the surface behind it does. Not under an orthographic
    // camera, whose depth is not a distance a lens focuses by -- the 2D layer's
    // view -- and not on a machine that has turned it off (ADR 0044).
    if (look.depthOfField && settings_.depthOfField && world.camera.valid && !orthographic)
        sceneColor = focusImage(device, cmd, world, sceneColor);

    // **Sun rays**, after the focus -- a shaft is light in the air between the
    // camera and everything, which no lens focuses away -- and before the blur,
    // which softens them with the rest.
    if (look.sunRays && settings_.sunRays && world.camera.valid && !orthographic)
        sunRaysOnto(device, cmd, world, sky, sceneColor);

    // **The blur**, last of them: it softens whatever the others made. On the
    // HDR image and before exposure, so a highlight blurs as light does -- a
    // bright lamp spreads into a glow rather than into a grey smear -- and the
    // interface, which the host draws after all of this, stays sharp.
    if (look.blurSize > 0.0f && world.camera.valid)
        blurImage(device, cmd, sceneColor, look.blurSize);

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
    // The machine's switch and the world's, and either turns it off: a scene
    // that wants a fixed exposure gets one, and a machine that cannot afford
    // the metering does not pay for it (ADR 0044, ADR 0096).
    if (!settings_.autoExposure || !world.environment.autoExposure) {
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
        const std::array<rhi::TextureBinding, 1> hdrBinding{rhi::TextureBinding{sceneColor, linearSampler_}};
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
    // **A `BloomEffect` governs bloom when one counts** (ADR 0096): its numbers
    // replace the engine's own, and one that is disabled turns bloom off. With
    // none, every number below is the constant it always was. The machine's
    // switch still wins over the world's (ADR 0044).
    const bool bloomOn = settings_.bloom && look.bloomEnabled;
    cmd.pushDebugGroup("bloom");
    if (!bloomOn) {
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
                bloom.threshold[0] = look.bloomGoverned ? look.bloomThreshold : kBloomThreshold;
                bloom.threshold[1] = kBloomKnee;
            }
            // **Clamped at the edges, never wrapped** (D181): the material
            // sampler repeats, and a kernel this wide at a coarse level reached
            // round the screen -- the ground's glow at the bottom edge drew a
            // band along the top.
            const std::array<rhi::TextureBinding, 1> source{
                rhi::TextureBinding{level == 0 ? sceneColor : bloom_[level - 1], environmentSampler_}};
            sourceWidth = bloomLevelSize(renderWidth_, level);
            sourceHeight = bloomLevelSize(renderHeight_, level);
            fullscreenPass(cmd, bloomDownPipeline_, bloom_[level], sourceWidth, sourceHeight, "bloom-down", source,
                           asBytes(&bloom, sizeof(bloom)));
        }

        for (u32 level = kBloomLevels - 1; level > 0; --level) {
            GpuBloomUniforms bloom;
            bloom.texelRadius[0] = 1.0f / static_cast<f32>(bloomLevelSize(renderWidth_, level));
            bloom.texelRadius[1] = 1.0f / static_cast<f32>(bloomLevelSize(renderHeight_, level));
            // The tent's radius is how far the glow reaches: every level's
            // kernel widens together, so the falloff keeps its shape.
            bloom.texelRadius[2] = look.bloomGoverned ? look.bloomSize / kBloomSize : 1.0f;
            const std::array<rhi::TextureBinding, 1> source{rhi::TextureBinding{bloom_[level], environmentSampler_}};
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
    tonemap.exposureBloom[1] = look.bloomGoverned ? kBloomIntensity * look.bloomIntensity : kBloomIntensity;

    // **Every colour correction, as one affine map, in the graded twin of the
    // tonemap** -- chosen only on a frame that has one, so a world without
    // draws through the plain pipeline with the plain block.
    GpuGradeUniforms grade;
    // The plain tonemap's own target format, whichever target it writes.
    const bool graded = look.graded && ensureLookPipeline(device, gradedTonemap_, "tonemap_graded", kLdrFormat);
    if (graded) {
        for (u32 row = 0; row < 3; ++row) {
            for (u32 column = 0; column < 4; ++column)
                grade.rows[row][column] = look.grade[row][column];
        }
    }
    const std::array<rhi::TextureBinding, 3> tonemapBindings{
        rhi::TextureBinding{sceneColor, environmentSampler_}, rhi::TextureBinding{bloom_[0], environmentSampler_},
        rhi::TextureBinding{exposure_[nextExposure], linearSampler_}};

    // With anti-aliasing on, this writes the LDR texture the resolve reads and
    // the resolve is what reaches the target. With it off, this IS the resolve
    // -- and it is also where a reduced render scale is upscaled, because a
    // fullscreen pass into a larger target sampling a smaller source is exactly
    // a bilinear upscale.
    cmd.pushDebugGroup("tonemap");
    fullscreenPass(cmd, graded ? gradedTonemap_.handle : tonemapPipeline_, settings_.antiAliasing ? ldr_ : target.color,
                   settings_.antiAliasing ? renderWidth_ : target.width,
                   settings_.antiAliasing ? renderHeight_ : target.height, "tonemap", tonemapBindings,
                   asBytes(&tonemap, sizeof(tonemap)), rhi::LoadOp::Clear,
                   graded ? asBytes(&grade, sizeof(grade)) : std::span<const std::byte>{});
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

    // --- The editor's selection silhouette -----------------------------------
    //
    // **Last, over the finished image, and only when something is selected.**
    // A game's draw list never carries `outlined`, so a packaged build walks
    // this loop once and does nothing -- which is what keeps the pass out of
    // every golden in the repository without the renderer knowing what an
    // editor is.
    //
    // After tonemapping rather than before it: the outline is a tool's mark on
    // a picture, not a thing in the world, and running it through exposure and
    // a tone curve would make its colour depend on how bright the scene is.
    bool anyOutlined = false;
    for (const DrawItem& draw : world.draws) {
        if (draw.outlined) {
            anyOutlined = true;
            break;
        }
    }
    if (!anyOutlined)
        return;

    cmd.pushDebugGroup("outline");
    cmd.beginRenderPass({
        .colorAttachments = std::array<rhi::ColorAttachment, 1>{rhi::ColorAttachment{
            .texture = outlineMask_,
            .loadOp = rhi::LoadOp::Clear,
            .storeOp = rhi::StoreOp::Store,
        }},
        .debugName = "outline-mask",
    });
    cmd.setViewport({.width = static_cast<f32>(renderWidth_), .height = static_cast<f32>(renderHeight_)});
    cmd.setScissor({.width = static_cast<core::i32>(renderWidth_), .height = static_cast<core::i32>(renderHeight_)});
    if (world.camera.valid) {
        cmd.setPipeline(outlinePipeline_);
        drawGeometry(cmd, world, meshes, world.camera.viewProjection, outlinePipeline_, outlineSkinnedPipeline_,
                     Selection::Outline);
    }
    cmd.endRenderPass();

    GpuOutlineUniforms outline;
    outline.texelWidth[0] = 1.0f / static_cast<f32>(renderWidth_);
    outline.texelWidth[1] = 1.0f / static_cast<f32>(renderHeight_);
    // In mask texels rather than in output pixels, because that is what the
    // taps step in. At a reduced render scale the line thins with everything
    // else, which is what a person who asked for a smaller image meant.
    outline.texelWidth[2] = 2.0f;
    // Orange, because nothing in a PBR scene is and it stays legible against
    // both the lit and the shadowed halves of a frame -- the same colour and the
    // same reason the wire box had.
    outline.color[0] = 1.0f;
    outline.color[1] = 0.45f;
    outline.color[2] = 0.05f;
    outline.color[3] = 1.0f;
    // Faint on purpose. This is what separates a selected object from an
    // unselected one standing in front of it, and it must not hide the colour
    // somebody selected the part in order to change.
    outline.fillColor[0] = 1.0f;
    outline.fillColor[1] = 0.45f;
    outline.fillColor[2] = 0.05f;
    outline.fillColor[3] = 0.12f;

    const std::array<rhi::TextureBinding, 1> maskBinding{rhi::TextureBinding{outlineMask_, linearSampler_}};
    fullscreenPass(cmd, outlineCompositePipeline_, target.color, target.width, target.height, "outline-composite",
                   maskBinding, asBytes(&outline, sizeof(outline)), rhi::LoadOp::Load);
    cmd.popDebugGroup();
}

std::unique_ptr<IRenderer> createDefaultRenderer()
{
    return std::make_unique<DefaultRenderer>();
}

} // namespace luaug::render
