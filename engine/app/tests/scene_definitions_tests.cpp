// The scene's tree as types (ADR 0078): what the analyzer is told `workspace`
// holds.
#include "luaug/app/scene_definitions.h"
#include "luaug/scene/components.h"
#include "luaug/scene/world.h"

#include <doctest/doctest.h>
#include <string>

#include "inspector_fixture.h"

using namespace luaug;

namespace {

[[nodiscard]] core::InstanceId child(app::testing::Fixture& fixture, scene::World& world, scene::ClassId classId,
                                     core::InstanceId parent, const char* name)
{
    const core::InstanceId id = world.create(classId);
    world.setName(id, fixture.atoms.intern(name));
    REQUIRE_FALSE(world.setParent(id, parent).has_value());
    return id;
}

} // namespace

TEST_CASE("the workspace's tree is declared, child by child, with each one's class")
{
    app::testing::Fixture fixture;
    scene::World world{fixture.classes, fixture.enums, fixture.atoms, 7u};
    const core::InstanceId workspace = world.create(fixture.workspaceClass);
    world.workspaces().add(workspace, scene::WorkspaceComponent{});

    const core::InstanceId player = child(fixture, world, fixture.folderClass, workspace, "Player");
    (void)child(fixture, world, fixture.partClass, player, "Walker");
    (void)child(fixture, world, fixture.partClass, workspace, "Lantern Post");
    (void)child(fixture, world, fixture.partClass, workspace, "end");
    // Two with one name: a dot reaches the first, so the first is declared.
    (void)child(fixture, world, fixture.partClass, workspace, "Crate");
    (void)child(fixture, world, fixture.folderClass, workspace, "Crate");
    // A child named like a member of its parent is never reached by a dot.
    const core::InstanceId box = child(fixture, world, fixture.partClass, workspace, "Box");
    (void)child(fixture, world, fixture.partClass, box, "Material");

    const std::string text = app::sceneDefinitions(world);
    CHECK(text.find("declare workspace: Workspace & {") != std::string::npos);
    CHECK(text.find("    Player: Folder & {\n        Walker: Part,\n    },") != std::string::npos);
    // Not an identifier, or a keyword: quoted, as it is indexed.
    CHECK(text.find("    [\"Lantern Post\"]: Part,") != std::string::npos);
    CHECK(text.find("    [\"end\"]: Part,") != std::string::npos);
    CHECK(text.find("    Crate: Part,") != std::string::npos);
    CHECK(text.find("Crate: Folder") == std::string::npos);
    CHECK(text.find("    Box: Part,") != std::string::npos);
    CHECK(text.find("Material:") == std::string::npos);
}

TEST_CASE("an empty workspace is declared plainly")
{
    app::testing::Fixture fixture;
    scene::World world{fixture.classes, fixture.enums, fixture.atoms, 7u};
    const core::InstanceId workspace = world.create(fixture.workspaceClass);
    world.workspaces().add(workspace, scene::WorkspaceComponent{});
    CHECK(app::sceneDefinitions(world).find("declare workspace: Workspace\n") != std::string::npos);
}

TEST_CASE("what the storages keep is declared on game, and only when they keep something")
{
    app::testing::Fixture fixture;
    scene::World world{fixture.classes, fixture.enums, fixture.atoms, 7u};
    const core::InstanceId dataModel = world.create(fixture.folderClass);
    const core::InstanceId workspace = world.create(fixture.workspaceClass);
    world.workspaces().add(workspace, scene::WorkspaceComponent{});
    REQUIRE_FALSE(world.setParent(workspace, dataModel).has_value());
    // The fixture registers its classes by hand; the two storages are plain
    // containers, which is all a class needs to be here.
    const auto container = [&](const char* name) {
        return fixture.classes.registerClass({
            .name = fixture.atoms.intern(name),
            .defaultName = fixture.atoms.intern(name),
        });
    };
    const scene::ClassId replicatedClass = container("ReplicatedStorage");
    const scene::ClassId serverClass = container("ServerStorage");
    const core::InstanceId storage = child(fixture, world, replicatedClass, dataModel, "ReplicatedStorage");
    (void)child(fixture, world, serverClass, dataModel, "ServerStorage");
    CHECK(app::sceneDefinitions(world).find("declare game") == std::string::npos);

    const core::InstanceId weapons = child(fixture, world, fixture.folderClass, storage, "Weapons");
    (void)child(fixture, world, fixture.partClass, weapons, "Sword");
    const std::string text = app::sceneDefinitions(world);
    CHECK(text.find("declare game: DataModel & {\n    ReplicatedStorage: ReplicatedStorage & {\n"
                    "        Weapons: Folder & {\n            Sword: Part,\n        },\n    },\n}") !=
          std::string::npos);
    // An empty storage is not declared at all.
    CHECK(text.find("ServerStorage") == std::string::npos);
}
