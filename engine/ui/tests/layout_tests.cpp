#include "luaug/scene/world.h"
#include "luaug/ui/scene_types.h"
#include "luaug/ui/ui.h"

#include <doctest/doctest.h>
#include <optional>

#include "class_descriptors.gen.h"

namespace {

namespace core = luaug::core;
namespace scene = luaug::scene;
namespace ui = luaug::ui;

using core::InstanceId;
using core::Vec2;

struct Fixture
{
    core::AtomTable atoms;
    scene::ClassRegistry classes;
    scene::EnumRegistry enums;
    std::optional<scene::World> world;
    InstanceId service;

    Fixture()
    {
        scene::generated::registerClasses(classes, atoms);
        ui::registerSceneTypes(classes, atoms);
        scene::generated::registerEnums(enums, atoms);
        world.emplace(classes, enums, atoms, 1u);
        service = make("UIService");
        ui::resetLayoutStats();
    }

    InstanceId make(const char* className)
    {
        const scene::ClassId id = classes.findId(atoms.intern(className));
        REQUIRE(id != scene::InvalidClass);
        return world->create(id);
    }

    InstanceId child(const char* className, InstanceId parent)
    {
        const InstanceId id = make(className);
        REQUIRE_FALSE(world->setParent(id, parent).has_value());
        return id;
    }

    [[nodiscard]] scene::UIObjectComponent& object(InstanceId id)
    {
        scene::UIObjectComponent* component = world->uiObjects().find(id);
        REQUIRE(component != nullptr);
        return *component;
    }

    void dirty(InstanceId screen) { world->screenGuis().find(screen)->layoutDirty = true; }

    void run(Vec2 windowSize = Vec2{1280.0f, 720.0f}) { ui::layout(*world, service, windowSize); }
};

} // namespace

TEST_CASE("a UDim2 rectangle is arithmetic, and this is the arithmetic")
{
    Fixture fixture;
    const InstanceId screen = fixture.child("ScreenGui", fixture.service);
    const InstanceId frame = fixture.child("Frame", screen);

    // Half the window across, 200 pixels down, offset in by 10 and 20.
    fixture.object(frame).size = core::UDim2{core::UDim{0.5f, 0.0f}, core::UDim{0.0f, 200.0f}};
    fixture.object(frame).position = core::UDim2{core::UDim{0.0f, 10.0f}, core::UDim{0.0f, 20.0f}};
    fixture.run();

    CHECK(fixture.object(frame).absoluteSize.x == doctest::Approx(640.0));
    CHECK(fixture.object(frame).absoluteSize.y == doctest::Approx(200.0));
    CHECK(fixture.object(frame).absolutePosition.x == doctest::Approx(10.0));
    CHECK(fixture.object(frame).absolutePosition.y == doctest::Approx(20.0));
}

TEST_CASE("AnchorPoint places a fraction of the element itself")
{
    Fixture fixture;
    const InstanceId screen = fixture.child("ScreenGui", fixture.service);
    const InstanceId frame = fixture.child("Frame", screen);

    fixture.object(frame).size = core::UDim2{core::UDim{0.0f, 100.0f}, core::UDim{0.0f, 50.0f}};
    fixture.object(frame).position = core::UDim2{core::UDim{0.5f, 0.0f}, core::UDim{0.5f, 0.0f}};
    fixture.object(frame).anchorPoint = Vec2{0.5f, 0.5f};
    fixture.run();

    // Centred: the middle of the element on the middle of the window. This is
    // one of the two cases ADR 0040 says Clay could not express -- its floating
    // attachment takes corner and centre enumerators, and (0.5, 0.5) is the one
    // fraction they happen to have a name for.
    CHECK(fixture.object(frame).absolutePosition.x == doctest::Approx(640.0 - 50.0));
    CHECK(fixture.object(frame).absolutePosition.y == doctest::Approx(360.0 - 25.0));

    // And a fraction with no name works the same way, which is the rest of it.
    fixture.object(frame).anchorPoint = Vec2{0.3f, 0.7f};
    fixture.dirty(screen);
    fixture.run();
    CHECK(fixture.object(frame).absolutePosition.x == doctest::Approx(640.0 - 30.0));
    CHECK(fixture.object(frame).absolutePosition.y == doctest::Approx(360.0 - 35.0));
}

