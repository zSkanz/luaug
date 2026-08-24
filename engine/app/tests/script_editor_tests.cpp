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

#include "class_descriptors.gen.h"

#include <doctest/doctest.h>
#include <ostream>
#include <string>

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

    editor.open(fixture.first, "src/scripts/a.luau", "src/scripts/a.luau", "a", "local x = 1");
    CHECK(editor.count() == 1);

    app::OpenScript* tab = editor.active();
    REQUIRE(tab != nullptr);
    tab->document.insert(Position{0, 11}, " -- edited");

    // Double-clicking it again in the Explorer is a focus, not a load.
    editor.open(fixture.first, "src/scripts/a.luau", "src/scripts/a.luau", "a", "local x = 1");
    CHECK(editor.count() == 1);
    CHECK(editor.active()->document.text() == "local x = 1 -- edited");
}

TEST_CASE("a second script is a second tab, and the newest is in front")
{
    TwoScripts fixture;
    ScriptEditor editor;

    editor.open(fixture.first, "a", "", "a", "");
    editor.open(fixture.second, "b", "", "b", "");
    CHECK(editor.count() == 2);
    CHECK(editor.activeIndex() == 1);
    CHECK(editor.indexOf(fixture.first).value() == 0);
    CHECK(editor.indexOf(fixture.second).value() == 1);
}

TEST_CASE("closing a tab leaves the eye where it already was")
{
    TwoScripts fixture;
    ScriptEditor editor;
    editor.open(fixture.first, "a", "", "a", "");
    editor.open(fixture.second, "b", "", "b", "");

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
    app::OpenScript& tab = editor.open(fixture.first, "a", "src/scripts/a.luau", "a", "local x = 1");

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
    editor.open(fixture.first, "a", "", "a", "");
    editor.open(fixture.second, "b", "", "b", "");

    fixture.world.destroy(fixture.first);
    fixture.world.retireDestroyed();

    // A script deleted from the Explorer, or every instance replaced by a hot
    // reload: a tab holding an id nothing answers to would draw a document
    // nobody could save.
    CHECK(editor.forgetDestroyed(fixture.world) == 1);
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
