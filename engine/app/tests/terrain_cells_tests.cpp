// A terrain saved as a folder of cells, edited and reopened (ADR 0087).
//
// End to end over real files and the real reader, through `TerrainCells` --
// the object the engine's frame loop and the editor's save both call -- so what
// is proved here is what the editor does: a terrain too large for its scene
// becomes cells, a reopened one streams around the camera rather than loading
// whole, a save writes the cells that changed and no others, and an undo that
// puts back a field from before a load does not leave holes.
#include "luaug/app/field_streamer.h"
#include "luaug/app/terrain_cells.h"
#include "luaug/asset/field_cells.h"
#include "luaug/asset/terrain.h"
#include "luaug/platform/async_io.h"
#include "luaug/platform/file.h"
#include "luaug/scene/components.h"
#include "luaug/scene/world.h"

#include <doctest/doctest.h>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>

#include "inspector_fixture.h"

using namespace luaug;

namespace {

struct IoScope
{
    IoScope() { REQUIRE(platform::initIo(4)); }
    ~IoScope() { platform::shutdownIo(); }
};

// Two-metre ground, so a 64 m cell is one column of chunks.
constexpr asset::FieldSettings Settings{.voxelSize = 2.0f, .minHeight = -32.0f, .maxHeight = 32.0f};

struct Editing
{
    app::testing::Fixture fixture;
    scene::World world{fixture.classes, fixture.enums, fixture.atoms, 99u};
    core::InstanceId workspace;
    core::InstanceId ground;
    app::FieldStreamer fields;
    std::filesystem::path content;
    app::TerrainCells cells;

    explicit Editing(const std::filesystem::path& contentRoot) : content(contentRoot), cells(fields, contentRoot)
    {
        workspace = world.create(fixture.workspaceClass);
        world.workspaces().add(workspace, scene::WorkspaceComponent{});
        ground = world.create(fixture.folderClass);
        scene::TerrainComponent terrain;
        terrain.field = asset::TerrainField(Settings);
        world.terrains().add(ground, std::move(terrain));
        (void)world.setParent(ground, workspace);
        scene::EngineState& state = world.engineState();
        state.streamingLoadRadius = 160.0;
        state.streamingMinRadius = 96.0;
        fields.setWorld(&world, workspace);
    }

    [[nodiscard]] scene::TerrainComponent& terrain() { return *world.terrains().find(ground); }
    [[nodiscard]] bool holds(core::i32 x, core::i32 z) { return !terrain().field.column(x, z).empty(); }

    // Frames of the editor looking from `eye`, until `done`.
    template <typename Done>
    [[nodiscard]] bool lookUntil(core::DVec3 eye, core::u64 restores, Done&& done)
    {
        for (int frame = 0; frame < 20000; ++frame) {
            cells.frame(world, workspace, restores, eye);
            fields.setWorld(&world, workspace);
            fields.pump(50.0);
            if (done())
                return true;
            std::this_thread::yield();
        }
        return false;
    }
};

struct ContentDirectory
{
    std::filesystem::path path = std::filesystem::temp_directory_path() / "luaug-terrain-cells-tests";
    ContentDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
        REQUIRE(platform::createDirectories(path));
    }
    ~ContentDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

[[nodiscard]] std::size_t cellFiles(const std::filesystem::path& folder)
{
    std::size_t count = 0;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
        if (entry.path().extension() == ".lterrain")
            ++count;
    }
    return count;
}

} // namespace

