#pragma once

#include <luaug/app/content_tree.h>
#include <luaug/app/inspector.h>
#include <luaug/app/picking.h>
#include <luaug/core/id.h>
#include <luaug/core/math.h>
#include <luaug/rhi/types.h>
#include <luaug/scene/world.h>

#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

// The editor's model: what it has selected, where its 3D view is, and what the
// mouse has asked it to find. No ImGui in this header, for the same reason
// `inspector.h` has none -- what a panel *decides* is testable and what it
// *draws* is a screenshot's business, and mixing the two is how a picking bug
// ends up only reproducible by clicking.
//
// **This does not own the selection.** `Inspector` already does, the explorer
// and the properties panel already read it, and a second copy would be two
// answers to one question the first time a hot reload cleared one of them. The
// editor asks the inspector to select what a click found.

namespace luaug::render {
class DebugDraw;
}

namespace luaug::rhi {
class IDevice;
}

namespace luaug::scene {
class World;
}

namespace luaug::app {

// The 3D view's own colour target.
//
// An editor's viewport is a panel with UI around it, so the world cannot be
// drawn straight to the swapchain: it is rendered into this and shown as an
// image inside the panel. That is also what makes picking well defined -- the
// ray through a pixel is the ray through a pixel *of this target*, and the
// panel's rectangle is the only thing that maps one to the other.
class ViewportTarget
{
public:
    ViewportTarget() = default;
    ~ViewportTarget();

    ViewportTarget(const ViewportTarget&) = delete;
    ViewportTarget& operator=(const ViewportTarget&) = delete;

    // Makes the target match `width` x `height`, creating or recreating it as
    // needed. Returns false only when creation failed, which the caller should
    // treat as "draw no viewport this frame" rather than as fatal: a panel
    // dragged to nothing is a normal thing for a person to do.
    //
    // **Call this before `beginFrame`.** Recreating waits for the device to go
    // idle first, because the texture being replaced may still be in flight
    // from the frame just submitted. That stall is real and it is paid while
    // somebody drags a splitter, which is the one moment nobody is measuring
    // frame time; the alternative is a use-after-free that reproduces on one
    // driver.
    bool resize(rhi::IDevice& device, core::u32 width, core::u32 height);

    void destroy();

    [[nodiscard]] rhi::TextureHandle texture() const noexcept { return m_texture; }
    [[nodiscard]] core::u32 width() const noexcept { return m_width; }
    [[nodiscard]] core::u32 height() const noexcept { return m_height; }
    [[nodiscard]] bool valid() const noexcept { return m_texture.valid(); }

private:
    rhi::IDevice* m_device = nullptr;
    rhi::TextureHandle m_texture;
    core::u32 m_width = 0;
    core::u32 m_height = 0;
};

// A click waiting to be turned into a selection.
//
// Picking is deferred rather than resolved inside the UI callback that noticed
// the click, and for the same reason a property write is: the world is walked
// at one known point in the frame, so what the editor selects cannot depend on
// where in the UI tree the click happened to be handled.
struct PickRequest
{
    // In the viewport panel's own pixels, origin at its top-left.
    core::Vec2 pixel;
    // Ctrl was held: add to the selection rather than replace it, and take out
    // what was already in it. The same gesture the Explorer's rows use, because
    // it is the same question asked of a different surface.
    bool additive = false;
};

// What the editor is doing with the world.
//
// **Three states, not two, and the third is the one a two-state model gets
// wrong.** `Editing` is not "playing, paused" -- it is not play mode at all,
// and while it holds, the TOOL owns the machine: no ticks, no render-rate
// script phases, no audio, the editor's own camera, and the cursor. `Playing`
// hands all of that back. `Paused` is inside play mode: the game's camera and
// the game's cursor, held still.
//
// Collapsing `Editing` and `Paused` into one state was the first design, and
// it made the play button a toggle between two things that are not opposites --
// which is what a person notices first, because pressing play and pressing
// stop are different questions and a single toggle can only answer one of them.
enum class RunState
{
    Editing,
    Playing,
    Paused,
};

// Whether the world advances, which is a different question from whether the
// editor is in play mode.
[[nodiscard]] constexpr bool advancing(RunState state) noexcept
{
    return state == RunState::Playing;
}

// Whether the TOOL owns the machine -- the cursor, the camera, the silence.
// True only outside play mode: pausing a running game does not hand the game's
// camera to the editor, and a person who paused to look at something would be
// surprised if it did.
[[nodiscard]] constexpr bool editing(RunState state) noexcept
{
    return state == RunState::Editing;
}

// Which panels are shown.
//
// **Every engine's Window menu is this**, and it exists for a reason a layout
// alone does not cover: a panel somebody closed has to stay closed, and a panel
// they cannot find again has to be findable. ImGui writes the open flags into
// the layout file beside the docking, so both survive a restart together.
// Which dialog is being asked for. Flags rather than direct calls, because a
// modal belongs to the shell and the things that open it -- a menu item, a
// toolbar button -- do not live there.
struct EditorDialogs
{
    bool saveAs = false;
    bool preferences = false;
    bool about = false;
    bool newFolder = false;
    // The new-script box. Beside `newFolder` because it is the same shape of
    // question: a name, and somewhere it goes.
    bool newScript = false;

