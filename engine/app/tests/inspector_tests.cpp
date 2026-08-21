// The DebugShell's explorer and properties panel, minus the pixels.
//
// What the panel draws is a screenshot's business. What it decides is not, and
// all four of the M4 brief's claims about it are checkable here: that the sweep
// is generic (Decision 16), that every write goes through `World::setProperty`
// (Decision 14), that a write waits for the FrameStart drain (Decision 15), and
// that every `ValueType` the registry can hold renders something (entering
// risk 6).
//
// The fixture below is a class hierarchy `engine/app` has never seen, declaring
// one property of **every** `ValueType`. That is deliberate: a generic sweep
// that has only ever swept the classes it was written against has not been
// shown to be generic, which is the brief's own wording of the risk.
#include "luaug/app/inspector.h"
#include "luaug/core/id.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/value.h"
#include "luaug/scene/world.h"

#include <doctest/doctest.h>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "inspector_fixture.h"

using luaug::app::collectProperties;
using luaug::app::collectTree;
using luaug::app::editable;
using luaug::app::editorFor;
using luaug::app::EditorKind;
using luaug::app::formatValue;
using luaug::app::Inspector;
using luaug::app::propertyTag;
using luaug::app::setResultLabel;
using luaug::app::TreeRow;

namespace core = luaug::core;
namespace scene = luaug::scene;

using SetResult = scene::World::SetResult;

namespace {

using luaug::app::testing::Fixture;

[[nodiscard]] std::vector<std::string> propertyNames(Fixture& fixture, scene::ClassId classId)
{
    std::vector<const scene::PropertyDesc*> properties;
    collectProperties(fixture.classes, classId, properties);

    std::vector<std::string> names;
    names.reserve(properties.size());
    for (const scene::PropertyDesc* descriptor : properties)
        names.emplace_back(fixture.atoms.text(descriptor->name));
    return names;
}

} // namespace

TEST_CASE("the sweep visits every property of a class it has never seen")
{
    Fixture fixture;

    // Inherited members first and declared members after, which is
    // `propertySlot`'s numbering. Written out rather than counted, because
    // "there are ten of them" would still pass with the wrong ten.
    const std::vector<std::string> expected{
        "Owner", "Flag", "Locked", "Sealed", "Count", "Label", "Offset", "Frame", "Tint", "Link", "Mood", "Nothing",
    };
    CHECK(propertyNames(fixture, fixture.widgetClass) == expected);

    // The same order the registry assigns, asserted against the registry rather
    // than against the list above: what the panel lists and what a subscription
    // addresses have to be one order, and a sweep that drifted from it would
    // still look plausible.
    std::vector<const scene::PropertyDesc*> properties;
    collectProperties(fixture.classes, fixture.widgetClass, properties);
    for (luaug::core::usize index = 0; index < properties.size(); ++index) {
        CHECK(fixture.classes.propertySlot(fixture.widgetClass, properties[index]->name) ==
              static_cast<luaug::core::u16>(index));
    }

    // The base is not given its child's members, which is the other way the
    // ancestry walk can be wrong.
    const std::vector<std::string> baseExpected{"Owner", "Flag", "Locked", "Sealed"};
    CHECK(propertyNames(fixture, fixture.thingClass) == baseExpected);
}

