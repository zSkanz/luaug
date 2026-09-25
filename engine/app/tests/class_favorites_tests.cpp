// The add-a-child list's order and its stars (the owner: "first what that
// instance accepts, A to Z, then what it does not, and a star for a
// favourite").

#include "luaug/app/class_favorites.h"
#include "luaug/app/inspector.h"
#include "luaug/core/name_atom.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/enum_registry.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <doctest/doctest.h>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "class_descriptors.gen.h"

using namespace luaug;
using app::ClassFavorites;
using app::ClassPick;
using app::ClassPickGroup;

TEST_CASE("a star goes on and comes off, and the file keeps it")
{
    ClassFavorites favorites;
    CHECK(favorites.toggle("Part", "PointLight"));
    CHECK(favorites.toggle("Part", "Decal"));
    CHECK(favorites.has("Part", "PointLight"));
    CHECK_FALSE(favorites.has("Model", "PointLight"));
    // In name order, whatever order they were starred in.
    REQUIRE(favorites.of("Part").size() == 2);
    CHECK(favorites.of("Part")[0] == "Decal");

    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / "luaug-class-favorites-tests" / "class-favorites.json";
    REQUIRE(app::saveClassFavorites(file, favorites));
    const ClassFavorites loaded = app::loadClassFavorites(file);
    CHECK(loaded.has("Part", "Decal"));
    CHECK(loaded.has("Part", "PointLight"));

    // Off again; the last one off takes the parent with it.
    CHECK_FALSE(favorites.toggle("Part", "PointLight"));
    CHECK_FALSE(favorites.toggle("Part", "Decal"));
    CHECK(favorites.byParent.empty());

    // No file is no favourites, not an error.
    CHECK(app::loadClassFavorites(file.parent_path() / "missing.json").byParent.empty());
    std::error_code ec;
    std::filesystem::remove_all(file.parent_path(), ec);
}

namespace {

struct Stage
{
    core::AtomTable atoms;
    scene::ClassRegistry classes;
    scene::EnumRegistry enums;
    scene::World world;
    core::InstanceId game;
    core::InstanceId workspace;
    core::InstanceId storage;

    Stage() : world((registerAll(), classes), enums, atoms, 1234u)
    {
        game = make("DataModel", "game", {});
        workspace = make("Workspace", "Workspace", game);
        storage = make("ReplicatedStorage", "ReplicatedStorage", game);
    }

    void registerAll()
    {
        scene::generated::registerClasses(classes, atoms);
        scene::generated::registerEnums(enums, atoms);
    }

    [[nodiscard]] scene::ClassId id(std::string_view name) { return classes.findId(atoms.intern(name)); }

    core::InstanceId make(std::string_view className, std::string_view name, core::InstanceId parent)
    {
        const core::InstanceId made = world.create(id(className));
        world.setName(made, atoms.intern(name));
        if (parent.valid())
            (void)world.setParent(made, parent);
        return made;
    }
};

} // namespace

TEST_CASE("a class works where the IDL says, a folder is transparent, and the storages take anything")
{
    Stage stage;
    const core::InstanceId part = stage.make("Part", "Crate", stage.workspace);
    const core::InstanceId script = stage.make("Script", "Main", stage.workspace);
    const core::InstanceId folder = stage.make("Folder", "Props", stage.workspace);

    // `Terrain` belongs to the workspace alone.
    CHECK(app::worksUnder(stage.world, stage.id("Terrain"), stage.workspace));
    CHECK_FALSE(app::worksUnder(stage.world, stage.id("Terrain"), part));
    // A part goes in the world and in a part, not in a script.
    CHECK(app::worksUnder(stage.world, stage.id("Part"), part));
    CHECK_FALSE(app::worksUnder(stage.world, stage.id("Part"), script));
    // A folder is judged by what it is under.
    CHECK(app::worksUnder(stage.world, stage.id("Terrain"), folder));
    // A storage takes anything: a template waiting to be cloned.
    CHECK(app::worksUnder(stage.world, stage.id("Part"), stage.storage));
    // A class that says nothing works anywhere.
    CHECK(app::worksUnder(stage.world, stage.id("Script"), part));
    CHECK(app::worksUnder(stage.world, stage.id("Folder"), script));
    // And one that says nothing of its own inherits: a ball socket is a
    // `Constraint`, and a constraint lives in the world.
    CHECK(app::worksUnder(stage.world, stage.id("BallSocketConstraint"), part));
    CHECK_FALSE(app::worksUnder(stage.world, stage.id("BallSocketConstraint"), script));
}

TEST_CASE("the list is the favourites, then what works, then the rest, each A to Z")
{
    Stage stage;
    const core::InstanceId script = stage.make("Script", "Main", stage.workspace);

    std::vector<scene::ClassId> creatable;
    app::collectCreatableClasses(stage.world, creatable);
    const std::vector<std::string> favorites{"Part"};
    std::vector<ClassPick> picks;
    app::orderClassPicks(stage.world, script, creatable, favorites, picks);

    REQUIRE_FALSE(picks.empty());
    CHECK(picks.front().id == stage.id("Part"));
    CHECK(picks.front().group == ClassPickGroup::Favorite);
    const auto groupOf = [&](std::string_view name) {
        const auto found =
            std::find_if(picks.begin(), picks.end(), [&](const ClassPick& pick) { return pick.id == stage.id(name); });
        REQUIRE(found != picks.end());
        return found->group;
    };
    CHECK(groupOf("Folder") == ClassPickGroup::Works);
    CHECK(groupOf("Terrain") == ClassPickGroup::Elsewhere);
    // The groups never interleave.
    CHECK(std::is_sorted(picks.begin(), picks.end(), [](const ClassPick& a, const ClassPick& b) {
        return static_cast<int>(a.group) < static_cast<int>(b.group);
    }));
    CHECK(picks.size() == creatable.size());

    // No parent is one group: the content browser's list.
    app::orderClassPicks(stage.world, core::InstanceId{}, creatable, {}, picks);
    CHECK(std::all_of(picks.begin(), picks.end(),
                      [](const ClassPick& pick) { return pick.group == ClassPickGroup::Works; }));
}
