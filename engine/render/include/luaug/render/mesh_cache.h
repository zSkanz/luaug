// Geometry on the GPU (roadmap M4, "Design constraints (not scope)").
//
// The roadmap requires two seams stay open here, and both are shapes rather
// than features: engine-generated geometry must be able to reach the renderer,
// and geometry that changes every frame must have an upload path that does not
// allocate every frame. Neither is built *for* a future caller -- both cost a
// parameter now and a refactor later, which is the same argument ADR 0014 made
// for `CFrame` carrying f64 from its first commit.
//
// So this takes **data, never a path**. `create` is handed vertices and indices
// that a caller already holds; where they came from -- a glTF file, a procedural
// generator, a voxel mesher -- is not this module's business and cannot become
// its business. A `RenderWorld` names geometry by `MeshHandle`, so nothing
// downstream can resolve an asset, and nothing downstream needs to.
#pragma once

#include "luaug/asset/model.h"
#include "luaug/core/error.h"
#include "luaug/core/math.h"
#include "luaug/rhi/device.h"

#include <optional>
#include <span>
#include <vector>

namespace luaug::render {

using core::AABB;
using core::u32;
using core::usize;

// Opaque to everything above: an index plus a generation, so a handle to a mesh
// that has been released cannot be mistaken for a handle to whatever took its
// slot. The same reasoning as `core::InstanceId`, for the same failure.
struct MeshHandle
{
    u32 index = 0;
    u32 generation = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return generation != 0; }
    [[nodiscard]] constexpr bool operator==(const MeshHandle&) const noexcept = default;
};

// How long the geometry has to live, which decides where it is stored.
enum class MeshUsage : core::u8
{
    // An immutable device buffer, uploaded once. What an imported mesh is.
    Static,
    // A slice of a per-frame ring, valid until the frame it was written in ends.
    // What geometry rebuilt every frame is -- debug draw today, procedural and
    // voxel meshing later.
    Dynamic,
};

// One drawable range, mirroring `asset::Submesh` but in GPU terms. Kept
// separately rather than by pointing back at the `asset::Mesh`, because the CPU
// copy is free to be dropped the moment the upload is done.
struct MeshSection
{
    u32 firstIndex = 0;
    u32 indexCount = 0;
    u32 material = 0;
    AABB bounds;
};

// Owns every vertex and index buffer the renderer draws from.
//
// Not a general resource manager and deliberately not reference counted: a mesh
// is released when its owner says so, and there is exactly one owner. Reference
// counting arrives with `asset`'s streaming policy at M7, above this, not here.
class MeshCache
{
public:
    MeshCache() = default;
    ~MeshCache();

    MeshCache(const MeshCache&) = delete;
    MeshCache& operator=(const MeshCache&) = delete;

    // `ringVertexCapacity` and `ringIndexCapacity` size the dynamic ring's
    // first allocation. It grows when a frame asks for more than it holds and
    // is never shrunk, because a frame's dynamic geometry swings wildly and
    // reallocating to match costs far more than the high-water mark does.
    [[nodiscard]] std::optional<core::EngineError> create(rhi::IDevice& device, u32 ringVertexCapacity = 4096,
                                                          u32 ringIndexCapacity = 8192);

    void destroy(rhi::IDevice& device);

    // Uploads a mesh and returns a handle to it.
    //
    // Takes `asset::Mesh` by const reference and copies nothing back: the
    // caller may drop its CPU copy as soon as this returns. Sections are
    // derived from the mesh's submeshes, so a mesh with no submeshes at all
    // produces a handle that draws nothing rather than an error -- an empty
    // mesh is a legal thing for a generator to produce.
    //
    // `Dynamic` meshes must be created after `beginFrame` and are invalidated
    // by the next one: `beginFrame` retires every dynamic entry, so its handle
    // stops resolving, and a slot recycled into a new mesh carries a new
    // generation. Holding a dynamic handle across frames yields nothing rather
    // than geometry from another frame -- which is the mistake this enum exists
    // to make nameable.
    [[nodiscard]] MeshHandle create(rhi::IDevice& device, rhi::ICmdList& cmd, const asset::Mesh& mesh, MeshUsage usage,
                                    core::EngineError* outError = nullptr);