    // A rename in flight. The seed is what the box opens with -- the current
    // name, because renaming is usually editing a name rather than replacing
    // one.
    bool renameInstance = false;
    bool renameContent = false;
    core::InstanceId renameTarget;
    std::string renameContentPath;
    std::string renameSeed;

    // A delete waiting to be confirmed. **Content only.** Deleting a file is
    // the one action here that survives the editor closing, so it is the one
    // that asks; an instance is recovered by reopening the scene, which is a
    // door a person already knows.
    std::string deleteContentPath;
};

struct EditorPanels
{
    bool explorer = true;
    bool properties = true;
    bool viewport = true;
    bool content = true;
    bool console = true;
    bool stats = true;

    // **Whether the Explorer shows what STREAMING made**, and it is off.
    //
    // Streaming pumps in edit mode as well as in play -- it is not gated on the
    // world advancing, and it must not be: you cannot edit a world you cannot
    // see, which is what Unreal's World Partition does in its editor too. But a
    // chunk is not part of the authored scene. The serializer skips a generated
    // subtree whole, nothing may be authored inside one, and sixty `Chunk_x_y_z`
    // folders between a person and the four things they wrote is the same
    // complaint the root's own row answered -- scrolling past a world to find
    // the thing you came for.
    //
    // A switch rather than a rule, because "what did streaming actually
    // materialise" is a real question with no other way to ask it.
    bool showGenerated = false;
};

// What the shell asked for this frame, drained by the frame loop at the safe
// point.
//
// The shell records intent and never acts, for the same reason a property edit
// queues instead of writing: play, stop and save each replace or walk the whole
// world, and doing that inside an ImGui callback would mutate a world that the
// panel behind this one is still drawing from.
struct EditorCommands
{
    // Set to the state asked for rather than a toggle, so two panels asking in
    // one frame cannot cancel each other out.
    // Whether to be in play mode. Set to the state asked for rather than a
    // toggle, so two panels asking in one frame cannot cancel each other out.
    std::optional<bool> play;
    // Whether to be paused, which is only meaningful inside play mode.
    std::optional<bool> pause;
    // Save the scene that is open. When none is, the shell asks for a name
    // instead of guessing one -- see `saveAs`.
    bool save = false;
    // Start over: empty what a scene describes and forget its name.
    bool newScene = false;
    // Save to a content-relative path somebody typed. Carries the whole
    // decision, so the frame loop does not have to know what the dialog asked.
    std::string saveAs;
    // A scene the browser asked to open, relative to the content root.
    std::string openScene;
    // A folder the browser asked to make, in its current directory.
    std::string createFolder;
    // **A new entry script, by NAME, written under the project's `src/scripts`.**
    //
    // A `Script` is `NotCreatable` and that is right: `Instance.new("Script")`
    // inside a sandboxed game VM has no filesystem to put a file on (R4), and a
    // `Script` instance with no file behind it is a lie the tree tells. The
    // EDITOR is not that VM -- ADR 0046 put it in the engine binary precisely
    // because it may touch the disk -- so it creates the FILE, and the instance
    // appears because the mount finds it (ADR 0048). The rule is honoured rather
    // than bypassed.
    std::string createScript;
    // Ask for the Save As dialog. A flag rather than the dialog opening itself,
    // because the toolbar button and File > Save Scene As have to reach the
    // same one.
    bool wantSaveAs = false;
    // Close the editor. The menu's File > Exit, which is the one every
    // application has and the one people reach for before the window button.
    bool quit = false;
    // --- What a right-click asked for ----------------------------------------
    //
    // Every one of these mutates a world or a directory, so none of them acts
    // where it was clicked: they are drained at the frame's safe point like the
    // property writes and the pick, for the reason that has not changed -- a
    // panel behind this one is still drawing from what they would change.

    // **Make one, under the instance the plus was pressed on.** Both halves or
    // neither: a class with no parent has nowhere to go, and a parent with no
    // class is not a request. `InvalidClass` is "nothing was asked for", which
    // is what makes this drainable beside everything else here.
    scene::ClassId createClass = scene::InvalidClass;
    core::InstanceId createParent;

    // **Delete or duplicate THE SELECTION**, not one named instance.
    //
    // A flag rather than an id because that is what actually happens: a
    // right-click puts the row into the selection before the menu opens, and
    // ctrl-A or a shift-range put four rows there. Naming one of them in the
    // command would have the menu act on a different set from the one the
    // person is looking at, which is the whole failure mode `isSelected` was
    // added to the right-click to avoid.
    bool deleteSelection = false;
    bool duplicateSelection = false;
    // Rename an instance. Both halves or neither -- and singular, because
    // renaming four things to one name is not a thing anybody means.
    core::InstanceId renameInstance;
    std::string renameInstanceTo;

