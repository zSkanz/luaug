// The open scripts, asserted without a window (ADR 0057).
//
// This is the editor's first multi-document surface, so the cases here are the
// ones every editor gets wrong once: opening the same thing twice, closing the
// tab you were looking at, and a document that says it is saved and is not.
#include "luaug/app/script_editor.h"
#include "luaug/app/script_editor_panel.h"
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
using luaug::app::parseSourceLocation;
using luaug::app::SourceLocation;

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

// --- Which console lines are links (S5.11) -----------------------------------
//
// **An error in the console names a file and a line and nothing takes you to
// it**, which is the difference between a console and a log file. What decides
// whether a line becomes a link is this parser, and what it must not do is turn
// ordinary output into something that looks clickable and goes somewhere wrong.

TEST_CASE("a runtime error's own location is found")
{
    const std::optional<SourceLocation> at =
        parseSourceLocation("init.luau:183: [scene.err.unknown_member] RunService has no member named \"Stepped\"");
    REQUIRE(at.has_value());
    CHECK(at->chunk == "init.luau");
    CHECK(at->line == 183u);
}

TEST_CASE("a chunk with directories in it keeps them")
{
    // The chunk name is what the VM gave the chunk, and matching a tab means
    // matching it exactly -- half of it would find nothing.
    const std::optional<SourceLocation> at = parseSourceLocation("src/scripts/init.luau:140: something went wrong");
    REQUIRE(at.has_value());
    CHECK(at->chunk == "src/scripts/init.luau");
    CHECK(at->line == 140u);
}

TEST_CASE("the FIRST location is the one, because the rest is the traceback")
{
    // Luau puts the raise site at the front and appends how it got there behind.
    // Jumping to the last would land in whatever called the thing that failed,
    // which is nearly always the wrong file.
    const std::optional<SourceLocation> at =
        parseSourceLocation("deep.luau:12: attempt to index nil\\nstack: caller.luau:99");
    REQUIRE(at.has_value());
    CHECK(at->chunk == "deep.luau");
    CHECK(at->line == 12u);
}

TEST_CASE("a log line with a prefix in brackets is still parsed")
{
    // What the console actually holds: the sink writes `[error] [key] message`.
    const std::optional<SourceLocation> at =
        parseSourceLocation("[error] [script.err.runtime] Error while running a handler: a.luau:7: boom");
    REQUIRE(at.has_value());
    CHECK(at->chunk == "a.luau");
    CHECK(at->line == 7u);
}

TEST_CASE("ordinary output is not a link")
{
    // A line that happens to contain a colon and a number is not a location, and
    // linking it would be a control that goes somewhere wrong -- worse than one
    // that is not there.
    CHECK_FALSE(parseSourceLocation("Loaded a scene of 400 instance(s).").has_value());
    CHECK_FALSE(parseSourceLocation("resident 12 loading 3 decoded 0 failed 0").has_value());
    CHECK_FALSE(parseSourceLocation("12:34:56 something happened").has_value());
    CHECK_FALSE(parseSourceLocation("").has_value());
}

TEST_CASE("a .luau with no line after it is not a location")
{
    // A path in a message is a path. Only a path with a line number on it is
    // somewhere to go.
    CHECK_FALSE(parseSourceLocation("mounted src/scripts/init.luau").has_value());
    CHECK_FALSE(parseSourceLocation("init.luau: no line here").has_value());
}

TEST_CASE("line zero is not a line")
{
    // Every editor and every error in the world counts lines from one, so a zero
    // is a parse that went wrong rather than the top of the file.
    CHECK_FALSE(parseSourceLocation("a.luau:0: nowhere").has_value());
}

TEST_CASE("an absurd run of digits is refused rather than wrapping into a plausible line")
{
    // Multiplying through it would overflow into a number that looks real, and
    // the click would scroll somewhere arbitrary in a file that is fine.
    CHECK_FALSE(parseSourceLocation("a.luau:99999999999999: nope").has_value());
}

