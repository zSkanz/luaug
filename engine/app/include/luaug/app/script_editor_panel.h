// The code pane's seam (ADR 0057).
//
// The panel draws and decides nothing, exactly as `EditorCommands` established:
// saving walks a file, reloading replaces the world, and neither may happen
// inside an ImGui callback while a panel behind this one is drawing from the
// same world. So the pane records intent here and the frame loop acts on it at
// the safe point.
//
// Declared unconditionally and inert in a shipping build, the shape ADR 0011
// asks for -- the caller carries no `#ifdef`.
#pragma once

#include "luaug/core/types.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace luaug::scene {
class World;
}

namespace luaug::app {

class ScriptEditor;

// What the debugger looks like to the panel: enough to draw, and nothing that
// has to be kept alive.
//
// A copy rather than a reference into `engine/script`, and the reason is the
// layering: `app` may see everything, but a panel holding a pointer into the VM
// would be a panel holding something a reload destroys. The frame loop fills
// this at the safe point from `Debugger::snapshot()`.
struct DebugValueView
{
    std::string name;
    std::string type;
    std::string preview;
};

struct DebugFrameView
{
    std::string function;
    std::string chunk;
    core::u32 line = 0;
    std::vector<DebugValueView> locals;
    std::vector<DebugValueView> upvalues;
};

struct DebugView
{
    bool parked = false;
    // Where execution is stopped. The pane marks this line in the gutter when
    // the chunk matches its own.
    std::string chunk;
    core::u32 line = 0;
    std::vector<DebugFrameView> frames;
    // Which frame the panel is looking at. Read and written by the panel.
    std::size_t selectedFrame = 0;
};

// What the transport asked for.
enum class DebugStep : core::u8
{
    None,
    Continue,
    Over,
    Into,
    Out,
};

// What the pane decided while it drew.
struct ScriptEditorCommands
{
    // A tab to write out: its index. Where it goes is the tab's own business --
    // its file when it has one, the scene otherwise.
    std::optional<std::size_t> save;
    std::optional<std::size_t> close;
    bool saveAll = false;

    // **The world should be rebuilt from source.** The editor's own reload,
    // which `luaug edit` has never had because it is started without
    // `--dev-control` and the only call site was gated on it.
    bool reload = false;

    // Tabs whose text moved this frame, so the loop can put it into the
    // instance's `Source` at the safe point. Indices rather than pointers,
    // because a tab can close between the draw and the drain.
    std::vector<std::size_t> edited;

    // Somebody clicked the gutter. Acted on by the loop so the debugger and the
    // panel cannot disagree about which lines are armed.
    std::optional<core::u32> toggleBreakpointLine;

    // Continue, or a step. The loop hands it to the debugger, which is the only
    // thing that may resume a parked coroutine.
    DebugStep step = DebugStep::None;

    [[nodiscard]] bool any() const noexcept
    {
        return save.has_value() || close.has_value() || saveAll || reload || !edited.empty() ||
               toggleBreakpointLine.has_value() || step != DebugStep::None;
    }
};

// Draws every open script as a sibling of the Viewport in the central dock node.
//
// A window per tab rather than a tab bar of our own: the dockspace already makes
// siblings in one node into a tab strip, and it also lets somebody drag one out
// to sit beside the world instead of over it -- which is the arrangement asked
// for and which a hand-rolled tab bar would have refused.
// `dockNode` is the dockspace's central node -- where the Viewport lives -- so a
// script opened for the first time appears beside it rather than floating in the
// middle of the screen. Zero docks nothing, which is what a shell with no
// dockspace wants. `ImGuiID` is an unsigned int; taking it as one is what keeps
// this header free of ImGui.
void drawScriptEditor(ScriptEditor& editor, core::u32 dockNode, DebugView& debug, const scene::World* world,
                      ScriptEditorCommands& out);

// The stack, the variables and the transport. A panel of its own rather than a
// strip inside the code pane, because it is worth looking at while looking at
// the code -- which is what a dock node is for.
void drawDebugPanel(ScriptEditor& editor, DebugView& debug, ScriptEditorCommands& out, bool& open);

} // namespace luaug::app