    // Move the selection under this. Set by a drop in the Explorer.
    core::InstanceId reparentTo;

    // Content-relative. Delete removes a folder with everything in it.
    std::string deleteContent;
    std::string renameContent;
    std::string renameContentTo;

    // Step back, or forward again.
    bool undo = false;
    bool redo = false;

    // Let go of whatever is selected. Escape, from anywhere in the shell.
    bool clearSelection = false;
    // Put the panels back where they started. Not "close everything" -- a
    // person who has lost a panel behind another wants the arrangement back,
    // not an empty window.
    bool resetLayout = false;

    void clear() noexcept { *this = EditorCommands{}; }
    [[nodiscard]] bool any() const noexcept
    {
        return play.has_value() || pause.has_value() || save || newScene || quit || resetLayout || clearSelection ||
               undo || redo || createClass != scene::InvalidClass || deleteSelection || duplicateSelection ||
               reparentTo.valid() || renameInstance.valid() || !saveAs.empty() || !openScene.empty() ||
               !createFolder.empty() || !createScript.empty() || !deleteContent.empty() || !renameContent.empty();
    }
};

// The undo stack, and it is snapshots rather than commands.
//
// **The reversible-command design is the one this does NOT use, and the reason
// is the delete.** Undoing a property write is remembering a value; undoing a
// delete is recreating an instance, its whole subtree, its attributes and tags,
// and every reference anybody held to it -- with the same ids, or every one of
// those references is now pointing at nothing. `World::snapshot` already does
// exactly that, and does it correctly: the generation work behind it exists so
// a handle taken before a restore still resolves after one.
//
// The price is memory, and it is bounded rather than argued about: a fixed
// number of steps, oldest dropped. A step is the world's component pools, which
// for an authored scene is small and for a streamed world is not -- so the cap
// is a number somebody can move when a measurement says to, not a guess
// defended forever.
//
// **What is not in it**: saving, creating a folder, deleting a file. Undo is of
// the WORLD, not of the disk. A Ctrl+Z that resurrected a deleted file would be
// a promise that cannot be kept every time, and one that is kept sometimes is
// worse than one nobody made.
class UndoStack
{
public:
    // Records the state to come back to, labelled with what is about to happen.
    //
    // `coalesceKey` joins consecutive actions into one step when it repeats,
    // which is what keeps a two-second drag on a colour from burying everything
    // before it under a hundred and twenty steps. Empty never coalesces.
    void record(const scene::World& world, std::string label, core::u64 coalesceKey = 0);

    bool undo(scene::World& world);
    bool redo(scene::World& world);

    [[nodiscard]] bool canUndo() const noexcept { return !m_undo.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !m_redo.empty(); }
    // What undoing would undo, for a menu item that says so rather than saying
    // "Undo" and leaving somebody to find out.
    [[nodiscard]] std::string_view undoLabel() const noexcept;
    [[nodiscard]] std::string_view redoLabel() const noexcept;

    // After a scene load, a new scene, or a stop. Undoing into a world that no
    // longer exists is not undoing.
    void clear() noexcept;

    static constexpr core::usize Depth = 64;

private:
    struct Step
    {
        scene::WorldSnapshot state;
        std::string label;
        core::u64 key = 0;
    };

    std::deque<Step> m_undo;
    std::deque<Step> m_redo;
};

// What the last save or load did, kept so the shell can say it. A save that
// silently dropped four references is a save somebody should be told about.
struct EditorStatus
{
    // Shown until something else happens. Not a catalog key: R3 does not govern
    // what an editor draws (ADR 0046), and this text names paths and counts.
    std::string message;
    bool failed = false;
};

class Editor
{
public:
    // Where the 3D view sits in the window, in pixels, y down. Written by the
    // UI each frame; read by picking. Zero-sized while the panel is collapsed,
    // which `rayThroughPixel` handles rather than divides by.
    void setViewport(const ViewportRect& rect) noexcept { m_viewport = rect; }
    [[nodiscard]] const ViewportRect& viewport() const noexcept { return m_viewport; }

    // The camera the viewport was last rendered with. Picking needs exactly the
    // matrices the image the person clicked on was drawn with -- taking them
    // from anywhere else is how a pick drifts by one frame's camera motion,
    // which is invisible standing still and wrong while walking.
    void setCamera(const core::Mat4& projection, const core::Mat4& view, core::DVec3 origin) noexcept;
    // What the image was drawn with, read back. The manipulator needs the same
    // three the picker does, and a test driving a drag needs them to know which
    // pixel a world point falls at.
    [[nodiscard]] const core::Mat4& projection() const noexcept { return m_projection; }
    [[nodiscard]] const core::Mat4& view() const noexcept { return m_view; }
    [[nodiscard]] core::DVec3 cameraOrigin() const noexcept { return m_cameraOrigin; }
    [[nodiscard]] bool hasCamera() const noexcept { return m_hasCamera; }

