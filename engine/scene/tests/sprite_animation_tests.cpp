// `SpriteAnimator` (ADR 0102): frames of a sheet on the simulation clock,
// written into the parent sprite through the world's own verb.
#include "luaug/physics/physics2d.h"
#include "luaug/scene/components.h"
#include "luaug/scene/sprite_animation.h"
#include "luaug/scene/world.h"

#include <doctest/doctest.h>
#include <string_view>

#include "../generated/class_descriptors.gen.h"

using namespace luaug;

namespace {

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

struct Sheet
{
    Registries registries;
    scene::World world{registries.classes, registries.enums, registries.atoms, 7u};
    core::InstanceId workspace;
    core::InstanceId sprite;
    core::InstanceId animator;

    Sheet()
    {
        workspace = world.create(world.classes().findId(atom("Workspace")));
        sprite = world.create(world.classes().findId(atom("Part2D")));
        REQUIRE_FALSE(world.setParent(sprite, workspace).has_value());
        animator = world.create(world.classes().findId(atom("SpriteAnimator")));
        REQUIRE(animator.valid());
        REQUIRE_FALSE(world.setParent(animator, sprite).has_value());
    }

    [[nodiscard]] core::NameAtom atom(std::string_view text) { return registries.atoms.intern(text); }

    void set(core::InstanceId id, std::string_view property, const scene::Value& value)
    {
        REQUIRE(world.setProperty(id, atom(property), value) != scene::World::SetResult::InvalidValue);
    }

    // Eight frames a second on a 60 Hz tick: a page every 7.5 ticks.
    void ticks(int count)
    {
        for (int at = 0; at < count; ++at)
            scene::stepSpriteAnimators(world, 1.0 / 60.0);
    }

    [[nodiscard]] const scene::Part2DComponent& part() { return *world.parts2d().find(sprite); }
    [[nodiscard]] const scene::SpriteAnimatorComponent& clip() { return *world.spriteAnimators().find(animator); }
};

} // namespace

TEST_CASE("a playing animator turns the sheet's pages along its rows")
{
    Sheet sheet;
    sheet.set(sheet.animator, "FrameSize", scene::Value{core::Vec2{32.0f, 16.0f}});
    sheet.set(sheet.animator, "Columns", scene::Value{2.0});
    sheet.set(sheet.animator, "SheetOffset", scene::Value{core::Vec2{0.0f, 64.0f}});
    sheet.set(sheet.animator, "FrameCount", scene::Value{3.0});

    // Stopped, it writes nothing.
    sheet.ticks(20);
    CHECK(sheet.part().imageRectSize == core::Vec2{0.0f, 0.0f});

    sheet.set(sheet.animator, "Playing", scene::Value{true});
    sheet.ticks(1);
    CHECK(sheet.part().imageRectOffset == core::Vec2{0.0f, 64.0f});
    CHECK(sheet.part().imageRectSize == core::Vec2{32.0f, 16.0f});
    sheet.ticks(7);
    CHECK(sheet.clip().frame == 1);
    CHECK(sheet.part().imageRectOffset == core::Vec2{32.0f, 64.0f});
    // The third frame is the second row's first.
    sheet.ticks(8);
    CHECK(sheet.clip().frame == 2);
    CHECK(sheet.part().imageRectOffset == core::Vec2{0.0f, 80.0f});
    // Looped: back to the top.
    sheet.ticks(7);
    CHECK(sheet.clip().frame == 0);
    CHECK(sheet.world.getProperty(sheet.animator, sheet.atom("Frame")) == scene::Value{0.0});
}

