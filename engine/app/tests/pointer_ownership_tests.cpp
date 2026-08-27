// Who owns the mouse -- the four defects, as cases.
//
// **Every one of D049, D059, D063 and D069 was reported by a person**, because
// the rule was arithmetic inline in the frame loop and nothing could call it.
// These are that rule, asserted. A case here is named after the defect it would
// have caught, so a change that reintroduces one is told which report it is
// bringing back.
#include "luaug/app/pointer_ownership.h"

#include <doctest/doctest.h>

using luaug::app::decidePointer;
using luaug::app::PointerOwnership;
using luaug::app::PointerRequest;

TEST_CASE("a player build gives the game exactly what it asked for")
{
    // The whole of D049: the property is what decides, and nothing else is in
    // the way. It was stored and read by nothing at all.
    PointerRequest request;
    request.editorProfile = false;
    request.gameWantsLocked = true;
    request.gameWantsVisible = false;

    const PointerOwnership owned = decidePointer(request);
    CHECK(owned.locked);
    CHECK_FALSE(owned.visible);
    CHECK(owned.gameHoldsPointer);
}

TEST_CASE("D059: an editor opened on a project that locks its pointer still has a cursor")
{
    // `examples/10-open-world` locks the pointer at file scope for its mouse
    // look, and the boot drain runs that before the first frame -- so the editor
    // came up with no cursor and no way to click a panel.
    PointerRequest request;
    request.editorProfile = true;
    request.editing = true;
    request.gameWantsLocked = true;
    request.gameWantsVisible = false;

    const PointerOwnership owned = decidePointer(request);
    CHECK_FALSE(owned.locked);
    CHECK(owned.visible);
    CHECK_FALSE(owned.gameHoldsPointer);
}

TEST_CASE("pressing play hands the pointer back to the game")
{
    // The other half of the same rule, and the half that would go unnoticed: an
    // editor that never handed it back is a game whose mouse look does not work
    // when you press play, which reads as the game being broken.
    PointerRequest request;
    request.editorProfile = true;
    request.editing = false;
    request.gameWantsLocked = true;
    request.gameWantsVisible = false;

    const PointerOwnership owned = decidePointer(request);
    CHECK(owned.locked);
    CHECK_FALSE(owned.visible);
    CHECK(owned.gameHoldsPointer);
}

TEST_CASE("D063: turning the editor camera holds the pointer as well as hiding it")
{
    // Hiding without holding is a cursor that walks across the desktop and a
    // turn that stops when it leaves the window.
    PointerRequest request;
    request.editorProfile = true;
    request.editing = true;
    request.lookActive = true;

    const PointerOwnership owned = decidePointer(request);
    CHECK(owned.locked);
    CHECK_FALSE(owned.visible);
    // And it is the EDITOR holding it, not the game -- which is what keeps the
    // panels reachable the moment the button comes up.
    CHECK_FALSE(owned.gameHoldsPointer);
}

TEST_CASE("D063: letting go of the drag gives the cursor back")
{
    PointerRequest request;
    request.editorProfile = true;
    request.editing = true;
    request.lookActive = false;

    const PointerOwnership owned = decidePointer(request);
    CHECK_FALSE(owned.locked);
    CHECK(owned.visible);
}

TEST_CASE("D069: while the game holds the pointer the panels are told to ignore it")
{
    // Relative mode keeps posting motion with a logical position SDL
    // accumulates, so the invisible cursor hovers and clicks rows in the
    // Explorer that a player turning their head cannot see.
    PointerRequest request;
    request.editorProfile = true;
    request.editing = false;
    request.gameWantsLocked = true;

    CHECK(decidePointer(request).gameHoldsPointer);
}

TEST_CASE("D069: a game that did not lock the pointer never takes it from the panels")
{
    PointerRequest request;
    request.editorProfile = true;
    request.editing = false;
    request.gameWantsLocked = false;

    const PointerOwnership owned = decidePointer(request);
    CHECK_FALSE(owned.locked);
    CHECK_FALSE(owned.gameHoldsPointer);
}

TEST_CASE("a detached view owns the pointer even while the game is playing")
{
    // Not a second decision: the fly camera is a right-drag, and a game holding
    // the pointer means the drag never arrives -- so detaching without this is a
    // camera nobody can turn.
    PointerRequest request;
    request.editorProfile = true;
    request.editing = false;
    request.cameraDetached = true;
    request.gameWantsLocked = true;
    request.gameWantsVisible = false;

    const PointerOwnership owned = decidePointer(request);
    CHECK_FALSE(owned.locked);
    CHECK(owned.visible);
    CHECK_FALSE(owned.gameHoldsPointer);

    // And turning it takes the pointer for the EDITOR, not for the game.
    request.lookActive = true;
    const PointerOwnership turning = decidePointer(request);
    CHECK(turning.locked);
    CHECK_FALSE(turning.visible);
    CHECK_FALSE(turning.gameHoldsPointer);
}

TEST_CASE("a look that is not the editor's own is not a look at all")
{
    // `lookActive` is the editor's right-drag. In a player build there is no
    // editor camera to turn, so it may not reach the answer by any path.
    PointerRequest request;
    request.editorProfile = false;
    request.lookActive = true;
    request.gameWantsVisible = true;

    const PointerOwnership owned = decidePointer(request);
    CHECK_FALSE(owned.locked);
    CHECK(owned.visible);
}
