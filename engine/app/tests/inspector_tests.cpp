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
#include "luaug/scene/enum_registry.h"
#include "luaug/scene/value.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <doctest/doctest.h>
#include <limits>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "inspector_fixture.h"

using luaug::app::coalesceKeyFor;
using luaug::app::collectCommonProperties;
using luaug::app::collectProperties;
using luaug::app::collectTree;
using luaug::app::editable;
using luaug::app::editorFor;
using luaug::app::EditorKind;
using luaug::app::enumDomainOf;
using luaug::app::formatValue;
using luaug::app::Inspector;
using luaug::app::propertyTag;
using luaug::app::sameValue;
using luaug::app::selectVisibleRange;
using luaug::app::setResultLabel;
using luaug::app::SharedState;
using luaug::app::sharedValue;
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

TEST_CASE("an enum property's item set comes from the descriptor, with no instance in hand")
{
    Fixture fixture;

    // No `World`, no instance, no value: an editor's property grid populates a
    // combo box before anything of the class exists, and reading the enum off a
    // property's CURRENT VALUE could never answer that -- nor answer at all for
    // a property whose value is unset.
    const scene::PropertyDesc* mood = fixture.classes.findProperty(fixture.widgetClass, fixture.atom("Mood"));
    REQUIRE(mood != nullptr);
    REQUIRE(mood->type == scene::ValueType::EnumItem);

    const scene::EnumId domain = enumDomainOf(fixture.enums, *mood);
    CHECK(domain == fixture.moodEnum);

    const scene::EnumDescriptor* enumDescriptor = fixture.enums.find(domain);
    REQUIRE(enumDescriptor != nullptr);

    // Declaration order and the declared values, not indices: `Cross` is 7.
    std::vector<std::string> items;
    std::vector<int> values;
    for (const scene::EnumItemDesc& item : enumDescriptor->items) {
        items.emplace_back(fixture.atoms.text(item.name));
        values.push_back(static_cast<int>(item.value));
    }
    CHECK(items == std::vector<std::string>{"Calm", "Cross"});
    CHECK(values == std::vector<int>{0, 7});
}

TEST_CASE("a property that is not an enum, or names an enum nobody registered, has no domain")
{
    Fixture fixture;

    const scene::PropertyDesc* count = fixture.classes.findProperty(fixture.widgetClass, fixture.atom("Count"));
    REQUIRE(count != nullptr);
    CHECK(enumDomainOf(fixture.enums, *count) == scene::InvalidEnum);

    // An `EnumItem` property whose descriptor names nothing -- which a
    // hand-built one is allowed to be. The answer is "no domain", not the first
    // enum registered, because an `EnumId` of 0 is what a zero-initialised
    // `EnumValue` already means.
    scene::PropertyDesc unnamed;
    unnamed.type = scene::ValueType::EnumItem;
    CHECK(enumDomainOf(fixture.enums, unnamed) == scene::InvalidEnum);

    // And a name this registry does not know is a miss rather than a guess.
    scene::PropertyDesc stranger;
    stranger.type = scene::ValueType::EnumItem;
    stranger.enumName = fixture.atom("Weather");
    CHECK(enumDomainOf(fixture.enums, stranger) == scene::InvalidEnum);
}

TEST_CASE("a property's documentation reaches the runtime, through the inherited sweep as well")
{
    Fixture fixture;

    std::vector<const scene::PropertyDesc*> properties;
    collectProperties(fixture.classes, fixture.widgetClass, properties);

    std::unordered_map<std::string, std::string> docs;
    for (const scene::PropertyDesc* descriptor : properties) {
        // Never null, so a panel can print it without a guard.
        REQUIRE(descriptor->doc != nullptr);
        docs.emplace(std::string(fixture.atoms.text(descriptor->name)), std::string(descriptor->doc));
    }

    // Declared on the leaf, and declared on the base and reached through the
    // ancestry walk -- the tooltip has to work for `Instance.Name` too.
    CHECK(docs.at("Mood") == "How the widget feels about being inspected.");
    CHECK(docs.at("Flag") == "Whether the thing is flagged.");

    // A descriptor that carries none reads as empty, not as a null pointer.
    CHECK(docs.at("Nothing").empty());
    CHECK(std::string_view(scene::PropertyDesc{}.doc).empty());
}

// --- E2: the selection is a set, and the primary is the last thing clicked ---

