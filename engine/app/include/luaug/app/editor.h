#pragma once

#include <luaug/app/inspector.h>
#include <luaug/app/picking.h>
#include <luaug/core/id.h>
#include <luaug/core/math.h>
#include <luaug/rhi/types.h>

#include <optional>

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

// Whether the world this editor is looking at is being simulated.
//
// **An editor opens paused, and that is the whole point of having this**
// (D058). A world that is ticking is a world whose properties are being
// written by scripts sixty times a second, so somebody typing into the
// properties grid is arguing with the game and losing.
//
// There is deliberately no `Stopped`. In an editor, stop returns the world to
// what it was before play began -- to the EDITED state -- and nothing in this
// engine can remember an edited state yet, because nothing can serialize a
// world. That is E3, and a button that silently discarded a person's work would
// be worse than no button.
enum class RunState
{
    Paused,
    Playing,
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
    void setRunState(RunState state) noexcept { m_run = state; }

    // Ask for exactly one tick while paused. A step is how somebody watches a
    // thing happen instead of inferring it from before and after, and it is the
    // one control a paused editor cannot do without.
    void requestStep() noexcept { m_stepRequested = true; }

    // How many of the frame's owed ticks the world may actually take.
    //
    // Playing: all of them, unchanged -- the editor is not a second scheduler
    // and must not become one. Paused: none, unless a step was asked for, and
    // then exactly one however many the frame owed. Consuming the request here
    // rather than at the button is what makes a step one tick rather than one
    // tick per frame the button stays held.
    [[nodiscard]] core::u32 allowedTicks(core::u32 owed) noexcept
    {
        if (m_run == RunState::Playing)
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
    RunState m_run = RunState::Paused;
    bool m_stepRequested = false;
};

} // namespace luaug::app
