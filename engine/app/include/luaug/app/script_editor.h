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

// **Which world a tab's instance lives in.**
//
// An `InstanceId` is a handle into ONE world's slotmap, and this editor has two:
// the scene's, and the separate world an open stamp is edited in (ADR 0049). An
// id from one asked of the other answers about whatever instance happens to
// occupy that slot -- so a tab that did not record where it came from showed the
// wrong name, read the wrong `Source`, and wrote what somebody typed into a
// different instance entirely.
//
// An enum rather than a `World*` on purpose: a stage's world is destroyed when
// the stamp is closed, and a stale pointer compared against a fresh allocation
// that reused the address is a worse version of the same bug.
enum class ScriptOrigin : core::u8
{
    // The project's own world -- what the Explorer shows with no stamp open.
    Scene,
    // The world the open stamp is edited in. A tab of this kind has nowhere to
    // live once that session closes, and closes with it.
    Stamp,
    // **No world at all**: a file of the project that is not a script -- a
    // surface shader (ADR 0091). `instance` is invalid, `file` is the path
    // under the content root, and Ctrl+S writes it there. Nothing runs it, so
    // there is nothing to debug and no instance for it to outlive.
    File,
};

// One tab.
// **The minimap's arithmetic** (the owner: "the thing our VS Code has, the
// whole code small on the right, with the errors"), apart from its drawing so
// it can be tested. Everything is in pixels, and `scroll` is the code pane's.
//
// Proportional, as the reference editor's default is: a line is `lineStep`
// tall in the map, and when the map is taller than its pane it scrolls with
// the code, so its top and bottom meet the file's at the same moment the
// code's do.
struct MinimapView
{
    // How far the map's content is scrolled, and which lines can be seen.
    float offset = 0.0f;
    core::u32 first = 0;
    core::u32 last = 0;
    // The slider -- the part of the file the code pane shows -- relative to
    // the map's top.
    float sliderTop = 0.0f;
    float sliderHeight = 0.0f;
    // Pixels of code scroll per pixel the slider is dragged.
    float dragRatio = 1.0f;
};

[[nodiscard]] MinimapView minimapView(core::u32 lineCount, float lineHeight, float lineStep, float mapHeight,
                                      float viewHeight, float scroll, float scrollMax) noexcept;

// Where the code scrolls to for a click at `y` (relative to the map's top)
// outside the slider: that line in the middle of the pane.
[[nodiscard]] float minimapJump(const MinimapView& view, float y, float lineHeight, float lineStep, float viewHeight,
                                float scrollMax) noexcept;

// **Which document line is on which row of the pane**, once blocks are
// folded (see `ScriptDocument::FoldRange`). A folded block keeps its first and
// last lines and hides the rest, so every other line moves up by what it
// hides. Pure, so the arithmetic every drawing and every click depends on is
// tested without a window.
struct FoldView
{
    // Row to line, one entry per row the pane draws.
    std::vector<core::u32> rowLine;
    // Line to row; a hidden line maps to the row of the line that folded it.
    std::vector<core::u32> lineRow;

    [[nodiscard]] core::u32 rows() const noexcept { return static_cast<core::u32>(rowLine.size()); }
    [[nodiscard]] bool hidden(core::u32 line) const noexcept
    {
        return line < lineRow.size() && rowLine[lineRow[line]] != line;
    }
};

// `folded` holds the `first` line of every folded range; a fold inside a
// folded one is hidden with it.
[[nodiscard]] FoldView foldView(core::u32 lineCount, std::span<const ScriptDocument::FoldRange> ranges,
                                std::span<const core::u32> folded);

struct OpenScript
{
    core::InstanceId instance;
    // See `ScriptOrigin`. Read before every use of `instance`.
    ScriptOrigin origin = ScriptOrigin::Scene;
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
    // The PRIMARY caret: the one the view follows, completion answers at, and
    // every command that is about one place acts on.
    Caret caret;
    // **The secondary carets** (multi-cursor editing, as the reference editor
    // and every code editor have it): what is typed at the primary is typed at
    // each of these too. In the order they were added, so Ctrl+U takes back
    // the most recent; Escape drops them all.
    std::vector<Caret> extraCarets;

    // **When and where the text was last typed into**, so a diagnostic on the
    // line somebody is still writing waits until they stop (the owner: "it
    // shows the error before I have finished writing"). Interface time, which
    // R10 does not govern.
    double lastEditTime = -1e9;
    core::u32 lastEditLine = ~0u;

    // **The line an error in the console was clicked through to**, marked in
    // the debugger's error-line colour until the text is edited.
    std::optional<core::u32> errorLine;
    // Kept so a tab comes back where it was left. The panel writes it; nothing
    // else reads it.
    core::f32 scroll = 0.0f;

    // The caret position the pane last scrolled to. **How it knows the caret
    // moved**, which is the only moment a pane should move the view: scrolling
    // every frame would fight the scrollbar, and never scrolling leaves somebody
    // arrowing down into a document they cannot see.
    Position shownCaret{~0u, 0};

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
    bool regex = false;
    std::string findText;
    std::string replaceText;
    // Give the find field the keyboard the next time the box is drawn.
    bool focusFind = false;
    // Every match of the find, for the highlights and "3 of 12" -- recomputed
    // when the text, the query or an option changes, not every frame.
    std::vector<Range> matches;
    core::u64 matchesRevision = ~0ull;
    std::string matchesKey;
    // What the last search matched, so the pane can highlight it and Enter can
    // step from it rather than from the caret.
    Range lastMatch;

