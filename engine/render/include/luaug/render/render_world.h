// The POD snapshot rendering reads (ADR 0027, architecture.md §4).
//
// Rendering never walks the ECS. It walks this, and the difference is the whole
// point: extraction happens once per frame at a known moment, so the renderer
// cannot observe a half-mutated world, cannot keep an `InstanceId` alive past
// its retirement, and can be handed to another thread the day one exists.
//
// It is a snapshot rather than a view for the same reason `scene`'s change queue
// carries POD facts: the two sides have different lifetimes and the seam is what
// keeps that from mattering.
//
// **Everything here is in camera-relative f32 space.** `CFrameD` carries f64
// because an open world needs it (ADR 0014), and a renderer does not: subtract
// the camera's position first and every coordinate the GPU sees is small.
// `origin` records what was subtracted. Deciding this at extraction rather than
// after four milestones of matrices is the M4 brief's Decision 8, and it is what
// makes the floating origin at M7 a change to one function.
#pragma once

#include "luaug/core/id.h"
#include "luaug/core/math.h"
#include "luaug/core/name_atom.h"
#include "luaug/core/types.h"
#include "luaug/render/animation.h"
#include "luaug/render/mesh_cache.h"
#include "luaug/render/shader_types.h"
#include "luaug/render/transform_history.h"
#include "luaug/rhi/types.h"

#include <span>
#include <vector>

namespace luaug::scene {
class World;
}

namespace luaug::render {

using core::AABB;
using core::CFrameD;
using core::Color3;
using core::DVec3;
using core::f32;
using core::Frustum;
using core::Mat4;
using core::u32;
using core::u64;
using core::usize;
using core::Vec3;

// One drawable part from the debug path. Deliberately not an `InstanceId`:
// nothing downstream may resolve one, because by the time a frame is drawn the
// instance behind it may have been destroyed and retired.
struct RenderPart
{
    CFrameD cframe;
    Vec3 size{1.0f, 1.0f, 1.0f};
    Color3 color{1.0f, 1.0f, 1.0f};
    f32 transparency = 0.0f;
    // `Enum.PartShape`'s stored value.
    core::i32 shape = 0;
};

// The view the frame is rendered from, already resolved into matrices.
//
// `valid` is false when `Workspace.CurrentCamera` is nil or names something that
// has been destroyed. The renderer draws nothing then, which is the documented
// behaviour: a view nobody asked for is harder to debug than a black frame.
struct RenderCamera
{
    bool valid = false;
    // The f64 world position every f32 coordinate in this snapshot is relative
    // to. It is the camera's own position, so the camera sits at the origin of
    // the space the GPU works in.
    DVec3 origin;
    Mat4 view;
    Mat4 projection;
    Mat4 viewProjection;
    // In the same camera-relative space, so culling needs no conversion.
    Frustum frustum;
    f32 nearPlane = 0.1f;
    f32 farPlane = 5000.0f;
    // A sub-pixel offset folded into the projection, in NDC units. Zero
    // everywhere, and that is the point.
    //
    // **A jitterable projection is a renderer OUTPUT, not private state of an
    // anti-aliasing pass** (roadmap, M7.5's second design constraint, human
    // decision 2026-08-21). Temporal anti-aliasing and every temporal upscaler
    // need exactly two things -- a per-pixel motion vector and this -- and
    // declaring them here rather than inside whichever pass first wants them is
    // what makes that later work days instead of a milestone.
    //
    // The other half deliberately does NOT ship: writing a velocity target on
    // every forward draw and carrying a previous transform on every `DrawItem`
    // is renderer-wide bandwidth for a consumer that does not exist, which is
    // the speculative abstraction the review bar forbids. What it would take is
    // written down in the M7.5 brief, Decision 10.
    core::Vec2 jitter;
};

enum class LightKind : core::u8
{
    Point,
    Spot,
};

struct RenderLight
{
    LightKind kind = LightKind::Point;
    // Camera-relative.
    Vec3 position;
    // Spot only; the direction the cone points, unit length.
    Vec3 direction{0.0f, -1.0f, 0.0f};
    Color3 color{1.0f, 1.0f, 1.0f};
    f32 brightness = 1.0f;
    f32 range = 16.0f;
    // Cosine of the HALF angle, precomputed because a shader compares against a
    // dot product and would otherwise take a cosine per fragment per light.
    //
    // **-1 for a point light, not 1.** This comment said 1 and called it "the
    // value that makes the cone test pass everywhere", which is exactly
    // backwards: cos(halfAngle) == 1 is the NARROWEST cone expressible, and -1
    // is the one that admits every direction. The renderer already wrote -1;
    // only the contract was wrong, which is the worse way round -- a shader
    // author reading it would have implemented the opposite.
    f32 spotCosHalfAngle = -1.0f;
    bool shadows = false;
};

// `Lighting`'s state, resolved. The sun is here rather than in the light list
// because there is exactly one of it and it is the only shadow caster in v1.
struct RenderEnvironment
{
    // Points from the world towards the sun, so shading dots it against a
    // normal without negating.
    Vec3 sunDirection{0.0f, 1.0f, 0.0f};
    Color3 ambient{0.15f, 0.16f, 0.2f};
    f32 sunBrightness = 2.0f;
    Color3 fogColor{0.6f, 0.7f, 0.85f};
    f32 fogStart = 200.0f;
    // At or below `fogStart` means no fog, which is how it is switched off.
    f32 fogEnd = 0.0f;
    // EV stops on top of the automatic exposure (M7.5). Zero means "whatever the
    // frame measured", positive is brighter, and the unit is the one a person
    // who has used a camera already knows. `Lighting.ExposureCompensation`.
    f32 exposureCompensation = 0.0f;
};

// A material, resolved into what the GPU binds.
//
// Copied INTO the snapshot rather than pointed at, which is ADR 0027's rule
// working: the renderer must not be able to follow a pointer into a library
// that something reloaded between extraction and submission. Sixty-four bytes
// and four handles per distinct material in the frame is a cheap price for that.
struct RenderMaterial
{
    GpuMaterialUniforms uniforms;
    rhi::TextureHandle baseColor{};
    rhi::TextureHandle normal{};
    rhi::TextureHandle metallicRoughness{};
    rhi::TextureHandle emissive{};