TEST_CASE("saving the scene saves every script the scene carries, and not a file's")
{
    // **Reported with a screenshot**: one scene script saved, and the others
    // kept the floppy -- although the scene they live in had just been written
    // with their text in it.
    TwoScripts fixture;
    ScriptEditor editor;
    (void)editor.open(fixture.first, app::ScriptOrigin::Scene, "a", "", "a", "local x = 1");
    (void)editor.open(fixture.second, app::ScriptOrigin::Scene, "b", "src/scripts/b.luau", "b", "local y = 2");
    // By index once both are open: a second `open` may move the first tab.
    editor.at(0)->document.insert(Position{0, 0}, "-");
    editor.at(1)->document.insert(Position{0, 0}, "-");
    REQUIRE(editor.dirtyCount() == 2);

    editor.markSavedWhere(app::ScriptOrigin::Scene);
    CHECK_FALSE(editor.at(0)->dirty());
    // Its own file under src/scripts is not in the scene, so it is not saved.
    CHECK(editor.at(1)->dirty());
}

TEST_CASE("the minimap shows a short file whole and a long one scrolling with the code")
{
    // Twenty-pixel lines in the pane, three in the map, a 600-pixel map over
    // a 400-pixel view. `scrollMax` is what the pane's extent gives: one
    // line of air under the last.
    const auto scrollMax = [](core::u32 lines) { return static_cast<float>(lines + 1) * 20.0f - 400.0f; };

    // Fifty lines are 150 pixels of map: all of it shows, nothing scrolls, and
    // the slider is the view's share at a line's scale.
    const app::MinimapView shortFile = app::minimapView(50, 20.0f, 3.0f, 600.0f, 400.0f, 200.0f, scrollMax(50));
    CHECK(static_cast<double>(shortFile.offset) == doctest::Approx(0.0));
    CHECK(shortFile.first == 0);
    CHECK(shortFile.last == 49);
    CHECK(static_cast<double>(shortFile.sliderHeight) == doctest::Approx(60.0));
    CHECK(static_cast<double>(shortFile.sliderTop) == doctest::Approx(30.0));
    CHECK(static_cast<double>(shortFile.dragRatio) == doctest::Approx(20.0 / 3.0));

    // A thousand lines are 3000 pixels of map. At the top the map is at its
    // top; at the bottom its last line is at the map's bottom, and so is the
    // slider -- they arrive together, which is what "proportional" means.
    const app::MinimapView atTop = app::minimapView(1000, 20.0f, 3.0f, 600.0f, 400.0f, 0.0f, scrollMax(1000));
    CHECK(static_cast<double>(atTop.offset) == doctest::Approx(0.0));
    CHECK(static_cast<double>(atTop.sliderTop) == doctest::Approx(0.0));
    const app::MinimapView atEnd =
        app::minimapView(1000, 20.0f, 3.0f, 600.0f, 400.0f, scrollMax(1000), scrollMax(1000));
    CHECK(static_cast<double>(atEnd.offset) == doctest::Approx(2400.0));
    CHECK(atEnd.last == 999);
    CHECK(static_cast<double>(atEnd.sliderTop + atEnd.sliderHeight) == doctest::Approx(600.0));
    // Dragging the slider across the map's free height covers the whole file.
    CHECK(static_cast<double>(atTop.dragRatio * (600.0f - atTop.sliderHeight)) ==
          doctest::Approx(static_cast<double>(scrollMax(1000))));

    // A click outside the slider puts that line in the middle of the view.
    CHECK(static_cast<double>(app::minimapJump(atTop, 300.0f, 20.0f, 3.0f, 400.0f, scrollMax(1000))) ==
          doctest::Approx(1800.0));
    // And never past either end.
    CHECK(static_cast<double>(app::minimapJump(atTop, 1.0f, 20.0f, 3.0f, 400.0f, scrollMax(1000))) ==
          doctest::Approx(0.0));

    // Nothing to show is nothing, not a division by zero.
    const app::MinimapView empty = app::minimapView(0, 20.0f, 3.0f, 600.0f, 400.0f, 0.0f, 0.0f);
    CHECK(empty.last == 0);
}
