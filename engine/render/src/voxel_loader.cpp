#include "luaug/render/voxel_loader.h"

#include "luaug/asset/voxel_mesher.h"
#include "luaug/core/log.h"
#include "luaug/jobs/jobs.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace luaug::render {
namespace {

using core::i32;
using core::u32;
using core::u64;
using core::usize;

// How many chunks one `sync` may mesh. A chunk is about a third of a
// millisecond, and they mesh in parallel, so this is a few milliseconds of one
// core at the worst -- and a world's first frame fills in over a handful of
// frames, nearest first, rather than stalling on all of it.
constexpr u32 ChunksPerSync = 32;

// Order-sensitive, which is what a key built from an ordered walk wants.
[[nodiscard]] u64 combine(u64 seed, u64 value) noexcept
{
    u64 z = seed ^ (value + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2));
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// What one chunk's mesh reads: itself and the 26 around it.
[[nodiscard]] u64 contentOf(const asset::VoxelGrid& grid, asset::VoxelChunkKey key, float blockSize) noexcept
{
    u64 content = combine(0, static_cast<u64>(std::bit_cast<u32>(blockSize)));
    for (i32 dy = -1; dy <= 1; ++dy) {
        for (i32 dz = -1; dz <= 1; ++dz) {
            for (i32 dx = -1; dx <= 1; ++dx) {
                const asset::VoxelChunk* chunk = grid.findChunk({key.x + dx, key.y + dy, key.z + dz});
                content = combine(content, chunk == nullptr ? 0x6E6F6E65ull : asset::digestOf(*chunk));
            }
        }
    }
    return content;
}

} // namespace

std::string voxelChunkUrn(asset::VoxelChunkKey key)
{
    return "voxel://" + std::to_string(key.x) + "," + std::to_string(key.y) + "," + std::to_string(key.z);
}

u32 VoxelLoader::sync(rhi::IDevice& device, rhi::ICmdList& cmd, const scene::World& world, core::AtomTable& atoms,
                      MeshCache& cache, MeshLibrary& library)
{
    m_lastRebuilds = 0;
    for (Resident& resident : m_resident)
        resident.seen = false;

    const scene::VoxelComponent* voxels = nullptr;
    world.voxels().forEach([&voxels](core::InstanceId, const scene::VoxelComponent& found) {
        if (voxels == nullptr)
            voxels = &found;
    });

    struct Want
    {
        double distance = 0.0;
        asset::VoxelChunkKey key;
        u64 content = 0;
        asset::VoxelMesh meshed;
    };
    std::vector<Want> wants;

    if (voxels != nullptr) {
        const float size = voxels->blockSize;
        const double chunkMetres = static_cast<double>(asset::VoxelChunkEdge) * static_cast<double>(size);
        for (const asset::VoxelChunkKey key : voxels->grid.chunkKeys()) {
            double distance = 0.0;
            if (m_hasFocus) {
                const auto axis = [&](i32 index, double focus) {
                    const double low = static_cast<double>(index) * chunkMetres;
                    return std::max({low - focus, 0.0, focus - (low + chunkMetres)});
                };
                const double dx = axis(key.x, m_focus.x);
                const double dy = axis(key.y, m_focus.y);
                const double dz = axis(key.z, m_focus.z);
                distance = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (distance > m_viewDistance)
                    continue; // not seen: released below
            }
            const auto at =
                std::lower_bound(m_resident.begin(), m_resident.end(), key,
                                 [](const Resident& entry, asset::VoxelChunkKey probe) { return entry.key < probe; });
            const bool exists = at != m_resident.end() && at->key == key;
            const u64 content = contentOf(voxels->grid, key, size);
            if (exists) {
                at->seen = true;
                if (at->content == content)
                    continue;
            }
            wants.push_back(Want{distance, key, content, {}});
        }
    }

    // Nearest first, then by key so equal distances cannot trade places.
    std::sort(wants.begin(), wants.end(), [](const Want& a, const Want& b) {
        if (a.distance != b.distance)
            return a.distance < b.distance;
        return a.key < b.key;
    });
    if (wants.size() > ChunksPerSync)
        wants.resize(ChunksPerSync);

    // Every chunk the mesher treats as solid; the registry decides colours, and
    // colour is the shader's business, not the mesher's.
    if (voxels != nullptr) {
        const float size = voxels->blockSize;
        jobs::parallelFor("voxel.mesh", jobs::Domain::Render, 0, wants.size(), 1,
                          [&wants, voxels, size](usize begin, usize end, u32) noexcept {
                              for (usize at = begin; at < end; ++at)
                                  wants[at].meshed = asset::meshVoxelChunk(voxels->grid, wants[at].key, {}, size);
                          });
    }

    for (Want& want : wants) {
        const core::NameAtom urn = atoms.intern(voxelChunkUrn(want.key));
        auto at = std::lower_bound(m_resident.begin(), m_resident.end(), want.key,
                                   [](const Resident& entry, asset::VoxelChunkKey probe) { return entry.key < probe; });
        const bool exists = at != m_resident.end() && at->key == want.key;

        MeshHandle handle;
        if (!want.meshed.mesh.indices.empty()) {
            core::EngineError uploadError;
            handle = cache.create(device, cmd, want.meshed.mesh, MeshUsage::Static, &uploadError);
            if (!handle.valid())
                core::logText(core::LogLevel::Warn, uploadError.message);
        }
        if (handle.valid()) {
            MeshLibrary::Entry entry;
            entry.mesh = handle;
            entry.bounds = want.meshed.mesh.bounds;
            entry.sectionCount = 1;
            entry.sectionMaterial.assign(1, 0u);
            entry.materials.push_back(RenderMaterial{});
            library.set(urn, std::move(entry));
        }
        else {
            library.remove(urn);
        }

        if (exists) {
            if (at->mesh.valid())
                cache.release(device, at->mesh);
            at->mesh = handle;
            at->content = want.content;
            at->seen = true;
        }
        else {
            m_resident.insert(at, Resident{want.key, urn, handle, want.content, voxels->blockSize, true});
        }
        m_lastRebuilds += 1;
    }

    // Chunks no longer in the world, or out of range, give their meshes back.
    for (usize at = m_resident.size(); at > 0; --at) {
        Resident& resident = m_resident[at - 1];
        if (resident.seen)
            continue;
        if (resident.mesh.valid())
            cache.release(device, resident.mesh);
        library.remove(resident.urn);
        m_resident.erase(m_resident.begin() + static_cast<std::ptrdiff_t>(at - 1));
    }
    return m_lastRebuilds;
}

void VoxelLoader::destroy(rhi::IDevice& device, MeshCache& cache, MeshLibrary& library)
{
    for (Resident& resident : m_resident) {
        if (resident.mesh.valid())
            cache.release(device, resident.mesh);
        library.remove(resident.urn);
    }
    m_resident.clear();
}

} // namespace luaug::render
