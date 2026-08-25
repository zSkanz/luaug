// The open scripts, asserted without a window (ADR 0057).
//
// This is the editor's first multi-document surface, so the cases here are the
// ones every editor gets wrong once: opening the same thing twice, closing the
// tab you were looking at, and a document that says it is saved and is not.
#include "luaug/app/script_editor.h"
#include "luaug/core/name_atom.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/enum_registry.h"
#include "luaug/scene/world.h"

#include <doctest/doctest.h>
#include <ostream>
#include <string>

#include "class_descriptors.gen.h"

using namespace luaug;
using app::Position;
using app::ScriptEditor;

namespace {

// A world with two real `Script` instances in it, so the cases that need an id
// have one a `World` will answer for -- alive while it is, and not once it is
// destroyed, which is the whole of the tab-closing case.
struct TwoScripts
{
    core::AtomTable atoms;
    scene::ClassRegistry classes;
    scene::EnumRegistry enums;
    scene::World world;
    core::InstanceId first;
    core::InstanceId second;

    TwoScripts() : world(classes, enums, atoms, 1234u)
    {
        scene::generated::registerEnums(enums, atoms);
        scene::generated::registerClasses(classes, atoms);
        const scene::ClassId scriptClass = classes.findId(atoms.intern("Script"));
        REQUIRE(scriptClass != scene::InvalidClass);
        first = world.create(scriptClass);
        second = world.create(scriptClass);
    }
};

} // namespace

TEST_CASE("opening the same instance twice is one tab, and does not lose typing")
{
    TwoScripts fixture;
    ScriptEditor editor;

    editor.open(fixture.first, app::ScriptOrigin::Scene, "src/scripts/a.luau", "src/scripts/a.luau", "a",
                "local x = 1");
    CHECK(editor.count() == 1);

    app::OpenScript* tab = editor.active();
    REQUIRE(tab != nullptr);
    tab->document.insert(Position{0, 11}, " -- edited");

    // Double-clicking it again in the Explorer is a focus, not a load.
    editor.open(fixture.first, app::ScriptOrigin::Scene, "src/scripts/a.luau", "src/scripts/a.luau", "a",
                "local x = 1");
    CHECK(editor.count() == 1);
    CHECK(editor.active()->document.text() == "local x = 1 -- edited");
}

TEST_CASE("a second script is a second tab, and the newest is in front")
{
    TwoScripts fixture;
    ScriptEditor editor;

    editor.open(fixture.first, app::ScriptOrigin::Scene, "a", "", "a", "");
    editor.open(fixture.second, app::ScriptOrigin::Scene, "b", "", "b", "");
    CHECK(editor.count() == 2);
    CHECK(editor.activeIndex() == 1);
    CHECK(editor.indexOf(fixture.first, app::ScriptOrigin::Scene).value() == 0);
    CHECK(editor.indexOf(fixture.second, app::ScriptOrigin::Scene).value() == 1);
}

TEST_CASE("closing a tab leaves the eye where it already was")
{
    TwoScripts fixture;
    ScriptEditor editor;
    editor.open(fixture.first, app::ScriptOrigin::Scene, "a", "", "a", "");
    editor.open(fixture.second, app::ScriptOrigin::Scene, "b", "", "b", "");

    // Closing the one in front falls back to the one on its left.
    CHECK(editor.close(1));
    CHECK(editor.count() == 1);
    CHECK(editor.activeIndex() == 0);
    CHECK(editor.active()->title == "a");

    CHECK(editor.close(0));
    CHECK(editor.count() == 0);
    CHECK(editor.active() == nullptr);
    // Out of range is a refusal, not a crash: the panel and the loop are a frame
    // apart and an index can outlive what it named.
    CHECK_FALSE(editor.close(0));
}

