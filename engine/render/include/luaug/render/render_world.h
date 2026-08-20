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

#include <vector>

#include "luaug/core/id.h"
#include "luaug/core/name_atom.h"
#include "luaug/core/math.h"
#include "luaug/core/types.h"
#include "luaug/render/mesh_cache.h"
#include "luaug/render/shader_types.h"
#include "luaug/rhi/types.h"

namespace luaug::scene
{
class World;
}

namespace luaug::render
{

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
    // Whether the camera can see it. False items are still in the list because
    // **a caster outside the view still casts into it**: dropping them from the
    // snapshot removed the shadows of everything behind the camera, which is a
    // correct-looking image with the wrong shadows in it. The shadow pass draws
    // every item; the forward pass draws only these.
    bool inCameraFrustum = true;
};

struct RenderWorld
{
    RenderCamera camera;
    RenderEnvironment environment;
    std::vector<RenderPart> parts;
    std::vector<RenderLight> lights;
    std::vector<RenderMaterial> materials;
    std::vector<DrawItem> draws;

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
[[nodiscard]] u64 drawSortKey(u32 pass, u32 pipeline, u32 material, f32 depth) noexcept;

// The two passes a draw can belong to, and the values `drawSortKey`'s `pass`
// argument takes. Opaque first because a `u64` compare orders the frame and the
// opaque pass must fill depth before anything blends against it.
inline constexpr u32 kOpaquePass = 0;
inline constexpr u32 kTransparentPass = 1;

// The largest depth `drawSortKey` can distinguish, in metres. Exposed because
// the transparent pass sorts back-to-front and does it by subtracting from this
// -- an inversion at extraction rather than a reversed walk at submission, so
// "walk the list in order" stays true in every backend.
inline constexpr f32 kMaxSortDepth = 655.0f;

// The mesh a `MeshPart` renders, and where the renderer keeps that mapping.
//
// `extract` needs to turn a `MeshPart`'s content URN into geometry, and it must
// not do that by loading anything: extraction runs inside a frame and a file
// read is not a frame's work. So resolution is a lookup, and whatever populates
// this does so at the FrameStart safe point like every other mutation.
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
void extract(
    const scene::World& world,
    core::InstanceId root,
    core::InstanceId lightingHost,
    const MeshLibrary& meshes,
    f32 viewportAspect,
    // Metres. Geometry further than this from the camera is dropped entirely --
    // it can neither be seen nor cast into view, because the shadow map only
    // covers a bounded region around the camera. The renderer owns the number
    // and passes it, rather than `extract` guessing at a constant that lives in
    // the pass list.
    f32 shadowRadius,
    RenderWorld& out);

} // namespace luaug::render