TEST_CASE("every ValueType the registry can hold gets a widget and a rendering")
{
    Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId subject = fixture.widget(world, "Subject");

    struct Row
    {
        scene::ValueType type;
        EditorKind editor;
        scene::Value sample;
    };

    // One row per alternative, and the static_assert above is what stops this
    // list quietly falling behind the variant.
    const std::vector<Row> rows{
        {scene::ValueType::Nil, EditorKind::ReadOnlyText, scene::Value{}},
        {scene::ValueType::Bool, EditorKind::Checkbox, scene::Value{true}},
        {scene::ValueType::Number, EditorKind::Number, scene::Value{core::f64{1.5}}},
        {scene::ValueType::String, EditorKind::Text, scene::Value{std::string("hello")}},
        {scene::ValueType::Vector3, EditorKind::Vector3, scene::Value{core::Vec3{1.0f, 2.0f, 3.0f}}},
        {scene::ValueType::CFrame, EditorKind::CFrame, scene::Value{core::CFrameD{}}},
        {scene::ValueType::Color3, EditorKind::Color, scene::Value{core::Color3{0.25f, 0.5f, 0.75f}}},
        {scene::ValueType::Instance, EditorKind::InstanceRef, scene::Value{subject}},
        {scene::ValueType::EnumItem, EditorKind::EnumCombo, scene::Value{scene::EnumValue{fixture.moodEnum, 7}}},
        {scene::ValueType::Vector2, EditorKind::Vector2, scene::Value{core::Vec2{4.0f, 5.0f}}},
        {scene::ValueType::UDim, EditorKind::UDim, scene::Value{core::UDim{0.5f, -8.0f}}},
        {scene::ValueType::UDim2, EditorKind::UDim2,
         scene::Value{core::UDim2{core::UDim{0.5f, -8.0f}, core::UDim{1.0f, 12.0f}}}},
        {scene::ValueType::Rect, EditorKind::Rect,
         scene::Value{core::Rect{core::Vec2{1.0f, 2.0f}, core::Vec2{3.0f, 4.0f}}}},
    };

    std::vector<std::string> renderings;
    for (const Row& row : rows) {
        CAPTURE(scene::valueTypeName(row.type));

        // The mapping is per type and not a shrug: a `ValueType` that fell back
        // to the read-only floor by accident would render, and would render as
        // a field nobody can edit for the rest of the milestone.
        CHECK(editorFor(row.type) == row.editor);

        // And every one of them renders SOMETHING. This is the half of risk 6
        // that a missing case would otherwise hide.
        const std::string text = formatValue(world, row.sample);
        CHECK_FALSE(text.empty());
        renderings.push_back(text);
    }

    // Distinct, because nine types all rendering as the same placeholder would
    // satisfy "renders something" while showing nothing.
    for (luaug::core::usize a = 0; a < renderings.size(); ++a) {
        for (luaug::core::usize b = a + 1; b < renderings.size(); ++b)
            CHECK(renderings[a] != renderings[b]);
    }
}

TEST_CASE("a write typed during a frame is not visible until the drain")
{
    Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId subject = fixture.widget(world, "Subject");

    Inspector inspector;
    inspector.select(subject);
    inspector.enqueue(subject, fixture.atom("Count"), scene::Value{core::f64{7.0}});

    // Decision 15: the panel draws after the tick, so applying where the value
    // was typed would mutate the world after the tick the drawn frame came
    // from. The world must still hold the old value here.
    CHECK(inspector.pendingCount() == 1);
    CHECK(world.getProperty(subject, fixture.atom("Count")) == scene::Value{core::f64{0.0}});

    // ... and the FrameStart drain is what makes it true.
    inspector.applyPending(world);
    CHECK(inspector.pendingCount() == 0);
    CHECK(world.getProperty(subject, fixture.atom("Count")) == scene::Value{core::f64{7.0}});

    REQUIRE(inspector.outcomes().size() == 1);
    CHECK(inspector.outcomes()[0].result == SetResult::Changed);
}