TEST_CASE("a selection holds several instances, newest last")
{
    Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId a = fixture.widget(world, "A");
    const core::InstanceId b = fixture.widget(world, "B");
    const core::InstanceId c = fixture.widget(world, "C");

    Inspector inspector;
    inspector.select(a);
    CHECK(inspector.selectionCount() == 1);
    CHECK(inspector.selection() == a);

    inspector.add(b);
    inspector.add(c);
    CHECK(inspector.selectionCount() == 3);
    // The primary is what a manipulator anchors to and what the properties grid
    // shows, so it has to be the one somebody last touched.
    CHECK(inspector.selection() == c);
    CHECK(inspector.isSelected(a));
    CHECK(inspector.isSelected(b));

    // Adding something already selected promotes it rather than duplicating it,
    // which is how you choose the anchor without losing the rest.
    inspector.add(a);
    CHECK(inspector.selectionCount() == 3);
    CHECK(inspector.selection() == a);

    // A plain click replaces.
    inspector.select(b);
    CHECK(inspector.selectionCount() == 1);
    CHECK(inspector.selection() == b);

    // And an invalid id is the deselect every caller already spelled as
    // `select({})`.
    inspector.select(core::InstanceId{});
    CHECK(inspector.selectionCount() == 0);
    CHECK_FALSE(inspector.selection().valid());
}

TEST_CASE("ctrl-click adds and removes, and removing the primary promotes the one before it")
{
    Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId a = fixture.widget(world, "A");
    const core::InstanceId b = fixture.widget(world, "B");

    Inspector inspector;
    inspector.toggle(a);
    inspector.toggle(b);
    CHECK(inspector.selectionCount() == 2);
    CHECK(inspector.selection() == b);

    inspector.toggle(b);
    CHECK(inspector.selectionCount() == 1);
    CHECK(inspector.selection() == a);

    inspector.toggle(a);
    CHECK(inspector.selectionCount() == 0);
}

TEST_CASE("pruning drops what the world no longer has, including a selected child of a deleted parent")
{
    Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId parent = fixture.widget(world, "Parent");
    const core::InstanceId child = fixture.widget(world, "Child");
    const core::InstanceId other = fixture.widget(world, "Other");
    REQUIRE_FALSE(world.setParent(child, parent).has_value());

    Inspector inspector;
    inspector.select(other);
    inspector.add(child);

    // **This is the case the old check could not see.** It compared the
    // selection against the id being deleted; the child was never that id, so
    // it stayed selected and pointing at an instance the world had retired.
    REQUIRE(world.destroy(parent));
    world.retireDestroyed();

    inspector.pruneDead(world);
    CHECK(inspector.selectionCount() == 1);
    CHECK(inspector.selection() == other);
}

TEST_CASE("shift-click takes the run between two visible rows and keeps the anchor primary")
{
    Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId root = fixture.widget(world, "Root");
    std::vector<core::InstanceId> made;
    for (int index = 0; index < 5; ++index) {
        const core::InstanceId id = fixture.widget(world, "Row");
        REQUIRE_FALSE(world.setParent(id, root).has_value());
        made.push_back(id);
    }

    std::vector<TreeRow> rows;
    collectTree(world, root, rows);
    REQUIRE(rows.size() == 6);

    Inspector inspector;
    selectVisibleRange(inspector, rows, made[1], made[3]);
    CHECK(inspector.selectionCount() == 3);
    CHECK(inspector.isSelected(made[1]));
    CHECK(inspector.isSelected(made[2]));
    CHECK(inspector.isSelected(made[3]));
    // The anchor stays primary: it is what the NEXT shift-click extends from,
    // and a range that promoted its far end would walk the anchor along with
    // every click.
    CHECK(inspector.selection() == made[1]);

    // Backwards is the same range.
    selectVisibleRange(inspector, rows, made[3], made[1]);
    CHECK(inspector.selectionCount() == 3);
    CHECK(inspector.selection() == made[3]);

    // A row that is not visible -- a collapsed branch makes this ordinary --
    // leaves the selection alone rather than guessing.
    inspector.select(made[0]);
    const core::InstanceId hidden = fixture.widget(world, "Hidden");
    selectVisibleRange(inspector, rows, made[0], hidden);
    CHECK(inspector.selectionCount() == 1);
    CHECK(inspector.selection() == made[0]);
}

// --- E2: a drag is one edit --------------------------------------------------

TEST_CASE("an open gesture is the undo key, however many writes it made")
{
    Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId a = fixture.widget(world, "A");
    const core::InstanceId b = fixture.widget(world, "B");

    Inspector inspector;
    const core::u64 gesture = inspector.beginGesture();
    CHECK(gesture != 0);
    // Nested is the same gesture: a widget inside a drag is still that drag.
    CHECK(inspector.beginGesture() == gesture);

    inspector.enqueue(a, fixture.atom("Count"), scene::Value{core::f64{1.0}});
    inspector.enqueue(b, fixture.atom("Count"), scene::Value{core::f64{2.0}});
    inspector.enqueue(b, fixture.atom("Flag"), scene::Value{true});

    // Three writes, two instances, two properties -- and one key, which is the
    // whole reason this exists. The old rule returned zero here, and zero never
    // coalesces, so this frame would have been its own undo step.
    CHECK(coalesceKeyFor(inspector.gesture(), inspector.pending()) == gesture);

    inspector.endGesture();
    CHECK(inspector.gesture() == 0);

    // And a second drag is a second key, so undoing it does not also undo the
    // first -- which the property-derived key got wrong.
    const core::u64 again = inspector.beginGesture();
    CHECK(again != gesture);
}

