// A terrain saved as a folder of cells, edited and played (ADR 0087).
//
// **The one place that knows a scene's ground can live beside it.** The field
// streamer streams cells and knows nothing of scenes; the scene writer names an
// index and knows nothing of files; the editor saves and knows nothing of
// cells. This joins them: it adopts the index the workspace's terrain names,
// keeps the streamer's picture in step with an undo, and on a save writes the
// cells that changed -- or, for a terrain grown past what a scene should carry,
// all of them, and from then on the scene names them instead.
#pragma once

#include "luaug/app/field_streamer.h"
#include "luaug/core/id.h"
#include "luaug/core/math.h"
#include "luaug/core/types.h"

#include <filesystem>
#include <optional>
#include <string>

namespace luaug::scene {
class World;
}

namespace luaug::app {

// A terrain split into more cells than this is saved as a folder of them. 256
// cells is a kilometre square at a metre, where a field inline in a text file
// stops being a thing anybody would want to diff or load whole.
inline constexpr core::usize InlineTerrainCells = 256;

class TerrainCells
{
public:
    TerrainCells(FieldStreamer& fields, std::filesystem::path contentRoot);

    // **Once a frame, before the streamer pumps.** Adopts the index when the
    // workspace's terrain names a different one; reconciles when the world
    // was put back since the last frame (`restores` moved); streams around
    // `focus` when there is one -- the editor's camera while editing -- and
    // around the world's own foci when there is not.
    void frame(scene::World& world, core::InstanceId workspace, core::u64 restores, std::optional<core::DVec3> focus);

    // **The save**, run before the scene is written: see `Editor::TerrainSaver`.
    bool save(scene::World& world, core::InstanceId workspace, const std::filesystem::path& scenePath,
              std::string& note);

private:
    [[nodiscard]] FieldStreamer::CellResolver resolver() const;

    FieldStreamer& m_fields;
    std::filesystem::path m_contentRoot;
    std::string m_adopted;
    core::InstanceId m_adoptedTerrain;
    core::u64 m_restores = 0;
};

} // namespace luaug::app