    // The four maps, and the four flags that say they are there.
    //
    // **One verb, because they are one fact stated twice** (D100). The shader
    // never branches on whether a map is bound -- every slot always has a
    // texture, a 1x1 stand-in when the material has none -- so `textureFlags`
    // is what decides whether the sample is used at all: it multiplies the
    // difference between "the map" and "the factor". A handle set without its
    // flag is therefore a texture that is bound, sampled, and then multiplied
    // by zero, which draws EXACTLY like a material whose map was never set.
    //
    // That is not hypothetical. `materialOf` assigned the four handles and left
    // the flags at their zero default, so every `Material` instance in this
    // engine ignored every map it was given -- reported as a preview sphere
    // that would not take a diffuse. The glTF path a few lines away had always
    // written both, which is why meshes from files looked right and made the
    // difference impossible to see from the symptom.
    void setMaps(rhi::TextureHandle base, rhi::TextureHandle normalMap, rhi::TextureHandle metallicRoughnessMap,
                 rhi::TextureHandle emissiveMap) noexcept
    {
        baseColor = base;
        normal = normalMap;
        metallicRoughness = metallicRoughnessMap;
        emissive = emissiveMap;
        uniforms.textureFlags[0] = baseColor.valid() ? 1.0f : 0.0f;
        uniforms.textureFlags[1] = normal.valid() ? 1.0f : 0.0f;
        uniforms.textureFlags[2] = metallicRoughness.valid() ? 1.0f : 0.0f;
        uniforms.textureFlags[3] = emissive.valid() ? 1.0f : 0.0f;
    }
};

// One draw: a mesh section with a transform and a material.
//
// `sortKey` is computed here and never in a backend. That is the roadmap's third
// design constraint: `RenderWorld` is a POD snapshot, so grouping by pipeline
// and material at extraction is inherited by every backend, while doing it
// inside `rhi_sdlgpu` would be work bgfx has to repeat.
struct DrawItem
{
    // Pass, then pipeline, then material, then quantized depth -- most
    // significant first, so one integer compare orders a frame.
    u64 sortKey = 0;
    Mat4 transform;
    MeshHandle mesh;
    // Index into the mesh's sections.
    u32 section = 0;
    // Index into `RenderWorld::materials`, deduplicated across the frame so the
    // sort key groups draws that share a bind set.
    u32 material = 0;
    // `1 - BasePart.Transparency` times the material's own base-colour alpha.
    // The product, because both are real sources of see-through and honouring
    // one leaves the other rendering wrong.
    f32 alpha = 1.0f;
    // Whether this draw belongs to the blended pass. Derived from `alpha` and
    // stored rather than recomputed, because the sort key was built from it and
    // a submission that re-derived the answer could disagree with the order it
    // is walking.
    bool transparent = false;
    // The draw's world bounds as a sphere, in the snapshot's camera-relative
    // space. A sphere rather than the box it came from, because every consumer
    // is a distance test: the shadow pass rejects a caster against a cascade's
    // own sphere, which is what keeps four cascades from costing four times the
    // submission. Conservative in the direction that never drops geometry.
    Vec3 boundsCenter;
    f32 boundsRadius = 0.0f;
    // Whether the camera can see it. False items are still in the list because
    // **a caster outside the view still casts into it**: dropping them from the
    // snapshot removed the shadows of everything behind the camera, which is a
    // correct-looking image with the wrong shadows in it. The shadow pass draws
    // every item; the forward pass draws only these.
    bool inCameraFrustum = true;
    // Where this draw's joint palette starts in `RenderWorld::bones`, and how
    // many matrices it has. Zero count is the common case and means "not
    // skinned": the draw goes through the static pipeline and binds one vertex
    // buffer, exactly as it did before skinning existed.
    u32 firstBone = 0;
    u32 boneCount = 0;
    // Whether the tool that is looking at this world has this draw SELECTED.
    //
    // The one thing in this struct that is not a property of the world, and it
    // is here rather than as a second list for the reason the sort key is here:
    // the outline pass walks the same draws in the same order as every other
    // pass, and a parallel list of instance ids would have to be searched per
    // draw by a renderer that deliberately does not know what an instance is.
    // False on every frame a game renders, so a packaged build's draw list is
    // the one it always was.
    bool outlined = false;
};

struct RenderWorld
{
    RenderCamera camera;
    RenderEnvironment environment;
    std::vector<RenderPart> parts;
    std::vector<RenderLight> lights;
    std::vector<RenderMaterial> materials;
    std::vector<DrawItem> draws;
    // Every skinned draw's palette, concatenated. One vector rather than one per
    // draw because it is uploaded per draw anyway and a vector of vectors would
    // be a heap allocation per character per frame.
    std::vector<Mat4> bones;