TEST_CASE("without a gesture the key is the old rule, so nothing that never opted in changed")
{
    Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId a = fixture.widget(world, "A");
    const core::InstanceId b = fixture.widget(world, "B");
    const core::NameAtom count = fixture.atom("Count");
    const core::NameAtom flag = fixture.atom("Flag");

    Inspector inspector;
    CHECK(coalesceKeyFor(0, inspector.pending()) == 0);

    inspector.enqueue(a, count, scene::Value{core::f64{1.0}});
    const core::u64 one = coalesceKeyFor(0, inspector.pending());
    CHECK(one != 0);

    // Same instance, same property, twice in a frame: still one edit.
    inspector.enqueue(a, count, scene::Value{core::f64{2.0}});
    CHECK(coalesceKeyFor(0, inspector.pending()) == one);

    // A second property in the same frame is not one edit, and zero says so.
    inspector.enqueue(a, flag, scene::Value{true});
    CHECK(coalesceKeyFor(0, inspector.pending()) == 0);

    // A different instance is a different key, so a drag on one part does not
    // swallow the edit to another.
    Inspector second;
    second.enqueue(b, count, scene::Value{core::f64{1.0}});
    CHECK(coalesceKeyFor(0, second.pending()) != one);
}

TEST_CASE("a reload closes an open gesture")
{
    Fixture fixture;
    Inspector inspector;
    (void)inspector.beginGesture();
    inspector.onWorldChanged();
    // Left open, the next unrelated edit would coalesce into whatever was being
    // dragged in a world that no longer exists.
    CHECK(inspector.gesture() == 0);
}

// --- Properties over a selection of more than one -----------------------------
//
// The panel is one function that cannot be called without a window, so what it
// DECIDES lives here: which rows a mixed selection has, and what each of those
// rows holds. Everything below is the difference between an editor that edits
// forty parts and one that edits forty parts by accident.

namespace {

[[nodiscard]] std::vector<std::string> commonNames(Fixture& fixture, scene::World& world,
                                                   std::span<const core::InstanceId> targets)
{
    std::vector<const scene::PropertyDesc*> properties;
    collectCommonProperties(world, targets, properties);

    std::vector<std::string> names;
    names.reserve(properties.size());
    for (const scene::PropertyDesc* property : properties)
        names.emplace_back(fixture.atoms.text(property->name));
    return names;
}

} // namespace

TEST_CASE("one instance has exactly the properties of its class")
{
    Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId a = fixture.widget(world, "A");

    // The single-selection case is the multi-selection case with one member,
    // and it has to stay identical -- the panel now takes only this path.
    const core::InstanceId targets[] = {a};
    CHECK(commonNames(fixture, world, targets) == propertyNames(fixture, fixture.widgetClass));
}

TEST_CASE("a selection of one class is that class, however many are in it")
{
    Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId targets[] = {fixture.widget(world, "A"), fixture.widget(world, "B"),
                                        fixture.widget(world, "C")};

    CHECK(commonNames(fixture, world, targets) == propertyNames(fixture, fixture.widgetClass));
}

TEST_CASE("a base and a derived instance share the base's properties")
{
    Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId thing = world.create(fixture.thingClass);
    const core::InstanceId widget = fixture.widget(world, "W");

    const core::InstanceId targets[] = {widget, thing};
    const std::vector<std::string> names = commonNames(fixture, world, targets);

    // The order is the FIRST target's -- the widget's -- and the widget carries
    // the inherited four first, so this is also the assertion that the rows do
    // not reshuffle when the set grows.
    CHECK(names == propertyNames(fixture, fixture.thingClass));

    // And the other way round is the same answer: an intersection has no
    // preferred member.
    const core::InstanceId reversed[] = {thing, widget};
    CHECK(commonNames(fixture, world, reversed) == names);
}

