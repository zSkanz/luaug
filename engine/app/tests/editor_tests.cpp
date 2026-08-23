// The editor's model, minus the pixels — the same split `inspector_tests.cpp`
// makes and for the same reason. What a click DECIDES is checkable here; what
// the panel draws is a screenshot's business.
//
// The case that matters most is the last one: clicking empty space clears the
// selection. It is the behaviour a person notices only by editing the object
// they thought they had let go of, which is to say only after doing damage.
#include "luaug/app/editor.h"
#include "luaug/app/picking.h"
#include "luaug/core/math.h"
#include "luaug/platform/file.h"
#include "luaug/render/debug_draw.h"
#include "luaug/scene/components.h"
#include "luaug/scene/enum_registry.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <doctest/doctest.h>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "class_descriptors.gen.h"
#include "inspector_fixture.h"

using namespace luaug;
using luaug::app::Editor;
using luaug::app::GizmoFrame;
using luaug::app::GizmoHandle;
using luaug::app::GizmoMode;
using luaug::app::Inspector;
using luaug::app::ViewportRect;

namespace {
// A camera at the origin looking down -Z with a square 800x600 viewport, which
// is deliberately not square: an aspect bug that survives `picking_tests.cpp`
// would have to survive it here too.
void aimEditor(Editor& editor)
{
    editor.setViewport(ViewportRect{320.0f, 48.0f, 800.0f, 600.0f});
    editor.setCamera(core::perspective(1.0472f, 800.0f / 600.0f, 0.1f, 1000.0f),
                     core::lookAt({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}),
                     core::DVec3{0.0, 0.0, 0.0});
}

core::InstanceId partAt(app::testing::Fixture& fixture, scene::World& world, std::string_view name,
                        core::DVec3 position, core::Vec3 size)
{
    const core::InstanceId id = fixture.widget(world, name);
    scene::PartComponent part;
    part.cframe = core::CFrameD{position, core::Mat3{}};
    part.size = size;
    world.parts().add(id, part);
    return id;
}
} // namespace

TEST_CASE("a click in the middle of the viewport selects what is in front of the camera")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId target = partAt(fixture, world, "Target", {0.0, 0.0, -20.0}, {4.0f, 4.0f, 4.0f});

    Editor editor;
    Inspector inspector;
    aimEditor(editor);

    editor.requestPick({400.0f, 300.0f});
    CHECK(editor.pickPending());

    const auto hit = editor.resolvePick(world, inspector);
    REQUIRE(hit.has_value());
    CHECK(hit->instance == target);
    CHECK(inspector.selection() == target);
    CHECK_FALSE(editor.pickPending());
}

TEST_CASE("the viewport's window offset does not displace the ray a second time")
{
    // The panel sits at (320, 48) in the window. A click at the panel's own
    // centre must hit dead ahead: correcting for the offset here as well as in
    // the UI is the classic double-correction, and its signature is that the
    // centre of the screen stops working.
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId ahead = partAt(fixture, world, "Ahead", {0.0, 0.0, -20.0}, {1.0f, 1.0f, 1.0f});

    Editor editor;
    Inspector inspector;
    aimEditor(editor);

    editor.requestPick({400.0f, 300.0f});
    const auto hit = editor.resolvePick(world, inspector);

    REQUIRE(hit.has_value());
    CHECK(hit->instance == ahead);
}

TEST_CASE("clicking empty space clears the selection")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId target = partAt(fixture, world, "Target", {0.0, 0.0, -20.0}, {4.0f, 4.0f, 4.0f});

    Editor editor;
    Inspector inspector;
    aimEditor(editor);

    editor.requestPick({400.0f, 300.0f});
    editor.resolvePick(world, inspector);
    REQUIRE(inspector.selection() == target);

    // The top-left corner, where nothing is.
    editor.requestPick({2.0f, 2.0f});
    const auto miss = editor.resolvePick(world, inspector);

    CHECK_FALSE(miss.has_value());
    CHECK_FALSE(inspector.selection().valid());
}

TEST_CASE("a pick before anything has been rendered does nothing rather than guessing")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId target = partAt(fixture, world, "Target", {0.0, 0.0, -20.0}, {4.0f, 4.0f, 4.0f});

    Editor editor;
    Inspector inspector;
    inspector.select(target);
    editor.setViewport(ViewportRect{0.0f, 0.0f, 800.0f, 600.0f});
    REQUIRE_FALSE(editor.hasCamera());

    editor.requestPick({400.0f, 300.0f});
    const auto hit = editor.resolvePick(world, inspector);

    CHECK_FALSE(hit.has_value());
    // Deliberately still selected: with no image to have clicked on, clearing
    // would be a guess.
    CHECK(inspector.selection() == target);
}

TEST_CASE("resolving with nothing pending is not a pick")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId target = partAt(fixture, world, "Target", {0.0, 0.0, -20.0}, {4.0f, 4.0f, 4.0f});

    Editor editor;
    Inspector inspector;
    inspector.select(target);
    aimEditor(editor);

    CHECK_FALSE(editor.resolvePick(world, inspector).has_value());
    CHECK(inspector.selection() == target);
}

TEST_CASE("a click nearer the top of the viewport picks the higher of two parts")
{
    // The Y flip, which is the other half of the aspect bug: mouse coordinates
    // run down and clip space runs up, and getting it wrong is invisible at the
    // centre and inverted everywhere else.
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId high = partAt(fixture, world, "High", {0.0, 4.0, -20.0}, {3.0f, 3.0f, 3.0f});
    const core::InstanceId low = partAt(fixture, world, "Low", {0.0, -4.0, -20.0}, {3.0f, 3.0f, 3.0f});

    Editor editor;
    Inspector inspector;
    aimEditor(editor);

    editor.requestPick({400.0f, 180.0f});
    const auto upper = editor.resolvePick(world, inspector);
    REQUIRE(upper.has_value());
    CHECK(upper->instance == high);

    editor.requestPick({400.0f, 420.0f});
    const auto lower = editor.resolvePick(world, inspector);
    REQUIRE(lower.has_value());
    CHECK(lower->instance == low);
}

TEST_CASE("an editor opens in EDIT mode, because a ticking world overwrites what you type into it")
{
    Editor editor;
    CHECK(editor.runState() == luaug::app::RunState::Editing);
    CHECK_FALSE(editor.inPlayMode());
    CHECK(editor.allowedTicks(4) == 0);
}

TEST_CASE("pause is a thing that happens inside play mode and nowhere else")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    Editor editor;
    Inspector inspector;

    // Asking to pause while editing is asking for a state that does not exist.
    // Entering play mode to provide it would be worse than doing nothing.
    editor.setPaused(true);
    CHECK(editor.runState() == luaug::app::RunState::Editing);

    editor.play(world);
    CHECK(editor.runState() == luaug::app::RunState::Playing);

    editor.setPaused(true);
    CHECK(editor.runState() == luaug::app::RunState::Paused);
    CHECK(editor.inPlayMode());
    CHECK(editor.allowedTicks(4) == 0);

    editor.setPaused(false);
    CHECK(editor.runState() == luaug::app::RunState::Playing);

    // Stop leaves play mode from either half of it.
    editor.setPaused(true);
    editor.stop(world, inspector);
    CHECK(editor.runState() == luaug::app::RunState::Editing);
}