TEST_CASE("a readOnly property is refused, and the panel does not offer the field")
{
    Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId subject = fixture.widget(world, "Subject");

    std::vector<const scene::PropertyDesc*> properties;
    collectProperties(fixture.classes, fixture.widgetClass, properties);

    const scene::PropertyDesc* locked = nullptr;
    const scene::PropertyDesc* sealed = nullptr;
    const scene::PropertyDesc* count = nullptr;
    for (const scene::PropertyDesc* descriptor : properties) {
        if (fixture.atoms.text(descriptor->name) == "Locked")
            locked = descriptor;
        if (fixture.atoms.text(descriptor->name) == "Sealed")
            sealed = descriptor;
        if (fixture.atoms.text(descriptor->name) == "Count")
            count = descriptor;
    }
    REQUIRE(locked != nullptr);
    REQUIRE(sealed != nullptr);
    REQUIRE(count != nullptr);

    // Honoured in the UI: the same `ValueType` as `Count`, and no widget.
    CHECK(locked->type == count->type);
    CHECK(editable(*count));
    CHECK_FALSE(editable(*locked));

    // `Sealed` carries a working setter and is read-only anyway, which is the
    // case a panel checking only `set == nullptr` gets wrong -- and gets wrong
    // invisibly, because the generator does not currently emit one. A mutation
    // that deleted the `readOnly` check survived the version of this test that
    // had only `Locked` in it.
    REQUIRE(sealed->set != nullptr);
    CHECK_FALSE(editable(*sealed));

    // And honoured by the world, which is the half that cannot be bypassed. A
    // panel that poked the component would have written both either way.
    Inspector inspector;
    inspector.enqueue(subject, locked->name, scene::Value{core::f64{99.0}});
    inspector.enqueue(subject, sealed->name, scene::Value{core::f64{99.0}});
    inspector.applyPending(world);

    REQUIRE(inspector.outcomes().size() == 2);
    CHECK(inspector.outcomes()[0].result == SetResult::ReadOnly);
    CHECK(inspector.outcomes()[1].result == SetResult::ReadOnly);
    CHECK(world.getProperty(subject, locked->name) == scene::Value{core::f64{42.0}});
    CHECK(world.getProperty(subject, sealed->name) == scene::Value{core::f64{13.0}});
}

TEST_CASE("each SetResult is reported distinctly")
{
    Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId subject = fixture.widget(world, "Subject");

    Inspector inspector;
    inspector.enqueue(subject, fixture.atom("Count"), scene::Value{core::f64{3.0}});    // Changed
    inspector.enqueue(subject, fixture.atom("Count"), scene::Value{core::f64{3.0}});    // Unchanged
    inspector.enqueue(subject, fixture.atom("Nonesuch"), scene::Value{core::f64{1.0}}); // UnknownProperty
    inspector.enqueue(subject, fixture.atom("Locked"), scene::Value{core::f64{1.0}});   // ReadOnly
    inspector.enqueue(subject, fixture.atom("Count"), scene::Value{core::f64{-1.0}});   // InvalidValue
    inspector.applyPending(world);

    REQUIRE(inspector.outcomes().size() == 5);

    const std::vector<SetResult> expected{
        SetResult::Changed,  SetResult::Unchanged,    SetResult::UnknownProperty,
        SetResult::ReadOnly, SetResult::InvalidValue,
    };
    for (luaug::core::usize index = 0; index < expected.size(); ++index)
        CHECK(inspector.outcomes()[index].result == expected[index]);

    // Reported distinctly, not merely distinguished. Two outcomes sharing a
    // label read as one thing happening twice, which is exactly what "nothing
    // happened" and "the engine refused you" must not look like.
    std::vector<std::string> labels;
    for (const SetResult result : expected)
        labels.emplace_back(setResultLabel(result));
    for (luaug::core::usize a = 0; a < labels.size(); ++a) {
        CHECK_FALSE(labels[a].empty());
        for (luaug::core::usize b = a + 1; b < labels.size(); ++b)
            CHECK(labels[a] != labels[b]);
    }
}