    // Counters the perf table records beside frame time, because the roadmap
    // asks for the *why* next to the *what*. `culled` is the interesting one: a
    // frame where it is zero is a frame the culler did not help.
    u32 candidateDraws = 0;
    // Not in the camera's frustum. They are still drawn into the shadow map, so
    // this is "how many the forward pass skipped" rather than "how many were
    // discarded".
    u32 culledDraws = 0;

    void clear() noexcept
    {
        camera = RenderCamera{};
        environment = RenderEnvironment{};
        parts.clear();
        lights.clear();
        materials.clear();
        draws.clear();
        bones.clear();
        candidateDraws = 0;
        culledDraws = 0;
    }
};

// Builds `sortKey`. Exposed because it is the ordering contract and a test
// asserts on it directly rather than on a sorted list, which would only prove
// that *something* was consistent.
//
// `depth` is the distance from the camera in metres; it is quantized to 16 bits
// so that a sub-millimetre wobble in a camera position cannot reorder two draws
// and change a golden command stream.
// `geometry` is what makes an instanced run CONTIGUOUS, and it is why this
// gained a parameter at M7.5. Build it with `drawGeometryKey`: it is the mesh
// AND the section, because a mesh with two sections and one material used to
// interleave its two halves by depth -- which chopped every run into pieces of
// one and made the instanced path draw nothing at all. That was measured rather
// than reasoned: the horde scene reported 15,390 draws for 4,002 visible
// objects, and the two sections of its enemy were why.
//
// **It is zero for a transparent draw**, deliberately. Grouping by geometry
// above depth would destroy the back-to-front order the blended pass IS, so the
// transparent pass sorts exactly as it did and is never instanced.
[[nodiscard]] u64 drawSortKey(u32 pass, u32 pipeline, u32 material, u32 geometry, f32 depth) noexcept;

// Mesh and section packed into the sixteen bits `drawSortKey` has for them:
// twelve of mesh and four of section. Four thousand distinct meshes in one frame
// and sixteen sections in one mesh; past either, two draws share a key and their
// runs are merely shorter, which costs performance and never correctness.
[[nodiscard]] constexpr u32 drawGeometryKey(u32 meshIndex, u32 section) noexcept
{
    return ((meshIndex & 0xFFFu) << 4) | (section & 0xFu);
}

// The two passes a draw can belong to, and the values `drawSortKey`'s `pass`
// argument takes. Opaque first because a `u64` compare orders the frame and the
// opaque pass must fill depth before anything blends against it.
inline constexpr u32 kOpaquePass = 0;
inline constexpr u32 kTransparentPass = 1;

// The values `drawSortKey`'s `pipeline` argument takes. A skinned draw binds a
// second vertex buffer and a 4 KB uniform block, so grouping them is worth a
// field that was already there and unused.
inline constexpr u32 kStaticPipeline = 0;
inline constexpr u32 kSkinnedPipeline = 1;

// The largest depth `drawSortKey` can distinguish, in metres. Exposed because
// the transparent pass sorts back-to-front and does it by subtracting from this
// -- an inversion at extraction rather than a reversed walk at submission, so
// "walk the list in order" stays true in every backend.
inline constexpr f32 kMaxSortDepth = 655.0f;

// The reserved content URN a generated primitive is registered under, for one of
// `Enum.PartShape`'s values. A scheme of its own rather than `asset://`, which
// is the project's: nothing a game ships can collide with these, and a URN in a
// log says immediately that the geometry came from arithmetic.
//
// Null for a value outside the enum, which is what makes the caller fall back to
// the debug wire box rather than to a lookup of an empty string.
[[nodiscard]] const char* primitiveContent(core::i32 shape) noexcept;

// The mesh a `MeshPart` renders, and where the renderer keeps that mapping.
//
// `extract` needs to turn a `MeshPart`'s content URN into geometry, and it must
// not do that by loading anything: extraction runs inside a frame and a file
// read is not a frame's work. So resolution is a lookup, and whatever populates
// this does so at the FrameStart safe point like every other mutation.
// The textures a `Material` instance's maps name, by content URN.
//
// **A texture library and not a material one**, which is what it was for one
// release. A material is an instance now: its numbers live in a component and
// its block is built from them each frame, so the only thing left to cache is
// the expensive half -- the decoded, uploaded image behind a URN. Two materials
// naming one image share one upload, which is the whole point of keying on the
// name.
class TextureLibrary
{
public:
    void set(core::NameAtom content, rhi::TextureHandle texture);
    void clear() noexcept;