TEST_CASE("playing does not become a second scheduler")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    Editor editor;
    editor.play(world);

    // Whatever the frame owed, unchanged -- including the catch-up clamp's
    // maximum and a frame that owed nothing.
    CHECK(editor.allowedTicks(4) == 4);
    CHECK(editor.allowedTicks(1) == 1);
    CHECK(editor.allowedTicks(0) == 0);
}

TEST_CASE("a step is one tick, once")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    Editor editor;
    editor.play(world);
    editor.setPaused(true);
    editor.requestStep();

    CHECK(editor.allowedTicks(4) == 1);
    // The next frame owes ticks again and must not take one: a step that
    // repeated while the world stayed paused would be play with extra steps.
    CHECK(editor.allowedTicks(4) == 0);
}

TEST_CASE("a step asked for on a frame that owes nothing is not swallowed")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    Editor editor;
    editor.play(world);
    editor.setPaused(true);
    editor.requestStep();

    CHECK(editor.allowedTicks(0) == 0);
    CHECK(editor.allowedTicks(1) == 1);
    CHECK(editor.allowedTicks(1) == 0);
}

TEST_CASE("a step outside play mode is refused, not merely hidden")
{
    // A step is one tick of the SIMULATION, and an edited world is one whose
    // simulation is deliberately stopped -- so a step there would advance
    // physics under somebody's hands for a reason they did not ask for. The
    // panel hides the button; this is the rule living where the rule belongs,
    // which is E1's five wrong-owner defects in one line.
    Editor editor;
    editor.requestStep();
    CHECK(editor.allowedTicks(4) == 0);
}

TEST_CASE("pausing mid-play stops the world at the next frame")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    Editor editor;
    editor.play(world);
    REQUIRE(editor.allowedTicks(2) == 2);

    editor.setPaused(true);
    CHECK(editor.allowedTicks(2) == 0);
}

TEST_CASE("the editor camera is seeded rather than teleported")
{
    Editor editor;
    CHECK_FALSE(editor.cameraAdopted());

    core::CFrameD start;
    start.position = {12.0, 3.0, -40.0};
    editor.adoptCamera(start);

    CHECK(editor.cameraAdopted());
    CHECK(editor.cameraCFrame().position == start.position);
}

TEST_CASE("driving does nothing until a camera has been adopted")
{
    Editor editor;
    const core::CFrameD before = editor.cameraCFrame();

    editor.driveCamera({50.0f, 0.0f}, {1.0f, 0.0f, 1.0f}, 1.0f);

    CHECK(editor.cameraCFrame().position == before.position);
}

TEST_CASE("the game takes its camera back when the world plays")
{
    Editor editor;
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    core::CFrameD start;
    start.position = {0.0, 0.0, 0.0};
    editor.adoptCamera(start);
    editor.play(world);

    editor.driveCamera({100.0f, 100.0f}, {1.0f, 1.0f, 1.0f}, 1.0f);

    // Not moved and not turned: while the world is playing the script owns the
    // camera, and an editor still writing it would be two authors for one
    // transform.
    CHECK(editor.cameraCFrame().position == start.position);
}

TEST_CASE("flying forward moves along the camera's own look direction")
{
    Editor editor;
    editor.adoptCamera(core::CFrameD{});
    editor.setCameraSpeed(10.0f);

    // One second of forward, from the identity rotation, which looks down -Z.
    editor.driveCamera({}, {0.0f, 0.0f, 1.0f}, 1.0f);
    CHECK(editor.cameraCFrame().position.z < -9.0);
    CHECK(editor.cameraCFrame().position.x == doctest::Approx(0.0));

    // Turn a quarter turn and the same key goes somewhere else. This is the
    // case that fails when movement is done in world axes rather than the
    // camera's.
    Editor turned;
    turned.adoptCamera(core::CFrameD{});
    turned.setCameraSpeed(10.0f);
    turned.driveCamera({static_cast<float>(-1.5708 / 0.0032), 0.0f}, {}, 0.0f);
    turned.driveCamera({}, {0.0f, 0.0f, 1.0f}, 1.0f);
    CHECK(std::abs(turned.cameraCFrame().position.x) > 9.0);
}

TEST_CASE("pitch stops short of the pole, where a fly camera spins on its own")
{
    Editor editor;
    editor.adoptCamera(core::CFrameD{});

    // Far more than a quarter turn of upward mouse movement, twice, so a clamp
    // that only holds for one frame does not pass.
    for (int i = 0; i < 8; ++i)
        editor.driveCamera({0.0f, -1000.0f}, {}, 0.016f);

    // Straight up would put the look direction's Y at 1. The clamp keeps it
    // just below, which is what stops yaw and look from becoming one axis.
    const core::Mat3& basis = editor.cameraCFrame().rotation;
    const double lookY = -static_cast<double>(basis.m[2][1]);
    CHECK(lookY < 1.0);
    CHECK(lookY > 0.99);
}

TEST_CASE("camera speed refuses a value that makes the camera unusable")
{
    Editor editor;

    editor.setCameraSpeed(0.0f);
    CHECK(editor.cameraSpeed() > 0.0f);

    editor.setCameraSpeed(-5.0f);
    CHECK(editor.cameraSpeed() > 0.0f);
}

TEST_CASE("play remembers the world, and stop puts it back")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId kept = fixture.widget(world, "Kept");

    Editor editor;
    Inspector inspector;

    CHECK_FALSE(editor.inPlayMode());
    editor.play(world);
    CHECK(editor.inPlayMode());
    CHECK(editor.runState() == luaug::app::RunState::Playing);

    // What "playing" did: a new instance, and the old one gone.
    const core::InstanceId spawned = fixture.widget(world, "Spawned");
    (void)world.destroy(kept);
    const core::u64 during = world.worldHash();

    editor.stop(world, inspector);

    CHECK(editor.runState() == luaug::app::RunState::Editing);
    CHECK_FALSE(editor.inPlayMode());
    CHECK(world.worldHash() != during);
    CHECK(world.alive(kept));
    CHECK_FALSE(world.alive(spawned));
}

TEST_CASE("stopping drops a selection the restore took away")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);

    Editor editor;
    Inspector inspector;
    editor.play(world);

    const core::InstanceId spawned = fixture.widget(world, "Spawned");
    inspector.select(spawned);
    REQUIRE(inspector.selection() == spawned);

    editor.stop(world, inspector);

    // Left selected, the properties grid would be pointed at a slot that now
    // holds nothing or somebody else -- which is how an edit lands on the wrong
    // object.
    CHECK_FALSE(inspector.selection().valid());
}

TEST_CASE("stop with nothing to restore is not a crash and not a lie")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId kept = fixture.widget(world, "Kept");

    Editor editor;
    Inspector inspector;
    editor.stop(world, inspector);

    CHECK(editor.runState() == luaug::app::RunState::Editing);
    CHECK(world.alive(kept));
}

