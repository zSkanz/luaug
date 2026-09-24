// A part wears a material (ADR 0090): the world's side of it -- the property,
// the overrides, clones, the hash, the scene file and a snapshot.
#include "luaug/asset/material.h"
#include "luaug/scene/components.h"
#include "luaug/scene/scene_file.h"
#include "luaug/scene/world.h"

#include <doctest/doctest.h>
#include <optional>
#include <string>

#include "../generated/class_descriptors.gen.h"

using namespace luaug;
using asset::MaterialField;

namespace {

constexpr std::string_view Brick = "asset://materials/brick.material.json";
constexpr std::string_view Moss = "asset://materials/moss.material.json";

struct Registries
{
    core::AtomTable atoms;
    scene::ClassRegistry classes;
    scene::EnumRegistry enums;

    Registries()
    {
        scene::generated::registerEnums(enums, atoms);
        scene::generated::registerClasses(classes, atoms);
    }
};

// A world with a Workspace, a library holding two materials, and a helper to
// make parts under it.
struct Stage
{
    Registries registries;
    asset::MaterialLibrary library;
    scene::World world{registries.classes, registries.enums, registries.atoms, 7u};
    core::InstanceId workspace;

    Stage()
    {
        asset::MaterialAsset brick;
        brick.properties.color = core::Color3{0.6f, 0.3f, 0.2f};
        brick.properties.roughness = 0.9f;
        brick.instanceParameters = asset::fieldBit(MaterialField::Color);
        brick.written = asset::AllMaterialFields;
        library.put(Brick, brick);
        asset::MaterialAsset moss;
        moss.properties.color = core::Color3{0.2f, 0.5f, 0.2f};
        moss.written = asset::AllMaterialFields;
        library.put(Moss, moss);
        world.setMaterialLibrary(&library);

        workspace = world.create(world.classes().findId(atom("Workspace")));
        REQUIRE(workspace.valid());
    }

    [[nodiscard]] core::NameAtom atom(std::string_view text) { return registries.atoms.intern(text); }

    [[nodiscard]] core::InstanceId part(std::string_view name = "Part")
    {
        const core::InstanceId id = world.create(world.classes().findId(atom("Part")));
        world.setName(id, atom(name));
        REQUIRE_FALSE(world.setParent(id, workspace).has_value());
        return id;
    }

    [[nodiscard]] scene::World::SetResult wear(core::InstanceId id, std::string_view urn, core::u32 clone = 0)
    {
        return world.setProperty(id, atom("Material"), scene::Value{scene::MaterialRef{std::string(urn), clone}});
    }

    [[nodiscard]] scene::World::SetResult tint(core::InstanceId id, core::Color3 colour)
    {
        asset::MaterialOverrides overrides = world.parts().find(id)->materialParameters;
        asset::MaterialProperties values;
        values.color = colour;
        (void)asset::setOverride(overrides, MaterialField::Color, values);
        return world.setProperty(id, atom("MaterialParameters"), scene::Value{overrides});
    }
};

} // namespace

TEST_CASE("a part wears a material by URN, and nil is the engine default")
{
    Stage stage;
    const core::InstanceId part = stage.part();
    // Nothing worn reads as nil and draws the engine default.
    CHECK(stage.world.getProperty(part, stage.atom("Material")) == scene::Value{});
    CHECK(stage.world.surfaceOf(*stage.world.parts().find(part)).properties == asset::defaultMaterial().properties);

    CHECK(stage.wear(part, Brick) == scene::World::SetResult::Changed);
    CHECK(stage.world.getProperty(part, stage.atom("Material")) ==
          scene::Value{scene::MaterialRef{std::string(Brick), 0}});
    CHECK(stage.world.surfaceOf(*stage.world.parts().find(part)).properties.roughness == 0.9f);

    // A file that is not a material is refused, and so is a clone this world
    // never made.
    CHECK(stage.world.setProperty(part, stage.atom("Material"),
                                  scene::Value{scene::MaterialRef{"asset://textures/brick.png", 0}}) ==
          scene::World::SetResult::InvalidValue);
    CHECK(stage.wear(part, Brick, 41) == scene::World::SetResult::InvalidValue);

    CHECK(stage.world.setProperty(part, stage.atom("Material"), scene::Value{}) == scene::World::SetResult::Changed);
    CHECK_FALSE(stage.world.parts().find(part)->material.valid());
}