    void requestPick(core::Vec2 pixelInViewport, bool additive = false) noexcept
    {
        m_pending = PickRequest{pixelInViewport, additive};
    }
    [[nodiscard]] bool pickPending() const noexcept { return m_pending.has_value(); }

    [[nodiscard]] RunState runState() const noexcept { return m_run; }

    // --- The loop: play, stop, save ------------------------------------------
    //
    // **Play remembers the world so Stop can put it back**, which is the Unity
    // and Unreal semantic and the one a person means by the word. Without it,
    // testing a change destroys the change -- and a tool where testing your work
    // costs you your work is not one anybody uses twice.
    //
    // The mechanism is `World::snapshot`, which is what every component in
    // `engine/scene` was made trivially copyable FOR: five comments across that
    // module say so, and this is the first caller they ever had.
    //
    // **What a restore does not put back is the Luau VM** -- variables,
    // connections, coroutines -- and that is stated rather than hidden. A
    // connection a script made during play is still connected after stop. The
    // honest fix is a VM rebuild, and it is not free: this engine's projects
    // still BUILD their worlds in script, so rebuilding the VM would rebuild the
    // world and undo the restore. ADR 0047 is what changes that, and until a
    // project's world is data the restore is the world's and not the VM's.
    // Enters play mode, remembering the world so `stop` can put it back. A
    // no-op while already in play mode, so a second press cannot move the point
    // stop returns to.
    void play(scene::World& world);
    // Leaves play mode and restores. The opposite of `play`, which is why they
    // share a button.
    void stop(scene::World& world, Inspector& inspector);
    // Holds a running world still without leaving play mode. Not the opposite
    // of anything, which is why it has its own.
    void setPaused(bool paused) noexcept;

    [[nodiscard]] bool inPlayMode() const noexcept { return m_run != RunState::Editing; }

    // Writes the world to `path`. Returns false and sets the status on failure;
    // the caller does not need to know which of the two steps failed, but a
    // person does, so the status says.
    bool save(const scene::World& world, const std::filesystem::path& path);
    bool load(scene::World& world, const std::filesystem::path& path, Inspector& inspector);

    // --- The content browser -------------------------------------------------
    //
    // **The content directory is the asset manager**, and a scene is one of the
    // assets in it (human decision, 2026-08-22). The editor knows which scene is
    // open so that saving writes back to THAT one rather than to a fixed name --
    // which is the difference between a project with scenes and a project with
    // a scene.
    [[nodiscard]] ContentTree& content() noexcept { return m_content; }
    [[nodiscard]] const ContentTree& content() const noexcept { return m_content; }

    // Writes a new entry script under `<project>/src/scripts/<name>.luau` and
    // returns whether it landed.
    //
    // **Content is a project's data and `src/` is its code**, and an editor that
    // showed only the first could not offer to make a script -- which is what a
    // person asked for the first day they used this one. The file is what makes
    // the `Script`: the next reload mounts it and the tree grows a row (ADR
    // 0048).
    //
    // Refuses a name that is not a name, and refuses to overwrite. A "new
    // script" that silently replaced somebody's file would be the worst button
    // in the editor.
    bool createScript(const std::filesystem::path& projectRoot, std::string_view name);

    // Opens a project's content root. A project with no `content/` is a normal
    // state and not an error -- every example before `06-scene` is one.
    void openContent(const std::filesystem::path& contentRoot);

    // Loads a scene BY ITS CONTENT-RELATIVE PATH and remembers it as the open
    // one. Leaving play mode first is the caller's business; loading a scene
    // over a running game would restore into a world the snapshot no longer
    // describes.
    bool openScene(scene::World& world, std::string_view relativePath, Inspector& inspector);

    // Writes the open scene back to where it came from. False when no scene is
    // open, which is a question rather than a failure: a project that has never
    // saved one has nothing for this to overwrite.
    bool saveOpenScene(const scene::World& world);

    // --- Remembering, across a restart ---------------------------------------
    //
    // **Which scene was open is state that belongs to a PERSON, not to a
    // project.** Two people working on the same repository were last looking at
    // different things, and a project file that recorded one of them would make
    // the other's editor jump somewhere on every pull. So it lives in
    // `.luaug/`, beside the panel layout and gitignored for the same reason,
    // while `[project] scene` in `luaug.toml` says which scene a RUN starts
    // with — which is a decision the project does make.
    //
    // The fallback chain when the editor opens: the remembered scene if it still
    // exists, then the project's declared one, then nothing — and nothing is an
    // untitled world somebody can build in and give a name to when they save.
    void rememberOpenScene(const std::filesystem::path& stateDirectory) const;
    [[nodiscard]] static std::string recallOpenScene(const std::filesystem::path& stateDirectory);