TEST_CASE("the tree is document order and the panel does not sort it")
{
    Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);

    // Named so that parenting order and alphabetical order disagree at every
    // level: a sorted view would pass a test built from names that already
    // happened to be in order.
    const core::InstanceId root = fixture.widget(world, "Root");
    const core::InstanceId zulu = fixture.widget(world, "Zulu");
    const core::InstanceId alpha = fixture.widget(world, "Alpha");
    const core::InstanceId yankee = fixture.widget(world, "Yankee");
    const core::InstanceId bravo = fixture.widget(world, "Bravo");

    CHECK_FALSE(world.setParent(zulu, root).has_value());
    CHECK_FALSE(world.setParent(alpha, root).has_value());
    CHECK_FALSE(world.setParent(yankee, zulu).has_value());
    CHECK_FALSE(world.setParent(bravo, zulu).has_value());

    std::vector<TreeRow> rows;
    collectTree(world, root, rows);

    const std::vector<core::InstanceId> expectedIds{root, zulu, yankee, bravo, alpha};
    const std::vector<luaug::core::u32> expectedDepths{0, 1, 2, 2, 1};

    REQUIRE(rows.size() == expectedIds.size());
    for (luaug::core::usize index = 0; index < rows.size(); ++index) {
        CHECK(rows[index].id == expectedIds[index]);
        CHECK(rows[index].depth == expectedDepths[index]);
    }

    // The same order `GetDescendants` promises, asserted against the world's
    // own walk rather than against the list above -- so a change to document
    // order shows up here as a disagreement instead of as two tests to update.
    std::vector<core::InstanceId> descendants;
    world.collectDescendants(root, descendants);
    REQUIRE(descendants.size() + 1 == rows.size());
    for (luaug::core::usize index = 0; index < descendants.size(); ++index)
        CHECK(rows[index + 1].id == descendants[index]);
}

TEST_CASE("a reload drops the selection and everything queued against the old world")
{
    Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId subject = fixture.widget(world, "Subject");

    Inspector inspector;
    inspector.select(subject);
    inspector.enqueue(subject, fixture.atom("Count"), scene::Value{core::f64{5.0}});

    // A reload rebuilds the world and slot indices restart from zero, so an id
    // minted by the outgoing world resolves in the new one -- to whatever
    // moved into the same slot. Replaying the queue would write there.
    inspector.onWorldChanged();

    CHECK(inspector.pendingCount() == 0);
    CHECK_FALSE(inspector.selection().valid());
    CHECK(inspector.outcomes().empty());

    inspector.applyPending(world);
    CHECK(world.getProperty(subject, fixture.atom("Count")) == scene::Value{core::f64{0.0}});
}

TEST_CASE("the outcome history is bounded")
{
    Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId subject = fixture.widget(world, "Subject");

    Inspector inspector;
    for (luaug::core::usize index = 0; index < Inspector::OutcomeHistory * 3; ++index)
        inspector.enqueue(subject, fixture.atom("Count"), scene::Value{static_cast<core::f64>(index + 1)});
    inspector.applyPending(world);

    // A panel is not a log: an unbounded history is memory a debug overlay
    // grows for as long as the session runs.
    CHECK(inspector.outcomes().size() == Inspector::OutcomeHistory);

    // Newest last, so the drop happens at the front. The final write is the one
    // the world is holding, and it must also be the one the log ends with.
    CHECK(world.getProperty(subject, fixture.atom("Count")) ==
          scene::Value{static_cast<core::f64>(Inspector::OutcomeHistory * 3)});
    CHECK(inspector.outcomes().back().result == SetResult::Changed);
}

TEST_CASE("a property that is stored and unread says so")
{
    // The mechanism M4.5 adds, and the reason it exists: all three of this
    // project's unbacked-behaviour defects were found by a human changing a
    // value here and watching nothing happen, while the engine behaved exactly
    // as designed and the panel implied otherwise.
    scene::PropertyDesc plain;
    CHECK(propertyTag(plain) == nullptr);

    scene::PropertyDesc readOnly;
    readOnly.readOnly = true;
    CHECK(std::string_view(propertyTag(readOnly)) == "(ro)");

    scene::PropertyDesc inert;
    inert.inert = true;
    CHECK(std::string_view(propertyTag(inert)) == "(stored)");

    // Still editable: it accepts the write, keeps it, and reads it back. What it
    // does not do is act on it, and that is the whole distinction from readOnly.
    inert.type = scene::ValueType::Bool;
    inert.set = [](scene::World&, core::InstanceId, const scene::Value&) { return true; };
    CHECK(editable(inert));

    // Read-only wins when a property is somehow both: "you cannot change this"
    // is the more useful thing to say.
    scene::PropertyDesc both;
    both.readOnly = true;
    both.inert = true;
    CHECK(std::string_view(propertyTag(both)) == "(ro)");
}
