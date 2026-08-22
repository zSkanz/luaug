#include <luaug/app/editor.h>
#include <luaug/core/json.h>
#include <luaug/core/json_writer.h>
#include <luaug/platform/file.h>
#include <luaug/rhi/device.h>
#include <luaug/scene/scene_file.h>
#include <luaug/scene/world.h>

#include <string>

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

void Editor::play(scene::World& world)
{
    // Already in play mode -- running or paused. Taking a second snapshot here
    // would move the point stop returns to into the middle of a play session.
    if (m_run != RunState::Editing)
        return;

    // Taken every time play is pressed rather than kept from the first, because
    // stop means "back to where I pressed play", not "back to where I opened
    // the editor".
    m_playSnapshot = std::make_unique<scene::WorldSnapshot>(world.snapshot());
    m_run = RunState::Playing;
    m_status = EditorStatus{"playing", false};
}

void Editor::setPaused(bool paused) noexcept
{
    // Pause is a thing that happens INSIDE play mode. Asking for it while
    // editing is asking for a state that does not exist, and silently entering
    // play mode to provide it would be worse than doing nothing.
    if (m_run == RunState::Editing)
        return;
    m_run = paused ? RunState::Paused : RunState::Playing;
}

void Editor::stop(scene::World& world, Inspector& inspector)
{
    m_run = RunState::Editing;
    if (m_playSnapshot == nullptr)
        return;

    world.restore(*m_playSnapshot);
    m_playSnapshot.reset();

    // A selection made DURING play can name something the restore removed. The
    // id would resolve to whatever the slot holds now, which is either nothing
    // or somebody else -- and a properties grid pointed at somebody else is how
    // an edit lands on the wrong object.
    if (!world.alive(inspector.selection()))
        inspector.select(core::InstanceId{});
    inspector.onWorldChanged();

    m_status = EditorStatus{"stopped -- the world is back where you pressed play", false};
}

bool Editor::save(const scene::World& world, const std::filesystem::path& path)
{
    scene::SceneIoReport report;
    const std::string text = scene::writeScene(world, &report);

    if (!platform::createDirectories(path.parent_path()) || !platform::writeTextFile(path, text)) {
        m_status = EditorStatus{"could not write " + path.string(), true};
        return false;
    }

    std::string message = "saved " + std::to_string(report.instances) + " instance(s) to " + path.string();
    // Counted rather than swallowed. A reference that pointed outside the scene
    // is a thing the person authored and the file cannot hold, and finding that
    // out when you reopen is finding it out too late.
    if (report.droppedReferences > 0)
        message += " (" + std::to_string(report.droppedReferences) + " reference(s) outside the scene were dropped)";
    m_status = EditorStatus{message, false};
    return true;
}

bool Editor::load(scene::World& world, const std::filesystem::path& path, Inspector& inspector)
{
    std::string text;
    if (!platform::readTextFile(path, text)) {
        m_status = EditorStatus{"could not read " + path.string(), true};
        return false;
    }

    scene::SceneIoReport report;
    if (const std::optional<core::EngineError> error = scene::readScene(world, text, &report); error.has_value()) {
        m_status = EditorStatus{error->message, true};
        return false;
    }

    // Everything the file named was created just now, so nothing selected
    // before it still means what it meant.
    inspector.select(core::InstanceId{});
    inspector.onWorldChanged();

    std::string message = "loaded " + std::to_string(report.instances) + " instance(s) from " + path.string();
    if (report.unknownClasses > 0)
        message += " (" + std::to_string(report.unknownClasses) + " unknown class(es) skipped)";
    m_status = EditorStatus{message, false};
    return true;
}

void Editor::rememberOpenScene(const std::filesystem::path& stateDirectory) const
{
    // JSON of one field rather than the bare path, because the second thing an
    // editor wants to remember arrives sooner than anybody expects and a file
    // that is only a string has nowhere to put it.
    core::JsonWriter writer;
    writer.beginObject();
    writer.field("openScene", m_openScene);
    writer.endObject();

    (void)platform::createDirectories(stateDirectory);
    (void)platform::writeTextFile(stateDirectory / "editor.json", writer.text());
}

std::string Editor::recallOpenScene(const std::filesystem::path& stateDirectory)
{
    std::string text;
    if (!platform::readTextFile(stateDirectory / "editor.json", text))
        return {};

    core::JsonDocument document;
    if (const core::JsonDocument::ParseResult parsed = document.parse(text); !parsed.ok)
        return {};

    return std::string(document.root()["openScene"].asString());
}

bool Editor::saveSceneAs(const scene::World& world, std::string_view relativePath)
{
    std::string path(relativePath);
    if (path.size() < kSceneExtension.size() ||
        path.compare(path.size() - kSceneExtension.size(), kSceneExtension.size(), kSceneExtension) != 0) {
        path += kSceneExtension;
    }

    if (!save(world, m_content.root() / std::filesystem::path(path)))
        return false;

    m_openScene = path;
    // The browser is showing the folder this was written into, and it does not
    // know a file appeared in it.
    (void)m_content.refresh();
    return true;
}

void Editor::adoptOpenScene(std::string_view relativePath)
{
    // The boot load already put a scene in the world; this is the editor being
    // told WHICH, so the first save writes back to it rather than refusing for
    // want of an open scene. Naming it here rather than re-loading it is the
    // difference between the editor knowing what it has and the editor
    // discarding a world to find out.
    m_openScene = std::string(relativePath);
}

void Editor::openContent(const std::filesystem::path& contentRoot)
{
    // The return value is deliberately ignored. A project with no `content/`
    // is every example before `06-scene`, and a browser that greeted those with
    // an error would be wrong about all of them.
    (void)m_content.open(contentRoot);
}

bool Editor::openScene(scene::World& world, std::string_view relativePath, Inspector& inspector)
{
    const std::filesystem::path absolute = m_content.root() / std::filesystem::path(relativePath);
    if (!load(world, absolute, inspector))
        return false;

    m_openScene = std::string(relativePath);
    // The status `load` set names the file; naming the scene is more useful,
    // because the browser is already showing the file.
    m_status = EditorStatus{"opened " + m_openScene, false};
    return true;
}

bool Editor::saveOpenScene(const scene::World& world)
{
    if (m_openScene.empty()) {
        m_status = EditorStatus{"no scene is open -- open one from the content browser first", true};
        return false;
    }
    return save(world, m_content.root() / std::filesystem::path(m_openScene));
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
    // Only while editing. Inside play mode the game owns its camera, paused or
    // not: a person who paused to look at something did not ask for the tool's
    // view.
    if (!editing(m_run) || !m_cameraAdopted)
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
