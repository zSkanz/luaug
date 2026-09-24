#pragma once

#include <luaug/app/content_tree.h>
#include <luaug/app/inspector.h>
#include <luaug/app/picking.h>
#include <luaug/asset/material.h>
#include <luaug/asset/terrain.h>
#include <luaug/asset/voxel.h>
#include <luaug/core/id.h>
#include <luaug/core/math.h>
#include <luaug/platform/window.h>
#include <luaug/rhi/types.h>
#include <luaug/scene/scene_file.h>
#include <luaug/scene/world.h>

#include <array>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
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

// The vertical field of view the editor's own camera looks through, in degrees.
// One number rather than two: the frame loop builds the `ViewOverride` from it
// and `framedCamera` frames against it, and a distance computed from a different
// angle than the one being rendered puts the thing you asked to see off-screen.
inline constexpr f32 EditorFieldOfView = 70.0f;

// The world-space sphere a selection occupies, descendants included -- so
// pressing F on a `Model` frames the model rather than its pivot.
//
// False when nothing in the selection has an extent, which is a real answer: a
// selection of one `Folder` has a position and no size, and moving the camera
// to "see" it would move it somewhere arbitrary.
[[nodiscard]] bool selectionBounds(const scene::World& world, std::span<const core::InstanceId> selection,
                                   core::DVec3& outCentre, core::f64& outRadius);

// Where a camera has to sit to frame a sphere, looking the way it already looks.
//
// The distance is what puts the sphere inside the vertical field of view with a
// margin, and it is clamped at the near end: a two-centimetre part would
// otherwise be framed from inside its own surface.
[[nodiscard]] core::CFrameD framedCamera(const core::CFrameD& current, core::DVec3 centre, core::f64 radius,
                                         f32 fieldOfViewDegrees = EditorFieldOfView);

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
    // **The replaced texture is RETIRED rather than destroyed** (D117). It may
    // still be in flight from the frame just submitted, and the two ways to be
    // safe about that are to wait for the device to go idle or to keep it alive
    // for as long as a frame can be. This used to wait -- and a resize happens
    // on every frame of a splitter drag or a window resize, so the editor
    // stalled the whole GPU sixty times a second for exactly as long as
    // somebody was dragging. That is the one moment nobody measures frame time
    // and the one moment they are looking hardest at how the tool feels.
    //
    // `MeshCache::beginFrame` already retires its ring buffers this way and for
    // the same reason, down to the two frames of slack.
    bool resize(rhi::IDevice& device, core::u32 width, core::u32 height);

    // Ages the retirement queue by one frame and frees what has outlived any
    // command list that could still name it. Call once per frame that renders.
    void retire(rhi::IDevice& device);

    void destroy();

    [[nodiscard]] rhi::TextureHandle texture() const noexcept { return m_texture; }
    [[nodiscard]] core::u32 width() const noexcept { return m_width; }
    [[nodiscard]] core::u32 height() const noexcept { return m_height; }
    [[nodiscard]] bool valid() const noexcept { return m_texture.valid(); }

    // How many frames a replaced texture is kept before it is freed. Two, for
    // `MeshCache`'s reason: a handle drawn with before a swap is legal for the
    // rest of that frame, and the GPU may still be executing those commands
    // when the next frame begins.
    static constexpr core::u32 RetirementFrames = 2;

private:
    struct Retired
    {
        rhi::TextureHandle texture;
        core::u32 framesLeft = 0;
    };

    rhi::IDevice* m_device = nullptr;
    rhi::TextureHandle m_texture;
    core::u32 m_width = 0;
    core::u32 m_height = 0;
    // Small and bounded in practice: a drag replaces the target once a frame
    // and each entry lives two, so this holds at most three.
    std::vector<Retired> m_retired;
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
    // A double-click: drill INTO whatever this resolves to (S5.3). A single
    // click selects the outermost `Model` above what it landed on, and this is
    // the gesture that gets at the parts inside one.
    bool opening = false;
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
    // **The PROJECT's settings, which are a different thing from Preferences.**
    // A preference is about this person and this machine -- which theme, which
    // panels; a project setting is about the game and is committed with it. Two
    // dialogs rather than two tabs, because putting them together is how
    // somebody changes the window title of a game they are only visiting.
    bool projectSettings = false;
    bool about = false;
    bool newFolder = false;
    // The browser's make-a-stamp-of-a-CLASS box, which is a different question
    // from `newStamp` below: that one stamps an instance somebody right-clicked,
    // this one makes the instance too.
    bool newStampFromClass = false;
    // **Asking for the class picker, rather than opening it.** A menu item that
    // called `OpenPopup` itself opened it inside the menu -- a child popup, in
    // the menu's own ID scope, closed the moment the menu was -- so the folder's
    // right-click item did nothing at all while the toolbar button worked. The
    // flag is drained where the picker is BEGUN, which is the same pattern every
    // other dialog in this shell already follows.
    bool pickStampClass = false;
    // What the picker chose, carried to the name box: the two are one gesture
    // and the answer arrives a frame before the question is finished.
    scene::ClassId newStampClass = scene::InvalidClass;
    // The make-a-stamp box, which is the same shape again -- and it carries the
    // instance the question is ABOUT, because "a stamp of what" is decided by
    // the row somebody right-clicked and not by whatever is selected when they
    // finish typing.
    bool newStamp = false;
    core::InstanceId stampSubject;

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

    // "New Material" and "New Variant" (ADR 0090): the name box, and the
    // material a variant would be made of -- empty for a new base.
    bool newMaterial = false;
    std::string newMaterialParent;

    // **What is about to throw work away, waiting to be answered.**
    //
    // Every application with a document asks this, and it asks it in one place
    // rather than at each door: closing, starting over, opening something else
    // and leaving for another project all lose the same edits, so they all raise
    // the same question and the answer re-issues whichever of them asked.
    enum class Pending : core::u8
    {
        None,
        Quit,
        NewScene,
        OpenScene,
        NewProject,
        OpenProject,
    };
    Pending pending = Pending::None;
    // The scene `Pending::OpenScene` was going to open. Carried because the
    // answer arrives frames after the double-click, and by then the browser is
    // looking at something else.
    std::string pendingScene;
};

struct EditorPanels
{
    bool explorer = true;
    bool properties = true;
    bool viewport = true;
    // **`files` on screen, and it browses `content/` on disk**: scenes, meshes,
    // textures, prefab files. The field keeps its old name because every layout
    // in `.luaug/` already stores it.
    //
    // **There is exactly one content, and it is a folder.** For one afternoon
    // there were two -- this, and a global tree of instances -- and the human
    // asked the question that settles it: Unity has one Project window and
    // Unreal has one Content Browser, both a folder of files, and a prefab in
    // either is a file. Two stores is two answers to "where does this live"
    // (ADR 0052 records the reversal and why).
    bool content = true;
    bool console = true;
    bool stats = true;
    bool streaming = false;
    bool viewportSettings = false;
    // **The terrain brush's own dock**, off until somebody asks for it -- a
    // panel every project sees whether or not it has ground would be furniture,
    // and the toolbar's `dig` and `paint` open it.
    bool terrain = false;
    // **The block world's dock (V1)**, on the same terms: off until the
    // toolbar's `blocks` or selecting `VoxelService` asks for it.
    bool blocks = false;
    // **The Tiles tool's dock (the 2D layer)**, on the same terms: off until
    // the toolbar or selecting a `Tilemap2D` asks for it.
    bool tiles = false;
    // The stack, the variables and the transport (ADR 0057). On by default
    // because a debugger nobody can find is a debugger nobody uses, and it says
    // "running" when nothing is stopped rather than being empty.
    bool debug = true;

    // How the content browser lays its entries out.
    //
    // Three, because the three answer three different questions and every
    // engine that has a browser has all of them: a LIST is what you read when
    // you are looking for a name, TILES are what you scan when you half
    // remember what a thing looked like, and ICONS are what you use when the
    // thing IS a picture. Unity and Unreal both ship the middle one as the
    // default and both keep the other two.
    enum class ContentView : core::u8
    {
        List,
        Tiles,
        Icons,
    };
    ContentView contentView = ContentView::List;

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

    // **Whether the viewport draws the rig**, and it is off.
    //
    // A skeleton is the one thing in a scene with no visual at all: joints are
    // a flat array inside a render-side library, and until this switch existed
    // the only way to find out what a rig called its hand was to type a name
    // into `Bone.JointName` and watch whether `JointIndex` stopped saying -1.
    // Every editor that animates draws the armature -- and draws it as lines
    // rather than as instances, because a 677-joint character is 677 rows in
    // the Explorer, 677 entries in the change queue and 677 contributions to
    // the world hash bought for a picture (E9 assumption 2).
    //
    // Off by default for the same reason the physics wireframe is: it is a line
    // per joint for every skinned mesh in the world, which is a frame cost
    // nobody should pay without asking.
    bool showSkeletons = false;

    // **Whether the viewport draws the streaming grid**, and it is off.
    //
    // The Stats panel prints how many cells are resident and that is a different
    // question from where the seam is. Streaming is about SPACE -- which cell
    // the character is standing in, whether the ring is lopsided because a focus
    // is off by half a cell, where the authored world stops -- and the answer to
    // a question about space belongs in the space.
    //
    // Off by default: it is four lines per cell for every cell the index knows
    // about, which for a real world is thousands.
    bool showChunkGrid = false;

    // **Whether the viewport draws what the SOLVER thinks each part is**, and it
    // is off.
    //
    // The one picture that can disagree with the rendered one, which is what
    // makes it worth having: a `MeshPart`'s collider is a hull rather than its
    // triangles, a `Capsule`'s caps are hemispheres the collider never
    // stretches, and a `Wedge` collides as its whole box. Every one of those is
    // invisible until something falls through the world.
    //
    // `DebugService:ShowPanel("Physics")` has shown it in a running game since
    // M5, and a script call is not something an editor has -- so an author
    // building a level was the one person who could not see the colliders they
    // were placing.
    //
    // Off by default: it is a line per shape edge for every body in the world.
    bool showCollision = false;

    // **Whether the viewport draws a reference grid**, at the translate snap
    // step, and it is off.
    //
    // Lines you can see and a snap you cannot are two grids, and the one that
    // catches is the invisible one -- so this draws at the SAME spacing the snap
    // uses rather than at a round number of its own. Turning it on is how "why
    // did it land there" stops being a question.
    //
    // Off by default because a grid is only useful while placing something, and
    // a permanent one is lines across every screenshot.
    bool showGrid = false;