TEST_CASE("dirty is a comparison, so it cannot be forgotten")
{
    TwoScripts fixture;
    ScriptEditor editor;
    app::OpenScript& tab =
        editor.open(fixture.first, app::ScriptOrigin::Scene, "a", "src/scripts/a.luau", "a", "local x = 1");

    CHECK_FALSE(tab.dirty());
    CHECK_FALSE(editor.anyDirty());

    tab.document.insert(Position{0, 0}, "-");
    CHECK(tab.dirty());
    CHECK(editor.dirtyCount() == 1);

    editor.markSaved(0);
    CHECK_FALSE(tab.dirty());

    // Undoing back to the saved text is still a new revision, and saying so is
    // the honest answer: what was written out is not what is in the buffer's
    // history, and pretending otherwise means a file that never gets rewritten.
    Position caret{0, 0};
    CHECK(tab.document.undo(caret));
    CHECK(tab.dirty());
}

TEST_CASE("a tab whose instance is gone closes itself")
{
    TwoScripts fixture;
    ScriptEditor editor;
    editor.open(fixture.first, app::ScriptOrigin::Scene, "a", "", "a", "");
    editor.open(fixture.second, app::ScriptOrigin::Scene, "b", "", "b", "");

    fixture.world.destroy(fixture.first);
    fixture.world.retireDestroyed();

    // A script deleted from the Explorer, or every instance replaced by a hot
    // reload: a tab holding an id nothing answers to would draw a document
    // nobody could save.
    CHECK(editor.forgetDestroyed(fixture.world, nullptr) == 1);
    CHECK(editor.count() == 1);
    CHECK(editor.active()->title == "b");
}

TEST_CASE("breakpoints are keyed by chunk, so they outlive both the tab and the world")
{
    ScriptEditor editor;

    CHECK(editor.toggleBreakpoint("src/scripts/a.luau", 12));
    CHECK(editor.hasBreakpoint("src/scripts/a.luau", 12));
    CHECK_FALSE(editor.hasBreakpoint("src/scripts/a.luau", 13));
    CHECK_FALSE(editor.hasBreakpoint("src/scripts/b.luau", 12));

    // Toggling the same line takes it away.
    CHECK_FALSE(editor.toggleBreakpoint("src/scripts/a.luau", 12));
    CHECK_FALSE(editor.hasBreakpoint("src/scripts/a.luau", 12));

    // **Closing every tab does not forget them**, because closing a file is not
    // saying you no longer care where it stops.
    editor.toggleBreakpoint("src/scripts/a.luau", 5);
    editor.toggleBreakpoint("Workspace.Rig.Walk", 2);
    editor.closeAll();
    CHECK(editor.breakpoints().size() == 2);

    // Sorted by chunk then line, so every walk of the list is in the same order
    // without any of them saying so.
    CHECK(editor.breakpoints()[0].chunk == "Workspace.Rig.Walk");
    CHECK(editor.breakpoints()[1].chunk == "src/scripts/a.luau");

    editor.clearBreakpoints("src/scripts/a.luau");
    CHECK(editor.breakpoints().size() == 1);
}

TEST_CASE("a bound line is where the VM really put the breakpoint")
{
    ScriptEditor editor;
    editor.toggleBreakpoint("src/scripts/a.luau", 7);

    // Luau moves a breakpoint forward to the next line carrying instructions and
    // says which -- so a marker clicked on a comment can be drawn where it will
    // actually stop rather than where the click was.
    editor.setBoundLine("src/scripts/a.luau", 7, 9);
    CHECK(editor.breakpoints()[0].line == 7);
    CHECK(editor.breakpoints()[0].boundLine == 9);
}

