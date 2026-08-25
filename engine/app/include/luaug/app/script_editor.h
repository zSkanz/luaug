// The open scripts: which are open, which is in front, which are unsaved, and
// where the breakpoints are (ADR 0057).
//
// **The editor's first multi-document surface.** Everything before it held one
// thing at a time -- one scene, one stamp session, and exactly two dirty bits in
// the whole application (`Editor::m_sceneDirty` and `StampSession::dirty`).
// Opening a stamp even REPLACES the world. Scripts are the first thing somebody
// has several of at once, so this is where per-document state starts existing.
//
// No ImGui, for the reason `inspector.h` gives: what a panel decides is testable
// and what it draws is a screenshot's business, and mixing the two is how a
// picking bug ends up only reproducible by clicking.
#pragma once

#include "luaug/app/script_complete.h"
#include "luaug/app/script_document.h"
#include "luaug/core/id.h"
#include "luaug/core/types.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace luaug::scene {
class World;
}

namespace luaug::app {

// Where the caret is and what it has hold of.
//
// `anchor` is where a selection began and `head` is where it is now, so dragging
// backwards is a selection like any other and nothing has to normalise at the
// call site.
struct Caret
{
    Position head;
    Position anchor;
    // **The column an up/down arrow is trying to return to.** Without it, moving
    // down through a short line and back up lands somewhere nobody asked for --
    // the one piece of state whose absence people notice within thirty seconds.
    core::u32 desiredColumn = 0;

    [[nodiscard]] bool hasSelection() const noexcept { return !(head == anchor); }
    [[nodiscard]] Range selection() const noexcept { return ordered(anchor, head); }
    void collapse() noexcept { anchor = head; }
};

// A breakpoint, keyed by the CHUNK rather than by the instance.
//
// **Because an instance does not survive a reload and a chunk name does.**
// `reloadWorld` destroys the whole `WorldHost`, so every `InstanceId` is a fresh
// one afterwards; the chunk name -- the file's path for a mounted script, its
// place in the tree for one the scene brought -- means the same thing in the new
// world, and is also exactly what `lua_Debug::source` reports when the VM stops.
struct Breakpoint
{
    std::string chunk;
    core::u32 line = 0;
    bool enabled = true;
    // Where the VM actually put it. Luau moves a breakpoint forward to the next
    // line carrying instructions and tells you which, so a marker on a comment
    // can be drawn where it will really stop instead of where it was clicked.
    // Zero means it has not been bound to a loaded chunk yet, which is the
    // normal state for a breakpoint set before the world runs.
    core::u32 boundLine = 0;
};

// One tab.
struct OpenScript
{
    core::InstanceId instance;
    // What the chunk is called: the file's project-relative path when the script
    // was mounted from one, its place in the tree otherwise. The key breakpoints
    // and the debugger both use.
    std::string chunk;
    // The file to write on Ctrl+S. **Empty means the scene owns this script**,
    // and saving it is saving the scene -- which is the whole of ADR 0057's
    // "where Ctrl+S sends the text is a property of where the instance came
    // from".
    std::string file;
    // What the tab says. The instance's name, which is short, rather than the
    // chunk, which is not.
    std::string title;

    ScriptDocument document;
    Caret caret;
    // Kept so a tab comes back where it was left. The panel writes it; nothing
    // else reads it.
    core::f32 scroll = 0.0f;

    // The revision the text had when it was last written out. **Dirty is a
    // comparison rather than a flag**, so there is no way to change the text and
    // forget to set it -- which is the defect a bool invites.
    core::u64 savedRevision = 0;

    // The revision seen on the previous frame. **How the pane knows the text is
    // at rest**: parsing on every keystroke would re-parse a file per character,
    // and parsing on a timer would need a clock in a panel. One frame after the
    // last edit is neither.
    core::u64 idleRevision = 0;

    // --- The find bar --------------------------------------------------------
    //
    // State rather than a dialog, because a search survives switching tabs and
    // coming back -- which is what somebody stepping through matches expects.
    bool findOpen = false;
    bool replaceOpen = false;
    bool matchCase = false;
    bool wholeWord = false;
    std::string findText;
    std::string replaceText;
    // What the last search matched, so the pane can highlight it and Enter can
    // step from it rather than from the caret.
    Range lastMatch;