    // **The terrain's own triangles, and its normals** (the owner's terrain
    // report): the mesh around the camera as lines, green where a triangle's
    // winding agrees with its normals and red where it does not, and a short
    // line along each vertex's normal. The shaded picture cannot tell a wrong
    // normal from a dark material; these can. `terrain_overlay.h` has the rest.
    bool showTerrainWireframe = false;
    bool showTerrainNormals = false;
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
    // A `Script` or `ModuleScript` to open a tab on (ADR 0057). The loop reads
    // its `Source` and works out which file it came from, neither of which a
    // tree row knows.
    core::InstanceId openScript;
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
    // Ask for the Save As dialog. A flag rather than the dialog opening itself,
    // because the toolbar button and File > Save Scene As have to reach the
    // same one.
    bool wantSaveAs = false;
    // Close the editor. The menu's File > Exit, which is the one every
    // application has and the one people reach for before the window button.
    bool quit = false;
    // **Leave this project for another one.** Both start the project browser as
    // a new process and close this editor, which is what a project being a
    // PROCESS makes them (ADR 0055): the browser is where a project is made and
    // where one is picked, so File has no second copy of either.
    bool newProject = false;
    bool openProject = false;
    // **Bring files in from the machine.** The browser asks for the system's own
    // picker; the loop shows it, because a dialog opened from inside an ImGui
    // callback is a frame that has not finished drawing.
    bool importAssets = false;
    // Set when the request came from the EXPLORER rather than from the browser.
    // What is imported still lands in `content/` -- that is where a project's
    // files live and there is nowhere else for them to go -- and what changes is
    // that a mesh also arrives in the world, under this instance. A file the
    // world has no class for is imported and nothing more, which is the honest
    // half of the answer.
    core::InstanceId importParent;
    // **A heightmap for the Terrain panel**, from the system's picker, for the
    // reason `importAssets` goes through the loop. What is chosen is read where
    // it lies rather than copied into `content/`: the ground it makes is saved
    // with the scene, and the image is not needed again.
    bool pickHeightmap = false;

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
    // **Group puts the selection under one new instance; ungroup takes the
    // children out and destroys the container.** Two flags rather than one with
    // a direction, because they are two verbs a person means separately and a
    // toolbar draws two buttons for.
    bool groupSelection = false;
    bool ungroupSelection = false;
    // Rename an instance. Both halves or neither -- and singular, because
    // renaming four things to one name is not a thing anybody means.
    core::InstanceId renameInstance;
    std::string renameInstanceTo;

    // Move the selection under this. Set by a drop in the Explorer.
    core::InstanceId reparentTo;

    // **Move ONE instance to a place among its siblings** (S5.18). Set by a drop
    // in the top or bottom band of an Explorer row, where a drop in the middle
    // sets `reparentTo` instead.
    //
    // Singular where the reparent is plural, and that is the honest shape rather
    // than a shortcut: `moveChild` takes one index, and four instances dropped
    // between two rows have no single answer for what order they land in. The
    // drag already narrows to one -- a reorder gesture starts on the row under
    // the pointer -- so the alternative is inventing an order nobody asked for.
    core::InstanceId reorderChild;
    // The place it will OCCUPY, counting from zero, which is what `moveChild`
    // takes. Read as "before whatever stands here now" and a downward drag lands
    // one place short, every time.
    core::u32 reorderIndex = 0;

    // **Colour a folder**, from either panel. `colorTarget` names one in the
    // world and `colorContentPath` names one on disk -- one or the other, never
    // both, because they are stored in different places for the reason
    // `setFolderColor` gives.
    //
    // `colorAsked` is what makes "take the colour off" different from "no
    // command", which an empty optional alone cannot say.
    bool colorAsked = false;
    core::InstanceId colorTarget;
    std::string colorContentPath;
    std::optional<core::Color3> color;

    // **Make a stamp of `stampSubject`, named `stampName`** (ADR 0049). Both
    // halves or neither, like every other pair here.
    core::InstanceId stampSubject;
    std::string stampName;
    // Set instead of `stampName` by a drop into the content browser: the folder
    // it landed in, with the name taken from the instance itself.
    std::string stampFolder;
    // Place one, under the selection if there is one and under `Workspace`
    // otherwise. The path is content-relative, which is what the browser has.
    std::string placeStamp;
    // Whether that placement INHERITS from the stamp or is a copy that does not
    // (ADR 0051). Both are things a person means by "instance this".
    bool placeStampLinked = true;
    // Under this, or under the selection when it is invalid. A drop names the
    // row it landed on, which is the whole reason a drop is worth having beside
    // the menu item that does the same thing.
    core::InstanceId placeStampParent;
    // Take the mark off this one, so it stops following its file.
    core::InstanceId breakStamp;

    // **One property of one instance, taken back or pushed up** (S5.6). Two
    // fields rather than a pair per verb, because a menu can only be open on
    // one row at a time and the alternative is four fields that must agree.
    core::InstanceId overrideSubject;
    core::NameAtom overrideProperty;
    // Which of the two. Absent means neither was asked for this frame.
    std::optional<bool> overrideApply;

    // **Copy, cut and the two pastes.** `pasteInto` is the difference between
    // "another one beside this" and "one inside this", which is the distinction
    // every editor with a tree draws and the only one a person has to be told.
    bool copySelection = false;
    bool cutSelection = false;
    bool paste = false;
    bool pasteInto = false;

    // **Open a stamp for editing**, save what is open, or close it. Opening
    // replaces the world with the stamp and closing puts the scene back, so
    // all three are drained at the safe point like everything else that
    // replaces a world.
    std::string openStamp;
    bool saveStamp = false;
    bool closeStamp = false;
    // Whether closing writes first. False is what "close without saving" means.
    bool closeStampSaving = true;

    // Content-relative. Delete removes a folder with everything in it.
    std::string deleteContent;
    // A content-relative path to copy beside itself. How a material is made
    // from a material, and how anything else in the browser is copied.
    std::string duplicateContent;
    // A new stamp in the browser's current folder: WHICH class, and what to call
    // it. One verb rather than one per class -- the browser asks the same
    // question the Explorer's add-a-child menu asks, and gets its answer from
    // the same picker.
    scene::ClassId newStampClass = scene::InvalidClass;
    std::string newStampName;
    std::string renameContent;
    std::string renameContentTo;

    // A stamp dropped onto an instance-reference property: the file, and which
    // property to point at it. The selection is the target, read at the drain
    // like every other batch verb.
    //
    // **A command rather than a write, because a reference needs an INSTANCE and
    // a file is not one.** Dropping `Wooden.stamp` on a part's `Material` has to
    // put a `Material` in the world before anything can point at it -- and it
    // has to reuse the one already there if the same file has been dropped
    // before, or ten parts sharing one material would be ten materials that
    // merely look alike and stop agreeing the first time one is edited.
    std::string assignStampPath;
    std::string assignStampProperty;

    // Materials (ADR 0090). A material row dropped on the selection's
    // `Material` field, a row opened in the material panel, and the two ways a
    // material file is made -- all content-relative paths.
    std::string assignMaterialPath;
    // Who wears it: an Explorer row it was dropped on, the part under a
    // viewport pixel it was dropped at, or -- neither -- the selection.
    core::InstanceId assignMaterialTarget;
    std::optional<core::Vec2> assignMaterialPixel;
    std::string openMaterial;
    std::string newMaterial;
    std::string newMaterialVariantOf;
    std::string newMaterialVariantName;

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
    // **Which of these actually change the world.** A narrower question than
    // `any()`, and it exists because the answer decides whether closing the
    // editor asks about unsaved work: `any()` is true for clearing a selection
    // and for resetting the layout, and a confirmation that appeared after
    // pressing Escape would be a confirmation people learn to dismiss without
    // reading.
    //
    // Saving is deliberately absent: it writes the document rather than changing
    // it, and the flag is cleared where the write happens.
    [[nodiscard]] bool mutatesWorld() const noexcept
    {
        return createClass != scene::InvalidClass || deleteSelection || duplicateSelection || groupSelection ||
               ungroupSelection || reparentTo.valid() || reorderChild.valid() || renameInstance.valid() || paste ||
               pasteInto || cutSelection || !placeStamp.empty() || breakStamp.valid() || stampSubject.valid() || undo ||
               redo || newScene || !assignMaterialPath.empty();
    }