    // Whether an instance is one of the engine's own -- a service, or the root.
    //
    // **A service is not a thing somebody put in the world**: it is reached
    // through `GetService`, there is one per world, and the engine creates it
    // whether or not anybody wanted it. Deleting one would leave a world that
    // cannot answer a call every script makes, and duplicating one would make
    // "one per world" false. The IDL already says which classes these are, so
    // this asks the class rather than a list of names that would go stale.
    // `root` is `game`, passed rather than inferred. "Has no parent" was the
    // first version of this test and it is wrong: an instance parented to nil is
    // a loose instance, not the world's root, and treating the two alike made
    // the editor refuse to delete anything somebody had detached.
    [[nodiscard]] static bool isEngineOwned(const scene::World& world, core::InstanceId id,
                                            core::InstanceId root) noexcept;

    // Deletes an instance and everything under it. Refuses the scene's root and
    // every service: the world itself is not a thing inside the world.
    //
    // **There is no undo yet** -- it is E2's -- and the honest safety net is
    // the one a person already knows: the scene is a file, so reopening it
    // brings back everything that was not saved. The status says so when this
    // happens rather than leaving somebody to discover it.
    bool deleteInstance(scene::World& world, core::InstanceId id, core::InstanceId root, Inspector& inspector);

    // A copy beside the original, selected, because the reason to duplicate
    // something is to change the copy.
    bool duplicateInstance(scene::World& world, core::InstanceId id, core::InstanceId root, Inspector& inspector);

    // **Makes an instance under `parent` and selects it**, which is what the
    // plus beside a row in the explorer does.
    //
    // The class must be one `Instance.new` would accept -- not abstract, not a
    // service, not `NotCreatable`. The editor does not get a second answer to
    // that question: `collectCreatableClasses` reads the same three flags off
    // the same descriptors, so a menu cannot offer what this refuses.
    //
    // **A part lands in front of the editor camera, not at the origin.** An
    // instance created four kilometres from the view is one nobody finds, and
    // in a streamed world the origin is not where anybody is standing. The
    // placement goes through `setProperty` like every other editor write, so a
    // class with no `CFrame` simply does not get one rather than needing a
    // special case here.
    bool createInstance(scene::World& world, scene::ClassId classId, core::InstanceId parent, core::InstanceId root,
                        Inspector& inspector);

    // **Whether anything a person authors may be PARENTED here.**
    //
    // Wider than `authorable` at one end and narrower at the other, and both
    // differences are the point. A SERVICE is a legal parent -- `Workspace`
    // holding a `Part` is what the service is FOR, and `Lighting` holding a
    // `PointLight` is too -- so being engine-owned does not disqualify one.
    //
    // And `generated` is asked of the whole ANCESTRY rather than of the
    // instance, because streaming marks a chunk's FOLDER and not its contents.
    // An instance-only test offered a plus on `Chunk_-3_0_0/Ground`, accepted
    // the create, and the next eviction destroyed what somebody made without a
    // word -- while the chunk's own row, which is the one that LOOKS like the
    // dangerous place, was correctly refused.
    [[nodiscard]] static bool canParentInto(const scene::World& world, core::InstanceId id, core::InstanceId root);

    // **Whether a person may author here at all**, which is a different and
    // wider question than `isEngineOwned`.
    //
    // That one knows about the world's root and about services. It says nothing
    // about `generated`, which is the flag streaming puts on a chunk's folder
    // and which the scene format reads three times -- so nothing stopped a drag
    // from dropping an authored part inside `Chunk_12_-4`, where the save skips
    // it (the serializer skips a generated subtree whole) and the next eviction
    // destroys it without a word.
    //
    // Ancestors count. A chunk marks its FOLDER and not its contents, which is
    // exactly the economy that makes checking the instance alone wrong.
    [[nodiscard]] static bool authorable(const scene::World& world, core::InstanceId id, core::InstanceId root);

    // Moves instances under a new parent, as ONE undo step.
    //
    // What it refuses, and each for its own reason: a target that is not
    // authorable, because the save would drop what lands there; a source that
    // is engine-owned, because a service is one per world and moving it is not
    // a thing a world can mean; and a cycle, which `World::setParent` already
    // refuses and which this does not duplicate -- it reads the error back.
    //
    // A refusal is per instance and not for the batch: dragging four things
    // onto a folder, one of which cannot go, moves the three that can and says
    // so. The alternative is a drag that silently does nothing because of a
    // member somebody did not notice selecting.
    // What a move under `newParent` would actually do, decided before anything
    // is recorded.
    //
    // **It exists because a drop target has to ask a frame before the drag
    // ends.** A row that lights up under the pointer and then refuses the drop
    // is a UI making a claim it cannot keep -- the same failure `editable`
    // exists to prevent in the properties grid -- and the only way to light it
    // up honestly is to ask the question early. Asking it any other way would
    // be a second copy of the rule, and the copy is always the one that goes
    // stale.
    struct ReparentPlan
    {
        // In document order, and only what would MOVE: an instance already
        // under `newParent` is not in it, and neither is one a rule turned
        // away.
        std::vector<core::InstanceId> movable;
        // How many a rule turned away, which is what tells "already there" from
        // "cannot go there" in the status line.
        core::usize refused = 0;
        // Nothing authored may live under `newParent` at all -- it is dead, or
        // it is inside something streaming materialised. Separate from
        // `refused` because it is a fact about the TARGET, and a drag of four
        // things onto it fails for one reason rather than four.
        bool targetRefuses = false;
    };
    [[nodiscard]] static ReparentPlan planReparent(const scene::World& world, std::span<const core::InstanceId> ids,
                                                   core::InstanceId newParent, core::InstanceId root);