TEST_CASE("pressing play twice does not move the point stop returns to")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId original = fixture.widget(world, "Original");

    Editor editor;
    Inspector inspector;
    editor.play(world);

    const core::InstanceId spawned = fixture.widget(world, "Spawned");
    // Already playing, so this is a no-op rather than a second snapshot -- the
    // alternative is a stop that returns to the middle of a play session.
    editor.play(world);
    editor.stop(world, inspector);

    CHECK(world.alive(original));
    CHECK_FALSE(world.alive(spawned));
}

TEST_CASE("undo puts back what a delete took, subtree and all")
{
    // The case that decided the design. Undoing a property write is remembering
    // a value; undoing a DELETE is recreating an instance, everything under it,
    // and the ids anybody was holding. A reversible-command stack is where that
    // goes wrong, and `World::snapshot` already does it correctly.
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    Editor editor;
    Inspector inspector;

    const core::InstanceId parent = fixture.widget(world, "Tower");
    const core::InstanceId child = fixture.widget(world, "Door");
    (void)world.setParent(child, parent);

    REQUIRE(editor.deleteInstance(world, parent, {}, inspector));
    // Gone for good, not merely marked: a paused world runs no signal drain, so
    // the editor retires what it deletes rather than leaving a record that
    // answers `alive` until somebody presses play.
    CHECK_FALSE(world.alive(parent));
    CHECK_FALSE(world.alive(child));

    REQUIRE(editor.undo(world, inspector));
    CHECK(world.alive(parent));
    // The subtree came with it, and by the SAME id -- which is what makes a
    // reference somebody was holding still mean something.
    CHECK(world.alive(child));
    CHECK(world.parentOf(child) == parent);
}

TEST_CASE("redo does it again, and a new edit throws the redo away")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    Editor editor;
    Inspector inspector;

    const core::InstanceId first = fixture.widget(world, "First");
    REQUIRE(editor.deleteInstance(world, first, {}, inspector));
    REQUIRE(editor.undo(world, inspector));
    CHECK(world.alive(first));

    REQUIRE(editor.redo(world, inspector));
    CHECK_FALSE(world.alive(first));

    REQUIRE(editor.undo(world, inspector));
    CHECK(editor.history().canRedo());

    // A future that no longer happens. Keeping it would let a redo apply a
    // change to a world that has moved on, which is the one way an undo stack
    // destroys work rather than restoring it.
    const core::InstanceId second = fixture.widget(world, "Second");
    editor.history().record(world, "Edit");
    CHECK_FALSE(editor.history().canRedo());
    CHECK(world.alive(second));
}

TEST_CASE("a drag on one property is one step, and two properties are two")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    Editor editor;

    // The same key, over and over, is what a drag looks like from here.
    for (int frame = 0; frame < 120; ++frame)
        editor.history().record(world, "Edit", 42);
    CHECK(editor.history().canUndo());

    Editor counted;
    for (int frame = 0; frame < 120; ++frame)
        counted.history().record(world, "Edit", 42);
    // One step, not a hundred and twenty. Walking back through a two-second
    // drag one frame at a time is not undo.
    int steps = 0;
    while (counted.history().undo(world))
        ++steps;
    CHECK(steps == 1);

    Editor separate;
    separate.history().record(world, "Edit", 1);
    separate.history().record(world, "Edit", 2);
    steps = 0;
    while (separate.history().undo(world))
        ++steps;
    CHECK(steps == 2);
}

TEST_CASE("the stack has a floor and drops the oldest rather than growing")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    Editor editor;

    for (std::size_t step = 0; step < luaug::app::UndoStack::Depth + 20; ++step)
        editor.history().record(world, "Edit");

    std::size_t steps = 0;
    while (editor.history().undo(world))
        ++steps;
    CHECK(steps == luaug::app::UndoStack::Depth);
}

TEST_CASE("stopping a play session clears the history")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    Editor editor;
    Inspector inspector;

    const core::InstanceId part = fixture.widget(world, "Part");
    REQUIRE(editor.deleteInstance(world, part, {}, inspector));
    REQUIRE(editor.history().canUndo());

    editor.play(world);
    editor.stop(world, inspector);

    // The edits before it belong to a world the restore has just replaced.
    CHECK_FALSE(editor.history().canUndo());
}

TEST_CASE("the engine's own instances cannot be deleted or duplicated")
{
    // A service is reached through `GetService`, there is one per world, and the
    // engine makes it whether or not anybody asked. Deleting one leaves a world
    // that cannot answer a call every script makes; duplicating one makes "one
    // per world" false.
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    Editor editor;
    Inspector inspector;

    // The fixture has no service classes, so the rule is checked through the
    // other half of it: an instance with no parent is the world's root.
    const core::InstanceId root = fixture.widget(world, "DataModel");
    CHECK(Editor::isEngineOwned(world, root, root));
    CHECK_FALSE(editor.deleteInstance(world, root, root, inspector));
    CHECK(world.alive(root));
    CHECK(editor.status().failed);

    const core::InstanceId ordinary = fixture.widget(world, "Part");
    (void)world.setParent(ordinary, root);
    CHECK_FALSE(Editor::isEngineOwned(world, ordinary, root));
    CHECK(editor.deleteInstance(world, ordinary, root, inspector));
}

// --- D068: what a person types into Save Scene As ---------------------------
//
// The dialog labels its box `content/` and then resolved what was typed against
// the content root, so the natural thing to type produced `content/content/`
// and `createDirectories` made the folder without a word. The first person to
// use it wrote a scene into a directory nothing would ever look in.
//
// Normalising is the fix and it is stated as a function so the dialog can show
// the resolved path while it is still being typed -- a preview of where a file
// will land is worth more than a rule nobody can see.

TEST_CASE("a typed scene path resolves to somewhere inside content/")
{
    // The prefix the dialog's own label already supplies.
    CHECK(Editor::normalizeScenePath("content/scenes/main") == "scenes/main.scene.json");
    CHECK(Editor::normalizeScenePath("content/content/scenes/main.scene.json") == "scenes/main.scene.json");
    // A name, which is the common case and must not be disturbed.
    CHECK(Editor::normalizeScenePath("main") == "main.scene.json");
    CHECK(Editor::normalizeScenePath("scenes/main.scene.json") == "scenes/main.scene.json");
    // Typed on Windows, where the separator on the keyboard is the other one.
    CHECK(Editor::normalizeScenePath("scenes\\main") == "scenes/main.scene.json");
    CHECK(Editor::normalizeScenePath("/scenes/main") == "scenes/main.scene.json");
    // A folder legitimately called `content` INSIDE the content root survives,
    // because the prefix that is stripped is the one the label already showed.
    CHECK(Editor::normalizeScenePath("levels/content/main") == "levels/content/main.scene.json");
}

TEST_CASE("a scene path that leaves content/ is refused rather than created")
{
    CHECK(Editor::sceneNameIsUsable("scenes/main.scene.json"));
    CHECK(Editor::sceneNameIsUsable("main.scene.json"));

    CHECK_FALSE(Editor::sceneNameIsUsable(""));
    CHECK_FALSE(Editor::sceneNameIsUsable("../main.scene.json"));
    CHECK_FALSE(Editor::sceneNameIsUsable("scenes/../../main.scene.json"));
    CHECK_FALSE(Editor::sceneNameIsUsable("C:/main.scene.json"));
    CHECK_FALSE(Editor::sceneNameIsUsable("scenes//main.scene.json"));
}