TEST_CASE("a property two classes declare with different types is not a row")
{
    Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId widget = fixture.widget(world, "W");
    const core::InstanceId gadget = fixture.gadget(world, "G");

    const core::InstanceId targets[] = {widget, gadget};
    const std::vector<std::string> names = commonNames(fixture, world, targets);

    // `Count` is a number on the widget and a string on the gadget. One row
    // cannot edit both: the widget branch reaches for `f64` with `std::get`,
    // which throws on the gadget's string.
    CHECK(std::find(names.begin(), names.end(), "Count") == names.end());
    CHECK(std::find(names.begin(), names.end(), "Flag") != names.end());
}

TEST_CASE("a property read-only on any member is read-only for the selection")
{
    Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId widget = fixture.widget(world, "W");
    const core::InstanceId gadget = fixture.gadget(world, "G");

    std::vector<const scene::PropertyDesc*> properties;
    const core::NameAtom flag = fixture.atom("Flag");
    const auto findFlag = [&properties, flag]() -> const scene::PropertyDesc* {
        for (const scene::PropertyDesc* property : properties) {
            if (property->name == flag)
                return property;
        }
        return nullptr;
    };

    // The widget's `Flag` is writable and the gadget's is not. Alone, the widget
    // gets a live checkbox.
    const core::InstanceId alone[] = {widget};
    collectCommonProperties(world, alone, properties);
    REQUIRE(findFlag() != nullptr);
    CHECK(editable(*findFlag()));

    // Together, it does not -- because a drag the world refuses for one of the
    // two is a claim the panel cannot keep.
    const core::InstanceId together[] = {widget, gadget};
    collectCommonProperties(world, together, properties);
    REQUIRE(findFlag() != nullptr);
    CHECK_FALSE(editable(*findFlag()));
}

TEST_CASE("a dead member of a selection is skipped rather than answered for")
{
    Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId widget = fixture.widget(world, "W");
    const core::InstanceId gadget = fixture.gadget(world, "G");
    world.destroy(gadget);
    // The handle stops resolving at the end of the drain, not where destroy is
    // called -- which is the world's own rule and not this test being careful.
    world.retireDestroyed();

    // What is left is one widget, so the rows are the widget's -- not the
    // intersection with a class the world has retired.
    const core::InstanceId targets[] = {widget, gadget};
    CHECK(commonNames(fixture, world, targets) == propertyNames(fixture, fixture.widgetClass));

    // And nothing alive at all is no rows, not a crash and not the first id's.
    const core::InstanceId dead[] = {gadget};
    CHECK(commonNames(fixture, world, dead).empty());
    CHECK(commonNames(fixture, world, {}).empty());
}

TEST_CASE("a shared value says same, mixed or unreadable")
{
    Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId a = fixture.widget(world, "A");
    const core::InstanceId b = fixture.widget(world, "B");
    const core::NameAtom count = fixture.atom("Count");

    REQUIRE(world.setProperty(a, count, scene::Value{core::f64{4.0}}) == SetResult::Changed);
    REQUIRE(world.setProperty(b, count, scene::Value{core::f64{4.0}}) == SetResult::Changed);

    const core::InstanceId targets[] = {a, b};
    luaug::app::SharedValue shared = sharedValue(world, targets, count);
    CHECK(shared.state == SharedState::Same);
    CHECK(std::get<core::f64>(shared.value) == doctest::Approx(4.0));

    REQUIRE(world.setProperty(b, count, scene::Value{core::f64{9.0}}) == SetResult::Changed);
    shared = sharedValue(world, targets, count);
    CHECK(shared.state == SharedState::Mixed);
    // The FIRST live target's, so a drag has somewhere to start.
    CHECK(std::get<core::f64>(shared.value) == doctest::Approx(4.0));

    // A property the class does not declare is not readable, and one member is
    // enough: a row that edited three instances of four would have an effect
    // nobody could predict.
    CHECK(sharedValue(world, targets, fixture.atom("Nonesuch")).state == SharedState::Unreadable);
    CHECK(sharedValue(world, {}, count).state == SharedState::Unreadable);
}

TEST_CASE("two NaNs are not a disagreement")
{
    // `operator==` says they differ, and a `Number` left at NaN would then read
    // as mixed across a selection holding exactly the same thing.
    const scene::Value nan{std::numeric_limits<core::f64>::quiet_NaN()};
    CHECK(sameValue(nan, nan));
    CHECK(sameValue(scene::Value{core::f64{1.0}}, scene::Value{core::f64{1.0}}));
    CHECK_FALSE(sameValue(scene::Value{core::f64{1.0}}, scene::Value{core::f64{2.0}}));
    // Zero and minus zero ARE the same value, which is what `==` says and what
    // a bitwise comparison would have got wrong.
    CHECK(sameValue(scene::Value{core::f64{0.0}}, scene::Value{core::f64{-0.0}}));
    CHECK_FALSE(sameValue(scene::Value{core::f64{1.0}}, scene::Value{std::string("1")}));
}