    // Whether a drop of `ids` onto `newParent` would move anything at all.
    [[nodiscard]] static bool canReparent(const scene::World& world, std::span<const core::InstanceId> ids,
                                          core::InstanceId newParent, core::InstanceId root);

    bool reparent(scene::World& world, std::span<const core::InstanceId> ids, core::InstanceId newParent,
                  core::InstanceId root, Inspector& inspector);

    // Delete and duplicate over a whole selection, as ONE undo step each --
    // because somebody who deleted four things did one thing, and four steps is
    // four presses of ctrl-Z to get back to where they were.
    //
    // Ordered by the tree before acting, so the result does not depend on the
    // order somebody happened to click in (R10's discipline applied to an
    // editor: an operation over a set has to be a function of the set).
    bool deleteInstances(scene::World& world, std::span<const core::InstanceId> ids, core::InstanceId root,
                         Inspector& inspector);
    bool duplicateInstances(scene::World& world, std::span<const core::InstanceId> ids, core::InstanceId root,
                            Inspector& inspector);

    bool renameInstance(scene::World& world, core::InstanceId id, core::InstanceId root, std::string_view name);

    // Empties the world of everything a scene describes and forgets which scene
    // was open, so the next save asks for a name.
    //
    // **It clears what a scene CONTAINS, not what the world is.** Services stay,
    // the `Workspace` stays, and anything a system made stays -- a streamed
    // chunk is not somebody's authored work and a new scene does not evict it.
    // What goes is exactly what `writeScene` would have written, which keeps
    // "new" and "save" describing the same set.
    void newScene(scene::World& world, Inspector& inspector);

    // Writes the world to a scene that does not exist yet, and adopts it as the
    // open one. `relativePath` is content-relative and gains the extension if it
    // does not carry it — a person typing a name should not have to know it.
    bool saveSceneAs(const scene::World& world, std::string_view relativePath);

    // What `saveSceneAs` will actually write, given what somebody typed. Public
    // and pure so the dialog can show the resolved path while it is being typed
    // rather than after it has been saved to the wrong place, and so a test can
    // drive it without a window (D068).
    [[nodiscard]] static std::string normalizeScenePath(std::string_view typed);
    // Whether a normalized path stays inside `content/`. False for a drive
    // letter, for `..`, and for anything the content browser would refuse as a
    // name.
    [[nodiscard]] static bool sceneNameIsUsable(std::string_view typed) noexcept;

    // Names the scene the world already holds, without loading anything. The
    // boot path uses it: the engine loads a project's scene before the editor
    // exists, and the editor has to know which one that was.
    void adoptOpenScene(std::string_view relativePath);

    // Empty when no scene has been opened. Content-relative.
    [[nodiscard]] const std::string& openScenePath() const noexcept { return m_openScene; }

    [[nodiscard]] const EditorStatus& status() const noexcept { return m_status; }

    [[nodiscard]] UndoStack& history() noexcept { return m_history; }
    [[nodiscard]] const UndoStack& history() const noexcept { return m_history; }

    // Steps back, and says what it undid. The selection is dropped when the
    // step it named is gone, for the reason `stop` drops it: an id that resolves
    // to whatever now occupies the slot is a properties grid pointed at
    // somebody else.
    bool undo(scene::World& world, Inspector& inspector);
    bool redo(scene::World& world, Inspector& inspector);

    // Ask for exactly one tick while paused. A step is how somebody watches a
    // thing happen instead of inferring it from before and after, and it is the
    // one control a paused editor cannot do without.
    // **Only inside play mode.** A step is one tick of the simulation, and a
    // world that is being edited is a world whose simulation is deliberately
    // not running -- so a step there advances physics under somebody's hands
    // for no reason they asked for. Every engine of this shape offers frame
    // advance while PAUSED and nowhere else.
    //
    // Refused here rather than only hidden in the panel, which is the lesson
    // five defects of E1 taught: a rule about the world belongs to the world's
    // model, and a panel that is the only thing enforcing it is a rule with one
    // caller.
    void requestStep() noexcept
    {
        if (inPlayMode())
            m_stepRequested = true;
    }

    // How many of the frame's owed ticks the world may actually take.
    //
    // Playing: all of them, unchanged -- the editor is not a second scheduler
    // and must not become one. Editing or paused: none, unless a step was asked
    // for, and then exactly one however many the frame owed. Consuming the request here
    // rather than at the button is what makes a step one tick rather than one
    // tick per frame the button stays held.
    [[nodiscard]] core::u32 allowedTicks(core::u32 owed) noexcept
    {
        if (advancing(m_run))
            return owed;
        // The request survives a frame that owed nothing. A step is a promise
        // that one tick will happen, not that one will happen if the frame
        // arrived at a convenient moment -- and at sixty hertz a frame owing
        // zero ticks is common enough that swallowing the press would make the
        // button feel broken.
        if (!m_stepRequested || owed == 0)
            return 0;
        m_stepRequested = false;
        return 1u;
    }