TEST_CASE("a scale past 1 is legal, which is the other thing Clay could not do")
{
    Fixture fixture;
    const InstanceId screen = fixture.child("ScreenGui", fixture.service);
    const InstanceId frame = fixture.child("Frame", screen);

    fixture.object(frame).size = core::UDim2{core::UDim{1.5f, 0.0f}, core::UDim{2.0f, 0.0f}};
    fixture.run();

    CHECK(fixture.object(frame).absoluteSize.x == doctest::Approx(1920.0));
    CHECK(fixture.object(frame).absoluteSize.y == doctest::Approx(1440.0));
}

TEST_CASE("the same layout scales with the window, which is what UDim2 is for")
{
    Fixture fixture;
    const InstanceId screen = fixture.child("ScreenGui", fixture.service);
    const InstanceId frame = fixture.child("Frame", screen);
    fixture.object(frame).size = core::UDim2{core::UDim{0.5f, 0.0f}, core::UDim{0.25f, 0.0f}};

    fixture.run(Vec2{1280.0f, 720.0f});
    CHECK(fixture.object(frame).absoluteSize.x == doctest::Approx(640.0));

    fixture.dirty(screen);
    fixture.run(Vec2{640.0f, 360.0f});
    CHECK(fixture.object(frame).absoluteSize.x == doctest::Approx(320.0));
    CHECK(fixture.object(frame).absoluteSize.y == doctest::Approx(90.0));
}

TEST_CASE("a screen nothing changed runs no solver")
{
    Fixture fixture;
    const InstanceId screen = fixture.child("ScreenGui", fixture.service);
    fixture.child("Frame", screen);

    fixture.run();
    const core::u64 first = ui::layoutStats().solverRuns;
    CHECK(first == 1);

    // The claim the milestone's benchmark makes, as a COUNTER rather than a
    // duration: at this scale a timing assertion measures the clock, and
    // "about zero microseconds" is the shape of gate that passes while doing
    // nothing.
    fixture.run();
    fixture.run();
    CHECK(ui::layoutStats().solverRuns == first);
}

TEST_CASE("a vertical list stacks its children with the padding between them")
{
    Fixture fixture;
    const InstanceId screen = fixture.child("ScreenGui", fixture.service);
    const InstanceId panel = fixture.child("Frame", screen);
    fixture.object(panel).size = core::UDim2{core::UDim{0.0f, 400.0f}, core::UDim{0.0f, 400.0f}};

    const InstanceId list = fixture.child("UIListLayout", panel);
    fixture.world->listLayouts().find(list)->padding = core::UDim{0.0f, 8.0f};

    const InstanceId a = fixture.child("Frame", panel);
    const InstanceId b = fixture.child("Frame", panel);
    fixture.object(a).size = core::UDim2{core::UDim{0.0f, 100.0f}, core::UDim{0.0f, 30.0f}};
    fixture.object(b).size = core::UDim2{core::UDim{0.0f, 100.0f}, core::UDim{0.0f, 40.0f}};
    fixture.run();

    CHECK(fixture.object(a).absolutePosition.y == doctest::Approx(0.0));
    // 30 tall plus 8 of padding. The gap is BETWEEN children and not around
    // them, which is what `UIPadding` is for instead.
    CHECK(fixture.object(b).absolutePosition.y == doctest::Approx(38.0));
    // And a laid-out child does not place itself: its own `Position` is not
    // consulted, which is what a layout IS.
    CHECK(fixture.object(b).absolutePosition.x == doctest::Approx(0.0));
}

