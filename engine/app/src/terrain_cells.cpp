#include "luaug/app/terrain_cells.h"

#include "luaug/asset/chunk.h"
#include "luaug/asset/field_cells.h"
#include "luaug/core/i18n.h"
#include "luaug/core/log.h"
#include "luaug/platform/file.h"
#include "luaug/scene/components.h"
#include "luaug/scene/world.h"

#include <system_error>
#include <utility>
#include <vector>

namespace luaug::app {
namespace {

// The workspace's own terrain, as `FieldStreamer` finds it.
[[nodiscard]] std::pair<core::InstanceId, scene::TerrainComponent*> workspaceTerrain(scene::World& world,
                                                                                     core::InstanceId workspace)
{
    std::pair<core::InstanceId, scene::TerrainComponent*> found{};
    world.terrains().forEach([&](core::InstanceId id, scene::TerrainComponent& component) {
        if (found.second == nullptr && world.parentOf(id) == workspace)
            found = {id, &component};
    });
    return found;
}

// A terrain's cell index, read from the project; empty when it cannot be.
[[nodiscard]] asset::ChunkIndex readCells(const std::filesystem::path& contentRoot, const std::string& cellIndex)
{
    asset::ChunkIndex index;
    std::string text;
    if (!platform::readTextFile(contentRoot / std::filesystem::path(cellIndex), text) ||
        asset::readChunkIndex(text, index).has_value()) {
        const core::I18nArg args[] = {{"content", cellIndex}};
        core::log(core::LogLevel::Warn, LUAUG_TR("app.warn.chunk_missing"), args);
        return asset::ChunkIndex{};
    }
    return index;
}

} // namespace

TerrainCells::TerrainCells(FieldStreamer& fields, std::filesystem::path contentRoot)
    : m_fields(fields), m_contentRoot(std::move(contentRoot))
{}

FieldStreamer::CellResolver TerrainCells::resolver() const
{
    return [root = m_contentRoot](const asset::ChunkIndexEntry& entry) -> std::optional<std::filesystem::path> {
        return root / std::filesystem::path(entry.urn);
    };
}

void TerrainCells::frame(scene::World& world, core::InstanceId workspace, core::u64 restores,
                         std::optional<core::DVec3> focus)
{
    const auto [terrainId, terrain] = workspaceTerrain(world, workspace);
    const std::string wanted = terrain != nullptr ? terrain->cellIndex : std::string{};
    // Only a change of INDEX is adopted -- or of terrain, for one that has an
    // index. A terrain with none has nothing to adopt, and adopting "nothing"
    // would drop the cells a partition gave the streamer for an inline field.
    if (wanted != m_adopted || (!wanted.empty() && !(terrainId == m_adoptedTerrain))) {
        m_adopted = wanted;
        m_adoptedTerrain = terrainId;
        m_fields.setWorld(&world, workspace);
        m_fields.adoptTerrain(wanted.empty() ? asset::ChunkIndex{} : readCells(m_contentRoot, wanted), resolver());
    }
    if (restores != m_restores) {
        m_restores = restores;
        m_fields.setWorld(&world, workspace);
        m_fields.reconcile();
    }
    m_fields.setFocusOverride(focus);
}

bool TerrainCells::save(scene::World& world, core::InstanceId workspace, const std::filesystem::path& scenePath,
                        std::string& note)
{
    const auto [terrainId, terrain] = workspaceTerrain(world, workspace);
    if (terrain == nullptr)
        return true;
    // The cells live beside their scene, under `content/terrain/`, named after
    // it -- so a scene and its ground travel together, and a second scene is a
    // second folder.
    std::string stem = scenePath.lexically_relative(m_contentRoot).generic_string();
    if (stem.ends_with(".json"))
        stem.resize(stem.size() - 5);
    const std::string folder = "terrain/" + stem;
    const std::string wanted = folder + "/index.json";

    if (terrain->cellIndex.empty()) {
        if (asset::splitTerrain(terrain->field).size() < InlineTerrainCells)
            return true;
    }
    else if (terrain->cellIndex != wanted) {
        // **Save As takes the ground with it**: the cells the old scene names
        // are copied into the new scene's folder, so editing one scene never
        // changes the other's ground.
        asset::ChunkIndex copied = readCells(m_contentRoot, terrain->cellIndex);
        for (asset::ChunkIndexEntry& entry : copied.chunks) {
            std::vector<std::byte> bytes;
            const std::string name = std::filesystem::path(entry.urn).filename().generic_string();
            const std::filesystem::path to = m_contentRoot / std::filesystem::path(folder) / name;
            if (!platform::readFile(m_contentRoot / std::filesystem::path(entry.urn), bytes) ||
                !platform::createDirectories(to.parent_path()) || !platform::writeFile(to, bytes)) {
                note = "could not copy " + entry.urn;
                return false;
            }
            entry.urn = folder + "/" + name;
        }
        m_fields.setWorld(&world, workspace);
        m_fields.adoptTerrain(copied, resolver());
    }
    terrain->cellIndex = wanted;
    m_adopted = wanted;
    m_adoptedTerrain = terrainId;
    m_fields.setWorld(&world, workspace);

    FieldStreamer::TerrainCellWriter writer;
    writer.write = [&folder, root = m_contentRoot](asset::ChunkId id,
                                                   std::span<const std::byte> bytes) -> std::optional<std::string> {
        const std::string name = "cell_" + std::to_string(id.x) + "_" + std::to_string(id.z) + ".lterrain";
        const std::filesystem::path to = root / std::filesystem::path(folder) / name;
        if (!platform::createDirectories(to.parent_path()) || !platform::writeFile(to, bytes))
            return std::nullopt;
        return folder + "/" + name;
    };
    writer.read = [root = m_contentRoot](const asset::ChunkIndexEntry& entry) -> std::optional<std::vector<std::byte>> {
        std::vector<std::byte> bytes;
        if (!platform::readFile(root / std::filesystem::path(entry.urn), bytes))
            return std::nullopt;
        return bytes;
    };
    writer.remove = [root = m_contentRoot](const asset::ChunkIndexEntry& entry) {
        std::error_code ignored;
        std::filesystem::remove(root / std::filesystem::path(entry.urn), ignored);
    };
    writer.resolve = resolver();
    const FieldStreamer::TerrainSaveReport saved = m_fields.saveTerrain(writer);
    if (!platform::writeTextFile(m_contentRoot / std::filesystem::path(wanted), asset::writeChunkIndex(saved.index))) {
        note = "could not write " + wanted;
        return false;
    }
    if (!saved.ok) {
        note = "some of the terrain's cells could not be written";
        return false;
    }
    note = "terrain: " + std::to_string(saved.written) + " cell(s) written, " + std::to_string(saved.unchanged) +
           " unchanged";
    if (saved.removed > 0)
        note += ", " + std::to_string(saved.removed) + " emptied";
    return true;
}

} // namespace luaug::app
