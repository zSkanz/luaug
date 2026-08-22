// The developer overlay (ADR 0011): Dear ImGui on the docking branch, bound to
// F3, drawn on top of the finished frame.
//
// Dev builds only. ADR 0011 compiles ImGui out of shipping entirely, and the
// build expresses that by not declaring the dependency at all
// (LUAUG_DEBUG_UI, third_party/CMakeLists.txt). What is left in that profile is
// this class with the same shape and methods that do nothing -- so the frame
// loop calls it unconditionally and carries no #ifdef.
//
// It is also inert wherever there is nothing to draw with: `--rhi=capture` and
// `--rhi=null` have no GPU device, and a headless run has no window. That is a
// normal outcome, not a failure, and `active()` is how a caller asks.
//
// R3 does not apply to what this draws. The overlay exists for whoever is
// building the engine, never for a player, so its labels are literals rather
// than catalog keys -- the same reason a GPU debug-group name is one.
#pragma once

#include "luaug/app/editor.h"
#include "luaug/app/frame_scheduler.h"
#include "luaug/app/inspector.h"
#include "luaug/core/id.h"
#include "luaug/platform/event.h"
#include "luaug/rhi/types.h"
#include "luaug/script/runtime.h"

#include <span>
#include <string>

namespace luaug::platform {
class Window;
}

namespace luaug::scene {
class World;
}

namespace luaug::rhi {
class ICmdList;
class IDevice;
} // namespace luaug::rhi

namespace luaug::app {

// Which shell this draws.
//
// The panels are the same either way and that is the point: the explorer, the
// properties grid, the console and the stats are what an editor is made of, and
// a second copy of them under a different name would be two things to keep in
// step. What differs is the furniture -- one floating window against a
// dockspace with a viewport in it -- and whether a layout is remembered.
enum class Shell
{
    // F3 over a running game. One window, nothing persisted.
    Overlay,
    // `luaug edit`. A dockspace, a 3D viewport, and a layout that survives a
    // restart.
    Editor,
};

class DebugOverlay
{
public:
    // `window` must already have been claimed by `device`: the colour format
    // the overlay's pipeline is built against comes from that pairing, and
    // asking for it before the claim answers with something the swapchain will
    // not deliver.
    //
    // Both must outlive the overlay. Shutting ImGui down releases GPU
    // resources, so the device has to still be there when this is destroyed --
    // which is the same declaration-order rule the window and device already
    // have between themselves.
    //
    // ImGui's context is process-wide, so exactly one overlay may be alive at a
    // time; a second one refuses to start rather than corrupting the first.
    //
    // `shell` decides the furniture and cannot change afterwards: ImGui reads
    // `IniFilename` when the context is created, so where a layout lives is a
    // decision taken once.
    //
    // `layoutPath` is where `Shell::Editor` remembers its docking, and it is
    // required for that shell and ignored for the other. It is copied, because
    // ImGui stores the pointer and reads it at every save.
    DebugOverlay(platform::Window& window, rhi::IDevice& device, Shell shell = Shell::Overlay,
                 std::string layoutPath = {});
    ~DebugOverlay();

    DebugOverlay(const DebugOverlay&) = delete;
    DebugOverlay& operator=(const DebugOverlay&) = delete;

    // False when the overlay never started: a shipping build, a backend that
    // rasterizes nothing, or an ImGui that failed to initialize. Every other
    // method is a no-op in that state.
    [[nodiscard]] bool active() const noexcept { return active_; }

    // Whether the panel is currently drawn. Off until F3 is pressed, because a
    // debug overlay that greets everyone who starts the engine is in the way --
    // and on from the start in `Shell::Editor`, where it IS the application.
    [[nodiscard]] bool visible() const noexcept { return visible_; }
    void setVisible(bool visible) noexcept { visible_ = visible; }

    // The editor's model: where the viewport is, what it was rendered with, and
    // what a click asked it to find. Null -- the default -- draws no viewport
    // panel, which is what `Shell::Overlay` always wants.
    //
    // The overlay only ever WRITES the viewport rectangle and the pick request
    // here. Resolving a pick walks the world, and that happens at the frame's
    // safe point beside every other world mutation, not inside a UI callback.
    void setEditorTarget(Editor* editor, rhi::TextureHandle viewport) noexcept
    {
        editor_ = editor;
        viewportTexture_ = viewport;
    }