// --- D070: every path that replaces the world says so -----------------------
//
// The frame loop drops its `render::TransformHistory` when
// `Inspector::worldGeneration` moves, because that counter is the one signal
// every world-replacing path already raises and a second notion of "this is not
// the world it was" would only be somewhere for the two to disagree.
//
// So the counter moving is a contract rather than an implementation detail, and
// this is what holds it: a path added later that restores without announcing it
// would bring the flicker back, and nothing else in the tree would notice.
TEST_CASE("a stop, an undo, a redo and a new scene each announce that the world was replaced")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    Editor editor;
    Inspector inspector;

    const core::InstanceId root = fixture.widget(world, "Root");
    const core::InstanceId subject = fixture.widget(world, "Subject");
    REQUIRE_FALSE(world.setParent(subject, root).has_value());

    const core::u64 booted = inspector.worldGeneration();

    editor.play(world);
    editor.stop(world, inspector);
    const core::u64 afterStop = inspector.worldGeneration();
    CHECK(afterStop != booted);

    editor.history().record(world, "Edit", 0);
    REQUIRE(editor.undo(world, inspector));
    const core::u64 afterUndo = inspector.worldGeneration();
    CHECK(afterUndo != afterStop);

    REQUIRE(editor.redo(world, inspector));
    const core::u64 afterRedo = inspector.worldGeneration();
    CHECK(afterRedo != afterUndo);

    editor.newScene(world, inspector);
    CHECK(inspector.worldGeneration() != afterRedo);
}

// --- D071: a restore is not a replacement -----------------------------------
//
// `World::restore` carries generations and the free list precisely so that an
// `InstanceId` means the same thing after one as before it. Everything a panel
// keyed by an id therefore survives -- which rows are expanded, what is
// selected -- and an undo that threw those away took back more than the edit it
// was asked to. Reported as the explorer collapsing on every ctrl-Z.
//
// The two counters are the contract, and this holds it: a path added later that
// restores through `onWorldChanged` would collapse the tree again, and nothing
// else in the tree would notice.
TEST_CASE("undo, redo and stop leave id-keyed panel state alone; a scene load does not")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    Editor editor;
    Inspector inspector;

    const core::InstanceId root = fixture.widget(world, "Root");
    const core::InstanceId subject = fixture.widget(world, "Subject");
    REQUIRE_FALSE(world.setParent(subject, root).has_value());
    inspector.select(subject);

    const core::u64 identity = inspector.worldIdentity();
    const core::u64 generation = inspector.worldGeneration();

    editor.play(world);
    editor.stop(world, inspector);
    // The world's VALUES were replaced, so anything cached about them goes --
    // the frame loop's transform history reads this and a stale one is the
    // flicker D070 was.
    CHECK(inspector.worldGeneration() != generation);
    // Its IDENTITY did not, so the expanded set and the selection stay.
    CHECK(inspector.worldIdentity() == identity);
    CHECK(inspector.selection() == subject);

    editor.history().record(world, "Edit", 0);
    REQUIRE(editor.undo(world, inspector));
    CHECK(inspector.worldIdentity() == identity);
    CHECK(inspector.selection() == subject);

    REQUIRE(editor.redo(world, inspector));
    CHECK(inspector.worldIdentity() == identity);
    CHECK(inspector.selection() == subject);

    // A new scene IS a different world: slot indices restart, so a row that
    // remembered being expanded would hand that to whatever moved into the
    // slot.
    editor.newScene(world, inspector);
    CHECK(inspector.worldIdentity() != identity);
    CHECK_FALSE(inspector.selection().valid());
}

// --- E2: reparenting, and doing things to four instances at once ------------

TEST_CASE("a batch delete is one undo step, and undoing it brings back the same ids")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    Editor editor;
    Inspector inspector;

    const core::InstanceId root = fixture.widget(world, "Root");
    std::vector<core::InstanceId> made;
    for (int index = 0; index < 4; ++index) {
        const core::InstanceId id = fixture.widget(world, "Doomed");
        REQUIRE_FALSE(world.setParent(id, root).has_value());
        made.push_back(id);
    }

    REQUIRE(editor.deleteInstances(world, made, root, inspector));
    for (const core::InstanceId id : made)
        CHECK_FALSE(world.alive(id));
    CHECK(editor.history().canUndo());

    // **One press**, because somebody who deleted four things did one thing.
    REQUIRE(editor.undo(world, inspector));
    for (const core::InstanceId id : made)
        CHECK(world.alive(id));
    CHECK_FALSE(editor.history().canUndo());
}

TEST_CASE("a batch is ordered by the tree, so the same selection is the same result")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);

    const core::InstanceId root = fixture.widget(world, "Root");
    const core::InstanceId first = fixture.widget(world, "First");
    const core::InstanceId second = fixture.widget(world, "Second");
    const core::InstanceId nested = fixture.widget(world, "Nested");
    REQUIRE_FALSE(world.setParent(first, root).has_value());
    REQUIRE_FALSE(world.setParent(second, root).has_value());
    REQUIRE_FALSE(world.setParent(nested, first).has_value());

    // Clicked in the reverse of the tree's order, which is what ctrl-clicking
    // down a list and then back up produces.
    const std::array<core::InstanceId, 3> clicked{nested, second, first};
    std::vector<core::InstanceId> ordered;
    app::orderByTree(world, root, clicked, ordered);

    REQUIRE(ordered.size() == 3);
    // Document order: the parent ahead of its own child, whatever order the
    // clicks arrived in.
    CHECK(ordered[0] == first);
    CHECK(ordered[1] == nested);
    CHECK(ordered[2] == second);

    // And it is idempotent over duplicates, because a selection can hold one.
    const std::array<core::InstanceId, 4> twice{second, first, second, first};
    app::orderByTree(world, root, twice, ordered);
    REQUIRE(ordered.size() == 2);
    CHECK(ordered[0] == first);
    CHECK(ordered[1] == second);
}

TEST_CASE("deleting a parent and its child together is not an error")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    Editor editor;
    Inspector inspector;

    const core::InstanceId root = fixture.widget(world, "Root");
    const core::InstanceId parent = fixture.widget(world, "Parent");
    const core::InstanceId child = fixture.widget(world, "Child");
    REQUIRE_FALSE(world.setParent(parent, root).has_value());
    REQUIRE_FALSE(world.setParent(child, parent).has_value());

    // The parent goes first and takes the child with it, so the child is
    // already gone by the time the walk reaches it. Selecting both and pressing
    // delete means both, and both is what happened.
    const std::array<core::InstanceId, 2> both{child, parent};
    REQUIRE(editor.deleteInstances(world, both, root, inspector));
    CHECK_FALSE(world.alive(parent));
    CHECK_FALSE(world.alive(child));
    CHECK(inspector.selectionCount() == 0);
}

