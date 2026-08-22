#include <luaug/app/editor.h>
#include <luaug/rhi/device.h>
#include <luaug/scene/world.h>

namespace luaug::app {
namespace {
// The same format the headless path renders into. An editor's viewport is the
// headless render with somebody watching, so a second format here would be a
// second thing to keep in step for no gain.
constexpr rhi::TextureFormat kViewportFormat = rhi::TextureFormat::Rgba8Unorm;

// A panel dragged to nothing is a normal thing for a person to do, and a
// zero-sized texture is not a thing a device will make.
constexpr core::u32 kMinimumViewport = 1;
} // namespace

ViewportTarget::~ViewportTarget()
{
    destroy();
}

bool ViewportTarget::resize(rhi::IDevice& device, core::u32 width, core::u32 height)
{
    const core::u32 wantWidth = width > kMinimumViewport ? width : kMinimumViewport;
    const core::u32 wantHeight = height > kMinimumViewport ? height : kMinimumViewport;

    if (m_texture.valid() && m_width == wantWidth && m_height == wantHeight)
        return true;

    // The texture being replaced may still be in flight from the frame that was
    // just submitted, so the old one cannot simply be released. See the header
    // for why the stall is the right trade here and nowhere else.
    if (m_texture.valid()) {
        device.waitIdle();
        device.destroy(m_texture);
        m_texture = {};
    }

    m_device = &device;
    m_texture = device.createTexture({
        .format = kViewportFormat,
        // Sampled as well as drawn into: ImGui shows it, which means reading it
        // in a shader.
        .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled,
        .width = wantWidth,
        .height = wantHeight,
        .debugName = "editor-viewport",
    });

    if (!m_texture.valid()) {
        m_width = 0;
        m_height = 0;
        return false;
    }

    m_width = wantWidth;
    m_height = wantHeight;
    return true;
}

void ViewportTarget::destroy()
{
    if (m_device != nullptr && m_texture.valid()) {
        m_device->waitIdle();
        m_device->destroy(m_texture);
    }
    m_texture = {};
    m_width = 0;
    m_height = 0;
    m_device = nullptr;
}

void Editor::setCamera(const core::Mat4& projection, const core::Mat4& view, core::DVec3 origin) noexcept
{
    m_projection = projection;
    m_view = view;
    m_cameraOrigin = origin;
    m_hasCamera = true;
}

PickRay Editor::rayThrough(core::Vec2 pixelInViewport) const noexcept
{
    // The viewport's own space, not the window's: the offscreen target IS the
    // viewport, so its rectangle starts at its own origin. Carrying the panel's
    // window offset in here as well would be two corrections for one
    // displacement, and the second one is always the wrong sign.
    const ViewportRect local{0.0f, 0.0f, m_viewport.width, m_viewport.height};
    return rayThroughPixel(m_projection, m_view, m_cameraOrigin, local, pixelInViewport);
}

void Editor::adoptCamera(const core::CFrameD& cframe) noexcept
{
    m_cameraCFrame = cframe;
    // The angles come out of the matrix ONCE, here. Deriving them every frame
    // would round-trip a rotation through Euler angles sixty times a second,
    // and that is how a fly camera acquires a slow roll nobody can explain.
    const core::Vec3 angles = core::toEulerYxz(cframe.rotation);
    m_yaw = angles.y;
    m_pitch = angles.x;
    m_cameraAdopted = true;
}

void Editor::setCameraSpeed(f32 metresPerSecond) noexcept
{
    // A speed of zero is a camera that cannot move and a negative one flies
    // backwards from every key, both of which read as broken rather than as
    // configured.
    constexpr f32 kSlowest = 0.1f;
    constexpr f32 kFastest = 2000.0f;
    m_cameraSpeed = metresPerSecond < kSlowest ? kSlowest : (metresPerSecond > kFastest ? kFastest : metresPerSecond);
}

core::CFrameD Editor::driveCamera(core::Vec2 lookDelta, core::Vec3 move, f32 dt) noexcept
{
    // Playing means the game owns its camera again. Writing here would be two
    // authors for one transform, and the visible result is a camera that
    // stutters between where the script wants it and where the editor left it.
    if (m_run == RunState::Playing || !m_cameraAdopted)
        return m_cameraCFrame;

    constexpr f32 kRadiansPerPixel = 0.0032f;
    // Just short of straight up and straight down. AT the pole the yaw axis and
    // the look direction are the same line and the camera spins on its own.
    constexpr f32 kPitchLimit = 1.5533f;

    m_yaw -= lookDelta.x * kRadiansPerPixel;
    m_pitch -= lookDelta.y * kRadiansPerPixel;
    m_pitch = m_pitch > kPitchLimit ? kPitchLimit : (m_pitch < -kPitchLimit ? -kPitchLimit : m_pitch);

    m_cameraCFrame.rotation = core::fromEulerYxz(core::Vec3{m_pitch, m_yaw, 0.0f});

    // `Mat3`'s columns are right, up and BACK, so forward is the negated third
    // -- which `math.h` says at the type and which is the one thing to get
    // wrong here.
    const core::Mat3& basis = m_cameraCFrame.rotation;
    const core::Vec3 right{basis.m[0][0], basis.m[0][1], basis.m[0][2]};
    const core::Vec3 up{basis.m[1][0], basis.m[1][1], basis.m[1][2]};
    const core::Vec3 forward{-basis.m[2][0], -basis.m[2][1], -basis.m[2][2]};

    const core::Vec3 step = (right * move.x + up * move.y + forward * move.z) * (m_cameraSpeed * dt);
    // Accumulated in f64. The editor's camera is the one thing in an open world
    // that a person drives for minutes at a time, and adding centimetre steps
    // to a four-kilometre f32 position is exactly the drift ADR 0014 exists to
    // prevent.
    m_cameraCFrame.position = m_cameraCFrame.position + core::toDVec3(step);
    return m_cameraCFrame;
}

std::optional<PickHit> Editor::resolvePick(const scene::World& world, Inspector& inspector) noexcept
{
    if (!m_pending.has_value())
        return std::nullopt;

    const PickRequest request = *m_pending;
    m_pending.reset();

    // No camera means nothing has been rendered yet, so there is no image the
    // click could have been aimed at. Clearing the selection would be a guess;
    // doing nothing is not.
    if (!m_hasCamera)
        return std::nullopt;

    const std::optional<PickHit> hit = pickNearest(world, rayThrough(request.pixel));

    // Clicking empty space deselects. See the header: leaving the last thing
    // selected is how somebody edits the object they believed they had let go
    // of.
    inspector.select(hit.has_value() ? hit->instance : core::InstanceId{});
    return hit;
}

} // namespace luaug::app
