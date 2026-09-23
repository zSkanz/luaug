#include "luaug/asset/voxel_mesher.h"

#include <algorithm>
#include <array>

namespace luaug::asset {
namespace {

using core::i32;
using core::u32;
using core::u8;
using core::usize;
using core::Vec3;

constexpr i32 Edge = static_cast<i32>(VoxelChunkEdge);
// The chunk and a one-block border on every side: every neighbour a face or an
// occlusion corner can ask about, read once rather than per question.
constexpr i32 Padded = Edge + 2;

struct Face
{
    BlockId id = AirBlock;
    // Corner occlusion, 0 to 3, in the order (-u,-v), (+u,-v), (+u,+v), (-u,+v).
    std::array<u8, 4> ao{};
    bool present = false;

    [[nodiscard]] bool same(const Face& other) const noexcept
    {
        return present && other.present && id == other.id && ao == other.ao;
    }
};

} // namespace

VoxelMesh meshVoxelChunk(const VoxelGrid& grid, VoxelChunkKey key, std::span<const BlockLook> looks, float blockSize)
{
    VoxelMesh out;
    const VoxelChunk* chunk = grid.findChunk(key);
    if (chunk == nullptr)
        return out;

    const i32 baseX = key.x * Edge;
    const i32 baseY = key.y * Edge;
    const i32 baseZ = key.z * Edge;

    // The padded neighbourhood, from -1 to 16 on each axis.
    std::vector<BlockId> padded(static_cast<usize>(Padded) * Padded * Padded, AirBlock);
    const auto paddedIndex = [](i32 x, i32 y, i32 z) noexcept {
        return static_cast<usize>(((y + 1) * Padded + (z + 1)) * Padded + (x + 1));
    };
    for (i32 y = -1; y <= Edge; ++y) {
        for (i32 z = -1; z <= Edge; ++z) {
            for (i32 x = -1; x <= Edge; ++x) {
                const bool inside = x >= 0 && y >= 0 && z >= 0 && x < Edge && y < Edge && z < Edge;
                padded[paddedIndex(x, y, z)] =
                    inside ? chunk->blocks[voxelIndex(static_cast<u32>(x), static_cast<u32>(y), static_cast<u32>(z))]
                           : grid.get(baseX + x, baseY + y, baseZ + z);
            }
        }
    }

    const auto solidId = [&looks](BlockId id) noexcept {
        if (id == AirBlock)
            return false;
        return id >= looks.size() || looks[id].solid;
    };
    const auto solidAt = [&](const std::array<i32, 3>& at) noexcept {
        return solidId(padded[paddedIndex(at[0], at[1], at[2])]);
    };

    std::array<Face, VoxelChunkVolume / VoxelChunkEdge> mask{};

    for (int axis = 0; axis < 3; ++axis) {
        const int u = (axis + 1) % 3;
        const int v = (axis + 2) % 3;
        for (int direction = -1; direction <= 1; direction += 2) {
            for (i32 slice = 0; slice < Edge; ++slice) {
                // The mask: which blocks of this slice show a face this way.
                for (i32 j = 0; j < Edge; ++j) {
                    for (i32 i = 0; i < Edge; ++i) {
                        std::array<i32, 3> block{};
                        block[static_cast<usize>(axis)] = slice;
                        block[static_cast<usize>(u)] = i;
                        block[static_cast<usize>(v)] = j;
                        std::array<i32, 3> front = block;
                        front[static_cast<usize>(axis)] += direction;

                        Face& face = mask[static_cast<usize>(j * Edge + i)];
                        face = Face{};
                        const BlockId id = padded[paddedIndex(block[0], block[1], block[2])];
                        if (!solidId(id) || solidAt(front))
                            continue;
                        face.present = true;
                        face.id = id;
                        // Each corner: the two blocks beside it and the one
                        // diagonal to it, in the layer the face looks into.
                        static constexpr std::array<std::array<i32, 2>, 4> Corners{
                            {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}};
                        for (usize corner = 0; corner < 4; ++corner) {
                            std::array<i32, 3> sideU = front;
                            std::array<i32, 3> sideV = front;
                            std::array<i32, 3> diagonal = front;
                            sideU[static_cast<usize>(u)] += Corners[corner][0];
                            sideV[static_cast<usize>(v)] += Corners[corner][1];
                            diagonal[static_cast<usize>(u)] += Corners[corner][0];
                            diagonal[static_cast<usize>(v)] += Corners[corner][1];
                            const int a = solidAt(sideU) ? 1 : 0;
                            const int b = solidAt(sideV) ? 1 : 0;
                            const int c = solidAt(diagonal) ? 1 : 0;
                            face.ao[corner] = static_cast<u8>(a == 1 && b == 1 ? 0 : 3 - (a + b + c));
                        }
                    }
                }

                // The sweep: the largest rectangle of identical faces from each
                // unclaimed one, row-major -- the same walk every run (R10).
                for (i32 j = 0; j < Edge; ++j) {
                    for (i32 i = 0; i < Edge;) {
                        const Face face = mask[static_cast<usize>(j * Edge + i)];
                        if (!face.present) {
                            ++i;
                            continue;
                        }
                        i32 width = 1;
                        while (i + width < Edge && face.same(mask[static_cast<usize>(j * Edge + i + width)]))
                            ++width;
                        i32 height = 1;
                        for (bool grows = true; grows && j + height < Edge;) {
                            for (i32 k = 0; k < width; ++k) {
                                if (!face.same(mask[static_cast<usize>((j + height) * Edge + i + k)])) {
                                    grows = false;
                                    break;
                                }
                            }
                            if (grows)
                                ++height;
                        }
                        for (i32 row = 0; row < height; ++row) {
                            for (i32 k = 0; k < width; ++k)
                                mask[static_cast<usize>((j + row) * Edge + i + k)].present = false;
                        }

                        // The quad, on the face's plane.
                        const float plane = static_cast<float>(slice + (direction > 0 ? 1 : 0));
                        const std::array<std::array<float, 2>, 4> spans{{
                            {static_cast<float>(i), static_cast<float>(j)},
                            {static_cast<float>(i + width), static_cast<float>(j)},
                            {static_cast<float>(i + width), static_cast<float>(j + height)},
                            {static_cast<float>(i), static_cast<float>(j + height)},
                        }};
                        const std::array<float, 3> base{static_cast<float>(baseX), static_cast<float>(baseY),
                                                        static_cast<float>(baseZ)};
                        const auto first = static_cast<u32>(out.mesh.vertices.size());
                        for (usize corner = 0; corner < 4; ++corner) {
                            std::array<float, 3> at{};
                            at[static_cast<usize>(axis)] = plane;
                            at[static_cast<usize>(u)] = spans[corner][0];
                            at[static_cast<usize>(v)] = spans[corner][1];
                            Vertex vertex;
                            vertex.position = Vec3{(base[0] + at[0]) * blockSize, (base[1] + at[1]) * blockSize,
                                                   (base[2] + at[2]) * blockSize};
                            std::array<float, 3> normal{};
                            normal[static_cast<usize>(axis)] = static_cast<float>(direction);
                            vertex.normal = Vec3{normal[0], normal[1], normal[2]};
                            vertex.tangent[0] = static_cast<float>(face.id);
                            vertex.tangent[1] = static_cast<float>(face.ao[corner]) / 3.0f;
                            vertex.tangent[2] = 0.0f;
                            vertex.tangent[3] = 1.0f;
                            vertex.uv[0] = spans[corner][0];
                            vertex.uv[1] = spans[corner][1];
                            out.mesh.vertices.push_back(vertex);
                            out.colliderPoints.push_back(vertex.position);
                        }

                        // **The diagonal follows the occlusion**: split along the
                        // brighter pair, or a darkened corner bleeds across the
                        // quad in a streak -- the anisotropy every block renderer
                        // has to turn by hand.
                        const bool flip = face.ao[0] + face.ao[2] < face.ao[1] + face.ao[3];
                        std::array<u32, 6> triangles =
                            flip ? std::array<u32, 6>{0, 1, 3, 1, 2, 3} : std::array<u32, 6>{0, 1, 2, 0, 2, 3};
                        // Corners run counter-clockwise about +axis (u x v is the
                        // axis); a face looking down the axis winds the other way.
                        if (direction < 0) {
                            std::swap(triangles[1], triangles[2]);
                            std::swap(triangles[4], triangles[5]);
                        }
                        for (const u32 index : triangles) {
                            out.mesh.indices.push_back(first + index);
                            out.colliderIndices.push_back(first + index);
                        }
                        i += width;
                    }
                }
            }
        }
    }

    if (!out.mesh.vertices.empty()) {
        Vec3 low = out.mesh.vertices.front().position;
        Vec3 high = low;
        for (const Vertex& vertex : out.mesh.vertices) {
            low = Vec3{std::min(low.x, vertex.position.x), std::min(low.y, vertex.position.y),
                       std::min(low.z, vertex.position.z)};
            high = Vec3{std::max(high.x, vertex.position.x), std::max(high.y, vertex.position.y),
                        std::max(high.z, vertex.position.z)};
        }
        out.mesh.bounds = core::AABB{low, high};
        Submesh section;
        section.firstIndex = 0;
        section.indexCount = static_cast<u32>(out.mesh.indices.size());
        out.mesh.submeshes.push_back(section);
    }
    return out;
}

} // namespace luaug::asset