TEST_CASE("a batch duplicate is one step and selects the copies")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    Editor editor;
    Inspector inspector;

    const core::InstanceId root = fixture.widget(world, "Root");
    std::vector<core::InstanceId> made;
    for (int index = 0; index < 3; ++index) {
        const core::InstanceId id = fixture.widget(world, "Original");
        REQUIRE_FALSE(world.setParent(id, root).has_value());
        made.push_back(id);
    }

    REQUIRE(editor.duplicateInstances(world, made, root, inspector));
    CHECK(world.childCount(root) == 6);
    // The copies, because the point of duplicating is to change what came out.
    CHECK(inspector.selectionCount() == 3);
    for (const core::InstanceId id : inspector.selectionSet())
        CHECK(std::find(made.begin(), made.end(), id) == made.end());

    REQUIRE(editor.undo(world, inspector));
    CHECK(world.childCount(root) == 3);
}

TEST_CASE("reparenting moves a subtree, refuses a cycle, and never records a step that does nothing")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    Editor editor;
    Inspector inspector;

    const core::InstanceId root = fixture.widget(world, "Root");
    const core::InstanceId from = fixture.widget(world, "From");
    const core::InstanceId to = fixture.widget(world, "To");
    const core::InstanceId moved = fixture.widget(world, "Moved");
    const core::InstanceId child = fixture.widget(world, "Child");
    REQUIRE_FALSE(world.setParent(from, root).has_value());
    REQUIRE_FALSE(world.setParent(to, root).has_value());
    REQUIRE_FALSE(world.setParent(moved, from).has_value());
    REQUIRE_FALSE(world.setParent(child, moved).has_value());

    const std::array<core::InstanceId, 1> one{moved};
    REQUIRE(editor.reparent(world, one, to, root, inspector));
    CHECK(world.parentOf(moved) == to);
    // The subtree came with it, which is what moving a thing means.
    CHECK(world.parentOf(child) == moved);

    // Onto its own child: a cycle, refused by `World::setParent` and asked of it
    // rather than re-implemented.
    const std::array<core::InstanceId, 1> cycle{moved};
    const bool undoBefore = editor.history().canUndo();
    CHECK_FALSE(editor.reparent(world, cycle, child, root, inspector));
    CHECK(world.parentOf(moved) == to);
    // **And no step was recorded**, which is the part that matters: a step that
    // undoes nothing eats a press of ctrl-Z, and the second press takes back
    // something the person had stopped thinking about.
    CHECK(editor.history().canUndo() == undoBefore);

    // Onto itself is the same refusal.
    const std::array<core::InstanceId, 1> self{moved};
    CHECK_FALSE(editor.reparent(world, self, moved, root, inspector));

    // And undo puts it back where it was.
    REQUIRE(editor.undo(world, inspector));
    CHECK(world.parentOf(moved) == from);
}

TEST_CASE("nothing authored may live inside what a system made")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    Editor editor;
    Inspector inspector;

    const core::InstanceId root = fixture.widget(world, "Root");
    const core::InstanceId chunk = fixture.widget(world, "Chunk_12_-4");
    const core::InstanceId insideChunk = fixture.widget(world, "Ground");
    const core::InstanceId authored = fixture.widget(world, "Authored");
    REQUIRE_FALSE(world.setParent(chunk, root).has_value());
    REQUIRE_FALSE(world.setParent(insideChunk, chunk).has_value());
    REQUIRE_FALSE(world.setParent(authored, root).has_value());

    // Streaming marks the chunk's FOLDER and not its contents, which is the
    // economy that makes checking the instance alone wrong.
    world.setGenerated(chunk, true);

    CHECK(Editor::authorable(world, authored, root));
    CHECK_FALSE(Editor::authorable(world, chunk, root));
    CHECK_FALSE(Editor::authorable(world, insideChunk, root));

    // The save would skip anything dropped in there -- the serializer skips a
    // generated subtree whole -- and the next eviction would destroy it without
    // a word.
    const std::array<core::InstanceId, 1> one{authored};
    CHECK_FALSE(editor.reparent(world, one, chunk, root, inspector));
    CHECK(world.parentOf(authored) == root);
}

TEST_CASE("a drop target lights up for exactly the drops that would move something")
{
    // **The Explorer's drop target asks this a frame before the drag ends**, and
    // it has to be the same rule the verb applies or the row lights up under
    // the pointer and then refuses -- the broken promise `editable` exists to
    // prevent one panel over.
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    Editor editor;
    Inspector inspector;

    const core::InstanceId root = fixture.widget(world, "Root");
    const core::InstanceId folder = fixture.widget(world, "Folder");
    const core::InstanceId moved = fixture.widget(world, "Moved");
    const core::InstanceId child = fixture.widget(world, "Child");
    const core::InstanceId chunk = fixture.widget(world, "Chunk_12_-4");
    REQUIRE_FALSE(world.setParent(folder, root).has_value());
    REQUIRE_FALSE(world.setParent(moved, root).has_value());
    REQUIRE_FALSE(world.setParent(child, moved).has_value());
    REQUIRE_FALSE(world.setParent(chunk, root).has_value());
    world.setGenerated(chunk, true);

    const std::array<core::InstanceId, 1> one{moved};
    CHECK(Editor::canReparent(world, one, folder, root));
    // Onto itself, and into its own subtree: the two cycles.
    CHECK_FALSE(Editor::canReparent(world, one, moved, root));
    CHECK_FALSE(Editor::canReparent(world, one, child, root));
    // Where it already is. Not a refusal and still not a drop: a row that lit
    // up for it would promise a move that cannot happen.
    CHECK_FALSE(Editor::canReparent(world, one, root, root));
    // Into what streaming made, which the save would skip and the next eviction
    // would destroy.
    CHECK_FALSE(Editor::canReparent(world, one, chunk, root));
    // Nothing selected is nothing to drop.
    CHECK_FALSE(Editor::canReparent(world, {}, folder, root));

    // **A batch lights up if ANY member can go**, because that is what the
    // drop then does: `reparent` refuses per instance and moves the rest, so a
    // target that refused the whole drag over one member nobody noticed
    // selecting would be stricter than the verb behind it.
    const std::array<core::InstanceId, 2> mixed{moved, folder};
    CHECK(Editor::canReparent(world, mixed, folder, root));
    REQUIRE(editor.reparent(world, mixed, folder, root, inspector));
    CHECK(world.parentOf(moved) == folder);
    CHECK(world.parentOf(folder) == root);
}

TEST_CASE("creating lands under the parent that was asked, is selected, and one undo takes it back")
{
    // The gate item, driven through `Editor` rather than through a mouse: the
    // menu that calls this cannot be opened by a test, and what it decides can.
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    Editor editor;
    Inspector inspector;

    const core::InstanceId root = fixture.widget(world, "Root");
    const core::InstanceId here = fixture.widget(world, "Here");
    const core::InstanceId elsewhere = fixture.widget(world, "Elsewhere");
    REQUIRE_FALSE(world.setParent(here, root).has_value());
    REQUIRE_FALSE(world.setParent(elsewhere, root).has_value());

    // Something else selected first, because "it lands under the parent the
    // menu was opened on" is only a claim worth testing when the selection
    // disagrees with it.
    inspector.select(elsewhere);

    REQUIRE(editor.createInstance(world, fixture.widgetClass, here, root, inspector));
    const core::InstanceId made = inspector.selection();
    REQUIRE(made.valid());
    CHECK(world.parentOf(made) == here);
    // Selected, and ALONE: the point of making a thing is to change it, and a
    // selection that still held what was there before would send the next edit
    // somewhere nobody is looking.
    CHECK(inspector.selectionCount() == 1);
    CHECK(world.childCount(here) == 1);

    REQUIRE(editor.undo(world, inspector));
    CHECK(world.childCount(here) == 0);
    CHECK_FALSE(world.alive(made));
    // One step, not two. A create that recorded the world twice -- once for the
    // instance and once for the placement `setProperty` -- would need two
    // presses of ctrl-Z to take back one thing anybody did.
    CHECK_FALSE(editor.history().canUndo());

    // A class nothing may create is refused before anything is recorded, so the
    // refusal does not eat a press of ctrl-Z either.
    CHECK_FALSE(editor.createInstance(world, scene::InvalidClass, here, root, inspector));
    CHECK_FALSE(editor.history().canUndo());
}