TEST_CASE("an override applies where the material declares it, and is kept and ignored where it does not")
{
    Stage stage;
    const core::InstanceId part = stage.part();
    REQUIRE(stage.tint(part, core::Color3{0.0f, 0.0f, 1.0f}) == scene::World::SetResult::Changed);
    // The default declares Color.
    CHECK(stage.world.surfaceOf(*stage.world.parts().find(part)).properties.color == core::Color3{0.0f, 0.0f, 1.0f});

    // Brick declares Color too.
    REQUIRE(stage.wear(part, Brick) == scene::World::SetResult::Changed);
    CHECK(stage.world.surfaceOf(*stage.world.parts().find(part)).properties.color == core::Color3{0.0f, 0.0f, 1.0f});

    // Moss declares nothing: the override stays on the part and draws nothing.
    REQUIRE(stage.wear(part, Moss) == scene::World::SetResult::Changed);
    CHECK(stage.world.parts().find(part)->materialParameters.has(MaterialField::Color));
    CHECK(stage.world.surfaceOf(*stage.world.parts().find(part)).properties.color == core::Color3{0.2f, 0.5f, 0.2f});

    // And switching back restores the look rather than losing it.
    REQUIRE(stage.wear(part, Brick) == scene::World::SetResult::Changed);
    CHECK(stage.world.surfaceOf(*stage.world.parts().find(part)).properties.color == core::Color3{0.0f, 0.0f, 1.0f});
}

TEST_CASE("the hash names a material by URN and its overrides by value, and never a file's contents")
{
    Stage stage;
    const core::InstanceId part = stage.part();
    const core::u64 bare = stage.world.worldHash();

    REQUIRE(stage.wear(part, Brick) == scene::World::SetResult::Changed);
    const core::u64 brick = stage.world.worldHash();
    CHECK(brick != bare);

    REQUIRE(stage.wear(part, Moss) == scene::World::SetResult::Changed);
    CHECK(stage.world.worldHash() != brick);
    REQUIRE(stage.wear(part, Brick) == scene::World::SetResult::Changed);
    CHECK(stage.world.worldHash() == brick);

    REQUIRE(stage.tint(part, core::Color3{1.0f, 0.0f, 0.0f}) == scene::World::SetResult::Changed);
    const core::u64 tinted = stage.world.worldHash();
    CHECK(tinted != brick);

    // **An input, not state**: editing the file the part wears changes what it
    // draws and not what the world is.
    asset::MaterialAsset edited = *stage.library.asset(Brick);
    edited.properties.roughness = 0.1f;
    stage.library.put(Brick, edited);
    CHECK(stage.world.worldHash() == tinted);
}

TEST_CASE("a clone is state: hashed by its creation order and what it changed")
{
    Stage stage;
    const core::InstanceId part = stage.part();
    const core::u32 clone = stage.world.cloneMaterial(stage.atom(Brick), 0, asset::MaterialProperties{});
    stage.world.holdMaterialClone(clone);
    REQUIRE(stage.wear(part, Brick, clone) == scene::World::SetResult::Changed);
    const core::u64 fresh = stage.world.worldHash();

    scene::MaterialClone* writable = stage.world.writeMaterialClone(clone);
    REQUIRE(writable != nullptr);
    writable->values.color = core::Color3{0.0f, 1.0f, 0.0f};
    writable->set |= asset::fieldBit(MaterialField::Color);
    CHECK(stage.world.worldHash() != fresh);
    // And every part wearing it draws it.
    CHECK(stage.world.surfaceOf(*stage.world.parts().find(part)).properties.color == core::Color3{0.0f, 1.0f, 0.0f});
    // What it did not change still reads through to the asset.
    CHECK(stage.world.surfaceOf(*stage.world.parts().find(part)).properties.roughness == 0.9f);
}

TEST_CASE("a clone lives while something holds it or a part wears it")
{
    Stage stage;
    const core::InstanceId part = stage.part();
    const core::u32 clone = stage.world.cloneMaterial(stage.atom(Brick), 0, asset::MaterialProperties{});
    stage.world.holdMaterialClone(clone);
    REQUIRE(stage.wear(part, Brick, clone) == scene::World::SetResult::Changed);

    // The script lets go; the part still wears it.
    stage.world.releaseMaterialClone(clone);
    stage.world.sweepMaterialClones();
    CHECK(stage.world.materialClone(clone) != nullptr);

    // The part takes it off: nothing points at it, and the next sweep drops it.
    REQUIRE(stage.world.setProperty(part, stage.atom("Material"), scene::Value{}) == scene::World::SetResult::Changed);
    stage.world.sweepMaterialClones();
    CHECK(stage.world.materialClone(clone) == nullptr);

    // A clone nobody took a hold on does not outlive the sweep either.
    const core::u32 orphan = stage.world.cloneMaterial(stage.atom(Brick), 0, asset::MaterialProperties{});
    stage.world.sweepMaterialClones();
    CHECK(stage.world.materialClone(orphan) == nullptr);
    // And ids are never reused: creation order is state.
    CHECK(stage.world.cloneMaterial(stage.atom(Brick), 0, asset::MaterialProperties{}) == orphan + 1);
}

