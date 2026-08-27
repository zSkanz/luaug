#include <luaug/app/pointer_ownership.h>

namespace luaug::app {

PointerOwnership decidePointer(const PointerRequest& request) noexcept
{
    // **In the editor the cursor belongs to the person, not to the game**
    // (D059). The rule Unity and Unreal both use: while the world is paused the
    // editor owns the device, and pressing play hands it back.
    //
    // **A detached view owns it too**, and that is not a second decision. The
    // fly camera is driven by a right-drag, and a game holding the pointer
    // (D069) means the drag never reaches the editor -- so detaching without
    // this is a camera nobody can turn.
    const bool editorOwns = request.editorProfile && (request.editing || request.cameraDetached);

    // **While turning, the pointer is hidden and HELD.** That is SDL's relative
    // mode, and holding is what puts the cursor back where the button went down
    // (D063). Hiding without holding is a cursor that walks across the desktop
    // and a turn that stops when it leaves the window.
    const bool looking = editorOwns && request.lookActive;

    PointerOwnership out;
    out.locked = editorOwns ? looking : request.gameWantsLocked;
    out.visible = editorOwns ? !looking : request.gameWantsVisible;

    // **Handed back means handed back** (D069), and only when the GAME is the
    // one holding it. Relative mode keeps posting motion with a logical
    // position SDL accumulates, so an invisible cursor walks across the panels
    // hovering and clicking things a player turning their head cannot see.
    out.gameHoldsPointer = out.locked && !editorOwns;
    return out;
}

} // namespace luaug::app