TEST_CASE("making something inside an empty folder asks the tree to open it")
{
    // **The reported defect, and it was deterministic.** An empty row has no
    // chevron, so a fresh `Folder` could not have been opened -- which means a
    // `Part` created inside one was invisible EVERY time: created, selected,
    // showing in the properties grid, and nowhere in the Explorer. That reads
    // exactly like "I cannot add a child to a folder".
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    Editor editor;
    Inspector inspector;

    const core::InstanceId root = fixture.widget(world, "Root");
    const core::InstanceId folder = fixture.widget(world, "Folder");
    REQUIRE_FALSE(world.setParent(folder, root).has_value());
    REQUIRE(world.childCount(folder) == 0);

    REQUIRE(editor.createInstance(world, fixture.widgetClass, folder, root, inspector));
    const core::InstanceId made = inspector.selection();
    REQUIRE(made.valid());
    CHECK(world.parentOf(made) == folder);
    // The panel opens the way DOWN to this, which is the folder and everything
    // above it.
    CHECK(inspector.takeReveal() == made);

    // A move asks the same thing, because dropping something into a collapsed
    // folder and watching it vanish is the same defect through the other verb.
    const core::InstanceId elsewhere = fixture.widget(world, "Elsewhere");
    REQUIRE_FALSE(world.setParent(elsewhere, root).has_value());
    const std::array<core::InstanceId, 1> one{elsewhere};
    REQUIRE(editor.reparent(world, one, folder, root, inspector));
    CHECK(inspector.takeReveal() == elsewhere);
}

TEST_CASE("nothing can be created inside a chunk, including inside what the chunk holds")
{
    // **The chunk's own row was right and everything under it was wrong.**
    // Streaming marks a chunk's FOLDER and not its contents, so
    // `Chunk_-3_0_0/Ground` is not itself generated -- an instance-only test
    // therefore offered a plus on it, accepted the create, and the next
    // eviction destroyed what somebody made without a word.
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    Editor editor;
    Inspector inspector;

    const core::InstanceId root = fixture.widget(world, "Root");
    const core::InstanceId chunk = fixture.widget(world, "Chunk_-3_0_0");
    const core::InstanceId ground = fixture.widget(world, "Ground");
    const core::InstanceId folder = fixture.widget(world, "Folder");
    REQUIRE_FALSE(world.setParent(chunk, root).has_value());
    REQUIRE_FALSE(world.setParent(ground, chunk).has_value());
    REQUIRE_FALSE(world.setParent(folder, root).has_value());
    world.setGenerated(chunk, true);

    // The plus is drawn from this, and so is the refusal, which is the whole
    // point of it being one function.
    CHECK(Editor::canParentInto(world, root, root));
    CHECK(Editor::canParentInto(world, folder, root));
    CHECK_FALSE(Editor::canParentInto(world, chunk, root));
    CHECK_FALSE(Editor::canParentInto(world, ground, root));

    CHECK_FALSE(editor.createInstance(world, fixture.widgetClass, ground, root, inspector));
    CHECK(world.childCount(ground) == 0);
    REQUIRE(editor.createInstance(world, fixture.widgetClass, folder, root, inspector));
    CHECK(world.childCount(folder) == 1);
}

TEST_CASE("a folder in the world carries its own colour, and undo takes it back")
{
    // **An instance can carry it, so it does.** That is what makes it travel:
    // the scene file records it with no format change, and a rename or a
    // reparent cannot lose it because it was never keyed by where the folder
    // was.
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    Editor editor;
    Inspector inspector;

    const core::InstanceId folder = fixture.widget(world, "Props");
    CHECK_FALSE(Editor::folderColor(world, folder).has_value());

    const core::Color3 wanted{0.30f, 0.62f, 0.85f};
    editor.setFolderColor(world, folder, wanted);
    const std::optional<core::Color3> read = Editor::folderColor(world, folder);
    REQUIRE(read.has_value());
    CHECK(static_cast<double>(read->r) == doctest::Approx(static_cast<double>(wanted.r)));
    CHECK(static_cast<double>(read->b) == doctest::Approx(static_cast<double>(wanted.b)));

    // Taking it off is the same call with nothing in it, because the world's
    // own setter removes an attribute set to nil -- one path rather than two.
    editor.setFolderColor(world, folder, std::nullopt);
    CHECK_FALSE(Editor::folderColor(world, folder).has_value());

    // And both are ordinary edits, so both are ordinary undo steps.
    REQUIRE(editor.undo(world, inspector));
    CHECK(Editor::folderColor(world, folder).has_value());
    REQUIRE(editor.undo(world, inspector));
    CHECK_FALSE(Editor::folderColor(world, folder).has_value());
}

TEST_CASE("a content folder's colour survives the editor closing")
{
    // A directory cannot carry anything, so this one lives in the editor's own
    // state file -- and the round trip through it is the whole claim.
    const std::filesystem::path state = std::filesystem::temp_directory_path() / "luaug-editor-state-test" / ".luaug";
    std::error_code cleanup;
    std::filesystem::remove_all(state.parent_path(), cleanup);

    const core::Color3 teal{0.29f, 0.71f, 0.60f};
    {
        Editor editor;
        CHECK_FALSE(editor.contentColor("props").has_value());
        editor.setContentColor("props", teal);
        editor.setContentColor("props/trees", core::Color3{0.85f, 0.33f, 0.31f});
        editor.setContentColor("gone", teal);
        editor.setContentColor("gone", std::nullopt);
        editor.rememberState(state);
    }

    Editor reopened;
    reopened.recallState(state);
    const std::optional<core::Color3> read = reopened.contentColor("props");
    REQUIRE(read.has_value());
    // Through `#rrggbb`, so the check is that eight bits per channel is enough
    // -- which it is for a colour somebody picked out of a swatch.
    CHECK(static_cast<double>(read->r) == doctest::Approx(static_cast<double>(teal.r)).epsilon(0.01));
    CHECK(static_cast<double>(read->g) == doctest::Approx(static_cast<double>(teal.g)).epsilon(0.01));
    CHECK(static_cast<double>(read->b) == doctest::Approx(static_cast<double>(teal.b)).epsilon(0.01));
    CHECK(reopened.contentColor("props/trees").has_value());
    // Cleared before the write, so it is not in the file at all.
    CHECK_FALSE(reopened.contentColor("gone").has_value());

    // The same state written twice is the same bytes, which is what the ordered
    // container is for and what makes the file worth putting in a diff.
    reopened.rememberState(state);
    std::string first;
    REQUIRE(luaug::platform::readTextFile(state / "editor.json", first));
    reopened.rememberState(state);
    std::string second;
    REQUIRE(luaug::platform::readTextFile(state / "editor.json", second));
    CHECK(first == second);

    std::filesystem::remove_all(state.parent_path(), cleanup);
}