    // Resolves a pending pick against the world and hands the result to the
    // inspector. Returns what was hit, or nothing when the click landed on
    // empty space -- which clears the selection, because clicking nothing in a
    // 3D editor means nothing, and leaving the last thing selected is how a
    // person edits the object they thought they had deselected.
    std::optional<PickHit> resolvePick(const scene::World& world, Inspector& inspector) noexcept;

    // --- The editor's own camera ---------------------------------------------
    //
    // A paused world is a world whose scripts are not running, and in this
    // engine the camera is a script's job -- so pausing froze the view solid.
    // An editor needs to look around a world that is holding still, which is
    // most of what looking around is for.
    //
    // **It is the editor's own camera and the world never learns about it**,
    // which is what Unity's scene view and Unreal's editor viewport both are.
    // The first design wrote `Workspace.CurrentCamera` instead, and that made
    // the tool and the game two authors of one transform -- a disagreement no
    // arbitration settles, because the disagreement IS the design (D061). The
    // renderer is told which view to draw through `render::ViewOverride`, and
    // while the world plays it is told nothing and draws the game's.

    // --- The manipulators ----------------------------------------------------
    //
    // **The arithmetic is `picking.h`'s and the STATE is here**: which mode, in
    // which space, and what a drag in progress started from. The split is the
    // one the whole editor is built on -- what can be tested without a window
    // lives where a test can reach it, and what a person is doing with a mouse
    // right now is a thing an object remembers between frames.
    //
    // **A drag is solved against where it STARTED, never against last frame.**
    // Every selected instance's transform is recorded when the button goes down
    // and the delta is applied to that, so a drag is exact however long it lasts
    // and however slowly it is made -- and so dragging three parts moves each by
    // the same delta rather than stacking them on the one the gizmo sits on.

    [[nodiscard]] GizmoMode gizmoMode() const noexcept { return m_gizmoMode; }
    // Refused mid-drag: changing what a drag means half way through it is not
    // something a person can have meant.
    void setGizmoMode(GizmoMode mode) noexcept;

    // **World axes or the selection's own.** Which one is right depends on the
    // part, which is why it is a person's choice and not this file's: a rotated
    // crate is unusable in world space and a wall is unusable in local.
    [[nodiscard]] bool gizmoLocal() const noexcept { return m_gizmoLocal; }
    void setGizmoLocal(bool local) noexcept;

    // Snapping is ON, and a modifier suspends it. That way round because the
    // number somebody wants is far more often a round one, and because a
    // manipulator with no snap is the one that feels like a toy.
    [[nodiscard]] bool snapping() const noexcept { return m_snap && !m_snapSuspended; }
    void setSnapSuspended(bool suspended) noexcept { m_snapSuspended = suspended; }
    void setSnap(bool on) noexcept { m_snap = on; }
    // Metres for translate and scale, degrees for rotate.
    [[nodiscard]] f32 snapStep(GizmoMode mode) const noexcept;
    void setSnapStep(GizmoMode mode, f32 step) noexcept;

    // Where the manipulator is and how big, or nothing when there is nothing to
    // manipulate.
    //
    // The PRIMARY selection's transform, because a gizmo has to be somewhere and
    // the last thing clicked is the thing somebody is looking at. An instance
    // with no transform -- a `Folder`, a service -- has no manipulator, which is
    // honest rather than a limitation to work around: there is nothing to drag.
    [[nodiscard]] std::optional<GizmoFrame> gizmoFrame(const scene::World& world, const Inspector& inspector) const;

    // The handle under the pointer, or the one being dragged. For drawing.
    [[nodiscard]] std::optional<GizmoHandle> gizmoHandle() const noexcept;
    [[nodiscard]] bool gizmoDragging() const noexcept { return m_drag.has_value(); }

    // What the viewport saw the pointer doing. `pressed` is the frame the button
    // went down and only while the pointer was over the image; `down` is every
    // frame it is held, over the image or not -- a drag that leaves the panel is
    // still a drag, and one that ends outside it still ends.
    void setPointer(core::Vec2 pixelInViewport, bool pressed, bool down) noexcept;

    // Runs the manipulator for this frame, at the frame's safe point like every
    // other world mutation.
    //
    // Returns true when the pointer belongs to the gizmo, which is what stops a
    // click on a handle ALSO selecting whatever is behind it -- the commonest
    // way a first manipulator loses the thing it was about to move.
    bool driveGizmo(scene::World& world, Inspector& inspector);

    // Seeds the editor camera from wherever the world's camera currently is, so
    // pressing pause does not teleport the view. Called once, when the editor
    // first has a camera to copy.
    void adoptCamera(const core::CFrameD& cframe) noexcept;
    [[nodiscard]] bool cameraAdopted() const noexcept { return m_cameraAdopted; }