TEST_CASE("UIPadding insets the content")
{
    Fixture fixture;
    const InstanceId screen = fixture.child("ScreenGui", fixture.service);
    const InstanceId panel = fixture.child("Frame", screen);
    fixture.object(panel).size = core::UDim2{core::UDim{0.0f, 200.0f}, core::UDim{0.0f, 200.0f}};

    const InstanceId padding = fixture.child("UIPadding", panel);
    scene::UIPaddingComponent* pad = fixture.world->uiPaddings().find(padding);
    pad->paddingLeft = core::UDim{0.0f, 12.0f};
    pad->paddingTop = core::UDim{0.0f, 6.0f};

    const InstanceId inner = fixture.child("Frame", panel);
    fixture.object(inner).size = core::UDim2{core::UDim{0.0f, 50.0f}, core::UDim{0.0f, 50.0f}};
    fixture.run();

    CHECK(fixture.object(inner).absolutePosition.x == doctest::Approx(12.0));
    CHECK(fixture.object(inner).absolutePosition.y == doctest::Approx(6.0));
}

TEST_CASE("an invisible element is not laid out at all")
{
    Fixture fixture;
    const InstanceId screen = fixture.child("ScreenGui", fixture.service);
    const InstanceId frame = fixture.child("Frame", screen);
    fixture.object(frame).size = core::UDim2{core::UDim{0.0f, 100.0f}, core::UDim{0.0f, 100.0f}};
    fixture.object(frame).visible = false;
    fixture.run();

    // Not "laid out and then skipped when drawing": a hidden subtree costs
    // nothing, which is what makes hiding a menu the way to close it.
    CHECK(fixture.object(frame).absoluteSize.x == doctest::Approx(0.0));
}

TEST_CASE("the draw list is one flat ordering, ZIndex then document order")
{
    Fixture fixture;
    const InstanceId screen = fixture.child("ScreenGui", fixture.service);

    const InstanceId back = fixture.child("Frame", screen);
    const InstanceId front = fixture.child("Frame", screen);
    fixture.object(back).size = core::UDim2{core::UDim{0.0f, 10.0f}, core::UDim{0.0f, 10.0f}};
    fixture.object(front).size = core::UDim2{core::UDim{0.0f, 20.0f}, core::UDim{0.0f, 20.0f}};
    fixture.object(back).zIndex = 5.0f;
    fixture.object(front).zIndex = 1.0f;
    fixture.run();

    ui::DrawList list;
    ui::buildDrawList(*fixture.world, fixture.service, list);
    REQUIRE(list.quads.size() == 2);
    // The lower ZIndex draws first, whatever the tree order was.
    CHECK(list.quads[0].max.x == doctest::Approx(20.0));
    CHECK(list.quads[1].max.x == doctest::Approx(10.0));
}

TEST_CASE("a clip narrows rather than replacing the one already in force")
{
    Fixture fixture;
    const InstanceId screen = fixture.child("ScreenGui", fixture.service);

    const InstanceId outer = fixture.child("Frame", screen);
    fixture.object(outer).size = core::UDim2{core::UDim{0.0f, 100.0f}, core::UDim{0.0f, 100.0f}};
    fixture.object(outer).clipsDescendants = true;

    const InstanceId inner = fixture.child("Frame", outer);
    fixture.object(inner).position = core::UDim2{core::UDim{0.0f, 50.0f}, core::UDim{0.0f, 0.0f}};
    fixture.object(inner).size = core::UDim2{core::UDim{0.0f, 200.0f}, core::UDim{0.0f, 200.0f}};
    fixture.object(inner).clipsDescendants = true;
    fixture.run();

    ui::DrawList list;
    ui::buildDrawList(*fixture.world, fixture.service, list);
    // Three: the whole window, the outer clip, and the inner clip intersected
    // with it. A child that escaped its grandparent's clip is the classic
    // scrolling-list defect, and this is the case that rules it out.
    REQUIRE(list.scissors.size() == 3);
    CHECK(list.scissors[2].min.x == doctest::Approx(50.0));
    CHECK(list.scissors[2].max.x == doctest::Approx(100.0));
}