    // An invalid handle for a URN nothing has loaded, which is the ordinary
    // state of a map whose file has not been read yet. A surface in that state
    // draws untextured rather than not at all: one that vanished while its
    // texture loaded would be worse.
    [[nodiscard]] rhi::TextureHandle find(core::NameAtom content) const noexcept;
    [[nodiscard]] usize size() const noexcept { return entries_.size(); }

private:
    // Sorted by atom, like `MeshLibrary`, and for the same reason: R10 forbids
    // an unordered container's iteration reaching observable output.
    struct Slot
    {
        core::NameAtom content;
        rhi::TextureHandle texture;
    };
    std::vector<Slot> entries_;
};

class MeshLibrary
{
public:
    struct Entry
    {
        MeshHandle mesh;
        AABB bounds;
        u32 sectionCount = 0;
        // `sectionMaterial[i]` indexes `materials`. Two vectors rather than one
        // material per section, because a file whose four primitives share one
        // material should upload one material.
        std::vector<u32> sectionMaterial;
        std::vector<RenderMaterial> materials;

        // The mesh's vertex POSITIONS, for whoever needs a collision hull
        // (`MeshPart.CollisionFidelity`). Empty for a primitive and for a mesh
        // whose file failed.
        //
        // Kept here rather than decoded a second time by the physics mirror: the
        // decode already happened, the positions are twelve bytes a vertex, and
        // a second decode of a fifty-thousand-vertex mesh to answer a question
        // the first one already answered is the kind of cost nobody notices
        // until a world has a hundred of them.
        std::vector<Vec3> positions;
    };

    void set(core::NameAtom content, const Entry& entry);
    void remove(core::NameAtom content);
    void clear() noexcept;

    // Null for a URN nothing has loaded, which is the ordinary state of a
    // `MeshPart` whose file has not been read yet. `extract` skips it rather
    // than substituting a placeholder: a missing mesh that draws a cube is a
    // missing mesh nobody notices.
    [[nodiscard]] const Entry* find(core::NameAtom content) const noexcept;
    [[nodiscard]] usize size() const noexcept { return entries_.size(); }

