#include "luaug/render/mesh_cache.h"

#include "luaug/core/i18n.h"
#include "luaug/core/text_key.h"

#include <array>
#include <cstring>
#include <utility>

namespace luaug::render {
namespace {

using core::i32;

constexpr usize kVertexSize = sizeof(asset::Vertex);
constexpr usize kIndexSize = sizeof(u32);

[[nodiscard]] std::span<const std::byte> asBytes(const asset::Mesh& mesh) noexcept
{
    return std::span<const std::byte>(reinterpret_cast<const std::byte*>(mesh.vertices.data()),
                                      mesh.vertices.size() * kVertexSize);
}

[[nodiscard]] std::span<const std::byte> indexBytes(const asset::Mesh& mesh) noexcept
{
    return std::span<const std::byte>(reinterpret_cast<const std::byte*>(mesh.indices.data()),
                                      mesh.indices.size() * kIndexSize);
}

// Doubling from the high-water mark rather than fitting exactly: a ring that
// grows to precisely what one frame asked for grows again on the next frame that
// asks for one more vertex, and each growth is a device allocation.
[[nodiscard]] u32 grownTo(u32 current, u32 needed) noexcept
{
    u32 next = current == 0 ? 1u : current;
    while (next < needed) {
        // Saturate rather than wrap. A request this large is a bug upstream, and
        // the allocation below will fail with a keyed error instead of silently
        // producing a tiny buffer.
        if (next > 0x7FFFFFFFu)
            return needed;
        next *= 2u;
    }
    return next;
}

} // namespace

// The span in `Entry::resolved` points into `Entry::sections`'s heap buffer, so
// it survives `entries_` reallocating only because growth MOVES an Entry and a
// moved vector hands over its buffer. Copying one would leave the span pointing
// at the original's storage.
static_assert(std::is_nothrow_move_constructible_v<std::vector<MeshSection>>,
              "MeshCache::Entry must be moved, never copied, or Resolved::sections dangles");

MeshCache::~MeshCache()
{
    // Nothing to release here: GPU handles need the device, and holding a
    // reference to it for the lifetime of the cache would make destruction order
    // a trap. `destroy(device)` is the contract, and a cache destroyed without
    // it leaks -- loudly, in the backend's own leak report, rather than by
    // crashing during shutdown.
}

std::optional<core::EngineError> MeshCache::create(rhi::IDevice& device, u32 ringVertexCapacity, u32 ringIndexCapacity)
{
    if (auto error = growRing(device, ringVertexCapacity, ringIndexCapacity); error.has_value())
        return error;
    return std::nullopt;
}

std::optional<core::EngineError> MeshCache::growRing(rhi::IDevice& device, u32 vertices, u32 indices)
{
    const u32 nextVertices = grownTo(ringVertexCapacity_, vertices);
    const u32 nextIndices = grownTo(ringIndexCapacity_, indices);

    const rhi::BufferHandle newVertices = device.createBuffer({
        .usage = rhi::BufferUsage::Vertex,
        .sizeBytes = static_cast<u32>(nextVertices * kVertexSize),
        .debugName = "mesh-ring-vertices",
    });
    if (!newVertices.valid())
        return core::makeError(LUAUG_TR("render.err.mesh_buffer_failed"), {}, "dynamic vertex ring");

    const rhi::BufferHandle newIndices = device.createBuffer({
        .usage = rhi::BufferUsage::Index,
        .sizeBytes = static_cast<u32>(nextIndices * kIndexSize),
        .debugName = "mesh-ring-indices",
    });
    if (!newIndices.valid()) {
        device.destroy(newVertices);
        return core::makeError(LUAUG_TR("render.err.mesh_buffer_failed"), {}, "dynamic index ring");
    }

    // The old buffers are retired rather than destroyed, because dynamic handles
    // already issued THIS frame still name them and are still legal to draw.
    // They die at the next `beginFrame`, which is the moment those handles stop
    // resolving anyway.
    if (ringVertices_.valid())
        retiring_.push_back({ringVertices_, ringIndices_});

    ringVertices_ = newVertices;
    ringIndices_ = newIndices;
    ringVertexCapacity_ = nextVertices;
    ringIndexCapacity_ = nextIndices;
    ringVertexUsed_ = 0;
    ringIndexUsed_ = 0;
    return std::nullopt;
}

void MeshCache::destroy(rhi::IDevice& device)
{
    for (Entry& entry : entries_) {
        if (entry.live && !entry.dynamic) {
            device.destroy(entry.resolved.vertices);
            device.destroy(entry.resolved.indices);
            if (entry.resolved.skin.valid())
                device.destroy(entry.resolved.skin);
        }
        entry.live = false;
    }
    entries_.clear();
    freeSlots_.clear();

    for (const RetiredRing& ring : retiring_) {
        device.destroy(ring.vertices);
        device.destroy(ring.indices);
    }
    retiring_.clear();
    for (const RetiredRing& ring : retired_) {
        device.destroy(ring.vertices);
        device.destroy(ring.indices);
    }
    retired_.clear();

    if (ringVertices_.valid())
        device.destroy(ringVertices_);
    if (ringIndices_.valid())
        device.destroy(ringIndices_);
    ringVertices_ = {};
    ringIndices_ = {};
    ringVertexCapacity_ = 0;
    ringIndexCapacity_ = 0;
    ringVertexUsed_ = 0;
    ringIndexUsed_ = 0;
}

void MeshCache::beginFrame(rhi::IDevice& device)
{
    // A dynamic entry is not erased -- its slot is recycled, and the frame stamp
    // is what stops its handle resolving. Erasing would let a slot be reused by
    // a static mesh with the same generation, which is the one way a stale
    // dynamic handle could come back as somebody else's geometry.
    for (usize index = 0; index < entries_.size(); ++index) {
        Entry& entry = entries_[index];
        if (entry.live && entry.dynamic) {
            entry.live = false;
            entry.sections.clear();
            entry.resolved = Resolved{};
            freeSlots_.push_back(static_cast<u32>(index));
        }
    }

    ringVertexUsed_ = 0;
    ringIndexUsed_ = 0;

    // Two frames of slack, not one. A handle issued before a mid-frame grow is
    // legal to draw for the rest of that frame, and the GPU may still be
    // executing those commands when the next frame begins.
    for (const RetiredRing& ring : retired_) {
        device.destroy(ring.vertices);
        device.destroy(ring.indices);
    }
    retired_.swap(retiring_);
    retiring_.clear();
}

MeshHandle MeshCache::createSkinned(rhi::IDevice& device, rhi::ICmdList& cmd, const asset::Mesh& mesh,
                                    std::span<const asset::SkinVertex> skin, core::EngineError* outError)
{
    if (skin.size() != mesh.vertices.size()) {
        if (outError != nullptr)
            *outError = core::makeError(LUAUG_TR("render.err.mesh_buffer_failed"), {}, "skin stream length");
        return {};
    }

    const MeshHandle handle = create(device, cmd, mesh, MeshUsage::Static, outError);
    if (!handle.valid() || skin.empty())
        return handle;

    Entry& entry = entries_[handle.index];
    const auto sizeBytes = static_cast<u32>(skin.size() * sizeof(asset::SkinVertex));
    entry.resolved.skin = device.createBuffer({
        .usage = rhi::BufferUsage::Vertex,
        .sizeBytes = sizeBytes,
        .debugName = "mesh-skin",
    });
    if (!entry.resolved.skin.valid()) {
        // The geometry is already up and drawable; what is lost is the skinning.
        // Releasing the whole mesh over it would turn a character that stands
        // still into a character that is not there, which is the worse failure.
        if (outError != nullptr)
            *outError = core::makeError(LUAUG_TR("render.err.mesh_buffer_failed"), {}, "skin stream");
        return handle;
    }

    cmd.upload(entry.resolved.skin, std::as_bytes(skin), 0);
    return handle;
}

MeshHandle MeshCache::create(rhi::IDevice& device, rhi::ICmdList& cmd, const asset::Mesh& mesh, MeshUsage usage,
                             core::EngineError* outError)
{
    const auto vertexCount = static_cast<u32>(mesh.vertices.size());
    const auto indexCount = static_cast<u32>(mesh.indices.size());

    Resolved resolved;
    resolved.bounds = mesh.bounds;

    if (usage == MeshUsage::Static) {
        // An empty mesh still gets a handle: a generator that produced nothing
        // this frame is not an error, and the alternative is every caller
        // branching on emptiness before it can draw.
        if (vertexCount > 0) {
            resolved.vertices = device.createBuffer({
                .usage = rhi::BufferUsage::Vertex,
                .sizeBytes = static_cast<u32>(vertexCount * kVertexSize),
                .debugName = "mesh-vertices",
            });
            resolved.indices = device.createBuffer({
                .usage = rhi::BufferUsage::Index,
                .sizeBytes = static_cast<u32>(indexCount * kIndexSize),
                .debugName = "mesh-indices",
            });
            if (!resolved.vertices.valid() || !resolved.indices.valid()) {
                if (resolved.vertices.valid())
                    device.destroy(resolved.vertices);
                if (resolved.indices.valid())
                    device.destroy(resolved.indices);
                if (outError != nullptr)
                    *outError = core::makeError(LUAUG_TR("render.err.mesh_buffer_failed"), {}, "static mesh");
                return {};
            }

            cmd.upload(resolved.vertices, asBytes(mesh), 0);
            if (indexCount > 0)
                cmd.upload(resolved.indices, indexBytes(mesh), 0);
        }
    }
    else {
        if (ringVertexUsed_ + vertexCount > ringVertexCapacity_ || ringIndexUsed_ + indexCount > ringIndexCapacity_) {
            if (auto error = growRing(device, ringVertexUsed_ + vertexCount, ringIndexUsed_ + indexCount);
                error.has_value()) {
                if (outError != nullptr)
                    *outError = *error;
                return {};
            }
        }

        resolved.vertices = ringVertices_;
        resolved.indices = ringIndices_;
        resolved.firstIndex = ringIndexUsed_;
        resolved.vertexOffset = static_cast<i32>(ringVertexUsed_);

        if (vertexCount > 0)
            cmd.upload(resolved.vertices, asBytes(mesh), static_cast<u32>(ringVertexUsed_ * kVertexSize));
        if (indexCount > 0)
            cmd.upload(resolved.indices, indexBytes(mesh), static_cast<u32>(ringIndexUsed_ * kIndexSize));

        ringVertexUsed_ += vertexCount;
        ringIndexUsed_ += indexCount;
        ringVertexHighWater_ = ringVertexUsed_ > ringVertexHighWater_ ? ringVertexUsed_ : ringVertexHighWater_;
        ringIndexHighWater_ = ringIndexUsed_ > ringIndexHighWater_ ? ringIndexUsed_ : ringIndexHighWater_;
    }

    u32 slot = 0;
    if (!freeSlots_.empty()) {
        slot = freeSlots_.back();
        freeSlots_.pop_back();
    }
    else {
        slot = static_cast<u32>(entries_.size());
        entries_.emplace_back();
    }

    Entry& entry = entries_[slot];
    // Generation starts at 1 and only ever rises, so a default-constructed
    // `MeshHandle` (generation 0) can never name a live entry.
    ++entry.generation;
    entry.dynamic = usage == MeshUsage::Dynamic;
    entry.live = true;

    entry.sections.clear();
    entry.sections.reserve(mesh.submeshes.size());
    for (const asset::Submesh& submesh : mesh.submeshes) {
        entry.sections.push_back(MeshSection{
            .firstIndex = submesh.firstIndex,
            .indexCount = submesh.indexCount,
            .material = submesh.material,
            .bounds = submesh.bounds,
        });
    }

    entry.resolved = resolved;
    entry.resolved.sections = entry.sections;
    return MeshHandle{slot, entry.generation};
}

void MeshCache::release(rhi::IDevice& device, MeshHandle handle)
{
    if (!handle.valid() || handle.index >= entries_.size())
        return;

    Entry& entry = entries_[handle.index];
    if (!entry.live || entry.generation != handle.generation || entry.dynamic)
        return;

    if (entry.resolved.vertices.valid())
        device.destroy(entry.resolved.vertices);
    if (entry.resolved.indices.valid())
        device.destroy(entry.resolved.indices);
    if (entry.resolved.skin.valid())
        device.destroy(entry.resolved.skin);

    entry.live = false;
    entry.sections.clear();
    entry.resolved = Resolved{};
    freeSlots_.push_back(handle.index);
}

const MeshCache::Resolved* MeshCache::resolve(MeshHandle handle) const noexcept
{
    if (!handle.valid() || handle.index >= entries_.size())
        return nullptr;

    const Entry& entry = entries_[handle.index];
    if (!entry.live || entry.generation != handle.generation)
        return nullptr;
    return &entry.resolved;
}

usize MeshCache::staticMeshCount() const noexcept
{
    usize count = 0;
    for (const Entry& entry : entries_) {
        if (entry.live && !entry.dynamic)
            ++count;
    }
    return count;
}

} // namespace luaug::render