// --- E2: dragging a manipulator ---------------------------------------------
//
// The whole loop, headless: a camera, a viewport, a press on a handle, frames
// of movement, a release. What no test can reach is the picture; what every one
// of these reaches is what the picture is drawn FROM.

namespace {

// **The REAL classes, because a manipulator moves a `Part` and nothing else.**
// The inspector fixture's synthetic hierarchy has no `PartComponent`, so a gizmo
// over it would have nothing to sit on -- and a test that invented one would be
// testing a world nobody ships.
struct DragRig
{
    core::AtomTable atoms;
    scene::ClassRegistry classes;
    scene::EnumRegistry enums;
    scene::World world;
    Editor editor;
    Inspector inspector;
    core::InstanceId root;
    scene::ClassId partClass = scene::InvalidClass;
    ViewportRect rect{0.0f, 0.0f, 1920.0f, 1080.0f};

    DragRig() : world(classes, enums, atoms, 1234u)
    {
        scene::generated::registerClasses(classes, atoms);
        scene::generated::registerEnums(enums, atoms);
        partClass = classes.findId(atoms.intern("Part"));
        REQUIRE(partClass != scene::InvalidClass);

        root = world.create(classes.findId(atoms.intern("Folder")));
        REQUIRE(root.valid());
    }

    // A camera at `eye` looking down -Z, in the camera-relative space the
    // renderer works in -- which is why the origin travels separately.
    void look(core::DVec3 eye)
    {
        editor.setViewport(rect);
        editor.setCamera(core::perspective(60.0f * 3.14159265f / 180.0f, rect.width / rect.height, 0.1f, 5000.0f),
                         core::lookAt(core::Vec3{}, core::Vec3{0.0f, 0.0f, -1.0f}, core::Vec3{0.0f, 1.0f, 0.0f}), eye);
    }

    [[nodiscard]] core::InstanceId part(core::DVec3 at)
    {
        const core::InstanceId id = world.create(partClass);
        REQUIRE(id.valid());
        (void)world.setParent(id, root);
        scene::PartComponent* component = world.parts().find(id);
        REQUIRE(component != nullptr);
        component->cframe.position = at;
        return id;
    }

    // The pixel a world point falls at, which is how a test aims at a handle it
    // can only describe in world space.
    [[nodiscard]] core::Vec2 pixelOf(core::DVec3 point) const
    {
        const std::optional<core::Vec2> pixel =
            app::worldToViewport(editor.projection(), editor.view(), editor.cameraOrigin(), rect, point);
        REQUIRE(pixel.has_value());
        return *pixel;
    }

    // One frame of the loop: report the pointer, then run the manipulator and
    // the drain exactly as the frame does.
    void frame(core::Vec2 pixel, bool pressed, bool down)
    {
        editor.setPointer(pixel, pressed, down);
        const bool took = editor.driveGizmo(world, inspector);
        if (inspector.pendingCount() > 0) {
            editor.history().record(world, "Edit", app::coalesceKeyFor(inspector.gesture(), inspector.pending()));
        }
        inspector.applyPending(world);
        (void)took;
    }
};

} // namespace

TEST_CASE("one drag is one undo step, however many frames it lasts")
{
    DragRig rig;
    rig.look({0.0, 0.0, 0.0});
    const core::InstanceId subject = rig.part({0.0, 0.0, -30.0});
    rig.inspector.select(subject);
    rig.editor.setSnap(false);

    const std::optional<GizmoFrame> frame = rig.editor.gizmoFrame(rig.world, rig.inspector);
    REQUIRE(frame.has_value());

    const core::DVec3 grab =
        frame->transform.position + core::DVec3{static_cast<core::f64>(frame->size) * 0.7, 0.0, 0.0};
    rig.frame(rig.pixelOf(grab), true, true);
    REQUIRE(rig.editor.gizmoDragging());

    // Sixty frames of movement, which at sixty hertz is a second of dragging.
    for (int step = 1; step <= 60; ++step) {
        const core::DVec3 to = grab + core::DVec3{static_cast<core::f64>(step) * 0.05, 0.0, 0.0};
        rig.frame(rig.pixelOf(to), false, true);
    }
    rig.frame(rig.pixelOf(grab), false, false);
    CHECK_FALSE(rig.editor.gizmoDragging());

    // **One.** Without the gesture this is sixty world snapshots and sixty
    // presses of ctrl-Z to get back to where the drag began.
    CHECK(rig.editor.history().canUndo());
    REQUIRE(rig.editor.undo(rig.world, rig.inspector));
    CHECK_FALSE(rig.editor.history().canUndo());

    const scene::PartComponent* part = rig.world.parts().find(subject);
    REQUIRE(part != nullptr);
    CHECK(part->cframe.position.x == doctest::Approx(0.0).epsilon(0.001));
}

TEST_CASE("a drag over a multi-selection moves each instance by the same delta")
{
    DragRig rig;
    rig.look({0.0, 0.0, 0.0});
    const core::InstanceId first = rig.part({-1.0, 0.0, -30.0});
    const core::InstanceId second = rig.part({0.0, 0.0, -30.0});
    const core::InstanceId third = rig.part({1.0, 0.0, -30.0});
    rig.inspector.select(first);
    rig.inspector.add(second);
    rig.inspector.add(third);
    rig.editor.setSnap(false);

    // The gizmo sits on the primary, which is the last one added.
    const std::optional<GizmoFrame> frame = rig.editor.gizmoFrame(rig.world, rig.inspector);
    REQUIRE(frame.has_value());
    CHECK(frame->transform.position.x == doctest::Approx(1.0));

    const core::DVec3 grab =
        frame->transform.position + core::DVec3{static_cast<core::f64>(frame->size) * 0.7, 0.0, 0.0};
    rig.frame(rig.pixelOf(grab), true, true);
    rig.frame(rig.pixelOf(grab + core::DVec3{4.0, 0.0, 0.0}), false, true);
    rig.frame(rig.pixelOf(grab + core::DVec3{4.0, 0.0, 0.0}), false, false);

    // **A metre apart before, a metre apart after.** Broadcasting one absolute
    // value instead of a delta stacks all three on the one the gizmo sat on,
    // which is the failure this exists to prevent.
    const scene::PartComponent* a = rig.world.parts().find(first);
    const scene::PartComponent* b = rig.world.parts().find(second);
    const scene::PartComponent* c = rig.world.parts().find(third);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);
    CHECK(a->cframe.position.x == doctest::Approx(3.0).epsilon(0.01));
    CHECK(b->cframe.position.x == doctest::Approx(4.0).epsilon(0.01));
    CHECK(c->cframe.position.x == doctest::Approx(5.0).epsilon(0.01));
    // And nothing moved in the other two axes.
    CHECK(a->cframe.position.y == doctest::Approx(0.0).epsilon(0.001));
    CHECK(a->cframe.position.z == doctest::Approx(-30.0).epsilon(0.001));
}

