#include "luaug/asset/terrain_palette.h"

#include <array>

namespace luaug::asset {
namespace {

using core::Vec3;

// Linear, and chosen to be told apart at a glance in a row of swatches rather
// than to be photographic. A palette whose entries look alike is a palette a
// person picks the wrong one from.
constexpr std::array<TerrainMaterial, 8> Palette{{
    {1, "Grass", Vec3{0.16f, 0.36f, 0.10f}},
    {2, "Sand", Vec3{0.72f, 0.62f, 0.36f}},
    {3, "Rock", Vec3{0.32f, 0.31f, 0.30f}},
    {4, "Snow", Vec3{0.86f, 0.89f, 0.94f}},
    {5, "Mud", Vec3{0.26f, 0.19f, 0.13f}},
    {6, "Sandstone", Vec3{0.62f, 0.44f, 0.28f}},
    {7, "Basalt", Vec3{0.12f, 0.12f, 0.14f}},
    {8, "Ice", Vec3{0.55f, 0.74f, 0.86f}},
}};

// A magenta nobody would choose, so an id this build does not know is visibly a
// missing entry rather than a colour somebody picked.
constexpr Vec3 Unknown{0.90f, 0.10f, 0.75f};

} // namespace

std::span<const TerrainMaterial> terrainPalette() noexcept
{
    return Palette;
}

const TerrainMaterial* terrainMaterial(core::u8 id) noexcept
{
    if (id == 0) {
        return nullptr;
    }
    for (const TerrainMaterial& entry : Palette) {
        if (entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

Vec3 terrainColorOf(core::u8 id) noexcept
{
    if (const TerrainMaterial* entry = terrainMaterial(id); entry != nullptr) {
        return entry->color;
    }
    return Unknown;
}

} // namespace luaug::asset