TEST_CASE("a terrain too large for its scene becomes cells, streams around the camera, and saves only what changed")
{
    IoScope io;
    ContentDirectory content;
    const std::filesystem::path scene = content.path / "scenes" / "big.scene.json";
    const std::string folder = "terrain/scenes/big.scene";

    // **Written**: 1152 m square, eighteen cells a side -- past the 256 a
    // scene carries inline.
    std::string cellIndex;
    std::size_t written = 0;
    {
        Editing editor(content.path);
        (void)asset::fillFlat(editor.terrain().field, core::DVec3{0.0, 0.0, 0.0}, 1152.0f, 4.0f, 1);
        REQUIRE(asset::splitTerrain(editor.terrain().field).size() >= app::InlineTerrainCells);
        std::string note;
        REQUIRE(editor.cells.save(editor.world, editor.workspace, scene, note));
        cellIndex = editor.terrain().cellIndex;
        CHECK(cellIndex == folder + "/index.json");
        written = cellFiles(content.path / folder);
        CHECK(written == 18 * 18);
        CHECK(platform::fileExists(content.path / cellIndex));
    }

    // **Reopened**: what `readScene` makes of a terrain saved as cells -- the
    // settings, the index, and no ground -- and the editor looking at the middle.
    Editing editor(content.path);
    editor.terrain().cellIndex = cellIndex;
    REQUIRE(editor.lookUntil(core::DVec3{8.0, 10.0, 8.0}, 0, [&] { return editor.holds(0, 0) && editor.holds(1, 1); }));
    // The ground under the camera came, and ground five hundred metres off did
    // not: the whole field never was in memory.
    CHECK_FALSE(editor.holds(8, 8));

    // An edit near the camera, then a save: the cells the ball reached are
    // written, and nothing else is -- not the other resident cells, and not
    // the three hundred nobody loaded.
    (void)asset::fillBall(editor.terrain().field, core::DVec3{10.0, 4.0, 10.0}, 6.0, 0);
    editor.terrain().fieldRevision += 1;
    std::string note;
    REQUIRE(editor.cells.save(editor.world, editor.workspace, scene, note));
    CHECK(note.find("terrain: 1 cell(s) written") != std::string::npos);
    CHECK(cellFiles(content.path / folder) == written);

    // Saved, the edited cell is an ordinary cell again: looking elsewhere lets
    // it go, where an unsaved edit would have been kept.
    REQUIRE(editor.lookUntil(core::DVec3{520.0, 10.0, 520.0}, 0,
                             [&] { return !editor.holds(0, 0) && editor.holds(8, 8); }));

    // Back again: the dig came back from its file.
    REQUIRE(editor.lookUntil(core::DVec3{8.0, 10.0, 8.0}, 0, [&] { return editor.holds(0, 0); }));
    CHECK(asset::sampleField(editor.terrain().field, core::DVec3{10.0, 3.0, 10.0}).distance > 0.0f);
}

TEST_CASE("an undo that puts back a field from before a load leaves no hole where the loaded ground was")
{
    IoScope io;
    ContentDirectory content;
    const std::filesystem::path scene = content.path / "scenes" / "undo.scene.json";
    std::string cellIndex;
    {
        Editing editor(content.path);
        (void)asset::fillFlat(editor.terrain().field, core::DVec3{0.0, 0.0, 0.0}, 1152.0f, 4.0f, 1);
        std::string note;
        REQUIRE(editor.cells.save(editor.world, editor.workspace, scene, note));
        cellIndex = editor.terrain().cellIndex;
    }

    Editing editor(content.path);
    editor.terrain().cellIndex = cellIndex;
    REQUIRE(editor.lookUntil(core::DVec3{8.0, 10.0, 8.0}, 0, [&] { return editor.holds(0, 0); }));
    // What an undo snapshot of this moment holds.
    const asset::TerrainField before = editor.terrain().field;

    // The camera moves on and ground loads there...
    REQUIRE(editor.lookUntil(core::DVec3{8.0, 10.0, 400.0}, 0, [&] { return editor.holds(0, 6); }));
    // ...and the world is put back to the snapshot, which never had it.
    editor.terrain().field = before;
    REQUIRE_FALSE(editor.holds(0, 6));

    // One frame that knows the world was restored, and the ground loaded
    // around the camera is there again.
    editor.cells.frame(editor.world, editor.workspace, 1, core::DVec3{8.0, 10.0, 400.0});
    CHECK(editor.holds(0, 6));
}