    [[nodiscard]] bool any() const noexcept
    {
        return play.has_value() || pause.has_value() || save || newScene || quit || resetLayout || clearSelection ||
               undo || redo || colorAsked || copySelection || cutSelection || paste || pasteInto ||
               stampSubject.valid() || !stampFolder.empty() || !placeStamp.empty() || breakStamp.valid() ||
               !openStamp.empty() || saveStamp || closeStamp || createClass != scene::InvalidClass || deleteSelection ||
               duplicateSelection || groupSelection || ungroupSelection || reparentTo.valid() || reorderChild.valid() ||
               renameInstance.valid() || !saveAs.empty() || !openScene.empty() || !createFolder.empty() ||
               !deleteContent.empty() || !duplicateContent.empty() || newStampClass != scene::InvalidClass ||
               !renameContent.empty() || !assignStampPath.empty() || importAssets || importParent.valid() ||
               openScript.valid() || !assignMaterialPath.empty() || !openMaterial.empty() || !newMaterial.empty() ||
               !newMaterialVariantOf.empty();
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
    // How many steps are on the stack. For a test asserting that a verb which
    // refused left nothing behind -- a step that undoes nothing eats a press of
    // ctrl-Z, and `canUndo` cannot tell one step from two.
    [[nodiscard]] core::usize depth() const noexcept { return m_undo.size(); }
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

// Where the editor's one-line status lives, and a counter that moves every time
// it is written.
//
// **The counter is what the toast needs.** A message that fades has to know when
// it was SET, and comparing the text cannot answer that: saving twice produces
// the same sentence, and a toast that refused to reappear for it would be a
// toast that stops working the second time you use a command.
//
// A wrapper rather than a setter at fifty-seven call sites: every one of them
// says `m_status = EditorStatus{...}`, which is the clearest way to write it,
// and this keeps that spelling while making the assignment observable.
class StatusSlot
{
public:
    StatusSlot& operator=(EditorStatus next)
    {
        m_value = std::move(next);
        ++m_serial;
        return *this;
    }

    [[nodiscard]] const EditorStatus& value() const noexcept { return m_value; }
    [[nodiscard]] core::u64 serial() const noexcept { return m_serial; }

private:
    EditorStatus m_value;
    core::u64 m_serial = 0;
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

    void requestPick(core::Vec2 pixelInViewport, bool additive = false, bool opening = false) noexcept
    {
        m_pending = PickRequest{pixelInViewport, additive, opening};
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
    bool save(scene::World& world, const std::filesystem::path& path);
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
    // Non-const because a save READS the stamps a scene names, to write each
    // stamped instance as a mark plus what differs (ADR 0051) -- and building
    // those reference trees needs the world's registries.
    bool saveOpenScene(scene::World& world);

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
    // Writes everything the editor remembers per person: the open scene, and
    // the colours somebody has put on their content folders. One file, because
    // the comment this replaced predicted the second thing correctly -- "the
    // second thing an editor wants to remember arrives sooner than anybody
    // expects, and a file that is only a string has nowhere to put it".
    void rememberState(const std::filesystem::path& stateDirectory) const;
    // Reads it back into this editor. The static `recallOpenScene` stays beside
    // it because the frame loop has to know which scene to BOOT before an
    // `Editor` exists at all.
    void recallState(const std::filesystem::path& stateDirectory);
    [[nodiscard]] static std::string recallOpenScene(const std::filesystem::path& stateDirectory);

    // --- Folder colour -------------------------------------------------------
    //
    // **A coloured folder is the cheapest navigation there is**, and both panels
    // that show folders offer it. Unity and Unreal both have it and both keep it
    // in editor state; this keeps it in two different places, and the split is
    // not an inconsistency but the only honest answer to what each folder IS.
    //
    // **A folder in the world is an INSTANCE, so it carries the colour itself**,
    // as an attribute. That is what makes it travel: the scene file records it
    // with no format change, undo takes it back like any other edit, and a
    // rename or a reparent cannot lose it because it was never keyed by where
    // the folder was.
    //
    // **A folder in `content/` is a DIRECTORY, and a directory cannot carry
    // anything.** So that one lives in `.luaug/editor.json` beside the
    // remembered scene, keyed by its content-relative path -- which does mean a
    // renamed folder comes back uncoloured, and that is a consequence worth
    // stating rather than a bug worth hiding.

    // The attribute an instance's colour is stored in. Named rather than
    // spelled out at four call sites, and prefixed so that a project reading its
    // own attributes can tell whose it is.
    static constexpr std::string_view FolderColorAttribute = "EditorColor";

    [[nodiscard]] static std::optional<core::Color3> folderColor(const scene::World& world, core::InstanceId id);

    // Sets or clears it, as one undo step. Through `setAttribute` rather than
    // into a component, for the same reason every other editor write goes
    // through the world's own setter.
    void setFolderColor(scene::World& world, core::InstanceId id, std::optional<core::Color3> color);

    [[nodiscard]] std::optional<core::Color3> contentColor(std::string_view path) const;
    void setContentColor(std::string_view path, std::optional<core::Color3> color);

    // The colours a person is offered before they reach for the picker.
    //
    // Ten, because a palette somebody has to scroll is a palette nobody uses,
    // and they are spread round the hue circle rather than picked by eye so that
    // two folders coloured a minute apart are actually distinguishable.
    static std::span<const core::Color3> folderPalette() noexcept;

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

    // --- The stage a stamp is edited on --------------------------------------
    //
    // **A stamp opens into a WORLD OF ITS OWN**, and the reason is the one a
    // person sees immediately: editing a prefab inside the game's scene shows
    // the prefab standing in the middle of the game, and every service the game
    // has is in the tree beside it. Unity calls this a prefab stage and Unreal
    // gives a blueprint its own viewport; both are the same answer to the same
    // complaint.
    //
    // **It is a bare `scene::World`, not a second host.** A stage has no
    // scripts, no physics, no streaming and no project -- it is a place to
    // arrange instances and look at them -- so the runtime a `WorldHost` builds
    // would be a `lua_State` per open prefab for nothing. What it does have is
    // exactly what drawing one needs: a `Workspace` to hold the instances and a
    // `Lighting` to see them by.
    //
    // **The registries are SHARED with the game's world**, not copied. A
    // `ClassId` is an index into a registry, so two registries mint different
    // ids for the same class and an instance described by one could not be read
    // by the other -- which is exactly what moving a stamp between the two has
    // to do.
    class Stage
    {
    public:
        // Builds an empty stage against the given registries. Cheap: no
        // runtime, no physics, no scripts.
        Stage(scene::ClassRegistry& classes, scene::EnumRegistry& enums, core::AtomTable& atoms, core::u64 seed);

        [[nodiscard]] scene::World& world() noexcept { return m_world; }
        [[nodiscard]] const scene::World& world() const noexcept { return m_world; }
        // What `extract` is given: where the instances are, and what lights them.
        [[nodiscard]] core::InstanceId workspace() const noexcept { return m_workspace; }
        [[nodiscard]] core::InstanceId lighting() const noexcept { return m_lighting; }

    private:
        scene::World m_world;
        core::InstanceId m_workspace;
        core::InstanceId m_lighting;
    };

    // The stage, or nullptr when no stamp is open. The frame loop points the
    // panels, the picker and the renderer at this world instead of the game's
    // while it exists -- which is the whole of "a separate environment".
    [[nodiscard]] Stage* stage() noexcept { return m_stage.get(); }
    [[nodiscard]] const Stage* stage() const noexcept { return m_stage.get(); }

    // --- Editing a stamp (ADR 0049) ------------------------------------------
    //
    // **Opening a stamp replaces the world with it**, and that is the whole
    // design: the tree, the properties grid, the manipulators, the plus, delete
    // and rename all work on it because it is made of ordinary instances in the
    // ordinary world. There is no second editor and no second set of verbs --
    // which is what Unity's prefab mode and Unreal's blueprint editor both are,
    // and why neither of them grew a parallel toolset.
    //
    // **The isolation is `play`'s machinery, not a new one.** Entering takes a
    // `WorldSnapshot`, clears the scene and builds the stamp alone; leaving puts
    // the snapshot back. That is the same pair `play` and `stop` already use and
    // it is already proven to restore ids, generations and the free list -- so
    // an instance the scene held before is the same instance afterwards.
    //
    // The price, stated rather than discovered: **the undo history is cleared on
    // the way in and on the way out**, exactly as a play session clears it.
    // Mixing steps taken inside a stamp with steps taken in a scene would let one
    // ctrl-Z apply a world that never existed.
    struct StampSession
    {
        // Content-relative, and empty when no stamp is open.
        std::string path;
        // The subtree being edited, in the world.
        core::InstanceId root;
        // Whether anything has changed since the last save. Advisory: it is what
        // the close button asks about, not a lock.
        bool dirty = false;
        // **The file as the world's linked instances were built from it** --
        // read on open and replaced on every save. `scene::restamp` needs it to
        // tell an override apart from an instance that is merely out of date,
        // and there is nowhere else it could come from: the file on disk is
        // already the new one by the time anybody asks.
        std::string baseline;

        [[nodiscard]] bool open() const noexcept { return !path.empty(); }
    };

    [[nodiscard]] const StampSession& stampSession() const noexcept { return m_stamp; }

    // --- Materials (ADR 0090) -----------------------------------------------
    //
    // **A material is a file under `content/`, and never an instance.** It has
    // no transform and no parent that means anything, and putting it in the
    // tree made every boundary of the tree a place it could be lost -- D115,
    // D133 and D142 were that one fact paid three times. So the editor makes
    // files, points parts at them by URN, and edits a file in a panel of its
    // own, with its own undo.

    // The library every world the editor draws resolves materials through --
    // the host's, borrowed. An edit to the open material is put in it at once,
    // so every part wearing that material shows the edit as it is made.
    void setMaterialLibrary(asset::MaterialLibrary* library) noexcept { m_materials = library; }
    [[nodiscard]] asset::MaterialLibrary* materialLibrary() const noexcept { return m_materials; }

    // What `createMaterial` will write for what somebody typed: under
    // `materials/`, with the compound suffix, content-relative. Public and pure
    // so a dialog can show the resolved path while it is typed.
    [[nodiscard]] static std::string normalizeMaterialPath(std::string_view typed);
    // `asset://` and the content-relative path: what a part wears.
    [[nodiscard]] static std::string contentUrn(std::string_view relative);

    // **Writes a new base material** -- the engine default's values, declaring
    // no parameters, so it is exactly what its author makes it (ADR 0090) --
    // and returns its content-relative path, or empty with `status()` saying
    // why. Refused over an existing file.
    [[nodiscard]] std::string createMaterial(std::string_view name);
    // Writes a VARIANT of the material at `parent` (content-relative): a file
    // that names it and overrides nothing yet, so it looks exactly like its
    // parent until somebody changes one field.
    [[nodiscard]] std::string createMaterialVariant(std::string_view parent, std::string_view name);

    // **Makes every instance in `targets` wear the material at `path`**, as one
    // undo step. The gesture is a drop of a material row onto a part, or onto
    // a part's `Material` field. A part that already wears it is not a
    // failure, and a drop onto parts that all already wear it records nothing
    // -- a step that undoes nothing eats a press of ctrl-Z (D134).
    bool assignMaterialTo(scene::World& world, std::string_view path, std::span<const core::InstanceId> targets);

    // The material open in the material panel.
    struct MaterialSession
    {
        // Content-relative, and empty when none is open.
        std::string path;
        // What the panel shows and edits, and what the file held when it was
        // opened or last saved.
        asset::MaterialAsset asset;
        asset::MaterialAsset saved;
        // **The panel's own undo**, separate from the world's: an edit to a
        // file is not an edit to the scene, and one ctrl-Z must not reach
        // across the two.
        std::vector<asset::MaterialAsset> undo;
        std::vector<asset::MaterialAsset> redo;

        [[nodiscard]] bool open() const noexcept { return !path.empty(); }
        [[nodiscard]] bool dirty() const noexcept { return open() && !(asset == saved); }
    };

    [[nodiscard]] const MaterialSession& materialSession() const noexcept { return m_material; }

    // Opens the material file at `path` in the panel. A file that cannot be
    // read is refused with the reason in `status()`.
    bool openMaterial(std::string_view path);
    // Closes it. **Unsaved edits are taken back out of the library**, so every
    // world draws the file as it stands on disk again.
    void closeMaterial();
    // One edit to the open material, recorded for the panel's undo and shown
    // in every open world at once. `continuing` folds it into the step before
    // -- a slider dragged across a hundred frames is one thing to undo.
    void editMaterial(const asset::MaterialAsset& next, bool continuing = false);
    bool undoMaterial();
    bool redoMaterial();
    // Writes the file (ADR 0090's fixed key order) and keeps it open.
    bool saveMaterial();

    // Opens a stamp onto a stage of its own. Refused while playing: a stamp is
    // authored, and a world that is ticking is not one somebody is authoring.
    //
    // The registries are the game world's, shared rather than copied -- see
    // `Stage` for why that is not optional.
    bool openStamp(std::string_view path, scene::ClassRegistry& classes, scene::EnumRegistry& enums,
                   core::AtomTable& atoms, Inspector& inspector);

    // Writes the open stage back to its file, and moves every linked instance
    // of it in `game` to match (ADR 0051).
    //
    // **The world is the game's, not the stage's**, and that is the whole point:
    // a stamp is a definition, so saving one is the moment everything that is an
    // instance of it changes. `gameRoot` is where the walk starts -- the
    // `DataModel` rather than the `Workspace`, because a linked instance is not
    // obliged to live under one.
    bool saveStamp(scene::World& game, core::InstanceId gameRoot);

    // --- What a placed stamp has of its own (S5.6) --------------------------
    //
    // ADR 0049 was reversed to inheritance-with-overrides: an instance inherits
    // from its stamp, a change to one instance stays local, and a change to the
    // stamp reaches every instance that has not overridden that property. The
    // SAVE has understood that since ADR 0051 and the EDITOR could not say it,
    // so a person could see neither which properties were theirs nor how to
    // take one back.
    //
    // Which properties of `id` are its own rather than its stamp's. Empty for
    // anything that is not inside a placed stamp, which is most of a world.
    //
    // **This reads the stamp FILE, so a caller drawing every frame must not ask
    // every frame** -- that is the shape of D118, the defect that made the
    // editor feel like it reloaded the world whenever anybody touched anything.
    // Deliberately NOT cached here: the world can be changed by a gizmo drag, a
    // script or a queued property write, none of which this object hears about,
    // so a cache in here would be a wrong answer with no way to notice. The
    // Properties panel caches it against its own selection and a frame budget,
    // which is a cost decision made where the cost is.
    [[nodiscard]] std::vector<core::NameAtom> overridesOf(const scene::World& world, core::InstanceId id);

    // Put the stamp's value back. ONE undo step, and refused rather than
    // recorded when there is nothing to revert -- a step that undoes nothing
    // eats a press of ctrl-Z, which is the invariant D134 and D141 both record.
    bool revertOverride(scene::World& world, core::InstanceId id, core::NameAtom property);

    // Push this instance's value up into the stamp FILE, so every other
    // instance that has not overridden that property follows it.
    //
    // **The instance stops being overridden as a consequence rather than as a
    // step**: once the file says what the instance says, there is nothing left
    // to differ. Instances that had overridden the same property with some
    // OTHER value keep theirs, because `restamp` measures them against the
    // file's previous text -- applying is not a way to overwrite other people's
    // edits.
    bool applyOverride(scene::World& world, core::InstanceId gameRoot, core::InstanceId id, core::NameAtom property);

    // Drops the stage. `save` writes it out first; without it the edits go with
    // it, which is what "close without saving" means.
    //
    // **The game's world was never touched, so there is nothing to put back.**
    // That is the difference between a stage and the first cut of this, and it
    // is why a person no longer finds their prefab standing in their game.
    bool closeStamp(scene::World& game, core::InstanceId gameRoot, Inspector& inspector, bool save);

    // Marks the open stamp as changed. Called by the frame loop whenever an
    // editor verb touches the world, because "did anything change" is a question
    // about EVERY verb rather than about any one of them.
    void touchStamp() noexcept
    {
        if (m_stamp.open())
            m_stamp.dirty = true;
    }

    // --- Stamps (ADR 0049) ---------------------------------------------------
    //
    // Content holds SOURCES and the world holds a world; an instance in the
    // world may be a link to a source, and it stops being one the moment
    // somebody changes it. These four verbs are the whole of that, and the rule
    // each of them applies is ADR 0049's, not one invented here.

    // **Makes a stamp out of `id` and turns `id` into an instance of it.**
    //
    // That second half is what every engine does and it is the useful part: the
    // thing you just made a source out of should BE one of its instances, or
    // you have a file and a copy of it that will drift apart by tomorrow.
    //
    // `name` is a stamp name without an extension -- `lantern-post`, not
    // `stamps/lantern-post.stamp.json` -- and lands in `content/stamps/` unless
    // it carries a folder of its own. Refuses: something the engine owns,
    // something inside what a system made, and **a subtree that already
    // contains a stamped instance**, which ADR 0049 declines to answer for
    // rather than half-answering.
    // Makes a default instance of `classId` in the world and stamps it into the
    // content browser in one step, returning the stamp's content-relative path
    // or empty.
    //
    // **Both, because that is what a new stamp IS.** A stamp is a file of an
    // instance: making only the file would leave nothing to edit, and making
    // only the instance would leave nothing to reuse. So this does what
    // converting does, one step earlier -- it writes the file, and the instance
    // becomes an instance OF that file.
    //
    // What it makes is the class's own defaults, which for a `Material` is the
    // identity -- white, dielectric, no maps, so a part pointed at it looks
    // exactly as it did with none. That is the right starting point for any of
    // them: change one field, see one change.
    [[nodiscard]] std::string createStampOfClass(scene::World& world, core::InstanceId root, scene::ClassId classId,
                                                 std::string_view name);

    bool createStamp(scene::World& world, core::InstanceId id, core::InstanceId root, std::string_view name);

    // **Places a stamp under `parent`**, selects it and asks the tree to reveal
    // it, as one undo step. `name` is what `createStamp` took.
    //
    // `linked` is the difference between the two things a person means by
    // "instance this" (ADR 0051):
    //
    //   * **linked** -- it INHERITS. Change the stamp and this changes with it,
    //     except where somebody has overridden a property here.
    //   * **a copy** -- it is its own from the first frame. The stamp made it
    //     and has nothing more to do with it.
    //
    // Both are real things to want, which is why both are here rather than one
    // being the "right" one: a lamp post you will place forty of wants the
    // link, and a starting point you are about to rebuild does not.
    bool instantiateStamp(scene::World& world, std::string_view name, core::InstanceId parent, core::InstanceId root,
                          Inspector& inspector, bool linked = true);

    // Points `property` on every instance in `targets` at the stamp named by
    // `path`, placing one under `parent` if the world has none yet. One undo
    // step for the whole gesture.
    //
    // **A reference needs an instance and a file is not one.** Dragging a stamp
    // onto an instance-valued field means "point at one of those", so one has
    // to be in the world before the write can happen. (It was built for
    // `BasePart.Material`, which is a URN now -- `assignMaterialTo` -- and it
    // stays for the properties that are still instance-valued.)
    //
    // **The one already in the world wins.** Placing a fresh copy per drop
    // would give ten instances ten targets that merely look alike, and they
    // would stop looking alike the first time anybody edited one.
    //
    // False when the file is unreadable or nothing accepted the write, with
    // `status()` saying which; the undo step is taken back rather than left,
    // because a step that undoes nothing eats a press of ctrl-Z.
    bool assignStampTo(scene::World& world, core::InstanceId root, core::InstanceId parent, std::string_view path,
                       std::string_view property, std::span<const core::InstanceId> targets);

    // **Takes the mark off**, so the instance becomes an ordinary subtree that
    // serialises in full and no longer follows the file.
    //
    // Breaking is not a failure and nothing is lost: a broken instance can be
    // stamped again. It is a separate verb as well as an automatic consequence
    // because "I want this one to stop following the source" is a thing people
    // mean deliberately.
    bool breakStamp(scene::World& world, core::InstanceId id);

    // **Editing a stamped instance does NOT break its mark** (ADR 0051), and
    // this is where a function used to sit that made it.
    //
    // ADR 0049 chose break-on-edit, from the human's own words at the time. They
    // used it and reversed it: an instance INHERITS from its stamp, a change to
    // one instance is an OVERRIDE that stays local, and a change to the stamp
    // reaches every instance that has not overridden that property. So there is
    // nothing to break and nothing to warn about -- the serializer writes what
    // differs, and `breakStamp` below stays for the one case that is still
    // deliberate.
    //
    // What is not an override is a STRUCTURAL change -- a child added or
    // removed inside an instance -- and that is not refused either: the save
    // writes such an instance in full and drops its mark, which loses nothing
    // and is counted.

    // How a scene reads the stamps it names. Bound to this editor's content
    // root, and the one place that knows `content/` is where they live --
    // `scene` is L3 and has no filesystem.
    [[nodiscard]] scene::StampSource stampSource() const;

    // What `createStamp` will actually write, given what somebody typed.
    // Public and pure so a dialog can preview the resolved path while it is
    // being typed, which is the half that makes the rule visible rather than
    // surprising (D068's lesson, applied before it can happen again).
    [[nodiscard]] static std::string normalizeStampPath(std::string_view typed);
    [[nodiscard]] static bool stampNameIsUsable(std::string_view typed);

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

    // --- The clipboard -------------------------------------------------------
    //
    // **It holds TEXT, not ids**, and that is what makes it a clipboard rather
    // than a note about the world: an `InstanceId` stops meaning anything the
    // moment its instance is deleted, a scene is loaded or a stamp is opened,
    // and every one of those is a thing somebody does between a copy and a
    // paste. What is kept is what `writeStamp` writes -- the same description a
    // prefab is made of and the same one a drag between worlds carries.
    //
    // So a copy survives everything, and a cut is a copy plus a delete rather
    // than a third mechanism holding a subtree in limbo.

    void copySelection(const scene::World& world, std::span<const core::InstanceId> ids, core::InstanceId root);
    [[nodiscard]] bool hasClipboard() const noexcept { return !m_clipboard.empty(); }
    [[nodiscard]] core::usize clipboardCount() const noexcept { return m_clipboard.size(); }

    // Builds what was copied under `parent`, selects it, and asks the tree to
    // reveal it -- as ONE undo step however many subtrees it holds.
    bool paste(scene::World& world, core::InstanceId parent, core::InstanceId root, Inspector& inspector);

    // Whether a drop of `ids` onto `newParent` would move anything at all.
    [[nodiscard]] static bool canReparent(const scene::World& world, std::span<const core::InstanceId> ids,
                                          core::InstanceId newParent, core::InstanceId root);

    bool reparent(scene::World& world, std::span<const core::InstanceId> ids, core::InstanceId newParent,
                  core::InstanceId root, Inspector& inspector);

    // **Moves `child` to `index` among its siblings**, as one undo step (S5.18).
    //
    // The Explorer's other half of a drag: a drop in the MIDDLE of a row is a
    // reparent, and a drop in its top or bottom band is this. `World::moveChild`
    // has existed since the verb was built and had nothing to call it, which is
    // why a scene's child order was whatever the order of creation had been.
    //
    // Records nothing when the move changes nothing, for the reason D134 gives:
    // a step that undoes nothing eats a press of ctrl-Z, and recording one
    // clears the redo stack with it.
    bool reorder(scene::World& world, core::InstanceId child, core::u32 index, Inspector& inspector);

    // Delete and duplicate over a whole selection, as ONE undo step each --
    // because somebody who deleted four things did one thing, and four steps is
    // four presses of ctrl-Z to get back to where they were.
    //
    // Ordered by the tree before acting, so the result does not depend on the
    // order somebody happened to click in (R10's discipline applied to an
    // editor: an operation over a set has to be a function of the set).
    bool deleteInstances(scene::World& world, std::span<const core::InstanceId> ids, core::InstanceId root,
                         Inspector& inspector);
    // **Puts `ids` under one new container and selects it** (S5.4).
    //
    // The container is a `Model` when anything in the selection has a transform
    // and a `Folder` when nothing does. That is the whole rule, and it is the
    // right one: a `Model` has a pivot, extents and a scale, all of which are
    // meaningless around four scripts -- and a `Folder` around four parts throws
    // away the one thing grouping parts is for.
    //
    // It is created under the SHALLOWEST common parent, so grouping things from
    // two branches does not silently move the group into one of them.
    //
    // Refused, with a reason, when: nothing is selected, everything selected is
    // engine-owned, or the selection includes the authored root -- a world
    // cannot be put inside something in it.
    bool groupSelection(scene::World& world, std::span<const core::InstanceId> ids, core::InstanceId root,
                        Inspector& inspector);

    // **Takes each selected container's children out and destroys it**, and
    // selects what came out.
    //
    // Only containers: something with no children is not a group, and refusing
    // it by name is better than a verb that silently deletes a part. The
    // children keep their world transform, because ungrouping is a change to the
    // TREE and not to where anything is -- a `Model` has no transform of its own
    // to have been applying, so this is true by construction and is asserted
    // rather than arranged.
    bool ungroupSelection(scene::World& world, std::span<const core::InstanceId> ids, core::InstanceId root,
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
    bool saveSceneAs(scene::World& world, std::string_view relativePath);

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

    // How the content browser was laid out last time.
    //
    // **Kept here rather than only in `EditorPanels`** because this is the class
    // that has a file: the panel struct lives for as long as the shell and is
    // rebuilt from nothing every launch, and a preference somebody set once
    // belongs to the project rather than to the run. The shell seeds its own
    // copy from this on the first frame and writes back through the setter.
    // **What the DEFAULT arrangement knows that a saved one may not.**
    //
    // ImGui's `layout.ini` remembers where somebody put a panel, which is
    // exactly right and is why nothing here overwrites it. What it cannot tell
    // apart is a choice from an accident: a tab that opens because it was docked
    // last looks identical, in that file, to one somebody clicked. So when a
    // default changes for a reason -- see `selectDockTab` -- this number moves,
    // the shell applies the new default once against a layout that predates it,
    // and everything a person actually arranged is left alone.
    //
    // Bump it only for a default that was WRONG rather than merely different. A
    // number that moved for a preference would be a preference overwritten.
    static constexpr core::i64 CurrentLayoutRevision = 1;

    [[nodiscard]] core::i64 layoutRevision() const noexcept { return m_layoutRevision; }
    void setLayoutRevision(core::i64 revision) noexcept
    {
        if (m_layoutRevision == revision)
            return;
        m_layoutRevision = revision;
        m_preferencesDirty = true;
    }

    [[nodiscard]] EditorPanels::ContentView contentView() const noexcept { return m_contentView; }
    void setContentView(EditorPanels::ContentView view) noexcept
    {
        m_contentView = view;
        m_preferencesDirty = true;
    }

    // Where the OS window was when somebody last had it.
    //
    // **A static reader, like `recallOpenScene`**, and for the same reason: the
    // window is created before there is an editor to ask, so this has to be
    // answerable from the file alone. Nothing when the file has no window block,
    // which is what a first launch and every non-editor shell get.
    [[nodiscard]] static std::optional<platform::WindowPlacement>
    recallWindow(const std::filesystem::path& stateDirectory);

    // Records where the window is now. Called from the frame loop when the
    // window moves or is resized -- there is no other moment that knows.
    void rememberWindow(const platform::WindowPlacement& placement) noexcept;

    // One-shot: true once after anything a PERSON chose has changed -- the
    // manipulator's mode and space, snapping, the browser's layout.
    //
    // A flag rather than a write inside each setter, because the toggles are
    // keystrokes: `Ctrl+L` twice would be two files written from inside an input
    // handler. The frame loop drains this once and writes once, which is the
    // same shape as every other command in this editor.
    //
    // **On change rather than at exit** for the reason the open scene is: an
    // editor that only wrote this on a clean shutdown would forget it the one
    // time somebody most wants it.
    [[nodiscard]] bool takePreferencesDirty() noexcept
    {
        const bool changed = m_preferencesDirty;
        m_preferencesDirty = false;
        return changed;
    }

    [[nodiscard]] const EditorStatus& status() const noexcept { return m_status.value(); }

    // Moves every time the status is written, whether or not the words changed.
    // What the viewport's toast restarts its fade on.
    [[nodiscard]] core::u64 statusSerial() const noexcept { return m_status.serial(); }

    // Says something in the editor's own voice, from outside it.
    //
    // The one caller is the properties grid reporting a write the world
    // refused. It has no other way to say so: the grid is drawn by the shell and
    // the status belongs to the editor, and a refusal nobody is told about is a
    // value that silently did not change.
    void report(std::string message, bool failed) { m_status = EditorStatus{std::move(message), failed}; }

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
    // **Whether a script is stopped in the debugger** (ADR 0057). Written every
    // frame by the loop, read only by `allowedTicks`.
    void setDebuggerParked(bool parked) noexcept { m_debuggerParked = parked; }
    [[nodiscard]] bool debuggerParked() const noexcept { return m_debuggerParked; }

    [[nodiscard]] core::u32 allowedTicks(core::u32 owed) noexcept
    {
        // **No tick begins while a script is parked, and that is what keeps R10
        // true rather than a courtesy.** ADR 0025's guarantee is indexed by
        // TICKS and `task.wait` deadlines are tick indices -- so if the world
        // advanced while a coroutine sat on a breakpoint, that script would
        // resume N ticks later than it would have without a debugger and the run
        // would diverge. With this, the tick sequence is byte-identical to the
        // one nobody debugged and the pause happens between ticks as far as the
        // simulation can tell.
        //
        // Here rather than in the panel for the reason this function already
        // states about pausing: a rule about the world belongs to the world's
        // model, and a rule with one caller in a panel is a rule that is wrong
        // the first time something else asks.
        if (m_debuggerParked)
            return 0;
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
    // `root` is the world root the viewport is DRAWING -- the stage's workspace
    // while a stamp is open, the host's otherwise. It has to be the renderer's,
    // or a click can land on something the renderer never put on screen.
    std::optional<PickHit> resolvePick(const scene::World& world, core::InstanceId root, Inspector& inspector) noexcept;

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

    // --- What a click in the viewport is FOR (F1) -----------------------------
    //
    // **Not a fourth `GizmoMode`**, and the reason is worth stating because the
    // fourth mode is the obvious shape and it is wrong twice over. Mechanically:
    // `snapStep` indexes `f32 m_snapStep[3]` by the raw enum with no bounds
    // check, so a fourth value reads past the array. Conceptually: the three
    // gizmo modes are three ways to transform a SELECTION, and a brush has no
    // selection -- it acts on the ground under the pointer, which is a different
    // question from "what does dragging this handle do".
    //
    // A tool is what a click MEANS. `Select` is the editor as it has always
    // been: the manipulator gets the pointer, then a pick. `Sculpt` and `Paint`
    // put a brush in front of both, and `Blocks` puts the block world's
    // place-and-break in front of both (V1).
    enum class Tool : core::u8
    {
        Select,
        Sculpt,
        Paint,
        Blocks,
        // Paints and erases a `Tilemap2D`'s cells (the 2D layer).
        Tiles,
    };
    [[nodiscard]] Tool tool() const noexcept { return m_tool; }
    // Refused mid-stroke, for the reason `setGizmoMode` is refused mid-drag:
    // changing what a gesture means half way through it is not something a
    // person can have meant.
    void setTool(Tool tool) noexcept;

    // **What the brush DOES**, which is a different question from which tool is
    // selected.
    //
    // The six are what the reference editor offers (the owner, 2026-09-23:
    // "it has to be faithful"): two that stamp volume where the brush is aimed,
    // two that move the surface it is aimed at, one that softens and one that
    // levels. **None of them moves a column** -- a click on the side of a cliff
    // builds out from the cliff, never a pillar down from it.
    enum class BrushOp : core::u8
    {
        // A ball (or box) of ground, centred where the brush is aimed: held
        // down, it builds towards the camera.
        Add,
        // The same, taken away. **Not "material zero"** -- that is the
        // encoding's spelling and a person choosing the first entry of a
        // palette must never mean it.
        Subtract,
        // The surface moves out along its own normal (`asset::growBall`):
        // a field rises, a cliff comes forward.
        Grow,
        // The surface moves in along its normal: the ground wears away.
        Erode,
        // Blur the ground towards the average of its neighbours.
        Smooth,
        // Pull the ground towards one height, captured where the stroke began.
        Flatten,
    };

    // The brush's footprint.
    //
    // Two, and a cylinder is the obvious third: it is a box in plan and a ball
    // in section, and the field has no verb for it. Adding one means a third
    // depth function beside `ballDepth` and `blockDepth`, which is small and is
    // not what makes this panel usable.
    enum class BrushShape : core::u8
    {
        Sphere,
        Box,
    };

    // What the brush is, and every field of it is persisted.
    //
    // **`spacing` is a fraction of the radius, not a strength.** It is how
    // densely a drag stamps. `strength` is how hard the brush works: how fast a
    // held Add or Subtract builds or bores, how far one stamp of Grow or Erode
    // moves the surface, and how far smoothing and flattening move the ground
    // towards their target.
    struct Brush
    {
        BrushOp op = BrushOp::Add;
        BrushShape shape = BrushShape::Sphere;
        // Metres.
        f32 radius = 4.0f;
        // Stamps every `spacing * radius` metres along a stroke.
        f32 spacing = 0.25f;
        // How hard the brush works: for `Smooth` and `Flatten`, how far
        // towards the target one stamp moves the ground (one goes all the way);
        // for `Grow` and `Erode`, how far it moves the surface; for `Add` and
        // `Subtract`, how fast a held brush builds or bores.
        f32 strength = 0.35f;
        // What ground is made of. Never zero: erasing is `BrushOp::Subtract`,
        // because a material picker whose first entry deleted the world would be
        // the worst possible reading of one shared convention.
        core::u8 material = 1;
    };
    [[nodiscard]] const Brush& brush() const noexcept { return m_brush; }
    void setBrushRadius(f32 metres) noexcept;
    void setBrushSpacing(f32 fraction) noexcept;
    void setBrushMaterial(core::u8 material) noexcept;
    void setBrushStrength(f32 strength) noexcept;
    void setBrushOp(BrushOp op) noexcept
    {
        m_brush.op = op;
        m_preferencesDirty = true;
    }
    void setBrushShape(BrushShape shape) noexcept
    {
        m_brush.shape = shape;
        m_preferencesDirty = true;
    }

    // --- Making ground exist ---------------------------------------------
    //
    // **The gap that made the brush useless.** For one commit the only way to
    // get a `Terrain` into a world was a script calling `Instance.new`, so
    // opening the editor on any project showed no brush, no panel and no way to
    // begin -- which is not a missing feature, it is the feature not being
    // reachable. Every editor in this shape has a Create step for exactly this
    // reason.

    // Creates a `Terrain` under `root`, or returns the one already there.
    // Records an undo step.
    core::InstanceId createTerrain(scene::World& world, core::InstanceId root, Inspector& inspector);

    // Fills a square of ground centred on the origin, from the world's floor up
    // to `height`.
    //
    // **From the floor and not from `height` down**, which is the rule F1 paid
    // for: a fill that reaches the floor is a height function and stays cheap,
    // and one that does not is a floating slab that costs voxels for every
    // column of it.
    bool generateGround(scene::World& world, core::InstanceId root, Inspector& inspector, f32 size, f32 height,
                        core::u8 material);

    // Empties the field. Records an undo step, so it is not the disaster it
    // sounds like.
    bool clearTerrain(scene::World& world, core::InstanceId root, Inspector& inspector);

    // **Ground from a heightmap image**, the way every terrain editor starts a
    // real landscape: a square of `size` metres centred on the terrain's
    // origin, black at `low` and white at `high`, in world metres.
    //
    // It is `Terrain:WriteHeights` with a file in front of it, so a column that
    // carries voxels -- a cave -- has its top moved and keeps the cave, and
    // heights past the terrain's `MinHeight` and `MaxHeight` are clamped to
    // them; the status line says so. Creates the terrain when there is none, as
    // `generateGround` does, and that is an undo step of its own.
    struct HeightmapImport
    {
        std::filesystem::path source;
        f32 size = 256.0f;
        f32 low = 0.0f;
        f32 high = 64.0f;
        core::u8 material = 1;
    };
    bool importHeightmap(scene::World& world, core::InstanceId root, Inspector& inspector, const HeightmapImport& spec);
    // The file the Terrain panel imports from, as the picker last answered.
    [[nodiscard]] const std::filesystem::path& heightmapSource() const noexcept { return m_heightmapSource; }
    void setHeightmapSource(std::filesystem::path source) { m_heightmapSource = std::move(source); }

    // The terrain under the root the viewport is drawing, or nothing.
    [[nodiscard]] core::InstanceId terrainIn(const scene::World& world, core::InstanceId root) const;

    // **Where world content goes, given whatever root a caller happens to
    // hold.**
    //
    // The shell's panels are handed the root the EXPLORER draws, which is the
    // `DataModel` -- so a verb that took it at face value put a `Terrain` beside
    // `Lighting` and `RunService` instead of in the world. Reported by the owner
    // the first time they pressed the button, which is the shortest path from a
    // wrong root to a visible symptom there is.
    //
    // Returns `root` when it already is a workspace -- a stamp stage's root is
    // one -- and its `Workspace` child otherwise. Nothing when neither, because
    // a world with nowhere to put content is a world this verb must refuse
    // rather than guess about.
    [[nodiscard]] core::InstanceId workspaceUnder(const scene::World& world, core::InstanceId root) const;

    // **Where a stroke aims when the ray meets no ground.**
    //
    // Every volumetric terrain grows this, and for the reason the research
    // found: an empty field is a field a ray misses, so a brush over one stamps
    // nothing anywhere and the tool reads as broken. It is the same failure the
    // panel fixed one layer up -- a tool that cannot be reached is not a tool --
    // and it is why the engines with sparse storage all offer a plane to aim at.
    //
    // The plane is horizontal at the terrain's own origin, or at the height the
    // stroke last hit ground, so extending a hillside past its edge continues it
    // rather than dropping to zero. A heightmap engine never needs this because
    // its plane is the allocation; ours is a fallback for the case theirs cannot
    // have.
    [[nodiscard]] bool brushPlaneLock() const noexcept { return m_brushPlaneLock; }
    void setBrushPlaneLock(bool locked) noexcept
    {
        m_brushPlaneLock = locked;
        m_preferencesDirty = true;
    }

    // Where the brush is aiming, in world space, or nothing when it is over sky
    // -- for the ring the viewport draws. Cast against the FIELD rather than
    // against physics, because an editor in edit mode holds no bodies at all
    // (`asset::raycastField` says why at length).
    [[nodiscard]] std::optional<asset::TerrainHit> brushAim() const noexcept { return m_brushAim; }

    // Whether the world the viewport is drawing has terrain in it, as of the
    // last `driveSculpt`. The toolbar shows the brush only when it does, because
    // a tool with nothing to act on is furniture.
    [[nodiscard]] bool hasTerrain() const noexcept { return m_hasTerrain; }
    [[nodiscard]] bool sculpting() const noexcept { return m_stroke.has_value(); }
    // **Whether the Terrain panel is on screen**: open, and its tab the one
    // showing where it is docked. The shell says so every frame it draws.
    // **The brush acts only while it is** (the owner, 2026-09-23: moving
    // around the viewport, passing over the ground opened the terrain editor's
    // brush). A tool chosen and then put out of sight is a tool at rest, not a
    // ring following the pointer over every hill. True until a shell says
    // otherwise, so a caller that draws no panels -- a test -- keeps a brush.
    void setTerrainPanelShown(bool shown) noexcept { m_terrainPanelShown = shown; }
    [[nodiscard]] bool terrainPanelShown() const noexcept { return m_terrainPanelShown; }

    // **How many times the world has been put back** -- an undo, a redo, a
    // stop. Something that holds state about the world from outside it, the
    // way a terrain's cell streamer holds which cells it loaded, compares this
    // to know its picture may have been replaced (ADR 0087).
    [[nodiscard]] core::u64 worldRestores() const noexcept { return m_worldRestores; }

    // **Run at the start of every save, before the scene is written** (ADR
    // 0087): a terrain saved as cells writes the cells it changed, and one
    // grown past what a scene should carry becomes cells here. False refuses
    // the save, with `note` saying why; true may leave a note for the status.
    using TerrainSaver =
        std::function<bool(scene::World& world, const std::filesystem::path& scenePath, std::string& note)>;
    void setTerrainSaver(TerrainSaver saver) { m_terrainSaver = std::move(saver); }
    // How many stamps the last finished stroke laid down. Zero before the first
    // one. For the status line, and for the test that a drag cut into forty
    // frames edits the ground the same number of times as the same drag cut
    // into four.
    [[nodiscard]] core::u32 lastStrokeStamps() const noexcept { return m_lastStrokeStamps; }

    // Runs the brush for this frame, before the manipulator and before the pick.
    //
    // Returns true when the pointer belongs to the brush, which is what stops a
    // sculpt click ALSO re-selecting whatever is behind the ground it just dug.
    //
    // `root` is the world root the viewport is DRAWING, for the same reason
    // `resolvePick` takes one: the terrain a click can reach is the terrain on
    // screen.
    //
    // `dt` is the render clock's seconds since the last frame. Only a carving
    // stroke reads it -- held still against a wall it bores at a speed, not at
    // a framerate -- and zero leaves every other stroke exactly as it was.
    bool driveSculpt(scene::World& world, core::InstanceId root, Inspector& inspector, double dt = 0.0);

    // --- The block world (V1, `VoxelService`) ------------------------------
    //
    // **Not the terrain brush with a cube on it.** A brush has a radius and a
    // falloff and acts on ground; a block tool acts on exactly one cell, and
    // the cell it acts on is decided by which FACE the pointer is over -- place
    // goes against that face, break takes the block behind it. That is the
    // interaction every block-building game converges on, because it is the
    // only one where a person can say which of two adjacent cells they meant.

    // What a click does with the block tool.
    enum class BlockOp : core::u8
    {
        // A block of the selected type against the face under the pointer.
        Place,
        // The block under the pointer goes.
        Break,
        // The block under the pointer becomes the selected type, and nothing
        // is added or removed -- the block world's `Paint`.
        Replace,
    };
    [[nodiscard]] BlockOp blockOp() const noexcept { return m_blockOp; }
    void setBlockOp(BlockOp op) noexcept
    {
        m_blockOp = op;
        m_preferencesDirty = true;
    }
    // The type placing and replacing lay down, by id. Ids start at 1; air is
    // `Break`, for the reason `setBrushMaterial` refuses zero.
    [[nodiscard]] asset::BlockId blockType() const noexcept { return m_blockType; }
    void setBlockType(asset::BlockId id) noexcept
    {
        m_blockType = id == asset::AirBlock ? 1 : id;
        m_preferencesDirty = true;
    }

    // The block world of `world`, or nothing: the first `VoxelService`'s. A
    // stamp stage has no services, so it has no block world either.
    [[nodiscard]] static scene::VoxelComponent* voxelsIn(scene::World& world) noexcept;

    // Registers a block type from the editor and selects it. Records an undo
    // step. A name already registered updates that type's colours and answers
    // its id -- exactly what `VoxelService:RegisterBlock` does, so a type made
    // here and one a script registers by the same name are the same type.
    // Answers air when the world has no block world or the name is empty.
    asset::BlockId addBlockType(scene::World& world, Inspector& inspector, std::string_view name, core::Color3 top,
                                core::Color3 side, core::Color3 bottom);
    // Recolours a type. `gesture` coalesces a colour-picker drag into one undo
    // step; zero records one step per call.
    bool setBlockTypeColors(scene::World& world, Inspector& inspector, asset::BlockId id, core::Color3 top,
                            core::Color3 side, core::Color3 bottom, core::u64 gesture = 0);
    // A type's images and how see-through it is: what `SetBlockTextures` and
    // `SetBlockOpacity` set from a script, set from the panel. `textures` are
    // content URNs for the top, the sides and the bottom, empty for none;
    // `opacity` is `Enum.BlockOpacity`'s value. `gesture` as for the colours.
    bool setBlockTypeLook(scene::World& world, Inspector& inspector, asset::BlockId id,
                          const std::array<core::NameAtom, 3>& textures, core::i32 opacity, f32 transparency,
                          core::u64 gesture = 0);
    // Removes every block and keeps the types. Records an undo step.
    bool clearBlocks(scene::World& world, Inspector& inspector);

    // Where the block tool is aiming.
    struct BlockAim
    {
        // The block under the pointer, and the face the pointer is over.
        std::array<core::i32, 3> block{};
        std::array<core::i32, 3> face{};
        // No block was under the pointer and the aim is the ground plane at
        // y = 0 instead: `block` is the cell just below it, so `block + face`
        // is the first layer -- which is how an empty block world gets its
        // first block at all.
        bool onPlane = false;
    };
    [[nodiscard]] std::optional<BlockAim> blockAim() const noexcept { return m_blockAim; }
    // The cell the current op would change, in block coordinates, or nothing.
    [[nodiscard]] std::optional<std::array<core::i32, 3>> blockTarget() const noexcept;
    // Whether the drawn world has a block world at all, as of the last
    // `driveBlocks`, and its block size.
    [[nodiscard]] bool hasVoxels() const noexcept { return m_hasVoxels; }
    [[nodiscard]] f32 voxelBlockSize() const noexcept { return m_voxelBlockSize; }
    // How many blocks the last finished stroke changed.
    [[nodiscard]] core::u32 lastBlockEdits() const noexcept { return m_lastBlockEdits; }

    // Runs the block tool for this frame, beside `driveSculpt` and on the same
    // terms: true when the pointer belongs to it. A drag is one undo step and
    // edits each new cell the pointer crosses, aimed against the blocks as they
    // were when it began -- so dragging `Place` across a floor lays a layer
    // instead of building a staircase towards the camera.
    bool driveBlocks(scene::World& world, Inspector& inspector);

    // --- The 2D layer (phase 3): the 2D view and the Tiles tool --------------
    //
    // **The 2D view is the same editor camera with an orthographic lens**,
    // looking down -Z at the plane every `Part2D` and `Tilemap2D` lies on --
    // not a second viewport. The right or middle drag pans instead of turning,
    // WASD pans, and the wheel zooms about the pointer, which is what every 2D
    // editor's hands expect. Leaving it puts the 3D camera back where it was.
    [[nodiscard]] bool view2D() const noexcept { return m_view2D; }
    void setView2D(bool on) noexcept;
    // Half the view's height in metres: `Camera.OrthographicSize` for the
    // editor's own lens.
    [[nodiscard]] f32 orthographicSize() const noexcept { return m_orthographicSize; }
    // `steps` of the wheel, positive towards the screen; the world point under
    // `pointerInViewport` stays under it.
    void zoom2D(f32 steps, core::Vec2 pointerInViewport) noexcept;

    // What a click does with the Tiles tool.
    enum class TileOp : core::u8
    {
        // The selected tile goes into the cell under the pointer.
        Paint,
        // The cell under the pointer is emptied.
        Erase,
    };
    [[nodiscard]] TileOp tileOp() const noexcept { return m_tileOp; }
    void setTileOp(TileOp op) noexcept
    {
        m_tileOp = op;
        m_preferencesDirty = true;
    }
    // The tile painting lays down: the tileset's `n`-th, counted from 1 along
    // its rows. Zero is `Erase`, so it is refused here as air is by the blocks.
    [[nodiscard]] core::u16 tile() const noexcept { return m_tile; }
    void setTile(core::u16 tile) noexcept
    {
        m_tile = tile == 0 ? core::u16{1} : tile;
        m_preferencesDirty = true;
    }

    // **Which tilemap the tool paints**: the selected one, or one whose
    // descendant is selected, or else the first under `root`'s workspace. The
    // fallback is what lets somebody open a level and paint without first
    // finding the tilemap in the tree.
    [[nodiscard]] static core::InstanceId tilemapFor(const scene::World& world, const Inspector& inspector,
                                                     core::InstanceId root) noexcept;

    // Where the Tiles tool is aiming: the tilemap and the cell under the pointer.
    struct TileAim
    {
        core::InstanceId tilemap;
        std::array<core::i32, 2> cell{};
    };
    [[nodiscard]] std::optional<TileAim> tileAim() const noexcept { return m_tileAim; }
    // How many cells the last finished stroke changed.
    [[nodiscard]] core::u32 lastTileEdits() const noexcept { return m_lastTileEdits; }

    // **The tool follows its panel**, as the terrain brush does: it takes the
    // pointer only while the Tiles panel is open and on screen, so a viewport
    // click with the panel closed is a selection and never a stray stroke.
    void setTilesPanelShown(bool shown) noexcept { m_tilesPanelShown = shown; }
    [[nodiscard]] bool tilesPanelShown() const noexcept { return m_tilesPanelShown; }

    // Runs the Tiles tool for this frame, beside `driveBlocks` and on the same
    // terms: true when the pointer belongs to it. A drag is one undo step and
    // paints every cell the pointer crosses -- a line between two frames'
    // cells, so a fast stroke leaves no gaps.
    bool driveTiles(scene::World& world, core::InstanceId root, Inspector& inspector);

    // The tileset the palette shows: the painted tilemap's image as the
    // renderer loaded it, and its size and tile size in pixels. Handed over by
    // the frame loop, which owns the texture library, and read by the shell.
    struct TilesetPreview
    {
        rhi::TextureHandle texture;
        core::Vec2 pixels{0.0f, 0.0f};
        core::Vec2 tileSize{16.0f, 16.0f};
    };
    void setTilesetPreview(const TilesetPreview& preview) noexcept { m_tilesetPreview = preview; }
    [[nodiscard]] const TilesetPreview& tilesetPreview() const noexcept { return m_tilesetPreview; }

    [[nodiscard]] GizmoMode gizmoMode() const noexcept { return m_gizmoMode; }
    // Refused mid-drag: changing what a drag means half way through it is not
    // something a person can have meant.
    void setGizmoMode(GizmoMode mode) noexcept;

    // **World axes or the selection's own.** Which one is right depends on the
    // part, which is why it is a person's choice and not this file's: a rotated
    // crate is unusable in world space and a wall is unusable in local.
    // **Where the manipulator sits over a SELECTION** (S5.17).
    //
    // The primary's own transform, or the middle of everything selected. Over
    // one instance the two are the same and the control does nothing; over
    // forty they are the difference between rotating a row of columns about the
    // one you clicked last and rotating it about itself, which is two different
    // gestures somebody means at different moments.
    //
    // `Pivot` is the default because it is the one with no surprise in it: the
    // gizmo is on the thing you last clicked, which is where you are looking.
    enum class GizmoOrigin : core::u8
    {
        Pivot,
        Centre,
    };
    [[nodiscard]] GizmoOrigin gizmoOrigin() const noexcept { return m_gizmoOrigin; }
    void setGizmoOrigin(GizmoOrigin origin) noexcept
    {
        m_gizmoOrigin = origin;
        m_preferencesDirty = true;
    }

    [[nodiscard]] bool gizmoLocal() const noexcept { return m_gizmoLocal; }
    void setGizmoLocal(bool local) noexcept;

    // Snapping is ON, and a modifier suspends it. That way round because the
    // number somebody wants is far more often a round one, and because a
    // manipulator with no snap is the one that feels like a toy.
    [[nodiscard]] bool snapping() const noexcept { return m_snap && !m_snapSuspended; }
    void setSnapSuspended(bool suspended) noexcept { m_snapSuspended = suspended; }
    void setSnap(bool on) noexcept
    {
        m_snap = on;
        m_preferencesDirty = true;
    }
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

    // **What a double-click in the viewport opened** (S5.3), or nothing.
    //
    // A click resolves to the outermost `Model` above what it landed on, which
    // is what makes a grouped thing a thing. Double-clicking is the escape
    // hatch: it drills in, and from then on clicks inside that model select the
    // parts. Clicking outside it, or pressing Escape, comes back out.
    [[nodiscard]] core::InstanceId drilled() const noexcept { return m_drilled; }
    void setDrilled(core::InstanceId id) noexcept { m_drilled = id; }

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
    // --- Unsaved work ---------------------------------------------------------
    //
    // **A scene is a document, and a document that has changed says so.** The
    // stamp stage has had this since E3 (`StampSession::dirty`); the scene
    // itself did not, which is why closing the editor threw away an afternoon
    // without a word.
    //
    // Advisory rather than a lock, exactly as the stamp's is: what it decides is
    // whether a question is asked, never whether an edit is allowed.
    [[nodiscard]] bool sceneDirty() const noexcept { return m_sceneDirty; }
    [[nodiscard]] bool hasUnsavedWork() const noexcept { return m_sceneDirty || m_stamp.dirty; }

    // Marks whatever is being edited as changed: the STAGE when one is open,
    // and the scene otherwise. One call at the frame's safe point rather than a
    // flag on each verb, because "did anything change" is a question about every
    // verb rather than about any one of them.
    void touch() noexcept { touchAs(m_stamp.open()); }

    // The same, told WHICH document rather than asking. A frame's command drain
    // can open or close a stamp partway through, and a mutation belongs to the
    // document that was open when it happened rather than to whichever one is
    // open by the time the frame gets round to marking it.
    void touchAs(bool stamped) noexcept
    {
        if (stamped)
            m_stamp.dirty = true;
        else
            m_sceneDirty = true;
    }

    // Somebody asked to close and there is work to lose, so the shell owes them
    // a question. Held on the editor rather than in the overlay's dialog state
    // because the window's own close button arrives as a platform event, which
    // the frame loop sees and the panels do not.
    // What an import did, as the one line the status bar shows. Here rather
    // than in the browser because the status bar is the editor's and every other
    // verb reports through it.
    void reportImport(const ContentTree::ImportReport& report) noexcept;

    void requestClose() noexcept { m_closeRequested = true; }
    [[nodiscard]] bool closeRequested() const noexcept { return m_closeRequested; }
    void clearCloseRequest() noexcept { m_closeRequested = false; }

    void adoptCamera(const core::CFrameD& cframe) noexcept;
    [[nodiscard]] bool cameraAdopted() const noexcept { return m_cameraAdopted; }

    // **Whether the viewport is showing the editor's camera while the game
    // runs** (S5.8), and it is off.
    //
    // Pressing play hands the view to the game's camera, which is what pressing
    // play means. Detaching takes the VIEW back without touching the simulation:
    // the world keeps ticking, the game's camera keeps doing whatever it does,
    // and you fly around and watch. That is the only way to see a running game
    // from anywhere other than where it puts you -- an enemy behind a wall, a
    // chunk that failed to stream, a character stuck inside geometry the player
    // camera is inside of too.
    //
    // **It takes the pointer back as well**, and that is not a separate
    // decision: the fly camera is driven by a right-drag, and a game holding the
    // pointer (D069) means the drag never reaches the editor -- so detaching
    // without it is a camera you cannot turn.
    //
    // Cleared by play and by stop, because "am I looking through my own camera"
    // is a question about the current run and not a preference. Reading it while
    // editing answers false, since the view is already the editor's there.
    [[nodiscard]] bool cameraDetached() const noexcept { return m_cameraDetached && m_run != RunState::Editing; }
    void setCameraDetached(bool detached) noexcept { m_cameraDetached = detached; }

    // **F: put the camera where it can see what is selected**, which every
    // editor in this shape does and which somebody's hands therefore already
    // know. The DIRECTION is kept and only the position moves: reorienting as
    // well would answer "show me this" with "and from over here", and a person
    // who has arranged a view is not asking to lose it.
    //
    // Eased over a fraction of a second rather than snapped, and any camera
    // input cancels it -- a tool that keeps moving the view after somebody has
    // taken the controls is a tool arguing with them.
    void focusCamera(core::DVec3 centre, core::f64 radius) noexcept;
    [[nodiscard]] bool focusing() const noexcept { return m_focusRemaining > 0.0f; }

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
    // Content-folder colours, keyed by content-relative path. Ordered rather
    // than hashed so the file it is written to is the same bytes for the same
    // state -- the property every other format in this repository has.
    std::map<std::string, core::Color3> m_contentColors;

    // What a copy left behind, as text. See `copySelection`.
    std::vector<std::string> m_clipboard;

    StampSession m_stamp;
    MaterialSession m_material;
    asset::MaterialLibrary* m_materials = nullptr;
    // The world a stamp is edited in, or nothing. Built on open and dropped on
    // close, so an editor with no stamp open carries no stage at all.
    std::unique_ptr<Stage> m_stage;

    // A drag in progress. `start` is where the pointer was solved to on the
    // frame the button went down, and `before` is every selected instance's
    // transform at that moment -- the two things a delta is measured from.
    // What a drag is moving, and HOW to move it (S5.2).
    //
    // **Four kinds, because four things are transformed four ways.** A part and
    // a camera each own a world `CFrame` and take one straight; an attachment's
    // `CFrame` is relative to the part it is on, so a world transform has to be
    // divided back through the parent's; a `Model` has no transform at all and
    // moves by moving every part under it, which is what `PivotTo` does.
    //
    // The kind is decided ONCE, when the drag starts, rather than asked per
    // frame: an instance does not change what it is mid-drag, and re-deciding
    // would put four pool lookups in the hot path of a gesture that runs every
    // frame.
    enum class DragKind : core::u8
    {
        Part,
        Camera,
        Attachment,
        Model,
        // A sprite on the plane: a `Position` and a `Rotation` about Z rather
        // than a `CFrame` (the 2D layer).
        Part2D,
    };

    // Puts one dragged instance at a world `CFrame` by whatever route its kind
    // is transformed through. Defined beside the drag because it reads
    // `m_drag`'s parallel arrays.
    void applyDragTransform(scene::World& world, Inspector& inspector, core::usize index, const core::CFrameD& after);

    struct GizmoDrag
    {
        GizmoHandle handle;
        GizmoFrame frame;
        core::DVec3 startPoint;
        f32 startAngle = 0.0f;
        std::vector<core::InstanceId> targets;
        std::vector<core::CFrameD> before;
        std::vector<core::Vec3> sizes;
        std::vector<DragKind> kinds;
        // For an attachment, the parent's world frame at the START of the drag;
        // identity for everything else. Captured rather than read live because
        // dragging a bone whose part is itself being dragged would otherwise
        // divide by a frame that has already moved this tick.
        std::vector<core::CFrameD> parents;
        core::u64 gesture = 0;
    };

    ViewportRect m_viewport;
    core::Mat4 m_projection;
    core::Mat4 m_view;
    core::DVec3 m_cameraOrigin;
    bool m_hasCamera = false;
    std::optional<PickRequest> m_pending;
    core::InstanceId m_drilled;
    RunState m_run = RunState::Editing;
    // **A stroke in progress**, which is one undo step however many frames and
    // however many stamps it turns into.
    struct Stroke
    {
        core::InstanceId terrain;

        // Where the last stamp of the previous frame landed, so this frame's
        // stamps are walked from there rather than from the frame before.
        core::DVec3 last;
        core::u64 gesture = 0;
        core::u32 stamps = 0;
        // **Captured where the stroke began**, which is what makes `Flatten` a
        // tool a person can aim: dragging across a hillside levels it to where
        // you first clicked rather than to wherever the pointer happens to be,
        // which would chase its own result downhill.
        f32 plane = 0.0f;
        // **A stroke that stamps volume at the aim**: `Add` or `Subtract`.
        // Held still, it builds or bores a ball every `radius / speed` seconds,
        // so a tunnel is as deep after a second at 30 Hz as at 144.
        bool carve = false;
        // **The ground as it was when a volume stroke began**, which its DRAG
        // aims at. A ball centred on the aim, dragged over the ground it is
        // digging, lands each frame in the hole the last one left and digs a
        // radius deeper -- so a drag tunnelled towards the floor, and deeper
        // the higher the framerate. Dragged, a stroke follows the ground it
        // started on and cuts an even trench or builds an even ridge; held
        // still, it aims at the ground as it now is and bores or builds. A
        // vector of shared pointers, not a copy of the ground (ADR 0082).
        asset::TerrainField aimField;
        // Seconds banked toward the next stamp of a brush held still.
        double carveClock = 0.0;
    };

    // How far a brush can reach, in metres. A ray fired at the horizon has to
    // terminate, and this is also the honest limit of "click the ground": past
    // a few hundred metres a pixel covers more ground than the brush does.
    static constexpr double BrushReach = 512.0;

    // One stamp, in WORLD space. Converted to the field's own inside, because a
    // terrain can be moved and the field does not know it.
    void applyBrushAt(scene::TerrainComponent& terrain, core::DVec3 worldAt);
    // Whether a stroke with this tool and brush stamps volume at the aim
    // (`Stroke::carve`).
    [[nodiscard]] static bool carves(Tool tool, const Brush& brush) noexcept;
    // One frame of a stroke, aimed at the ground as it now is: a drag stamps
    // by distance, and a pointer held still stamps on the clock -- Add and
    // Subtract at their building speed, every other tool at a rate its strength
    // sets.
    void holdStroke(scene::TerrainComponent& terrain, const PickRay& ray, double dt);

    // A block stroke: the grid it aims against, frozen while the button is
    // held so a drag does not climb the blocks it has just placed, and the last
    // cell it changed so a drag held over one cell edits it once.
    struct BlockStroke
    {
        asset::VoxelGrid aimGrid;
        std::optional<std::array<core::i32, 3>> last;
        core::u64 gesture = 0;
        core::u32 edits = 0;
    };
    // One click of the block tool at the current aim. Answers whether a block
    // changed.
    bool applyBlockAt(scene::VoxelComponent& voxels);
    // Whether the current op at `at` would change a block -- asked before the
    // undo step is recorded, so a click that does nothing records nothing.
    [[nodiscard]] bool blockEditChanges(const scene::VoxelComponent& voxels,
                                        const std::array<core::i32, 3>& at) const noexcept;

    BlockOp m_blockOp = BlockOp::Place;
    asset::BlockId m_blockType = 1;
    bool m_hasVoxels = false;
    f32 m_voxelBlockSize = 1.0f;
    core::u32 m_lastBlockEdits = 0;
    std::optional<BlockAim> m_blockAim;
    std::optional<BlockStroke> m_blockStroke;

    // The 2D view, and the 3D camera it put aside.
    bool m_view2D = false;
    f32 m_orthographicSize = 10.0f;
    core::CFrameD m_saved3DCamera;
    f32 m_saved3DYaw = 0.0f;
    f32 m_saved3DPitch = 0.0f;
    // A Tiles stroke: the tilemap it began on, so a drag across another cannot
    // switch targets half way, and the last cell it reached.
    struct TileStroke
    {
        core::InstanceId tilemap;
        std::optional<std::array<core::i32, 2>> last;
        core::u64 gesture = 0;
        core::u32 edits = 0;
    };
    TileOp m_tileOp = TileOp::Paint;
    core::u16 m_tile = 1;
    std::optional<TileAim> m_tileAim;
    std::optional<TileStroke> m_tileStroke;
    core::u32 m_lastTileEdits = 0;
    bool m_tilesPanelShown = true;
    TilesetPreview m_tilesetPreview;

    Tool m_tool = Tool::Select;
    bool m_hasTerrain = false;
    bool m_terrainPanelShown = true;
    core::u64 m_worldRestores = 0;
    TerrainSaver m_terrainSaver;
    bool m_brushPlaneLock = true;
    std::filesystem::path m_heightmapSource;
    core::u32 m_lastStrokeStamps = 0;
    Brush m_brush;
    std::optional<asset::TerrainHit> m_brushAim;
    std::optional<Stroke> m_stroke;

    GizmoMode m_gizmoMode = GizmoMode::Translate;
    bool m_gizmoLocal = false;
    GizmoOrigin m_gizmoOrigin = GizmoOrigin::Pivot;
    bool m_snap = true;
    bool m_snapSuspended = false;
    EditorPanels::ContentView m_contentView = EditorPanels::ContentView::List;
    // Zero for a project arranged before this existed, which is the case the
    // migration is for.
    core::i64 m_layoutRevision = 0;
    std::optional<platform::WindowPlacement> m_window;
    bool m_preferencesDirty = false;
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
    StatusSlot m_status;
    ContentTree m_content;
    std::string m_openScene;
    UndoStack m_history;

    bool m_sceneDirty = false;
    bool m_debuggerParked = false;
    bool m_closeRequested = false;
    core::CFrameD m_cameraCFrame;
    bool m_cameraAdopted = false;
    bool m_cameraDetached = false;
    // Where `focusCamera` is taking the position, and how long it has left.
    core::DVec3 m_focusTarget;
    f32 m_focusRemaining = 0.0f;
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