TEST_CASE("a run that is not looped stops on its last frame and says so")
{
    Sheet sheet;
    sheet.set(sheet.animator, "FrameCount", scene::Value{2.0});
    sheet.set(sheet.animator, "FirstFrame", scene::Value{4.0});
    sheet.set(sheet.animator, "Columns", scene::Value{4.0});
    sheet.set(sheet.animator, "Looped", scene::Value{false});
    sheet.set(sheet.animator, "Playing", scene::Value{true});
    sheet.world.setPropertySubscribed(sheet.animator, sheet.atom("Playing"), true);
    (void)sheet.world.changes().take();

    sheet.ticks(30);
    CHECK_FALSE(sheet.clip().playing);
    CHECK(sheet.clip().frame == 1);
    // Frame 5 of a four-wide sheet: the second row's second.
    CHECK(sheet.part().imageRectOffset == core::Vec2{16.0f, 16.0f});
    bool heard = false;
    for (const scene::Change& change : sheet.world.changes().take())
        heard = heard || (change.subject == sheet.animator && change.name == sheet.atom("Playing"));
    CHECK(heard);

    // Played again, it starts over rather than resuming on the last frame.
    sheet.set(sheet.animator, "Playing", scene::Value{true});
    sheet.ticks(1);
    CHECK(sheet.clip().frame == 0);
    CHECK(sheet.part().imageRectOffset == core::Vec2{0.0f, 16.0f});
}

TEST_CASE("paused and resumed, an animator carries on from where it was")
{
    Sheet sheet;
    sheet.set(sheet.animator, "FrameCount", scene::Value{4.0});
    sheet.set(sheet.animator, "Columns", scene::Value{4.0});
    sheet.set(sheet.animator, "Playing", scene::Value{true});
    sheet.ticks(16);
    CHECK(sheet.clip().frame == 2);
    sheet.set(sheet.animator, "Playing", scene::Value{false});
    sheet.ticks(30);
    CHECK(sheet.clip().frame == 2);
    sheet.set(sheet.animator, "Playing", scene::Value{true});
    sheet.ticks(1);
    CHECK(sheet.clip().frame == 2);
}

TEST_CASE("an animator's values are checked")
{
    Sheet sheet;
    const auto set = [&](std::string_view property, const scene::Value& value) {
        return sheet.world.setProperty(sheet.animator, sheet.atom(property), value);
    };
    CHECK(set("Columns", scene::Value{0.0}) == scene::World::SetResult::InvalidValue);
    CHECK(set("Columns", scene::Value{1.5}) == scene::World::SetResult::InvalidValue);
    CHECK(set("FrameCount", scene::Value{0.0}) == scene::World::SetResult::InvalidValue);
    CHECK(set("FirstFrame", scene::Value{-1.0}) == scene::World::SetResult::InvalidValue);
    CHECK(set("FramesPerSecond", scene::Value{0.0}) == scene::World::SetResult::InvalidValue);
    CHECK(set("FrameSize", scene::Value{core::Vec2{0.0f, 4.0f}}) == scene::World::SetResult::InvalidValue);
    CHECK(set("Frame", scene::Value{1.0}) == scene::World::SetResult::ReadOnly);
}

TEST_CASE("a 2D joint takes two sprites or nil, and its kind is its class")
{
    Sheet sheet;
    const core::InstanceId hinge = sheet.world.create(sheet.world.classes().findId(sheet.atom("HingeConstraint2D")));
    const core::InstanceId spring = sheet.world.create(sheet.world.classes().findId(sheet.atom("SpringConstraint2D")));
    REQUIRE(hinge.valid());
    CHECK(sheet.world.constraints2d().find(hinge)->kind == static_cast<core::i32>(physics::Joint2DType::Hinge));
    CHECK(sheet.world.constraints2d().find(spring)->kind == static_cast<core::i32>(physics::Joint2DType::Spring));

    CHECK(sheet.world.setProperty(hinge, sheet.atom("Part0"), scene::Value{sheet.sprite}) ==
          scene::World::SetResult::Changed);
    CHECK(sheet.world.getProperty(hinge, sheet.atom("Part0")) == scene::Value{sheet.sprite});
    // Not a sprite.
    CHECK(sheet.world.setProperty(hinge, sheet.atom("Part1"), scene::Value{sheet.workspace}) ==
          scene::World::SetResult::InvalidValue);
    CHECK(sheet.world.setProperty(hinge, sheet.atom("Part0"), scene::Value{}) == scene::World::SetResult::Changed);
    CHECK(sheet.world.setProperty(spring, sheet.atom("Length"), scene::Value{0.0}) ==
          scene::World::SetResult::InvalidValue);
}