    // --- Completion ----------------------------------------------------------
    //
    // On the tab rather than in the panel because it survives a frame in which
    // nothing was typed -- and because two tabs may each be half-way through a
    // word.
    bool completing = false;
    std::vector<Completion> completions;
    std::size_t completionIndex = 0;
    // What accepting a row replaces: the partial word, and nothing else.
    Range completionReplace;

    [[nodiscard]] bool dirty() const noexcept { return document.revision() != savedRevision; }
};

class ScriptEditor
{
public:
    // Opens `instance`, or focuses the tab that already has it. **Idempotent by
    // instance**, because double-clicking the same script twice is one document
    // -- and a second tab on the same text would be two undo histories editing
    // one thing.
    //
    // `source` seeds a NEW tab only. Re-opening does not overwrite what somebody
    // has been typing.
    OpenScript& open(core::InstanceId instance, std::string chunk, std::string file, std::string title,
                     std::string_view source);

    // Closes the tab at `index`. The next tab to be in front is the one to its
    // left, which is what leaves the eye where it already was.
    bool close(std::size_t index);
    void closeAll();

    [[nodiscard]] std::size_t count() const noexcept { return m_tabs.size(); }
    [[nodiscard]] std::span<const OpenScript> tabs() const noexcept { return m_tabs; }
    [[nodiscard]] OpenScript* at(std::size_t index) noexcept;
    [[nodiscard]] const OpenScript* at(std::size_t index) const noexcept;
    [[nodiscard]] OpenScript* active() noexcept;
    [[nodiscard]] std::size_t activeIndex() const noexcept { return m_active; }
    void setActive(std::size_t index) noexcept;

    [[nodiscard]] std::optional<std::size_t> indexOf(core::InstanceId instance) const noexcept;

    // **Which tab should be brought to the front, once.**
    //
    // Setting `m_active` is not enough and it is worth saying why: the tabs are
    // dock siblings, so which one is IN FRONT is ImGui's state and not this
    // class's. Somebody looking at the Viewport who double-clicks a script that
    // is already open would otherwise see nothing happen at all -- the model
    // would agree the script was active and the screen would still be showing
    // the world.
    //
    // Drained rather than read, so the focus is taken on the frame it was asked
    // for and never fights somebody who has since clicked another tab.
    [[nodiscard]] std::optional<std::size_t> takeFocusRequest() noexcept
    {
        const std::optional<std::size_t> taken = m_focusRequest;
        m_focusRequest.reset();
        return taken;
    }

    [[nodiscard]] bool anyDirty() const noexcept;
    [[nodiscard]] std::size_t dirtyCount() const noexcept;

    // Marks a tab as written out. Called after the file or the scene took the
    // text, never before -- a document that says it is saved and is not is the
    // one lie this class must never tell.
    void markSaved(std::size_t index) noexcept;

    // **Closes tabs whose instance is gone.** A script can be deleted from the
    // Explorer, and a hot reload replaces every instance in the world -- so a
    // tab holding an id nothing answers to would draw a document nobody could
    // save. Returns how many it closed.
    std::size_t forgetDestroyed(const scene::World& world);

    // --- Breakpoints ---------------------------------------------------------
    //
    // Held here rather than on a tab, because closing a file is not the same as
    // saying you no longer care where it stops -- and because they outlive the
    // world that the debugger sets them in.

    [[nodiscard]] std::span<const Breakpoint> breakpoints() const noexcept { return m_breakpoints; }
    // Adds one, or removes the one already on that line. Returns whether there
    // is now a breakpoint there, which is what a gutter click wants to know.
    bool toggleBreakpoint(std::string_view chunk, core::u32 line);
    void clearBreakpoints(std::string_view chunk);
    void clearAllBreakpoints() noexcept { m_breakpoints.clear(); }
    [[nodiscard]] bool hasBreakpoint(std::string_view chunk, core::u32 line) const noexcept;
    // Where the VM says it really landed, written back after binding.
    void setBoundLine(std::string_view chunk, core::u32 line, core::u32 boundLine) noexcept;

private:
    std::vector<OpenScript> m_tabs;
    std::size_t m_active = 0;
    std::optional<std::size_t> m_focusRequest;
    // Sorted by (chunk, line), so the order the debugger walks them is a
    // property of what they are rather than of when they were clicked (R10).
    std::vector<Breakpoint> m_breakpoints;
};

} // namespace luaug::app
