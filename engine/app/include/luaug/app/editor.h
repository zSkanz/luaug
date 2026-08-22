#pragma once

#include <luaug/app/content_tree.h>
#include <luaug/app/inspector.h>
#include <luaug/app/picking.h>
#include <luaug/core/id.h>
#include <luaug/core/math.h>
#include <luaug/rhi/types.h>
#include <luaug/scene/world.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

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
};

struct EditorPanels
{
    bool explorer = true;
    bool properties = true;
    bool viewport = true;
    bool content = true;
    bool console = true;
    bool stats = true;
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
    // Ask for the Save As dialog. A flag rather than the dialog opening itself,
    // because the toolbar button and File > Save Scene As have to reach the
    // same one.
    bool wantSaveAs = false;
    // Close the editor. The menu's File > Exit, which is the one every
    // application has and the one people reach for before the window button.
    bool quit = false;
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
               !saveAs.empty() || !openScene.empty() || !createFolder.empty();
    }
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
    [[nodiscard]] bool hasCamera() const noexcept { return m_hasCamera; }

    void requestPick(core::Vec2 pixelInViewport) noexcept { m_pending = PickRequest{pixelInViewport}; }
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

    // Names the scene the world already holds, without loading anything. The
    // boot path uses it: the engine loads a project's scene before the editor
    // exists, and the editor has to know which one that was.
    void adoptOpenScene(std::string_view relativePath);

    // Empty when no scene has been opened. Content-relative.
    [[nodiscard]] const std::string& openScenePath() const noexcept { return m_openScene; }

    [[nodiscard]] const EditorStatus& status() const noexcept { return m_status; }

    // Ask for exactly one tick while paused. A step is how somebody watches a
    // thing happen instead of inferring it from before and after, and it is the
    // one control a paused editor cannot do without.
    void requestStep() noexcept { m_stepRequested = true; }

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
    ViewportRect m_viewport;
    core::Mat4 m_projection;
    core::Mat4 m_view;
    core::DVec3 m_cameraOrigin;
    bool m_hasCamera = false;
    std::optional<PickRequest> m_pending;
    RunState m_run = RunState::Editing;
    bool m_stepRequested = false;
    // Held by pointer because a `WorldSnapshot` is thirty component pools and
    // an editor that is not playing should not be carrying an empty one.
    std::unique_ptr<scene::WorldSnapshot> m_playSnapshot;
    EditorStatus m_status;
    ContentTree m_content;
    std::string m_openScene;

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

} // namespace luaug::app