TEST_CASE("text measures wider as it gets bigger and taller as it wraps")
{
    // The built-in face is a fixed-shape vector one, so its metrics are exact
    // arithmetic rather than a rasterizer's opinion -- which is what lets a
    // headless layout be identical to a rendered one.
    const ui::TextRunMetrics small = ui::measureText("Hello", {}, 12.0f, 0.0f);
    const ui::TextRunMetrics large = ui::measureText("Hello", {}, 24.0f, 0.0f);
    CHECK(large.size.x == doctest::Approx(static_cast<double>(small.size.x) * 2.0));
    CHECK(large.size.y == doctest::Approx(static_cast<double>(small.size.y) * 2.0));
    CHECK(small.lineCount == 1);

    const ui::TextRunMetrics wrapped = ui::measureText("Hello there world", {}, 12.0f, 40.0f);
    CHECK(wrapped.lineCount > 1);
    CHECK(wrapped.size.x <= doctest::Approx(40.0));
}

// --- Cross-axis alignment (D029) ----------------------------------------------

TEST_CASE("a centred column centres against the CONTAINER, not against its widest child")
{
    // The defect a person found in the obby's menu: every child sat sixteen
    // pixels left of centre, which is half the thirty-two the children were
    // inset by. The cross-axis metric was the widest child in the line, so a
    // column of equal-width children got an offset of zero and landed on the
    // left edge -- "centred" and "flush left" were the same code path whenever
    // the children agreed about their width.
    Fixture fixture;
    const InstanceId screen = fixture.child("ScreenGui", fixture.service);
    const InstanceId panel = fixture.child("Frame", screen);
    fixture.object(panel).size = core::UDim2{core::UDim{0.0f, 400.0f}, core::UDim{0.0f, 400.0f}};

    const InstanceId list = fixture.child("UIListLayout", panel);
    scene::UIListLayoutComponent* layout = fixture.world->listLayouts().find(list);
    layout->horizontalAlignment = 1; // Center
    layout->verticalAlignment = 1;   // Center

    // Both the same width, and both narrower than the panel -- the exact shape
    // that produced the bug.
    const InstanceId a = fixture.child("Frame", panel);
    const InstanceId b = fixture.child("Frame", panel);
    for (const InstanceId child : {a, b})
        fixture.object(child).size = core::UDim2{core::UDim{0.0f, 200.0f}, core::UDim{0.0f, 50.0f}};
    fixture.run();

    // A 200-wide child in a 400-wide panel starts at 100 and its centre is the
    // panel's. Put `lineCross` back to the widest child and this reads 0.
    CHECK(fixture.object(a).absolutePosition.x == doctest::Approx(100.0));
    CHECK(fixture.object(b).absolutePosition.x == doctest::Approx(100.0));

    const auto childCentre = fixture.object(a).absolutePosition.x + fixture.object(a).absoluteSize.x * 0.5f;
    CHECK(static_cast<double>(childCentre) == doctest::Approx(200.0));
}

TEST_CASE("an End cross alignment reaches the container's far edge")
{
    // The same metric, from the other end: with the widest child as the extent,
    // "flush right" was also "flush left".
    Fixture fixture;
    const InstanceId screen = fixture.child("ScreenGui", fixture.service);
    const InstanceId panel = fixture.child("Frame", screen);
    fixture.object(panel).size = core::UDim2{core::UDim{0.0f, 400.0f}, core::UDim{0.0f, 400.0f}};

    const InstanceId list = fixture.child("UIListLayout", panel);
    fixture.world->listLayouts().find(list)->horizontalAlignment = 2; // End

    const InstanceId child = fixture.child("Frame", panel);
    fixture.object(child).size = core::UDim2{core::UDim{0.0f, 120.0f}, core::UDim{0.0f, 30.0f}};
    fixture.run();

    CHECK(fixture.object(child).absolutePosition.x == doctest::Approx(280.0));
}