    // Every entry, in atom order. For a caller that has to mirror this into
    // something else -- the physics mirror's collision points -- and that
    // therefore needs the whole set rather than one lookup.
    template <typename Fn>
    void forEach(Fn&& fn) const
    {
        for (const auto& entry : entries_)
            fn(entry.content, entry.entry);
    }

private:
    // A flat vector, kept sorted by atom, rather than a hash map: it holds one
    // entry per distinct mesh in the world, it is read once per MeshPart per
    // frame, and R10 forbids an unordered container's iteration reaching
    // observable output -- which a debug listing of loaded meshes would.
    struct Slot
    {
        core::NameAtom content;
        Entry entry;
    };
    std::vector<Slot> entries_;
};

// A view the CALLER supplies, instead of the one the world names.
//
// **An editor's viewport is not the game's view**, in this engine for the same
// reason it is not in Unity or Unreal: a scene view that borrowed the game's
// camera would have to take it away from the game to be usable, and handing it
// back is a negotiation nobody wins. Sharing one camera between a tool and the
// thing it edits produces exactly one symptom -- two authors writing one
// transform on alternate frames -- and no arbitration fixes it, because the
// disagreement is the design.
//
// So the editor owns a camera the world does not contain, and the renderer is
// TOLD which view to draw rather than asked to find one. Null -- the default,
// and what every game, every golden and every headless run passes -- means
// `Workspace.CurrentCamera`, unchanged.
struct ViewOverride
{
    core::CFrameD cframe;
    // Degrees, vertical, matching `Camera.FieldOfView`.
    f32 fieldOfView = 70.0f;
    f32 nearPlane = 0.1f;
    f32 farPlane = 5000.0f;
};

// Fills `out` from the world.
//
// `root` is `Workspace`: whatever is parented under it is in the world and
// whatever is not, is not (api-design.md §2.1). Passed in rather than looked up,
// because `render` has no business knowing what a service is. `lightingHost` is
// the `Lighting` service instance, for the same reason.
//
// Order is a pure function of the operation sequence: parts and lights come out
// in the pools' dense order, and draws are sorted by `sortKey` with the
// extraction index as a stable tie-break. Two runs of the same world produce the
// same command stream, which is what makes a capture golden a gate rather than a
// coin flip (R10).
void extract(const scene::World& world, core::InstanceId root, core::InstanceId lightingHost, const MeshLibrary& meshes,
             f32 viewportAspect,
             // Metres. Geometry further than this from the camera is dropped entirely --
             // it can neither be seen nor cast into view, because the shadow map only
             // covers a bounded region around the camera. The renderer owns the number
             // and passes it, rather than `extract` guessing at a constant that lives in
             // the pass list.
             f32 shadowRadius,
             // The poses skinned draws are in, or null in a build that does not
             // animate -- a capture harness, a screenshot tool. Null means every
             // skinned mesh comes out in bind pose, which is what an unanimated
             // one should look like.
             const AnimationSystem* animation,
             // Where this frame sits between the last tick and the next, and
             // where everything was at the tick before it (`transform_history.h`,
             // D047). Zero and null draw the world exactly as the last tick left
             // it, which is what every headless run does -- a golden has to be
             // the tick, not a point between two of them.
             //
             // **The camera is interpolated with everything else**, because what
             // has to be consistent is the TIME the frame is drawn at: a world
             // evaluated at `t + alpha` seen from a camera at `t` slides forward
             // and snaps back once a tick, which is the artifact this exists to
             // remove rather than a smaller version of it.
             f32 alpha, const TransformHistory* history, RenderWorld& out,
             // The view to draw from, or null for the world's own camera. See
             // `ViewOverride`.
             const ViewOverride* view = nullptr,
             // The instances a TOOL has selected, if any. Every draw belonging
             // to one of them comes out with `outlined` set, and the renderer
             // draws a silhouette around the union of them.
             //
             // A span rather than a set, and searched linearly, because an
             // editor selection is a handful of instances and the alternative
             // is a hash lookup per draw in a list that can be tens of
             // thousands long. Empty -- which is every frame a game renders --
             // costs one compare per draw.
             std::span<const core::InstanceId> outlined = {},
             // The textures the scene's materials name, or null in a build that
             // does not load them -- a capture harness, a test. Null draws every
             // material's numbers with no maps, which is a surface that has not
             // finished loading rather than one that is wrong.
             //
             // **A material REPLACES rather than merges.** The instance answers
             // with the whole block, for every section of the part; a merge would
             // need per-field "is set" bits on a struct whose virtue is being
             // flat, and "which half of this material is mine" is not a question
             // anybody wants to answer while looking at a wrong-coloured wall.
             const TextureLibrary* textures = nullptr);

} // namespace luaug::render