TEST_CASE("opening a script that is already open asks for its tab to be shown")
{
    TwoScripts fixture;
    ScriptEditor editor;

    editor.open(fixture.first, app::ScriptOrigin::Scene, "a", "", "a", "local x = 1");
    // Drained by the panel on the frame it draws.
    CHECK(editor.takeFocusRequest().value() == 0);
    CHECK_FALSE(editor.takeFocusRequest().has_value());

    editor.open(fixture.second, app::ScriptOrigin::Scene, "b", "", "b", "");
    CHECK(editor.takeFocusRequest().value() == 1);

    // **The case this exists for.** Somebody looking at the Viewport
    // double-clicks a script that is already open: the model already agrees it
    // is active, and without this nothing on the screen would move, because
    // which dock sibling is in front is ImGui's state rather than ours.
    editor.setActive(1);
    editor.open(fixture.first, app::ScriptOrigin::Scene, "a", "", "a", "ignored");
    CHECK(editor.activeIndex() == 0);
    CHECK(editor.takeFocusRequest().value() == 0);
    // And it is still a focus rather than a load.
    CHECK(editor.at(0)->document.text() == "local x = 1");

    // A close cannot leave a request pointing at an index that has moved.
    editor.open(fixture.second, app::ScriptOrigin::Scene, "b", "", "b", "");
    CHECK(editor.close(1));
    CHECK_FALSE(editor.takeFocusRequest().has_value());
}

TEST_CASE("two worlds hand out the same ids, and a tab knows which one it came from")
{
    // **The case that makes `ScriptOrigin` exist.** A stamp is edited in a world
    // of its own (ADR 0049), and two `World`s allocate from their own slotmaps:
    // the first instance in each has the same handle. A tab keyed on the id
    // alone therefore answered about whichever world it was asked -- the wrong
    // name, the wrong `Source`, and typing written into an unrelated instance.
    TwoScripts scene;
    TwoScripts stamp;
    REQUIRE(scene.first == stamp.first);

    ScriptEditor editor;
    editor.open(scene.first, app::ScriptOrigin::Scene, "Workspace.A", "", "A", "-- scene");
    editor.open(stamp.first, app::ScriptOrigin::Stamp, "Rig.B", "", "B", "-- stamp");

    // Two tabs, not one focus of the same tab.
    CHECK(editor.count() == 2);
    REQUIRE(editor.indexOf(scene.first, app::ScriptOrigin::Scene).has_value());
    REQUIRE(editor.indexOf(stamp.first, app::ScriptOrigin::Stamp).has_value());
    CHECK(editor.indexOf(scene.first, app::ScriptOrigin::Scene) !=
          editor.indexOf(stamp.first, app::ScriptOrigin::Stamp));
    CHECK(editor.at(*editor.indexOf(stamp.first, app::ScriptOrigin::Stamp))->document.text() == "-- stamp");
}

TEST_CASE("a tab is closed by the world it belongs to and by no other")
{
    TwoScripts scene;
    TwoScripts stamp;

    ScriptEditor editor;
    editor.open(scene.first, app::ScriptOrigin::Scene, "Workspace.A", "", "A", "");
    editor.open(stamp.first, app::ScriptOrigin::Stamp, "Rig.B", "", "B", "");

    // Destroying the scene's instance must not take the stamp's tab with it,
    // even though the two ids are equal.
    REQUIRE(scene.world.destroy(scene.first));
    scene.world.retireDestroyed();
    CHECK(editor.forgetDestroyed(scene.world, &stamp.world) == 1);
    CHECK(editor.count() == 1);
    CHECK(editor.at(0)->origin == app::ScriptOrigin::Stamp);
}

TEST_CASE("closing the stamp session closes the tabs that lived in it")
{
    // A stamp tab has nowhere left to be edited once the session is gone, which
    // is as gone as a deleted instance -- and leaving it open would leave a
    // document pointing into a world that no longer exists.
    TwoScripts scene;
    TwoScripts stamp;

    ScriptEditor editor;
    editor.open(scene.first, app::ScriptOrigin::Scene, "Workspace.A", "", "A", "");
    editor.open(stamp.first, app::ScriptOrigin::Stamp, "Rig.B", "", "B", "");

    CHECK(editor.forgetDestroyed(scene.world, nullptr) == 1);
    REQUIRE(editor.count() == 1);
    CHECK(editor.at(0)->origin == app::ScriptOrigin::Scene);
}