    // The skinned form: the same mesh plus its parallel joint/weight stream,
    // uploaded to a SECOND buffer. A separate overload rather than a defaulted
    // parameter, because the two produce different draws and a caller should
    // have to say which it means.
    //
    // `skin` must have one entry per vertex; a mismatched length is refused
    // rather than clamped, since a skin stream half as long as the mesh is a
    // file the importer should have rejected. `Static` only: a skinned mesh is
    // an imported asset, and the ring is for geometry rebuilt every frame.
    [[nodiscard]] MeshHandle createSkinned(rhi::IDevice& device, rhi::ICmdList& cmd, const asset::Mesh& mesh,
                                           std::span<const asset::SkinVertex> skin,
                                           core::EngineError* outError = nullptr);

    // Releases a static mesh's buffers. A dynamic handle is not released here;
    // the ring reclaims it at the next `beginFrame`.
    void release(rhi::IDevice& device, MeshHandle handle);

    // Retires every dynamic handle issued last frame and rewinds the ring.
    // Must be called once per frame, before any dynamic `create`.
    //
    // Takes the device because this is also where rings retired by a mid-frame
    // grow are finally destroyed, two frames after they stopped being written
    // to. Two rather than one: a handle issued before the grow is legal to draw
    // for the rest of that frame, and the GPU may still be executing that
    // frame's commands when the next one starts.
    void beginFrame(rhi::IDevice& device);

    struct Resolved
    {
        rhi::BufferHandle vertices{};
        rhi::BufferHandle indices{};
        // The joint/weight stream, bound at vertex slot 1 by the skinned
        // pipelines. Invalid for a static mesh, which is most of them -- and an
        // unskinned draw is byte-identical to M4's because of it.
        rhi::BufferHandle skin{};
        // Added to every section's `firstIndex`, and to the vertex offset of
        // every draw. Zero for a static mesh, which owns its buffers outright;
        // non-zero for a dynamic one, which is a slice of a shared ring.
        u32 firstIndex = 0;
        core::i32 vertexOffset = 0;
        std::span<const MeshSection> sections;
        AABB bounds;
    };

    // Null for a handle that was released, or for a dynamic handle from an
    // earlier frame. Callers draw only what resolves.
    [[nodiscard]] const Resolved* resolve(MeshHandle handle) const noexcept;

    [[nodiscard]] usize staticMeshCount() const noexcept;
    // The ring's high-water mark, in vertices and indices. The number worth
    // watching in a profile: if it climbs every frame, something is treating a
    // per-frame buffer as storage.
    [[nodiscard]] u32 ringVertexHighWater() const noexcept { return ringVertexHighWater_; }
    [[nodiscard]] u32 ringIndexHighWater() const noexcept { return ringIndexHighWater_; }

    // Rings replaced by a mid-frame grow and not yet destroyed. Worth watching
    // for the same reason as the high-water marks -- a number that does not
    // return to zero is GPU memory nobody is releasing -- and it is what makes
    // the deferred-destruction rule assertable at all, since a buffer handle
    // reveals nothing about whether it has been destroyed.
    [[nodiscard]] usize pendingRingReleases() const noexcept { return retiring_.size() + retired_.size(); }

private:
    struct Entry
    {
        Resolved resolved;
        std::vector<MeshSection> sections;
        u32 generation = 0;
        bool dynamic = false;
        bool live = false;
    };

    [[nodiscard]] std::optional<core::EngineError> growRing(rhi::IDevice& device, u32 vertices, u32 indices);

    std::vector<Entry> entries_;
    std::vector<u32> freeSlots_;

    // A ring replaced mid-frame, kept alive until nothing can be drawing from
    // it. `retiring_` collects this frame's; `retired_` holds last frame's and
    // is what `beginFrame` destroys.
    struct RetiredRing
    {
        rhi::BufferHandle vertices{};
        rhi::BufferHandle indices{};
    };

    std::vector<RetiredRing> retiring_;
    std::vector<RetiredRing> retired_;

    rhi::BufferHandle ringVertices_{};
    rhi::BufferHandle ringIndices_{};
    u32 ringVertexCapacity_ = 0;
    u32 ringIndexCapacity_ = 0;
    u32 ringVertexUsed_ = 0;
    u32 ringIndexUsed_ = 0;
    u32 ringVertexHighWater_ = 0;
    u32 ringIndexHighWater_ = 0;
};

} // namespace luaug::render
