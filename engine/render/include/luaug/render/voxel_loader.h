#pragma once

// The block world on the GPU (V1, `VoxelService`).
//
// Each chunk that holds blocks is meshed by `asset::meshVoxelChunk` -- greedy
// faces, hidden faces culled, ambient occlusion per corner -- uploaded through
// `MeshCache`, and filed in `MeshLibrary` under `voxelChunkUrn`, where `extract`
// finds it and emits a draw the renderer shades with the block shader.
//
// **A chunk is re-meshed when anything it READ changed**: its own blocks, and
// the 26 chunks around it, because a face at the chunk's edge is hidden by a
// neighbour's block and a corner's occlusion is darkened by one. The key is the
// digests of all 27, so an edit re-meshes the chunk it is in and, at an edge,
// the neighbours whose faces it touches -- and nothing else.

#include "luaug/asset/voxel.h"
#include "luaug/render/mesh_cache.h"
#include "luaug/render/render_world.h"
#include "luaug/rhi/device.h"
#include "luaug/scene/world.h"

#include <string>
#include <vector>

namespace luaug::render {

// `voxel://<x>,<y>,<z>`, in chunk keys.
[[nodiscard]] std::string voxelChunkUrn(asset::VoxelChunkKey key);

class VoxelLoader
{
public:
    // Meshes and uploads what changed near the viewer, and releases what is out
    // of range. Budgeted by a COUNT of chunks, never a clock. Answers how many
    // chunks it rebuilt.
    core::u32 sync(rhi::IDevice& device, rhi::ICmdList& cmd, const scene::World& world, core::AtomTable& atoms,
                   MeshCache& cache, MeshLibrary& library);

    // Where the viewer is. Chunks are meshed nearest first, and only within
    // `viewDistance` of it; with no focus, everything is.
    void setFocus(core::DVec3 focus) noexcept
    {
        m_focus = focus;
        m_hasFocus = true;
    }
    void setViewDistance(double metres) noexcept { m_viewDistance = metres; }

    void destroy(rhi::IDevice& device, MeshCache& cache, MeshLibrary& library);

    [[nodiscard]] core::usize residentCount() const noexcept { return m_resident.size(); }
    [[nodiscard]] core::u32 lastRebuilds() const noexcept { return m_lastRebuilds; }

private:
    struct Resident
    {
        asset::VoxelChunkKey key;
        core::NameAtom urn;
        MeshHandle mesh;
        core::u64 content = 0;
        float blockSize = 1.0f;
        bool seen = false;
    };

    core::DVec3 m_focus;
    bool m_hasFocus = false;
    double m_viewDistance = 384.0;
    core::u32 m_lastRebuilds = 0;
    // Sorted by key (R10).
    std::vector<Resident> m_resident;
};

} // namespace luaug::render