    // What the shell asked for while it drew, taken by the frame loop and reset.
    // Draining rather than reading, so a command cannot be acted on twice
    // because a frame did not draw.
    [[nodiscard]] EditorCommands takeCommands() noexcept
    {
        const EditorCommands taken = commands_;
        commands_.clear();
        return taken;
    }

    // Applies the F3 toggle from this frame's translated events, and forwards
    // the untranslated stream behind them -- platform::rawEvents() -- to
    // ImGui, which models far more input than the engine does.
    //
    // Call after platform::pumpEvents() and with the span it returned: both
    // halves describe the same pump, and the raw one is only valid until the
    // next.
    void handleEvents(std::span<const platform::Event> events);

    // What the explorer walks and where its edits go (M4 brief, Decisions 14
    // to 16). `world` null -- the default -- draws the stats panel alone,
    // which is the state of a host with no world and of every test with no
    // scene; `root` is the instance the tree is drawn from, `game`.
    //
    // Re-point this after a reload. A reload destroys the outgoing `WorldHost`
    // and with it the `World` this points at, so a stale pointer here is a
    // dangling one rather than a wrong one.
    void setInspectionTarget(scene::World* world, core::InstanceId root, Inspector* inspector) noexcept
    {
        world_ = world;
        root_ = root;
        inspector_ = inspector;
    }

    // The VM the console pane evaluates in and reads the memory table off
    // (D017). Null -- the default -- draws neither pane, which is the state of a
    // host with no world and of every test with no VM.
    //
    // Re-point this after a reload, for the same reason `setInspectionTarget`
    // says: the reload destroys the runtime this points at.
    void setScriptTarget(script::ScriptRuntime* runtime) noexcept { runtime_ = runtime; }

    // Takes over the process log sink and keeps the last few hundred lines for
    // the console pane, chaining to whatever sink was installed so the console
    // and the log FILE both get every line. Called once, by the host, after the
    // console sink is installed.
    //
    // The ring is bounded and drops the oldest: a shell that grew with the log
    // would be a memory leak with a scrollbar.
    void captureLog();

    // Draws the panel into `target` in a render pass of its own, so it sits on
    // top of whatever the frame already rendered.
    //
    // Contract: call between the frame's last endRenderPass() and
    // submitAndPresent(), with no copy pass open. ImGui uploads its vertex data
    // outside a render pass, SDL_GPU allows one pass at a time, and the seam
    // has no way to say "flush whatever is open" -- so the caller owns that
    // ordering, and it is the ordering a frame loop already has.
    void render(rhi::ICmdList& cmd, rhi::TextureHandle target, const Frame& frame);

private:
    bool active_ = false;
    bool visible_ = false;

    // Non-owning and rebindable, because the world they describe is rebuilt by
    // every hot reload while the overlay is not (ADR 0024: the reload destroys
    // `WorldHost` and nothing above it).
    scene::World* world_ = nullptr;
    core::InstanceId root_;
    Inspector* inspector_ = nullptr;
    script::ScriptRuntime* runtime_ = nullptr;

    // `[[maybe_unused]]` rather than an `#ifdef`, because this header's shape is
    // the promise at the top of the file: the same class in every profile, so
    // the frame loop carries no preprocessor. A build with no ImGui reads none
    // of these, and Clang's `-Wunused-private-field` is right about that -- it
    // is a state a shipping build is MEANT to be in, which is what the
    // attribute says and what a guard would hide.
    [[maybe_unused]] Shell shell_ = Shell::Overlay;
    // Owned, because `io.IniFilename` is a borrowed pointer ImGui keeps.
    [[maybe_unused]] std::string layoutPath_;
    // Whether the first-launch arrangement has been considered. Once, and a
    // member rather than a static so it belongs to an overlay rather than to
    // the process.
    [[maybe_unused]] bool layoutBuilt_ = false;
    EditorCommands commands_;
    [[maybe_unused]] Editor* editor_ = nullptr;
    [[maybe_unused]] rhi::TextureHandle viewportTexture_;
};

} // namespace luaug::app
