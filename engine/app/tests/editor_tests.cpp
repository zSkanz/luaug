// The editor's model, minus the pixels — the same split `inspector_tests.cpp`
// makes and for the same reason. What a click DECIDES is checkable here; what
// the panel draws is a screenshot's business.
//
// The case that matters most is the last one: clicking empty space clears the
// selection. It is the behaviour a person notices only by editing the object
// they thought they had let go of, which is to say only after doing damage.
#include "luaug/app/editor.h"
#include "luaug/core/math.h"
#include "luaug/scene/components.h"
#include "luaug/scene/world.h"

#include <cmath>
#include <doctest/doctest.h>

#include "inspector_fixture.h"

using namespace luaug;
using luaug::app::Editor;
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
    Editor editor;
    editor.requestStep();

    CHECK(editor.allowedTicks(4) == 1);
    // The next frame owes ticks again and must not take one: a step that
    // repeated while the world stayed paused would be play with extra steps.
    CHECK(editor.allowedTicks(4) == 0);
}

TEST_CASE("a step asked for on a frame that owes nothing is not swallowed")
{
    Editor editor;
    editor.requestStep();

    CHECK(editor.allowedTicks(0) == 0);
    CHECK(editor.allowedTicks(1) == 1);
    CHECK(editor.allowedTicks(1) == 0);
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