TEST_CASE("a snapshot carries the clones its parts wear")
{
    Stage stage;
    const core::InstanceId part = stage.part();
    const scene::WorldSnapshot before = stage.world.snapshot();
    const core::u64 hashBefore = stage.world.worldHash();

    const core::u32 clone = stage.world.cloneMaterial(stage.atom(Brick), 0, asset::MaterialProperties{});
    stage.world.holdMaterialClone(clone);
    REQUIRE(stage.wear(part, Brick, clone) == scene::World::SetResult::Changed);
    const scene::WorldSnapshot during = stage.world.snapshot();
    const core::u64 hashDuring = stage.world.worldHash();

    stage.world.restore(before);
    CHECK(stage.world.worldHash() == hashBefore);
    stage.world.restore(during);
    CHECK(stage.world.worldHash() == hashDuring);
    CHECK(stage.world.materialClone(clone) != nullptr);
}

TEST_CASE("a scene writes a material as its URN and the overrides in name order, and reads them back")
{
    Stage stage;
    const core::InstanceId part = stage.part("Wall");
    REQUIRE(stage.wear(part, Brick) == scene::World::SetResult::Changed);
    asset::MaterialOverrides overrides;
    asset::MaterialProperties values;
    values.transparency = 0.25f;
    values.color = core::Color3{0.5f, 0.5f, 0.5f};
    (void)asset::setOverride(overrides, MaterialField::Transparency, values);
    (void)asset::setOverride(overrides, MaterialField::Color, values);
    REQUIRE(stage.world.setProperty(part, stage.atom("MaterialParameters"), scene::Value{overrides}) ==
            scene::World::SetResult::Changed);

    const std::string text = scene::writeScene(stage.world);
    CHECK(text.find("\"version\":2") != std::string::npos);
    CHECK(text.find("\"Material\":\"asset://materials/brick.material.json\"") != std::string::npos);
    CHECK(text.find("\"MaterialParameters\":{\"Color\":[0.5,0.5,0.5],\"Transparency\":0.25}") != std::string::npos);

    Stage reloaded;
    REQUIRE_FALSE(scene::readScene(reloaded.world, text).has_value());
    const core::InstanceId wall = reloaded.world.findFirstChild(reloaded.workspace, reloaded.atom("Wall"));
    REQUIRE(wall.valid());
    const scene::PartComponent* read = reloaded.world.parts().find(wall);
    REQUIRE(read != nullptr);
    CHECK(reloaded.registries.atoms.text(read->material) == Brick);
    CHECK(read->materialParameters == overrides);
    CHECK(scene::writeScene(reloaded.world) == text);
}

TEST_CASE("a version 1 scene opens looking the same: its tints become the default material's overrides")
{
    const std::string version1 = R"({"format":"luaug-scene","version":1,"root":{"class":"Workspace","name":"Workspace",
        "children":[
          {"class":"Part","name":"Red","properties":{"Color":[1,0,0],"Transparency":0.5}},
          {"class":"Part","name":"Plain","properties":{"Color":[1,1,1],"Transparency":0}},
          {"class":"Part","name":"Worn","properties":{"Material":"Workspace.Brick"}},
          {"class":"Material","name":"Brick","properties":{"Roughness":0.2}}
        ]}})";
    Stage stage;
    scene::SceneIoReport report;
    REQUIRE_FALSE(scene::readScene(stage.world, version1, &report).has_value());

    const scene::PartComponent* red =
        stage.world.parts().find(stage.world.findFirstChild(stage.workspace, stage.atom("Red")));
    REQUIRE(red != nullptr);
    CHECK(red->materialParameters.has(MaterialField::Color));
    CHECK(red->materialParameters.color == core::Color3{1.0f, 0.0f, 0.0f});
    CHECK(red->materialParameters.transparency == 0.5f);
    CHECK_FALSE(red->material.valid());

    // The defaults are not overrides.
    const scene::PartComponent* plain =
        stage.world.parts().find(stage.world.findFirstChild(stage.workspace, stage.atom("Plain")));
    REQUIRE(plain != nullptr);
    CHECK(plain->materialParameters.empty());

    // A `Material` instance is an unknown class, and the reference to it is a
    // dropped reference -- both counted, neither fatal.
    CHECK(report.unknownClasses == 1);
    CHECK(report.droppedReferences == 1);
}
