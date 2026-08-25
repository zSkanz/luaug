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

namespace luaug::app {

class ScriptEditor;
struct DebugView;

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

    [[nodiscard]] bool any() const noexcept
    {
        return save.has_value() || close.has_value() || saveAll || reload || !edited.empty() ||
               toggleBreakpointLine.has_value();
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
void drawScriptEditor(ScriptEditor& editor, core::u32 dockNode, ScriptEditorCommands& out);

} // namespace luaug::app
