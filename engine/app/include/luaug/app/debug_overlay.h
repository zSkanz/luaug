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

#include "luaug/app/frame_scheduler.h"
#include "luaug/app/inspector.h"
#include "luaug/core/id.h"
#include "luaug/platform/event.h"
#include "luaug/rhi/types.h"

#include <span>

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
    DebugOverlay(platform::Window& window, rhi::IDevice& device);
    ~DebugOverlay();

    DebugOverlay(const DebugOverlay&) = delete;
    DebugOverlay& operator=(const DebugOverlay&) = delete;

    // False when the overlay never started: a shipping build, a backend that
    // rasterizes nothing, or an ImGui that failed to initialize. Every other
    // method is a no-op in that state.
    [[nodiscard]] bool active() const noexcept { return active_; }

    // Whether the panel is currently drawn. Off until F3 is pressed, because a
    // debug overlay that greets everyone who starts the engine is in the way.
    [[nodiscard]] bool visible() const noexcept { return visible_; }
    void setVisible(bool visible) noexcept { visible_ = visible; }

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
};

} // namespace luaug::app