    // --- The view ------------------------------------------------------------
    //
    // **The folded blocks**, by their first line, and the ranges they are
    // chosen from -- worked out again only when the text changed. Edits above
    // a fold move it with its lines (see `drawPane`).
    std::vector<core::u32> folded;
    std::vector<ScriptDocument::FoldRange> foldRanges;
    core::u64 foldRevision = ~0ull;
    core::u32 foldLineCount = 0;
    //
    // **The widest line, in cells**, which is how far the pane scrolls
    // sideways. Measured once per revision: a fixed two hundred columns hid
    // whatever a longer line held past them (the owner's friend: `1 :: string`
    // after two hundred spaces, which no scrollbar could reach).
    core::u64 widestRevision = ~0ull;
    core::u32 widestCells = 0;
    // The minimap's slider, while it is dragged: the scroll and the pointer
    // the drag started from.
    bool mapDragging = false;
    float mapGrabScroll = 0.0f;
    float mapGrabY = 0.0f;

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
    // **The edit an accept made is not a reason to offer the list again.** The
    // accepted word matched itself, so the list came straight back and the
    // next Enter accepted it a second time instead of breaking the line --
    // reported as "Enter does not work on a suggestion".
    bool justAccepted = false;
    // The word before the caret is already a whole name the list offered, so
    // the list is closed -- and stays closed when the type checker's answer
    // for the same word arrives a frame later.
    bool completionWhole = false;

    // **What the language service was last asked for this tab** (ADR 0093),
    // so an answer that arrives a frame or two later is matched to the text
    // and the caret it was asked about -- or dropped, when either moved on.
    std::string module;
    core::u64 askedRevision = ~0ull;
    Position askedAt;
    std::string completionPrefix;
    std::optional<SignatureHelp> signature;
    core::u64 signatureRevision = ~0ull;
    Position signatureAt;
    core::u64 checkedRevision = ~0ull;

    // **Take the caret the next time the pane is drawn**: set when the tab is
    // opened or focused, so a script just made is typed into at once, with the
    // caret on its first line, instead of waiting for a click.
    bool claimCaret = false;

    // **Its indentation was made tabs when it was opened** (`indentWithTabs`),
    // so the text differs from the instance's `Source`: the pane hands it over
    // on its first draw, and the tab is unsaved until somebody saves it.
    bool convertedIndent = false;

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
    OpenScript& open(core::InstanceId instance, ScriptOrigin origin, std::string chunk, std::string file,
                     std::string title, std::string_view source);
    // Opens a content file that is no instance's (`ScriptOrigin::File`), or
    // focuses the tab that has it. **Idempotent by path**, for the reason
    // `open` is idempotent by instance. Its language is its extension's, and
    // its indentation is left as the file has it: a file this editor did not
    // write is not this editor's to re-indent.
    OpenScript& openFile(std::string file, std::string title, std::string_view source);
    [[nodiscard]] std::optional<std::size_t> indexOfFile(std::string_view file) const noexcept;

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

    // **Keyed on the world as well as the id**, because the two worlds hand out
    // the same handles: slot 7 exists in both, and matching on the id alone
    // would focus a scene tab when somebody opened a stamp's script.
    [[nodiscard]] std::optional<std::size_t> indexOf(core::InstanceId instance, ScriptOrigin origin) const noexcept;

    // **How big the code is drawn, as a multiple of the interface's own size.**
    //
    // One number for the whole editor rather than one per tab: somebody who
    // makes the text bigger has made a decision about their eyes, and a second
    // tab that ignored it would be asking them to make it again.
    //
    // Clamped to a range a person can come back from -- and the reason the
    // panel shows the percentage at all. A zoom with no readout is a state
    // somebody can get into and not out of.
    [[nodiscard]] core::f32 zoom() const noexcept { return m_zoom; }
    static constexpr core::f32 MinZoom = 0.5f;
    static constexpr core::f32 MaxZoom = 3.0f;
    // Answers whether the number actually moved, which is what decides if the
    // readout is worth showing.
    bool setZoom(core::f32 value) noexcept;

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
    // Asks for `index` to be brought to the front and to take the caret, on
    // the next draw -- what Stop does for the script that had the keyboard
    // before Play.
    void requestFocus(std::size_t index) noexcept
    {
        if (index < m_tabs.size()) {
            m_active = index;
            m_focusRequest = index;
        }
    }

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
    // **Every tab the written file carries**: a scene or a stamp holds the
    // `Source` of each of its scripts, so writing it saves all of them -- and
    // marking only the tab that asked left the others with the floppy of an
    // unsaved script that was already on disk (reported with a screenshot).
    // A script that is its own file under `src/scripts` is not in either.
    void markSavedWhere(ScriptOrigin origin) noexcept;

    // **Closes tabs whose instance is gone.** A script can be deleted from the
    // Explorer, and a hot reload replaces every instance in the world -- so a
    // tab holding an id nothing answers to would draw a document nobody could
    // save. Returns how many it closed.
    //
    // Both worlds, because each tab is asked about its OWN: a `Stamp` tab is
    // dead the moment there is no open stamp, and asking the scene about its id
    // would answer about a different instance that happens to share the slot.
    // `stamp` is null when no stamp session is open.
    std::size_t forgetDestroyed(const scene::World& scene, const scene::World* stamp);

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
    core::f32 m_zoom = 1.0f;
};

// **The id ImGui docks a tab's window by**, after the `###`. An instance's
// tab is keyed on the instance; a file's on its path, hashed -- every file tab
// has the same invalid instance, and keying them on it made them one window.
[[nodiscard]] std::string scriptWindowId(const OpenScript& tab);

} // namespace luaug::app