TEST_CASE("a horizontal row centres its children vertically against the container")
{
    // The cross axis is the OTHER one for a horizontal list, and a fix that
    // only worked for columns would be half a fix.
    Fixture fixture;
    const InstanceId screen = fixture.child("ScreenGui", fixture.service);
    const InstanceId panel = fixture.child("Frame", screen);
    fixture.object(panel).size = core::UDim2{core::UDim{0.0f, 400.0f}, core::UDim{0.0f, 200.0f}};

    const InstanceId list = fixture.child("UIListLayout", panel);
    scene::UIListLayoutComponent* layout = fixture.world->listLayouts().find(list);
    layout->fillDirection = 0; // Horizontal
    layout->verticalAlignment = 1;

    const InstanceId child = fixture.child("Frame", panel);
    fixture.object(child).size = core::UDim2{core::UDim{0.0f, 40.0f}, core::UDim{0.0f, 60.0f}};
    fixture.run();

    CHECK(fixture.object(child).absolutePosition.y == doctest::Approx(70.0));
}

TEST_CASE("a wrapped line centres within its own band, which is a decision")
{
    // **Written rather than discovered.** With wrap, lines stack along the cross
    // axis and each occupies its own band; centring one against the whole
    // container would put every line on top of every other. Within the band is
    // the only arrangement that is still a stack -- and the case that says so is
    // here, because "what does a wrapped line centre against" is a real choice
    // and the answer must not be whatever the code happened to do.
    Fixture fixture;
    const InstanceId screen = fixture.child("ScreenGui", fixture.service);
    const InstanceId panel = fixture.child("Frame", screen);
    fixture.object(panel).size = core::UDim2{core::UDim{0.0f, 200.0f}, core::UDim{0.0f, 400.0f}};

    const InstanceId list = fixture.child("UIListLayout", panel);
    scene::UIListLayoutComponent* layout = fixture.world->listLayouts().find(list);
    layout->fillDirection = 0; // Horizontal
    layout->wraps = true;
    layout->verticalAlignment = 1; // Center, on the cross axis

    // Three 80-wide children in a 200-wide panel: two on the first line, one on
    // the second. The first line's band is as tall as its tallest child.
    const InstanceId a = fixture.child("Frame", panel);
    const InstanceId b = fixture.child("Frame", panel);
    const InstanceId c = fixture.child("Frame", panel);
    fixture.object(a).size = core::UDim2{core::UDim{0.0f, 80.0f}, core::UDim{0.0f, 60.0f}};
    fixture.object(b).size = core::UDim2{core::UDim{0.0f, 80.0f}, core::UDim{0.0f, 20.0f}};
    fixture.object(c).size = core::UDim2{core::UDim{0.0f, 80.0f}, core::UDim{0.0f, 30.0f}};
    fixture.run();

    // The band is 60 tall. `a` fills it, so it sits at the top; `b` is 20 tall
    // and centres within the band at 20 -- not at 190, which is where centring
    // against the 400-tall panel would put it.
    CHECK(fixture.object(a).absolutePosition.y == doctest::Approx(0.0));
    CHECK(fixture.object(b).absolutePosition.y == doctest::Approx(20.0));
    // And the second line is below the first rather than on top of it. Exactly
    // 60 here, because this list sets no `Padding` -- a band's height is its
    // tallest child and lines meet.
    CHECK(fixture.object(c).absolutePosition.y == doctest::Approx(60.0));
}

// --- UICorner (D030) ----------------------------------------------------------