    // What the shell saw the mouse and keyboard doing this frame.
    //
    // **The shell decides WHETHER to look and the frame loop decides HOW**, and
    // the split is forced by the mechanism rather than chosen: turning the
    // camera puts the pointer into SDL's relative mode, and in relative mode
    // ImGui stops receiving an absolute position -- so the delta it reports
    // becomes zero exactly when the camera starts needing one. The motion has to
    // come from the platform's own events, which the frame loop has and a UI
    // callback does not.
    struct LookInput
    {
        // Right button held, and the drag STARTED over the viewport image. A
        // drag that began in another panel and crossed this one must not fling
        // the camera.
        bool active = false;
        // WASD/QE, already scaled by the sprint modifier.
        core::Vec3 move;
    };

    void setLookInput(const LookInput& input) noexcept { m_look = input; }
    [[nodiscard]] const LookInput& lookInput() const noexcept { return m_look; }

    // One frame of fly-camera input. `lookDelta` is in pixels of mouse
    // movement, `move` is the WASD/QE axes in [-1, 1], `dt` is the render
    // clock's -- the editor camera is not simulation and must not be on the
    // fixed tick, because a paused world runs no ticks at all and a camera that
    // waited for one could not move.
    //
    // Returns the transform to write. Does nothing and returns the unchanged
    // camera while playing.
    core::CFrameD driveCamera(core::Vec2 lookDelta, core::Vec3 move, f32 dt) noexcept;

    [[nodiscard]] const core::CFrameD& cameraCFrame() const noexcept { return m_cameraCFrame; }

    // Metres per second, doubled by a sprint modifier at the call site. Public
    // because an editor that cannot change its own fly speed is one you cannot
    // use in both a room and a four-kilometre world.
    void setCameraSpeed(f32 metresPerSecond) noexcept;
    [[nodiscard]] f32 cameraSpeed() const noexcept { return m_cameraSpeed; }

    // The ray a pixel of the viewport casts, exposed so a test can drive a
    // click without a window. The pixel is in the viewport's own space.
    [[nodiscard]] PickRay rayThrough(core::Vec2 pixelInViewport) const noexcept;

private:
    // A drag in progress. `start` is where the pointer was solved to on the
    // frame the button went down, and `before` is every selected instance's
    // transform at that moment -- the two things a delta is measured from.
    struct GizmoDrag
    {
        GizmoHandle handle;
        GizmoFrame frame;
        core::DVec3 startPoint;
        f32 startAngle = 0.0f;
        std::vector<core::InstanceId> targets;
        std::vector<core::CFrameD> before;
        std::vector<core::Vec3> sizes;
        core::u64 gesture = 0;
    };

    ViewportRect m_viewport;
    core::Mat4 m_projection;
    core::Mat4 m_view;
    core::DVec3 m_cameraOrigin;
    bool m_hasCamera = false;
    std::optional<PickRequest> m_pending;
    RunState m_run = RunState::Editing;
    GizmoMode m_gizmoMode = GizmoMode::Translate;
    bool m_gizmoLocal = false;
    bool m_snap = true;
    bool m_snapSuspended = false;
    // Metres, metres, degrees -- indexed by `GizmoMode`. A quarter of a metre
    // and fifteen degrees are the steps every editor lands on because they are
    // the ones a room and a corner are built from.
    f32 m_snapStep[3] = {0.25f, 15.0f, 0.25f};
    core::Vec2 m_pointer;
    bool m_pointerPressed = false;
    bool m_pointerDown = false;
    std::optional<GizmoHandle> m_hover;
    std::optional<GizmoDrag> m_drag;
    bool m_stepRequested = false;
    // Held by pointer because a `WorldSnapshot` is thirty component pools and
    // an editor that is not playing should not be carrying an empty one.
    std::unique_ptr<scene::WorldSnapshot> m_playSnapshot;
    EditorStatus m_status;
    ContentTree m_content;
    std::string m_openScene;
    UndoStack m_history;

    core::CFrameD m_cameraCFrame;
    bool m_cameraAdopted = false;
    // Radians. Held separately from the CFrame because deriving them back out
    // of a rotation matrix every frame accumulates, and a fly camera that
    // slowly roll-drifts is a bug people describe as "the horizon is tilting".
    f32 m_yaw = 0.0f;
    f32 m_pitch = 0.0f;
    f32 m_cameraSpeed = 12.0f;
    LookInput m_look;
};

// The manipulator, drawn where the selection is.
//
// A free function rather than a method, for the reason `submitSelection` is one:
// what it needs is a frame, a mode and a buffer, and giving it the whole editor
// would be giving it three things it does not read. It also makes the one thing
// a headless test CAN check about the drawing reachable -- that the vertices
// come out camera-relative and therefore exact four kilometres from the origin.
void submitGizmo(const GizmoFrame& frame, GizmoMode mode, std::optional<GizmoHandle> active, core::DVec3 cameraOrigin,
                 render::DebugDraw& draw);

} // namespace luaug::app