TEST_CASE("the grid is what a drag lands on, and Alt suspends it")
{
    DragRig rig;
    rig.look({0.0, 0.0, 0.0});
    const core::InstanceId subject = rig.part({0.0, 0.0, -30.0});
    rig.inspector.select(subject);
    rig.editor.setSnap(true);
    rig.editor.setSnapStep(GizmoMode::Translate, 1.0f);

    const std::optional<GizmoFrame> frame = rig.editor.gizmoFrame(rig.world, rig.inspector);
    REQUIRE(frame.has_value());
    const core::DVec3 grab =
        frame->transform.position + core::DVec3{static_cast<core::f64>(frame->size) * 0.7, 0.0, 0.0};

    rig.frame(rig.pixelOf(grab), true, true);
    rig.frame(rig.pixelOf(grab + core::DVec3{3.4, 0.0, 0.0}), false, true);
    rig.frame(rig.pixelOf(grab + core::DVec3{3.4, 0.0, 0.0}), false, false);

    const scene::PartComponent* part = rig.world.parts().find(subject);
    REQUIRE(part != nullptr);
    CHECK(part->cframe.position.x == doctest::Approx(3.0).epsilon(0.001));

    // Held down, the same drag lands where the pointer is. The handle is grabbed
    // from where the gizmo is NOW -- it moved with the part, which is the whole
    // point of the first drag having worked.
    rig.editor.setSnapSuspended(true);
    const std::optional<GizmoFrame> moved = rig.editor.gizmoFrame(rig.world, rig.inspector);
    REQUIRE(moved.has_value());
    const core::DVec3 again =
        moved->transform.position + core::DVec3{static_cast<core::f64>(moved->size) * 0.7, 0.0, 0.0};
    rig.frame(rig.pixelOf(again), true, true);
    rig.frame(rig.pixelOf(again + core::DVec3{3.4, 0.0, 0.0}), false, true);
    rig.frame(rig.pixelOf(again + core::DVec3{3.4, 0.0, 0.0}), false, false);
    CHECK(part->cframe.position.x == doctest::Approx(6.4).epsilon(0.01));
}

TEST_CASE("a press on a handle does not also select what is behind it")
{
    DragRig rig;
    rig.look({0.0, 0.0, 0.0});
    const core::InstanceId subject = rig.part({0.0, 0.0, -30.0});
    // Something big behind it, which a stray pick would land on.
    const core::InstanceId behind = rig.part({0.0, 0.0, -60.0});
    rig.inspector.select(subject);

    const std::optional<GizmoFrame> frame = rig.editor.gizmoFrame(rig.world, rig.inspector);
    REQUIRE(frame.has_value());
    const core::DVec3 grab =
        frame->transform.position + core::DVec3{static_cast<core::f64>(frame->size) * 0.7, 0.0, 0.0};

    // The panel queues a pick for the same click, because it cannot know the
    // gizmo took it. The manipulator runs first and consumes it.
    rig.editor.requestPick(rig.pixelOf(grab));
    rig.editor.setPointer(rig.pixelOf(grab), true, true);
    CHECK(rig.editor.driveGizmo(rig.world, rig.inspector));
    CHECK_FALSE(rig.editor.pickPending());
    CHECK(rig.inspector.selection() == subject);
    (void)behind;
}

TEST_CASE("a manipulator four kilometres out is submitted where it is, not where a float can reach")
{
    // The gate item, and it is the same defect the selection outline had:
    // `DebugDraw::rebaseTo` subtracts in f32, so a submission in world
    // coordinates quantises the absolute metre value BEFORE the camera comes off
    // it. At four kilometres that is about half a millimetre, on the one thing
    // in the frame somebody is trying to place precisely.
    DragRig rig;
    const core::DVec3 eye{4000.0, 12.0, -4000.0};
    rig.look(eye);
    const core::DVec3 at{4000.0, 12.0, -4030.0};
    const core::InstanceId subject = rig.part(at);
    rig.inspector.select(subject);

    const std::optional<GizmoFrame> frame = rig.editor.gizmoFrame(rig.world, rig.inspector);
    REQUIRE(frame.has_value());

    render::DebugDraw draw;
    app::submitGizmo(*frame, GizmoMode::Translate, std::nullopt, eye, draw);
    REQUIRE_FALSE(draw.empty());

    // Every vertex is already camera-relative, so the whole gizmo is within its
    // own size of the origin of that space -- which is what makes a float exact
    // enough for it. Submitted in world coordinates these would be four
    // thousand, and the tenth of a millimetre below is what f32 cannot hold
    // there.
    const core::Vec3 expected = core::toVec3(at - eye);
    for (const render::DebugVertex& vertex : draw.vertices()) {
        CHECK(std::abs(vertex.position.x - expected.x) < frame->size * 2.0f);
        CHECK(std::abs(vertex.position.y - expected.y) < frame->size * 2.0f);
        CHECK(std::abs(vertex.position.z - expected.z) < frame->size * 2.0f);
    }

    // And the centre is exactly where the part is, to a tenth of a millimetre.
    const render::DebugVertex& first = draw.vertices()[0];
    const core::Vec3 fromCentre{first.position.x - expected.x, first.position.y - expected.y,
                                first.position.z - expected.z};
    CHECK(core::length(fromCentre) < frame->size * 1.5f);
}

// --- E2 / ADR 0048: a script is a file, and the editor writes it ------------

TEST_CASE("a new script becomes a file under src/scripts, and never overwrites one")
{
    // `Script` is `NotCreatable` and that stays right: `Instance.new("Script")`
    // inside a sandboxed game VM has no filesystem to put a file on (R4). The
    // editor is not that VM, so it makes the FILE and the instance appears
    // because the mount finds it -- the rule honoured rather than bypassed.
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "luaug-newscript-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    Editor editor;
    REQUIRE(editor.createScript(root, "patrol"));

    const std::filesystem::path written = root / "src" / "scripts" / "patrol.luau";
    REQUIRE(std::filesystem::exists(written));

    std::string source;
    REQUIRE(luaug::platform::readTextFile(written, source));
    // `--!strict` because R2 says every Luau file in this engine is, and a
    // template that starts a project off wrong teaches wrong.
    CHECK(source.starts_with("--!strict"));
    CHECK(source.find('\t') != std::string::npos);

    // The extension is the editor's to add and not the person's to remember.
    REQUIRE(editor.createScript(root, "chase.luau"));
    CHECK(std::filesystem::exists(root / "src" / "scripts" / "chase.luau"));

    // **Never over an existing file.** A "new script" that replaced somebody's
    // work would be the worst button in this editor, and the failure is silent
    // by nature -- the tree looks the same either way.
    CHECK_FALSE(editor.createScript(root, "patrol"));
    CHECK(editor.status().failed);

    // And a name a file cannot carry is refused rather than written somewhere
    // surprising.
    CHECK_FALSE(editor.createScript(root, "../escape"));
    CHECK_FALSE(std::filesystem::exists(root.parent_path() / "escape.luau"));

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}