TEST_CASE("a UICorner reaches the draw list, which is where a rounded corner is")
{
    // `CornerRadius` was declared, stored, read back and consumed by NOTHING for
    // a whole milestone -- neither the UI module nor the shader mentioned a
    // radius -- and a person found it by looking at a square button. This is the
    // consumer, and `inertcheck` is the lint that would have found the absence.
    Fixture fixture;
    const InstanceId screen = fixture.child("ScreenGui", fixture.service);
    const InstanceId panel = fixture.child("Frame", screen);
    fixture.object(panel).size = core::UDim2{core::UDim{0.0f, 200.0f}, core::UDim{0.0f, 100.0f}};

    const InstanceId corner = fixture.child("UICorner", panel);
    fixture.world->uiCorners().find(corner)->cornerRadius = core::UDim{0.0f, 12.0f};
    fixture.run();

    ui::DrawList list;
    ui::buildDrawList(*fixture.world, fixture.service, list);
    REQUIRE_FALSE(list.quads.empty());
    CHECK(list.quads[0].cornerRadius == doctest::Approx(12.0));
}

TEST_CASE("a radius is clamped to half the shorter side, and a Scale is a fraction of it")
{
    // Past half the shorter side a rounded rectangle IS a stadium, and beyond
    // that the arithmetic has no meaning -- so it is clamped here rather than in
    // the shader, which keeps the fragment stage a distance function with no
    // special cases.
    Fixture fixture;
    const InstanceId screen = fixture.child("ScreenGui", fixture.service);
    const InstanceId panel = fixture.child("Frame", screen);
    fixture.object(panel).size = core::UDim2{core::UDim{0.0f, 200.0f}, core::UDim{0.0f, 40.0f}};

    const InstanceId corner = fixture.child("UICorner", panel);
    fixture.world->uiCorners().find(corner)->cornerRadius = core::UDim{0.0f, 500.0f};
    fixture.run();

    ui::DrawList list;
    ui::buildDrawList(*fixture.world, fixture.service, list);
    REQUIRE_FALSE(list.quads.empty());
    CHECK(list.quads[0].cornerRadius == doctest::Approx(20.0));

    // A `Scale` is a fraction of the SHORTER side: half of a 200x40 panel is 20,
    // and a radius that meant a fraction of the WIDTH would make a wide button's
    // corners taller than the button.
    fixture.world->uiCorners().find(corner)->cornerRadius = core::UDim{0.25f, 0.0f};
    list.clear();
    ui::buildDrawList(*fixture.world, fixture.service, list);
    CHECK(list.quads[0].cornerRadius == doctest::Approx(10.0));
}

TEST_CASE("a UICorner changes the drawing and not the hit test")
{
    // `UICorner`'s own doc promises it: a button whose corner you could see
    // through but not click through would be worse than a square one.
    Fixture fixture;
    const InstanceId screen = fixture.child("ScreenGui", fixture.service);
    const InstanceId panel = fixture.child("Frame", screen);
    fixture.object(panel).size = core::UDim2{core::UDim{0.0f, 100.0f}, core::UDim{0.0f, 100.0f}};

    const InstanceId corner = fixture.child("UICorner", panel);
    fixture.world->uiCorners().find(corner)->cornerRadius = core::UDim{0.0f, 50.0f};
    fixture.run();

    // The very corner pixel, which a 50-radius round would have cut away.
    CHECK(ui::hitTest(*fixture.world, fixture.service, core::Vec2{1.0f, 1.0f}) == panel);
    // And the layout is untouched: the box is still the box.
    CHECK(fixture.object(panel).absoluteSize.x == doctest::Approx(100.0));
}

TEST_CASE("no UICorner means a radius of zero, which costs the shader one compare")
{
    Fixture fixture;
    const InstanceId screen = fixture.child("ScreenGui", fixture.service);
    const InstanceId panel = fixture.child("Frame", screen);
    fixture.object(panel).size = core::UDim2{core::UDim{0.0f, 80.0f}, core::UDim{0.0f, 80.0f}};
    fixture.run();

    ui::DrawList list;
    ui::buildDrawList(*fixture.world, fixture.service, list);
    REQUIRE_FALSE(list.quads.empty());
    CHECK(list.quads[0].cornerRadius == doctest::Approx(0.0));
}
