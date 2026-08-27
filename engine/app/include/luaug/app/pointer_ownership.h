#pragma once

// Who owns the mouse this frame: the person using the editor, or the game.
//
// **Four defects came out of this one question and every one was reported by a
// person** (S7.11). D049: the pointer lock did nothing at all, because the
// property was stored and read by nothing. D059: the editor opened on a project
// that locks its pointer at file scope, and had no cursor to click a panel
// with. D063: a right-drag to turn the camera hid the cursor and did not hold
// it, so it walked out of the window and reappeared somewhere else. D069: with
// the game holding the pointer, the invisible cursor still hovered and clicked
// rows in the Explorer that nobody could see.
//
// The answer was arithmetic inline in the frame loop, so nothing could reach it
// -- which is why all four arrived from a human rather than from a gate. It is a
// function here for the reason the shell's decisions are functions: a rule
// nobody can call is a rule nobody can test.
//
// Nothing in this header touches SDL or the world. It takes what is wanted and
// says what should be true, and the frame loop is what makes it so.

namespace luaug::app {

// What the frame knows before it decides.
struct PointerRequest
{
    // This is an editor build showing an editor. A player build never takes any
    // of the editor branches below.
    bool editorProfile = false;
    // The transport is not playing -- the world is paused and being edited.
    bool editing = false;
    // The view has been detached from the game camera, which is a fly camera
    // somebody drives with a right-drag.
    bool cameraDetached = false;
    // That right-drag is happening right now.
    bool lookActive = false;
    // `InputService.PointerLocked` and `PointerVisible`, as the GAME left them.
    bool gameWantsLocked = false;
    bool gameWantsVisible = true;
};

// What should be true of the pointer this frame.
struct PointerOwnership
{
    bool locked = false;
    bool visible = true;
    // What the overlay is told, so the panels can ignore a mouse nobody is
    // pointing (D069). It is NOT `locked`: the editor locks the pointer while
    // turning its own camera, and the panels must still be reachable the moment
    // the button comes up.
    bool gameHoldsPointer = false;
};

[[nodiscard]] PointerOwnership decidePointer(const PointerRequest& request) noexcept;

} // namespace luaug::app
