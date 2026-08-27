#include <luaug/app/brush_overlay.h>
#include <luaug/app/editor.h>
#include <luaug/core/json.h>
#include <luaug/core/json_writer.h>
#include <luaug/platform/file.h>
#include <luaug/render/debug_draw.h>
#include <luaug/rhi/device.h>
#include <luaug/scene/class_registry.h>
#include <luaug/scene/pivot.h>
#include <luaug/scene/scene_file.h>
#include <luaug/scene/world.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace luaug::app {
using core::Vec3;
namespace {
// The seed a stage's world is built with. A constant, because nothing in a
// stage is simulated and nothing in it reads the generator -- and a seed drawn
// from anywhere else would make a stamp's bytes depend on when it was opened.
constexpr core::u64 kStageSeed = 0x5741'4D50u;

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
    // just submitted, so it is put aside rather than freed. See the header: the
    // alternative was a full device stall on every frame of a splitter drag.
    if (m_texture.valid()) {
        m_retired.push_back(Retired{m_texture, RetirementFrames});
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

void ViewportTarget::retire(rhi::IDevice& device)
{
    // Walked backwards so an erase cannot move an entry past the cursor. The
    // list is at most three long, so this is not where the frame goes.
    for (core::usize index = m_retired.size(); index > 0; --index) {
        Retired& entry = m_retired[index - 1];
        if (entry.framesLeft > 0) {
            --entry.framesLeft;
            continue;
        }
        if (entry.texture.valid())
            device.destroy(entry.texture);
        m_retired.erase(m_retired.begin() + static_cast<std::ptrdiff_t>(index - 1));
    }
}

void ViewportTarget::destroy()
{
    // **The one place the stall belongs.** Shutting down is not a frame, there
    // is no next one for a retirement queue to be drained by, and every handle
    // here has to be gone before the device is -- so this waits for the GPU to
    // finish with all of them and frees them together.
    if (m_device != nullptr && (m_texture.valid() || !m_retired.empty())) {
        m_device->waitIdle();
        if (m_texture.valid())
            m_device->destroy(m_texture);
        for (const Retired& entry : m_retired) {
            if (entry.texture.valid())
                m_device->destroy(entry.texture);
        }
    }
    m_retired.clear();
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
    // **Attached, every time play is pressed** (S5.8). Detaching is a thing
    // somebody does DURING a run to look at something; carrying it into the next
    // one would mean pressing play and finding the view somewhere they left it a
    // session ago, with nothing on screen saying why.
    m_cameraDetached = false;
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
    m_cameraDetached = false;
    if (m_playSnapshot == nullptr)
        return;

    world.restore(*m_playSnapshot);
    m_playSnapshot.reset();
    // A play session's changes were never edits, and the edits before it belong
    // to a world this restore has just replaced.
    m_history.clear();

    // A selection made DURING play can name something the restore removed. The
    // id would resolve to whatever the slot holds now, which is either nothing
    // or somebody else -- and a properties grid pointed at somebody else is how
    // an edit lands on the wrong object.
    inspector.pruneDead(world);
    inspector.onWorldRestored();

    m_status = EditorStatus{"stopped -- the world is back where you pressed play", false};
}

bool Editor::save(scene::World& world, const std::filesystem::path& path)
{
    scene::SceneIoReport report;
    // **The stamps this scene names, read once each**, so a stamped instance
    // is written as a mark plus what differs rather than as a copy of the
    // subtree (ADR 0051). Without this every one of them would be written in
    // full and unlinked -- which loses nothing and is exactly what a save with
    // no content root does.
    scene::StampLibrary stamps(world, stampSource());
    const std::string text = scene::writeScene(world, &report, &stamps);

    if (!platform::createDirectories(path.parent_path()) || !platform::writeTextFile(path, text)) {
        m_status = EditorStatus{"could not write " + path.string(), true};
        return false;
    }

    // **Here rather than at each caller**, because every scene write goes
    // through this one function -- Save, Save As and the save half of a
    // confirmation all land here, and a flag cleared at three call sites is a
    // flag one of them will forget.
    m_sceneDirty = false;

    std::string message = "saved " + std::to_string(report.instances) + " instance(s) to " + path.string();
    // Counted rather than swallowed. A reference that pointed outside the scene
    // is a thing the person authored and the file cannot hold, and finding that
    // out when you reopen is finding it out too late.
    if (report.droppedReferences > 0)
        message += " (" + std::to_string(report.droppedReferences) + " reference(s) outside the scene were dropped)";
    // **Said out loud, because the alternative is finding out days later.** A
    // stamped instance whose contents no longer match its file is written in
    // full and unlinked -- the scene keeps everything, but the instance stops
    // following the stamp, and nothing about looking at it says so. Adding a
    // child to one is the ordinary way to arrive here.
    if (report.unlinkedStamps > 0) {
        message += " -- " + std::to_string(report.unlinkedStamps) +
                   " stamped instance(s) no longer match their stamp and were unlinked; add or remove anything "
                   "inside one and it stops being an instance of the file";
    }
    m_status = EditorStatus{message, report.unlinkedStamps > 0};
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
    // **The stamps the scene names, read through this editor's content root.**
    // `scene` is L3 and has no filesystem; a scene loaded without this opens
    // with its stamped instances missing and a count saying so.
    if (const std::optional<core::EngineError> error = scene::readScene(world, text, &report, stampSource());
        error.has_value()) {
        m_status = EditorStatus{error->message, true};
        return false;
    }

    // **The scene this replaced has to be RETIRED, not only destroyed.**
    // `readScene` clears the world with `destroy`, which unlinks and marks --
    // and the record stops resolving in `retireDestroyed`, which runs at the end
    // of a signal drain. A paused world runs no drains, so without this every
    // instance of the previous scene stays in the component pools for ever:
    // unparented, drawn by nothing, and accumulating one whole scene per load.
    //
    // The same argument `deleteInstance` makes, and the same place to make it:
    // nothing else will while the editor is editing.
    world.retireDestroyed();

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

namespace {
// Ten, spread round the hue circle rather than picked by eye, so that two
// folders coloured a minute apart are actually distinguishable -- which is the
// entire job. Values rather than a generator, because a palette is a decision
// and a decision should be readable.
constexpr core::Color3 kFolderPalette[] = {
    core::Color3{0.85f, 0.33f, 0.31f}, // red
    core::Color3{0.88f, 0.55f, 0.24f}, // orange
    core::Color3{0.87f, 0.76f, 0.28f}, // yellow
    core::Color3{0.53f, 0.76f, 0.35f}, // green
    core::Color3{0.29f, 0.71f, 0.60f}, // teal
    core::Color3{0.30f, 0.62f, 0.85f}, // blue
    core::Color3{0.44f, 0.47f, 0.83f}, // indigo
    core::Color3{0.65f, 0.44f, 0.82f}, // violet
    core::Color3{0.85f, 0.45f, 0.66f}, // pink
    core::Color3{0.60f, 0.62f, 0.66f}, // slate
};

// `#rrggbb`, which is what a person editing `editor.json` by hand expects to
// see. Written from the same 8-bit rounding both panels draw with, so a colour
// that survives the file is the colour that was chosen.
[[nodiscard]] std::string writeHexColor(core::Color3 color)
{
    const auto channel = [](core::f32 value) {
        return static_cast<int>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    };
    char buffer[8]{};
    (void)std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x", channel(color.r), channel(color.g), channel(color.b));
    return std::string(buffer);
}

[[nodiscard]] std::optional<core::Color3> parseHexColor(std::string_view text)
{
    if (text.size() != 7 || text[0] != '#')
        return std::nullopt;
    core::u32 packed = 0;
    for (std::size_t index = 1; index < text.size(); ++index) {
        const char c = text[index];
        const core::u32 digit = c >= '0' && c <= '9'   ? static_cast<core::u32>(c - '0')
                                : c >= 'a' && c <= 'f' ? static_cast<core::u32>(c - 'a' + 10)
                                : c >= 'A' && c <= 'F' ? static_cast<core::u32>(c - 'A' + 10)
                                                       : 16u;
        if (digit > 15u)
            return std::nullopt;
        packed = (packed << 4) | digit;
    }
    return core::Color3{static_cast<core::f32>((packed >> 16) & 0xFFu) / 255.0f,
                        static_cast<core::f32>((packed >> 8) & 0xFFu) / 255.0f,
                        static_cast<core::f32>(packed & 0xFFu) / 255.0f};
}
} // namespace

std::span<const core::Color3> Editor::folderPalette() noexcept
{
    return std::span<const core::Color3>(kFolderPalette, std::size(kFolderPalette));
}

std::optional<core::Color3> Editor::folderColor(const scene::World& world, core::InstanceId id)
{
    if (!world.alive(id))
        return std::nullopt;
    const core::NameAtom atom = world.atoms().lookup(FolderColorAttribute);
    if (!atom.valid())
        return std::nullopt;
    // `getAttribute` answers with a value rather than an optional: an absent
    // attribute is `monostate`, which `get_if` reports as "not a colour".
    const scene::Value value = world.getAttribute(id, atom);
    if (const core::Color3* color = std::get_if<core::Color3>(&value); color != nullptr)
        return *color;
    return std::nullopt;
}

void Editor::setFolderColor(scene::World& world, core::InstanceId id, std::optional<core::Color3> color)
{
    if (!world.alive(id))
        return;

    m_history.record(world, color.has_value() ? "Colour" : "Clear Colour");
    const core::NameAtom atom = world.atoms().intern(FolderColorAttribute);
    // A `Nil` value removes it, which the world's own setter documents -- so
    // clearing a colour is the same call as setting one and there is no second
    // path to keep in step.
    (void)world.setAttribute(id, atom, color.has_value() ? scene::Value{*color} : scene::Value{});
}

std::optional<core::Color3> Editor::contentColor(std::string_view path) const
{
    const auto found = m_contentColors.find(std::string(path));
    return found != m_contentColors.end() ? std::optional<core::Color3>(found->second) : std::nullopt;
}

void Editor::setContentColor(std::string_view path, std::optional<core::Color3> color)
{
    if (color.has_value())
        m_contentColors[std::string(path)] = *color;
    else
        m_contentColors.erase(std::string(path));
}

namespace {
// The manipulator's mode and the browser's layout, as WORDS.
//
// `.luaug/editor.json` is a file a person opens when something is wrong with
// it, and `"gizmo": 2` tells them nothing while `"gizmo": "scale"` tells them
// everything. An unknown word falls back rather than failing: a project written
// by a newer build should still open here, minus what this one cannot say --
// which is the rule the scene format already follows.
// The tool's name in the preferences file, and back. Unknown reads as `Select`,
// which is the tool that cannot lose work: a file written by a newer editor
// naming a tool this one does not have should open in the safe one.
[[nodiscard]] std::string_view brushOpName(Editor::BrushOp op) noexcept
{
    switch (op) {
    case Editor::BrushOp::Subtract:
        return "subtract";
    case Editor::BrushOp::Smooth:
        return "smooth";
    case Editor::BrushOp::Flatten:
        return "flatten";
    case Editor::BrushOp::Add:
        break;
    }
    return "add";
}

[[nodiscard]] Editor::BrushOp brushOpFrom(std::string_view name) noexcept
{
    if (name == "subtract")
        return Editor::BrushOp::Subtract;
    if (name == "smooth")
        return Editor::BrushOp::Smooth;
    if (name == "flatten")
        return Editor::BrushOp::Flatten;
    return Editor::BrushOp::Add;
}

[[nodiscard]] std::string_view toolName(Editor::Tool tool) noexcept
{
    switch (tool) {
    case Editor::Tool::Sculpt:
        return "sculpt";
    case Editor::Tool::Paint:
        return "paint";
    case Editor::Tool::Select:
        break;
    }
    return "select";
}

// What one stroke is called in the undo menu. A person reading "Undo Smooth"
// knows what is about to come back; "Undo Sculpt" for four different tools does
// not.
[[nodiscard]] const char* strokeLabel(Editor::Tool tool, Editor::BrushOp op) noexcept
{
    if (tool == Editor::Tool::Paint) {
        return "Paint Terrain";
    }
    switch (op) {
    case Editor::BrushOp::Subtract:
        return "Dig";
    case Editor::BrushOp::Smooth:
        return "Smooth";
    case Editor::BrushOp::Flatten:
        return "Flatten";
    case Editor::BrushOp::Add:
        break;
    }
    return "Sculpt";
}

[[nodiscard]] Editor::Tool toolFrom(std::string_view name) noexcept
{
    if (name == "sculpt")
        return Editor::Tool::Sculpt;
    if (name == "paint")
        return Editor::Tool::Paint;
    return Editor::Tool::Select;
}

[[nodiscard]] std::string_view gizmoModeName(GizmoMode mode) noexcept
{
    switch (mode) {
    case GizmoMode::Rotate:
        return "rotate";
    case GizmoMode::Scale:
        return "scale";
    case GizmoMode::Translate:
        break;
    }
    return "translate";
}

[[nodiscard]] GizmoMode gizmoModeFrom(std::string_view name) noexcept
{
    if (name == "rotate")
        return GizmoMode::Rotate;
    if (name == "scale")
        return GizmoMode::Scale;
    return GizmoMode::Translate;
}

[[nodiscard]] std::string_view contentViewName(EditorPanels::ContentView view) noexcept
{
    switch (view) {
    case EditorPanels::ContentView::Tiles:
        return "tiles";
    case EditorPanels::ContentView::Icons:
        return "icons";
    case EditorPanels::ContentView::List:
        break;
    }
    return "list";
}

[[nodiscard]] EditorPanels::ContentView contentViewFrom(std::string_view name) noexcept
{
    if (name == "tiles")
        return EditorPanels::ContentView::Tiles;
    if (name == "icons")
        return EditorPanels::ContentView::Icons;
    return EditorPanels::ContentView::List;
}
// The window block, or nothing. Shared by `recallState` and the static reader,
// because the window is asked about twice -- once before there is an editor and
// once after -- and two spellings of "what does this file say" is how the two
// answers end up different.
[[nodiscard]] std::optional<platform::WindowPlacement> readWindow(const core::JsonValue& root)
{
    const core::JsonValue window = root["window"];
    if (window.type() != core::JsonType::Object)
        return std::nullopt;

    platform::WindowPlacement placement;
    placement.x = static_cast<core::i32>(window["x"].asInteger());
    placement.y = static_cast<core::i32>(window["y"].asInteger());
    placement.width = static_cast<core::i32>(window["width"].asInteger());
    placement.height = static_cast<core::i32>(window["height"].asInteger());
    placement.maximized = window["maximized"].asBool();
    // A size of nothing is not a window somebody left; it is a truncated file.
    if (placement.width <= 0 || placement.height <= 0)
        return std::nullopt;
    return placement;
}
} // namespace

void Editor::rememberState(const std::filesystem::path& stateDirectory) const
{
    // JSON of one field rather than the bare path, because the second thing an
    // editor wants to remember arrives sooner than anybody expects and a file
    // that is only a string has nowhere to put it. It arrived: folder colours.
    core::JsonWriter writer;
    writer.beginObject();
    writer.field("openScene", m_openScene);
    // Which default arrangement this project has already been shown. See
    // `Editor::CurrentLayoutRevision`.
    writer.field("layoutRevision", static_cast<core::f64>(m_layoutRevision));

    // **What a person set, rather than what they did.** The manipulator's mode
    // and space, snapping and its steps, and how the browser lays entries out:
    // none of it is about the world, all of it is somebody's answer to "how do I
    // work", and an editor that asks the question again every launch is one they
    // answer by hand every morning. The docking, the panel sizes and which tab
    // is open are ImGui's `.luaug/layout.ini` -- two files because two writers,
    // not because two ideas.
    writer.key("tools");
    writer.beginObject();
    writer.field("gizmo", gizmoModeName(m_gizmoMode));
    // The word rather than a flag, for the same reason: `"space": false` names
    // nothing, and `local` and `world` are what the toolbar itself says.
    writer.field("space", m_gizmoLocal ? "local" : "world");
    // The same shape and the same argument: `"origin": true` names nothing, and
    // `pivot` and `centre` are what the toolbar itself says.
    writer.field("origin", m_gizmoOrigin == GizmoOrigin::Centre ? "centre" : "pivot");
    writer.field("snap", m_snap);
    writer.key("snapSteps");
    writer.beginArray();
    for (const f32 step : m_snapStep)
        writer.value(static_cast<core::f64>(step));
    writer.endArray();
    // The word again, for the third time and the same argument: `"tool": 1`
    // names nothing and `sculpt` is what the toolbar itself says.
    writer.field("tool", toolName(m_tool));
    writer.key("brush");
    writer.beginObject();
    writer.field("radius", static_cast<core::f64>(m_brush.radius));
    writer.field("spacing", static_cast<core::f64>(m_brush.spacing));
    writer.field("strength", static_cast<core::f64>(m_brush.strength));
    writer.field("material", static_cast<core::f64>(m_brush.material));
    writer.field("op", brushOpName(m_brush.op));
    writer.field("shape", m_brush.shape == BrushShape::Box ? "box" : "sphere");
    writer.endObject();
    writer.endObject();

    writer.key("panels");
    writer.beginObject();
    writer.field("contentView", contentViewName(m_contentView));
    writer.endObject();

    // **The OS window, which ImGui's layout file cannot hold**: that one knows
    // about the panels INSIDE a window and nothing about the window. Two halves
    // of one expectation -- "it opens the way I left it" -- and a person cannot
    // tell which file answers which half.
    if (m_window.has_value()) {
        writer.key("window");
        writer.beginObject();
        writer.field("x", static_cast<core::i64>(m_window->x));
        writer.field("y", static_cast<core::i64>(m_window->y));
        writer.field("width", static_cast<core::i64>(m_window->width));
        writer.field("height", static_cast<core::i64>(m_window->height));
        writer.field("maximized", m_window->maximized);
        writer.endObject();
    }

    if (!m_contentColors.empty()) {
        writer.key("folderColors");
        writer.beginObject();
        // `std::map`, so this is the same bytes for the same state without
        // sorting here -- the property every other format in this repository
        // has and the reason the container is ordered.
        for (const auto& entry : m_contentColors)
            writer.field(entry.first, writeHexColor(entry.second));
        writer.endObject();
    }
    writer.endObject();

    (void)platform::createDirectories(stateDirectory);
    (void)platform::writeTextFile(stateDirectory / "editor.json", writer.text());
}

void Editor::recallState(const std::filesystem::path& stateDirectory)
{
    m_contentColors.clear();

    std::string text;
    if (!platform::readTextFile(stateDirectory / "editor.json", text))
        return;

    core::JsonDocument document;
    if (const core::JsonDocument::ParseResult parsed = document.parse(text); !parsed.ok)
        return;

    const core::JsonValue root = document.root();

    // Every block is optional and each is read on its own. A file written before
    // one of these existed is not a broken file -- it is a file from last week,
    // and the missing block means "whatever this build's default is".
    //
    // Absent means zero here, and zero is the answer that matters: a project
    // arranged before this field existed is exactly the one whose default tab
    // was never applied.
    if (const core::JsonValue revision = root["layoutRevision"]; revision.type() == core::JsonType::Number)
        m_layoutRevision = static_cast<core::i64>(revision.asNumber());

    if (const core::JsonValue tools = root["tools"]; tools.type() == core::JsonType::Object) {
        m_gizmoMode = gizmoModeFrom(tools["gizmo"].asString());
        m_gizmoLocal = tools["space"].asString() == "local";
        m_gizmoOrigin = tools["origin"].asString() == "centre" ? GizmoOrigin::Centre : GizmoOrigin::Pivot;
        if (const core::JsonValue snap = tools["snap"]; snap.type() == core::JsonType::Boolean)
            m_snap = snap.asBool();
        if (const core::JsonValue steps = tools["snapSteps"]; steps.type() == core::JsonType::Array) {
            for (core::usize index = 0; index < steps.size() && index < std::size(m_snapStep); ++index) {
                // Through the setter's rule rather than around it: a hand-edited
                // zero or a negative is a snap that divides by nothing.
                const auto step = static_cast<f32>(steps.at(index).asNumber());
                m_snapStep[index] = step > 0.0f ? step : m_snapStep[index];
            }
        }
        m_tool = toolFrom(tools["tool"].asString());
        if (const core::JsonValue brush = tools["brush"]; brush.type() == core::JsonType::Object) {
            // **Through the setters, so a hand-edited file cannot make a brush
            // the UI could not have made.** A radius of zero stamps nothing and
            // a material of zero would erase the world on the first click.
            if (const core::JsonValue radius = brush["radius"]; radius.type() == core::JsonType::Number)
                setBrushRadius(static_cast<f32>(radius.asNumber()));
            if (const core::JsonValue spacing = brush["spacing"]; spacing.type() == core::JsonType::Number)
                setBrushSpacing(static_cast<f32>(spacing.asNumber()));
            if (const core::JsonValue material = brush["material"]; material.type() == core::JsonType::Number)
                setBrushMaterial(static_cast<core::u8>(std::clamp(material.asNumber(), 0.0, 255.0)));
            if (const core::JsonValue strength = brush["strength"]; strength.type() == core::JsonType::Number)
                setBrushStrength(static_cast<f32>(strength.asNumber()));
            m_brush.op = brushOpFrom(brush["op"].asString());
            m_brush.shape = brush["shape"].asString() == "box" ? BrushShape::Box : BrushShape::Sphere;
        }
    }

    if (const core::JsonValue panels = root["panels"]; panels.type() == core::JsonType::Object)
        m_contentView = contentViewFrom(panels["contentView"].asString());

    m_window = readWindow(root);

    // Read last and not returned from early: a file with no colours still has
    // everything above it.
    if (const core::JsonValue colors = root["folderColors"]; colors.type() == core::JsonType::Object) {
        for (core::usize index = 0; index < colors.size(); ++index) {
            const std::string_view path = colors.keyAt(index);
            if (const std::optional<core::Color3> color = parseHexColor(colors[path].asString()); color.has_value())
                m_contentColors.emplace(std::string(path), *color);
        }
    }

    // Recalling is not a change somebody made.
    m_preferencesDirty = false;
}

std::optional<platform::WindowPlacement> Editor::recallWindow(const std::filesystem::path& stateDirectory)
{
    std::string text;
    if (!platform::readTextFile(stateDirectory / "editor.json", text))
        return std::nullopt;

    core::JsonDocument document;
    if (const core::JsonDocument::ParseResult parsed = document.parse(text); !parsed.ok)
        return std::nullopt;

    return readWindow(document.root());
}

void Editor::rememberWindow(const platform::WindowPlacement& placement) noexcept
{
    // **A maximised window's geometry is not the geometry to keep.** SDL reports
    // the screen it fills, so storing that and restoring it would give somebody
    // who un-maximises a window the size of their display with a title bar. The
    // geometry stays whatever it was when the window was last normal, and the
    // flag carries the rest.
    if (placement.maximized) {
        if (m_window.has_value() && m_window->maximized)
            return;
        platform::WindowPlacement kept = m_window.value_or(placement);
        kept.maximized = true;
        m_window = kept;
        m_preferencesDirty = true;
        return;
    }

    if (m_window.has_value() && m_window->x == placement.x && m_window->y == placement.y &&
        m_window->width == placement.width && m_window->height == placement.height && !m_window->maximized) {
        return;
    }
    m_window = placement;
    m_preferencesDirty = true;
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

void UndoStack::record(const scene::World& world, std::string label, core::u64 coalesceKey)
{
    // Consecutive work on the same thing is one step. A drag on a colour writes
    // a value every frame, and without this a two-second drag buries everything
    // before it under a hundred and twenty steps of the same colour.
    if (coalesceKey != 0 && !m_undo.empty() && m_undo.back().key == coalesceKey)
        return;

    // Anything ahead of here is a future that no longer happens. Keeping it
    // would let a redo after a new edit apply a change to a world that has
    // moved on -- which is the one way an undo stack can destroy work rather
    // than restore it.
    m_redo.clear();

    m_undo.push_back(Step{world.snapshot(), std::move(label), coalesceKey});
    while (m_undo.size() > Depth)
        m_undo.pop_front();
}

bool UndoStack::undo(scene::World& world)
{
    if (m_undo.empty())
        return false;

    // The world as it is now becomes the redo, carrying the label of the step
    // being undone -- so "Redo Delete" names the thing it will do again rather
    // than the thing before it.
    Step step = std::move(m_undo.back());
    m_undo.pop_back();
    m_redo.push_back(Step{world.snapshot(), step.label, 0});
    world.restore(step.state);
    return true;
}

bool UndoStack::redo(scene::World& world)
{
    if (m_redo.empty())
        return false;

    Step step = std::move(m_redo.back());
    m_redo.pop_back();
    m_undo.push_back(Step{world.snapshot(), step.label, 0});
    world.restore(step.state);
    return true;
}

std::string_view UndoStack::undoLabel() const noexcept
{
    return m_undo.empty() ? std::string_view{} : std::string_view{m_undo.back().label};
}

std::string_view UndoStack::redoLabel() const noexcept
{
    return m_redo.empty() ? std::string_view{} : std::string_view{m_redo.back().label};
}

void UndoStack::clear() noexcept
{
    m_undo.clear();
    m_redo.clear();
}

bool Editor::undo(scene::World& world, Inspector& inspector)
{
    const std::string label(m_history.undoLabel());
    if (!m_history.undo(world))
        return false;

    inspector.pruneDead(world);
    inspector.onWorldRestored();

    m_status = EditorStatus{"undid " + label, false};
    return true;
}

bool Editor::redo(scene::World& world, Inspector& inspector)
{
    const std::string label(m_history.redoLabel());
    if (!m_history.redo(world))
        return false;

    inspector.pruneDead(world);
    inspector.onWorldRestored();

    m_status = EditorStatus{"redid " + label, false};
    return true;
}

bool Editor::isEngineOwned(const scene::World& world, core::InstanceId id, core::InstanceId root) noexcept
{
    if (!world.alive(id))
        return false;
    if (id == root)
        return true;

    const scene::ClassDescriptor* descriptor = world.classes().find(world.classOf(id));
    return descriptor != nullptr && scene::hasFlag(descriptor->flags, scene::ClassFlags::Service);
}

bool Editor::deleteInstance(scene::World& world, core::InstanceId id, core::InstanceId root, Inspector& inspector)
{
    if (!world.alive(id))
        return false;

    if (isEngineOwned(world, id, root)) {
        m_status =
            EditorStatus{"that one belongs to the engine -- services and the world itself cannot be deleted", true};
        return false;
    }

    const std::string name(world.atoms().text(world.name(id)));
    m_history.record(world, "Delete " + name);
    if (!world.destroy(id))
        return false;

    // **Retired here, because nothing else will while the editor is editing.**
    // `destroy` marks and unlinks; the record stops resolving in
    // `retireDestroyed`, which runs at the end of a signal drain -- and a paused
    // world runs no drains, so a deleted instance would keep answering `alive`
    // until somebody pressed play.
    //
    // The `Destroying` signal is therefore not fired for an editor's delete, and
    // that is the honest reading rather than an oversight: while editing, no
    // script is running to hear it. When a scene's scripts start running again
    // they do so against a world where the instance was never there.
    world.retireDestroyed();

    // The selection cannot outlive what it names. `destroy` leaves the handle
    // resolving until the end of the drain that carries `Destroying`, so this
    // is asked as a question about the tree rather than about the id.
    // A sweep rather than a comparison. `destroy` took the whole subtree, so
    // testing the selection against `id` alone would leave a selected CHILD of
    // what was deleted pointing at an instance the world has retired.
    inspector.pruneDead(world);

    m_status = EditorStatus{"deleted " + name + " -- there is no undo yet; reopening the scene brings it back", false};
    return true;
}

bool Editor::duplicateInstance(scene::World& world, core::InstanceId id, core::InstanceId root, Inspector& inspector)
{
    if (!world.alive(id))
        return false;

    const core::InstanceId parent = world.parentOf(id);
    if (!parent.valid())
        return false;

    if (isEngineOwned(world, id, root)) {
        // "One per world" is what a service IS. A second one would make every
        // `GetService` a question with two answers.
        m_status = EditorStatus{"a service is one per world, so there is no second one to make", true};
        return false;
    }

    m_history.record(world, "Duplicate " + std::string(world.atoms().text(world.name(id))));
    const core::InstanceId copy = world.clone(id);
    if (!copy.valid())
        return false;

    (void)world.setParent(copy, parent);
    // Selected, because the reason to duplicate a thing is to change the copy
    // and not to admire it.
    inspector.select(copy);
    inspector.reveal(copy);

    m_status = EditorStatus{"duplicated " + std::string(world.atoms().text(world.name(id))), false};
    return true;
}

bool Editor::createInstance(scene::World& world, scene::ClassId classId, core::InstanceId parent, core::InstanceId root,
                            Inspector& inspector)
{
    if (!world.alive(parent))
        return false;

    const scene::ClassDescriptor* descriptor = world.classes().find(classId);
    if (descriptor == nullptr || !creatable(*descriptor)) {
        m_status = EditorStatus{"that class cannot be created", true};
        return false;
    }

    // **A service is a legal parent and the world itself is too.** What
    // `isEngineOwned` refuses is deleting, duplicating or renaming one -- but
    // `Lighting` holding a `PointLight` and `Workspace` holding a `Part` is
    // what those services are FOR, so the guard the other three verbs share
    // does not belong here. What does belong is the one it never covered:
    // something a system made is not somewhere a person authors into -- and
    // that has to be asked of the ANCESTRY, because a chunk marks its folder
    // and not the ground inside it.
    if (!canParentInto(world, parent, root)) {
        m_status = EditorStatus{"that was made by the engine, so nothing authored can live in it", true};
        return false;
    }

    m_history.record(world, "Create " + std::string(world.atoms().text(descriptor->name)));

    const core::InstanceId made = world.create(classId);
    if (!made.valid()) {
        m_status = EditorStatus{"could not create that class", true};
        return false;
    }
    if (world.setParent(made, parent).has_value()) {
        (void)world.destroy(made);
        world.retireDestroyed();
        m_status = EditorStatus{"that cannot be parented there", true};
        return false;
    }

    // **In front of the camera rather than at the origin.** In a streamed world
    // the origin is not where anybody is standing, and a part created four
    // kilometres from the view is one nobody finds. Through `setProperty`
    // rather than into the component, so a class with no `CFrame` needs no
    // special case here -- it simply refuses and nothing is placed.
    if (m_cameraAdopted) {
        const core::Mat3& basis = m_cameraCFrame.rotation;
        const core::Vec3 forward{-basis.m[2][0], -basis.m[2][1], -basis.m[2][2]};
        // Far enough to be whole in the view and near enough to be reachable.
        constexpr f32 kSpawnDistance = 8.0f;
        core::CFrameD placed;
        placed.position = m_cameraCFrame.position + core::toDVec3(forward * kSpawnDistance);
        (void)world.setProperty(made, world.atoms().intern("CFrame"), scene::Value{placed});
    }

    // Selected, for the reason a duplicate is: the point of making a thing is
    // to change it. **And revealed**, because a parent that has never been
    // opened is not opened by gaining a child -- and an empty one had no
    // chevron to open it with, so a `Part` made inside a fresh `Folder` was
    // invisible every single time.
    inspector.select(made);
    inspector.reveal(made);

    m_status = EditorStatus{"added a " + std::string(world.atoms().text(descriptor->name)), false};
    return true;
}

bool Editor::canParentInto(const scene::World& world, core::InstanceId id, core::InstanceId root)
{
    if (!world.alive(id))
        return false;

    // Up the whole chain. Streaming marks a chunk's FOLDER and not the parts
    // inside it -- `streaming_glue.cpp` says so where it sets the flag, and the
    // scene serializer relies on exactly that economy -- so an instance that is
    // not itself generated may still be sitting inside something that is.
    for (core::InstanceId walk = id; walk.valid(); walk = world.parentOf(walk)) {
        if (world.generated(walk))
            return false;
        if (walk == root)
            break;
    }
    return true;
}

bool Editor::authorable(const scene::World& world, core::InstanceId id, core::InstanceId root)
{
    // The same walk, plus the one thing a PARENT is allowed to be and a moved
    // or deleted instance is not: one of the engine's own.
    return canParentInto(world, id, root) && !isEngineOwned(world, id, root);
}

Editor::ReparentPlan Editor::planReparent(const scene::World& world, std::span<const core::InstanceId> ids,
                                          core::InstanceId newParent, core::InstanceId root)
{
    ReparentPlan plan;
    if (!world.alive(newParent) || (newParent != root && !authorable(world, newParent, root))) {
        plan.targetRefuses = true;
        return plan;
    }

    // Document order rather than click order, so the result is a function of
    // the SET. It is also the order that keeps a parent ahead of its own child,
    // which stops a move of both depending on which was reached first.
    std::vector<core::InstanceId> ordered;
    orderByTree(world, root, ids, ordered);

    for (const core::InstanceId id : ordered) {
        if (isEngineOwned(world, id, root)) {
            ++plan.refused;
            continue;
        }
        // A cycle: onto itself, or into its own subtree. `World::setParent`
        // refuses both and this asks the SAME function rather than carrying a
        // second copy of the rule.
        if (id == newParent || world.isAncestorOf(id, newParent)) {
            ++plan.refused;
            continue;
        }
        // Already there. Not a refusal -- a parent earlier in the walk has
        // taken its children with it, and re-parenting a child to where it
        // already is would only move it to the end of the sibling list.
        if (world.parentOf(id) == newParent)
            continue;
        plan.movable.push_back(id);
    }
    return plan;
}

void Editor::copySelection(const scene::World& world, std::span<const core::InstanceId> ids, core::InstanceId root)
{
    m_clipboard.clear();
    if (ids.empty())
        return;

    // Document order, so pasting four things back reproduces the order they
    // were in rather than the order somebody happened to ctrl-click. It is the
    // same reason every batch verb sorts, and R10's discipline applied to a
    // clipboard.
    std::vector<core::InstanceId> ordered;
    orderByTree(world, root, ids, ordered);

    for (const core::InstanceId id : ordered) {
        if (isEngineOwned(world, id, root))
            continue;
        // **Anything inside another thing already copied is skipped.** Copying
        // a parent and its child and pasting would otherwise produce the child
        // twice: once inside the parent, where it belongs, and once beside it.
        bool insideAnother = false;
        for (const core::InstanceId other : ordered) {
            if (other != id && world.isAncestorOf(other, id)) {
                insideAnother = true;
                break;
            }
        }
        if (insideAnother)
            continue;
        m_clipboard.push_back(scene::writeStamp(world, id));
    }

    m_status = EditorStatus{"copied " + std::to_string(m_clipboard.size()) + " instance(s)", false};
}

bool Editor::paste(scene::World& world, core::InstanceId parent, core::InstanceId root, Inspector& inspector)
{
    if (m_clipboard.empty()) {
        m_status = EditorStatus{"there is nothing to paste", true};
        return false;
    }
    if (!canParentInto(world, parent, root)) {
        m_status = EditorStatus{"nothing authored can live in that", true};
        return false;
    }

    // Recorded before the first one, so a paste of four is one press of ctrl-Z.
    m_history.record(world, m_clipboard.size() == 1 ? "Paste" : "Paste " + std::to_string(m_clipboard.size()));

    std::vector<core::InstanceId> pasted;
    for (const std::string& text : m_clipboard) {
        scene::SceneIoReport report;
        const core::InstanceId placed = scene::readStamp(world, text, parent, "<clipboard>", &report);
        if (!placed.valid())
            continue;
        // **The mark does not come with it.** What was copied is a subtree, and
        // a mark naming `<clipboard>` would point at a file that does not
        // exist -- the marks INSIDE it are kept, because those name real ones.
        world.setStamp(placed, core::NameAtom{});
        pasted.push_back(placed);
    }

    if (pasted.empty()) {
        // Nothing was built, so the step is taken back rather than left: a step
        // that undoes nothing eats a press of ctrl-Z.
        (void)m_history.undo(world);
        m_status = EditorStatus{"nothing in the clipboard could be pasted", true};
        return false;
    }

    inspector.select(pasted);
    inspector.reveal(pasted.front());
    m_status = EditorStatus{"pasted " + std::to_string(pasted.size()) + " instance(s)", false};
    return true;
}

bool Editor::canReparent(const scene::World& world, std::span<const core::InstanceId> ids, core::InstanceId newParent,
                         core::InstanceId root)
{
    return !ids.empty() && !planReparent(world, ids, newParent, root).movable.empty();
}

std::string Editor::normalizeStampPath(std::string_view typed)
{
    std::string path(typed);

    // The same normalisation a scene path gets, for the same reason: the box is
    // labelled `content/`, so typing the prefix is the natural thing to do and
    // the wrong thing to keep (D068).
    for (char& c : path) {
        if (c == '\\')
            c = '/';
    }
    while (!path.empty() && path.front() == '/')
        path.erase(path.begin());
    constexpr std::string_view kContentPrefix = "content/";
    while (path.compare(0, kContentPrefix.size(), kContentPrefix) == 0)
        path.erase(0, kContentPrefix.size());

    if (path.empty())
        return path;

    // **A bare name lands in `content/stamps/`**, and a name with a folder in it
    // is taken at its word. A default that a person can step outside of, which
    // is what makes it a convention rather than a rule.
    if (path.find('/') == std::string::npos)
        path = std::string(kStampFolder) + "/" + path;

    if (path.size() < kStampExtension.size() ||
        path.compare(path.size() - kStampExtension.size(), kStampExtension.size(), kStampExtension) != 0) {
        path += kStampExtension;
    }
    return path;
}

bool Editor::stampNameIsUsable(std::string_view typed)
{
    return sceneNameIsUsable(normalizeStampPath(typed));
}

scene::StampSource Editor::stampSource() const
{
    // Captured by value: the source outlives the call that made it, and a
    // reference into an editor that has been destroyed is the kind of thing
    // that works until somebody loads a scene during shutdown.
    const std::filesystem::path root = m_content.root();
    return [root](std::string_view stamp) -> std::optional<std::string> {
        std::string text;
        if (!platform::readTextFile(root / std::filesystem::path(stamp), text))
            return std::nullopt;
        return text;
    };
}

bool Editor::breakStamp(scene::World& world, core::InstanceId id)
{
    const core::InstanceId stampRoot = world.stampRootOf(id);
    if (!stampRoot.valid()) {
        m_status = EditorStatus{"that is not a stamped instance", true};
        return false;
    }

    m_history.record(world, "Break Stamp");
    world.setStamp(stampRoot, core::NameAtom{});
    m_status = EditorStatus{"broken; it is its own now", false};
    return true;
}

Editor::Stage::Stage(scene::ClassRegistry& classes, scene::EnumRegistry& enums, core::AtomTable& atoms, core::u64 seed)
    : m_world(classes, enums, atoms, seed)
{
    // Exactly what drawing a subtree needs and nothing else: somewhere to put
    // the instances, and something to see them by. No DataModel, no services, no
    // scripts -- a stage is a place to arrange things and look at them, and
    // every row this does not create is a row that would have appeared in an
    // Explorer somebody opened to look at ONE prefab.
    //
    // `Service` and `NotCreatable` are not checked by `World::create` -- they
    // are rules about `Instance.new`, enforced in `script` -- which is what
    // lets the engine build its own furniture here as it does at boot.
    const auto make = [this, &classes, &atoms](std::string_view className) {
        const scene::ClassId id = classes.findId(atoms.intern(className));
        const core::InstanceId instance = m_world.create(id);
        if (instance.valid())
            m_world.setName(instance, atoms.intern(className));
        return instance;
    };

    m_workspace = make("Workspace");
    m_lighting = make("Lighting");
}

bool Editor::openStamp(std::string_view path, scene::ClassRegistry& classes, scene::EnumRegistry& enums,
                       core::AtomTable& atoms, Inspector& inspector)
{
    if (m_run != RunState::Editing) {
        m_status = EditorStatus{"stop the world before opening a stamp", true};
        return false;
    }
    if (m_stamp.open()) {
        m_status = EditorStatus{"a stamp is already open", true};
        return false;
    }

    const std::string relative = normalizeStampPath(path);
    std::string text;
    if (!platform::readTextFile(m_content.root() / std::filesystem::path(relative), text)) {
        m_status = EditorStatus{"that stamp is not there any more", true};
        return false;
    }

    // **Built before anything is committed to.** A stage that fails to read its
    // stamp is a stage nobody wanted, and dropping it costs nothing -- while a
    // game world half-cleared for a stamp that would not load is the mess the
    // first cut of this had to unwind with a snapshot.
    auto stage = std::make_unique<Stage>(classes, enums, atoms, kStageSeed);
    if (!stage->workspace().valid()) {
        m_status = EditorStatus{"could not build a stage for that stamp", true};
        return false;
    }

    scene::SceneIoReport report;
    const core::InstanceId root = scene::readStamp(stage->world(), text, stage->workspace(), relative, &report);
    if (!root.valid()) {
        m_status = EditorStatus{"that stamp could not be read", true};
        return false;
    }

    m_stage = std::move(stage);
    m_stamp = StampSession{relative, root, false, text};

    // A different world in the plainest sense -- a different `scene::World`
    // object -- so everything a panel keyed by id has to go. That is what
    // `onWorldChanged` is for, and it is exactly true here rather than
    // approximately true as it was when this cleared the game's scene instead.
    m_history.clear();
    inspector.onWorldChanged();
    inspector.select(root);
    inspector.reveal(root);

    m_status = EditorStatus{"editing " + relative, false};
    return true;
}

void Editor::syncMaterialPreview(const Inspector& inspector)
{
    if (m_stage == nullptr) {
        // The stage went away and took its world with it, so there is nothing to
        // take down -- the ids named instances in a `World` that no longer
        // exists, and forgetting them IS the cleanup.
        m_previewSphere = {};
        m_previewFloor = {};
        m_previewLight = {};
        m_previewOf = {};
        return;
    }

    scene::World& world = m_stage->world();

    // What is selected, and only if it is a material. Selecting a `Part` inside
    // a stamp that also contains a material should show the part being edited,
    // not a sphere -- the preview answers "what does this material look like",
    // and that is a question about a material.
    core::InstanceId subject = inspector.selection();
    if (subject.valid() && world.materials().find(subject) == nullptr)
        subject = {};

    if (subject == m_previewOf)
        return;

    // Taken down rather than hidden: a stage with a preview in it that nobody
    // asked for is a stage with something in it nobody put there.
    for (core::InstanceId* held : {&m_previewSphere, &m_previewFloor, &m_previewLight}) {
        if (held->valid())
            (void)world.destroy(*held);
        *held = {};
    }
    // The stage runs no drains, exactly as the editor's own world does not --
    // see `Editor::load`. Without this the taken-down preview would keep
    // resolving, and rebuilding it would leave the old sphere in the pools.
    world.retireDestroyed();

    m_previewOf = subject;
    if (!subject.valid())
        return;

    const scene::ClassId partClass = world.classes().findId(world.atoms().intern("Part"));
    if (partClass == scene::InvalidClass)
        return;

    // **A sphere, because a flat swatch shows none of what a material is.**
    // Roughness, metalness and a normal map are all about how light moves across
    // a curvature; a square of colour shows the base colour and nothing else.
    // Every engine with a material preview draws a curved surface for this
    // reason and not as a house style.
    m_previewSphere = world.create(partClass);
    world.setName(m_previewSphere, world.atoms().intern("Preview"));
    (void)world.setParent(m_previewSphere, m_stage->workspace());
    world.setGenerated(m_previewSphere, true);
    if (scene::PartComponent* sphere = world.parts().find(m_previewSphere); sphere != nullptr) {
        sphere->shape = 1; // Ball
        sphere->size = core::Vec3{2.0f, 2.0f, 2.0f};
        sphere->cframe.position = core::DVec3{0.0, 1.2, 0.0};
        sphere->material = subject;
    }

    // **A floor under it**, which is not decoration: a metal sphere in an empty
    // room is a black circle, because metal shows what is around it and there is
    // nothing around it. The floor is what a rough metal reads as metal against.
    m_previewFloor = world.create(partClass);
    world.setName(m_previewFloor, world.atoms().intern("PreviewFloor"));
    (void)world.setParent(m_previewFloor, m_stage->workspace());
    world.setGenerated(m_previewFloor, true);
    if (scene::PartComponent* floor = world.parts().find(m_previewFloor); floor != nullptr) {
        floor->size = core::Vec3{12.0f, 0.4f, 12.0f};
        floor->cframe.position = core::DVec3{0.0, -0.2, 0.0};
        floor->color = core::Color3{0.35f, 0.35f, 0.38f};
    }

    // And a light off to one side rather than straight on. A light behind the
    // camera flattens everything it touches -- the highlight lands in the middle
    // of the sphere and roughness stops being readable, which is the one thing
    // somebody is squinting at.
    const scene::ClassId lightClass = world.classes().findId(world.atoms().intern("PointLight"));
    if (lightClass != scene::InvalidClass) {
        m_previewLight = world.create(lightClass);
        world.setName(m_previewLight, world.atoms().intern("PreviewLight"));
        (void)world.setParent(m_previewLight, m_previewSphere);
        world.setGenerated(m_previewLight, true);
        if (scene::PointLightComponent* light = world.pointLights().find(m_previewLight); light != nullptr) {
            light->brightness = 6.0f;
            light->range = 20.0f;
        }
    }
}

std::vector<core::NameAtom> Editor::overridesOf(const scene::World& world, core::InstanceId id)
{
    if (!id.valid() || !world.alive(id))
        return {};

    // A library per call rather than one kept on the editor: it caches the
    // stamps it reads for as long as it lives, and a cache that outlives an
    // edit to the file it read is a panel showing yesterday's answer.
    scene::StampLibrary stamps(const_cast<scene::World&>(world), stampSource());
    return scene::stampOverrides(world, id, stamps);
}

bool Editor::revertOverride(scene::World& world, core::InstanceId id, core::NameAtom property)
{
    if (!id.valid() || !world.alive(id) || !property.valid()) {
        m_status = EditorStatus{"there is nothing selected to revert", true};
        return false;
    }

    scene::StampLibrary stamps(world, stampSource());
    const std::optional<scene::Value> theirs = scene::stampReferenceValue(world, id, property, stamps);
    if (!theirs.has_value()) {
        m_status = EditorStatus{"that is not part of a stamp, so there is nothing to revert to", true};
        return false;
    }

    const std::string name(world.atoms().text(property));

    // **Asked before it is recorded.** A revert of a property that already
    // matches the file is a step that undoes nothing, and `UndoStack::record`
    // clears the redo stack -- so it would destroy a real redo future and leave
    // a junk one in its place (D134).
    const scene::PropertyDesc* descriptor = world.classes().findProperty(world.classOf(id), property);
    if (descriptor == nullptr || descriptor->get == nullptr) {
        m_status = EditorStatus{name + " is not a property of that instance", true};
        return false;
    }
    const std::optional<scene::Value> mine = descriptor->get(world, id);
    if (mine.has_value() && *mine == *theirs) {
        m_status = EditorStatus{name + " already matches the stamp"};
        return true;
    }

    m_history.record(world, "Revert " + name);
    const scene::World::SetResult wrote = world.setProperty(id, property, *theirs);
    if (wrote != scene::World::SetResult::Changed && wrote != scene::World::SetResult::Unchanged) {
        m_status = EditorStatus{"could not revert " + name, true};
        return false;
    }
    touch();
    m_status = EditorStatus{"reverted " + name + " to the stamp"};
    return true;
}

bool Editor::applyOverride(scene::World& world, core::InstanceId gameRoot, core::InstanceId id, core::NameAtom property)
{
    if (!id.valid() || !world.alive(id) || !property.valid()) {
        m_status = EditorStatus{"there is nothing selected to apply", true};
        return false;
    }

    // The stamp this instance belongs to, which is the file about to change.
    core::InstanceId stampRoot = id;
    core::NameAtom mark{};
    while (stampRoot.valid()) {
        mark = world.stampOf(stampRoot);
        if (mark.valid())
            break;
        stampRoot = world.parentOf(stampRoot);
    }
    if (!mark.valid()) {
        m_status = EditorStatus{"that is not part of a stamp, so there is nothing to apply to", true};
        return false;
    }
    const std::string path(world.atoms().text(mark));

    // **Refused while that stamp is open on the stage**, because then there are
    // two writers of one file and the one a person can see would lose. Said
    // rather than silently preferred: the stage is right there.
    if (m_stamp.open() && m_stamp.path == path) {
        m_status = EditorStatus{path + " is open for editing; apply from the stage instead", true};
        return false;
    }

    const std::string name(world.atoms().text(property));
    const scene::PropertyDesc* descriptor = world.classes().findProperty(world.classOf(id), property);
    if (descriptor == nullptr || descriptor->get == nullptr) {
        m_status = EditorStatus{name + " is not a property of that instance", true};
        return false;
    }
    const std::optional<scene::Value> mine = descriptor->get(world, id);
    if (!mine.has_value()) {
        m_status = EditorStatus{"could not read " + name, true};
        return false;
    }

    const std::optional<std::string> before = stampSource()(path);
    if (!before.has_value()) {
        m_status = EditorStatus{"could not read " + path, true};
        return false;
    }

    // **Built into a scratch world of its own, edited there, and written back.**
    // The alternative -- editing the JSON text -- would be a second reader of
    // the stamp format, and `readSceneNode`'s own comment gives the reason that
    // is not worth having: one definition of what a stamp means.
    scene::World scratch(world.classes(), world.enums(), world.atoms(), 1u);
    const core::InstanceId scratchRoot = scene::readStamp(scratch, *before, core::InstanceId{}, path);
    if (!scratchRoot.valid()) {
        m_status = EditorStatus{"could not read " + path, true};
        return false;
    }

    // The same walk down from each root, which is what pairs the live instance
    // with the one in the file.
    std::vector<core::u32> descent;
    for (core::InstanceId step = id; step != stampRoot; step = world.parentOf(step)) {
        const core::InstanceId parent = world.parentOf(step);
        if (!parent.valid())
            break;
        core::u32 index = 0;
        core::InstanceId child = world.firstChild(parent);
        while (child.valid() && child != step) {
            child = world.nextSibling(child);
            ++index;
        }
        descent.push_back(index);
    }
    core::InstanceId target = scratchRoot;
    for (auto step = descent.rbegin(); step != descent.rend(); ++step) {
        core::InstanceId child = scratch.firstChild(target);
        for (core::u32 skipped = 0; skipped < *step && child.valid(); ++skipped)
            child = scratch.nextSibling(child);
        if (!child.valid()) {
            m_status = EditorStatus{"that instance is not in " + path + " any more", true};
            return false;
        }
        target = child;
    }

    const scene::World::SetResult intoStamp = scratch.setProperty(target, property, *mine);
    if (intoStamp != scene::World::SetResult::Changed && intoStamp != scene::World::SetResult::Unchanged) {
        m_status = EditorStatus{"could not write " + name + " into " + path, true};
        return false;
    }

    scene::SceneIoReport wrote;
    const std::string after = scene::writeStamp(scratch, scratchRoot, &wrote);
    const std::filesystem::path absolute = m_content.root() / std::filesystem::path(path);
    if (!platform::createDirectories(absolute.parent_path()) || !platform::writeTextFile(absolute, after)) {
        m_status = EditorStatus{"could not write " + path, true};
        return false;
    }

    // **Every linked instance follows, measured against the text they were
    // built from** -- the same arithmetic `saveStamp` does, and for the same
    // reason: an instance that overrode this property with some other value
    // differs from `before` and keeps what it has.
    scene::SceneIoReport moved;
    const core::u32 followed =
        gameRoot.valid() && world.alive(gameRoot) ? scene::restamp(world, gameRoot, path, *before, after, &moved) : 0u;
    if (m_stamp.open() && m_stamp.path == path)
        m_stamp.baseline = after;
    if (followed > 0 || moved.unlinkedStamps > 0)
        m_sceneDirty = true;

    std::string message = "applied " + name + " to " + path;
    if (followed > 0)
        message += ", " + std::to_string(followed) + " in the world";
    if (moved.unlinkedStamps > 0)
        message += ", " + std::to_string(moved.unlinkedStamps) + " left alone (changed structurally)";
    m_status = EditorStatus{message};
    return true;
}

bool Editor::saveStamp(scene::World& game, core::InstanceId gameRoot)
{
    if (!m_stamp.open() || m_stage == nullptr || !m_stage->world().alive(m_stamp.root)) {
        m_status = EditorStatus{"there is no stamp open to save", true};
        return false;
    }

    scene::SceneIoReport report;
    // **The stamps this stamp names, for the reason `save` gives about a scene**
    // (D133). A stamp can contain a stamped instance -- a lamp post inside a
    // street -- and without the library every one of them is written in full and
    // unlinked, so editing the lamp post stops reaching the street. `save` has
    // done this since ADR 0051 landed and this path never learned it.
    scene::StampLibrary stamps(m_stage->world(), stampSource());
    const std::string text = scene::writeStamp(m_stage->world(), m_stamp.root, &report, &stamps);
    const std::filesystem::path absolute = m_content.root() / std::filesystem::path(m_stamp.path);
    if (!platform::createDirectories(absolute.parent_path()) || !platform::writeTextFile(absolute, text)) {
        m_status = EditorStatus{"could not write " + m_stamp.path, true};
        return false;
    }

    // **Every linked instance in the game's world follows the file** (ADR
    // 0051), measured against the text they were built from -- which is why the
    // session carries it. Done AFTER the write, so a save that could not reach
    // the disk does not move the world to match a file that is not there.
    //
    // Not an undo step, and that is deliberate: this is a change to a FILE, and
    // the history a person can undo here belongs to the stage. What the world
    // now holds is what the file says, which is the one thing an undo could not
    // put back.
    scene::SceneIoReport moved;
    const core::u32 followed =
        game.alive(gameRoot) ? scene::restamp(game, gameRoot, m_stamp.path, m_stamp.baseline, text, &moved) : 0u;
    m_stamp.baseline = text;

    // **Every instance it moved is a change to the SCENE, and the scene has to
    // know.** `restamp` rebuilds live instances in the game's world -- that is
    // the whole point of a linked stamp -- and nothing else marks it, because
    // the frame's own `touch()` attributes a mutation to whatever is open and
    // what is open is this stamp.
    //
    // Without this the sequence is silent and it loses work: edit a stamp, watch
    // every instance in the scene change, close the editor or start a new scene,
    // and be asked nothing -- because the scene believes it is clean.
    if (followed > 0 || moved.unlinkedStamps > 0)
        m_sceneDirty = true;

    m_stamp.dirty = false;
    std::string message = "saved " + m_stamp.path + " (" + std::to_string(report.instances) + " instance(s))";
    if (followed > 0)
        message += ", " + std::to_string(followed) + " in the world";
    // Said out loud rather than counted quietly: an instance somebody changed
    // structurally stops following its stamp, and finding that out by noticing
    // one lamp post did not move is how a person stops trusting the link.
    if (moved.unlinkedStamps > 0)
        message += ", " + std::to_string(moved.unlinkedStamps) + " left alone (changed structurally)";
    // **Counted rather than swallowed**, exactly as `save` counts a scene's
    // (D133). A stamp is written from its root DOWN, so a reference pointing at
    // anything outside that subtree cannot be carried -- and pointing a part
    // inside a stamp at a `Material` that sits beside the stamp rather than
    // under it is the ordinary way to arrive here. It came back as `null` on the
    // next open, silently, and `restamp` then pushed that null into every
    // instance in the world.
    if (report.droppedReferences > 0) {
        message += ", " + std::to_string(report.droppedReferences) +
                   " reference(s) outside the stamp were dropped -- put what they name INSIDE it";
    }
    m_status = EditorStatus{message, report.droppedReferences > 0};
    return true;
}

bool Editor::closeStamp(scene::World& game, core::InstanceId gameRoot, Inspector& inspector, bool save)
{
    if (!m_stamp.open())
        return false;

    const std::string closed = m_stamp.path;
    const bool wrote = save && saveStamp(game, gameRoot);

    // The stage goes, and with it every instance in it. **The game's world was
    // never touched**, so there is nothing to restore and no snapshot to keep --
    // which is the whole reason a stage is a world of its own.
    m_stage.reset();
    m_stamp = StampSession{};

    m_history.clear();
    inspector.onWorldChanged();

    m_status = EditorStatus{wrote ? "saved and closed " + closed : "closed " + closed, false};
    return true;
}

std::string Editor::createStampOfClass(scene::World& world, core::InstanceId root, scene::ClassId classId,
                                       std::string_view name)
{
    if (!stampNameIsUsable(name))
        return {};

    const scene::ClassDescriptor* descriptor = world.classes().find(classId);
    if (descriptor == nullptr) {
        m_status = EditorStatus{"no such class", true};
        return {};
    }

    // Refused before anything is made, so a name that is taken does not leave an
    // orphan `Material` in the world with no file behind it.
    const std::string relative = normalizeStampPath(name);
    std::error_code ec;
    if (std::filesystem::exists(m_content.root() / std::filesystem::path(relative), ec)) {
        m_status = EditorStatus{"something is already called that", true};
        return {};
    }

    // Recorded BEFORE the instance exists, so one undo takes both back.
    m_history.record(world, "New Stamp");

    const core::InstanceId made = world.create(classId);
    // Named after the file, minus its folders and its suffix -- which is what
    // somebody typed and what they will look for in the Explorer.
    std::string stem = relative;
    if (const std::string::size_type slash = stem.rfind('/'); slash != std::string::npos)
        stem = stem.substr(slash + 1);
    if (const std::string::size_type dot = stem.find('.'); dot != std::string::npos)
        stem = stem.substr(0, dot);
    world.setName(made, world.atoms().intern(stem));
    if (world.setParent(made, root).has_value()) {
        (void)m_history.undo(world);
        m_status = EditorStatus{"nothing authored can live in that", true};
        return {};
    }

    if (!createStamp(world, made, root, name)) {
        // `createStamp` said why. Taking the instance back with it, because an
        // instance in the world that nothing wrote is not what was asked for.
        (void)m_history.undo(world);
        return {};
    }

    m_status = EditorStatus{"created " + relative, false};
    return relative;
}

bool Editor::createStamp(scene::World& world, core::InstanceId id, core::InstanceId root, std::string_view name)
{
    if (!world.alive(id)) {
        m_status = EditorStatus{"nothing to make a stamp of", true};
        return false;
    }
    if (!stampNameIsUsable(name)) {
        m_status = EditorStatus{"that is not a usable stamp name", true};
        return false;
    }
    // Engine-owned, or inside something a system made: neither is somebody's
    // authored work, and a stamp of a streamed chunk is a recording of where
    // streaming happened to be.
    if (isEngineOwned(world, id, root) || !canParentInto(world, id, root)) {
        m_status = EditorStatus{"that is not something a person authored", true};
        return false;
    }

    // **A stamp of a stamp is refused rather than half-answered** (ADR 0049).
    // Does the outer file record the inner link? Does breaking the outer break
    // the inner? Those are real questions with no answer yet, and a format that
    // silently picked one would be a format somebody depends on before anybody
    // decides.
    for (core::InstanceId child = world.firstChild(id); child.valid();) {
        if (world.stampOf(child).valid()) {
            m_status = EditorStatus{"that already contains a stamped instance", true};
            return false;
        }
        if (const core::InstanceId inner = world.firstChild(child); inner.valid()) {
            child = inner;
            continue;
        }
        while (child.valid() && child != id && !world.nextSibling(child).valid())
            child = world.parentOf(child);
        child = child == id ? core::InstanceId{} : world.nextSibling(child);
    }

    const std::string relative = normalizeStampPath(name);
    const std::filesystem::path absolute = m_content.root() / std::filesystem::path(relative);
    if (!platform::createDirectories(absolute.parent_path())) {
        m_status = EditorStatus{"could not make the folder for that stamp", true};
        return false;
    }

    scene::SceneIoReport report;
    const std::string text = scene::writeStamp(world, id, &report);
    if (!platform::writeTextFile(absolute, text)) {
        m_status = EditorStatus{"could not write that stamp", true};
        return false;
    }

    // **And the thing it was made from becomes an instance of it.** A file plus
    // a copy of it that nothing connects is two things that drift apart by
    // tomorrow, which is the state this whole model exists to avoid.
    m_history.record(world, "Create Stamp");
    world.setStamp(id, world.atoms().intern(relative));

    (void)m_content.refresh();
    m_status = EditorStatus{"stamped " + std::to_string(report.instances) + " instance(s) into " + relative, false};
    return true;
}

namespace {
// **A `Model` is moved by its PIVOT, because a pivot is the only handle it
// has.** `Model` declares no `CFrame` property at all, so a placement that
// wrote one wrote nothing: the class refused it, the refusal was a return value
// nobody read, and the subtree stayed at the coordinates its file records --
// which for anything authored near where it was built is the world origin.
// `Part` does have a `CFrame`, and a `Part` root is the case that got tried.
//
// This is `PivotTo` (`instance_binding.cpp`) reached without a VM, off the same
// `scene::pivotOf`. `pivot.h` was lifted out of the binding precisely so that
// "where is the middle of this model" has one answer below `script`, and it
// names an editor gizmo as a caller; nothing in `engine/app` had asked it yet.
// The rule about what travels comes with it: a model moves every part under it,
// which is what keeps the layout somebody built, and a part moves alone,
// because what hangs off a part is welds and constraints rather than geometry.
void pivotTo(scene::World& world, core::InstanceId id, const core::CFrameD& target)
{
    // `delta` puts the pivot on the target, and everything the object owns moves
    // by that same transform -- which is what preserves relative layout.
    const core::CFrameD delta = target * core::inverse(scene::pivotOf(world, id));
    const core::NameAtom cframeProperty = world.atoms().intern("CFrame");

    if (world.models().find(id) != nullptr) {
        std::vector<core::InstanceId> descendants;
        world.collectDescendants(id, descendants);
        for (const core::InstanceId descendant : descendants) {
            const scene::PartComponent* part = world.parts().find(descendant);
            if (part == nullptr)
                continue;
            // Through `setProperty` rather than into the component, so the
            // renderer and anything watching `CFrame` see it by the path they
            // already have. The component is what says the write can land, so
            // the result answers nothing this has not already asked.
            world.setProperty(descendant, cframeProperty, scene::Value{delta * part->cframe});
        }
        return;
    }

    if (const scene::PartComponent* part = world.parts().find(id); part != nullptr) {
        world.setProperty(id, cframeProperty, scene::Value{delta * part->cframe});
        return;
    }

    if (const scene::CameraComponent* camera = world.cameras().find(id); camera != nullptr)
        world.setProperty(id, cframeProperty, scene::Value{delta * camera->cframe});
}
} // namespace

bool Editor::instantiateStamp(scene::World& world, std::string_view name, core::InstanceId parent,
                              core::InstanceId root, Inspector& inspector, bool linked)
{
    if (!canParentInto(world, parent, root)) {
        m_status = EditorStatus{"nothing authored can live in that", true};
        return false;
    }

    const std::string relative = normalizeStampPath(name);
    std::string text;
    if (!platform::readTextFile(m_content.root() / std::filesystem::path(relative), text)) {
        m_status = EditorStatus{"that stamp is not there any more", true};
        return false;
    }

    // Recorded BEFORE the instance exists, so one undo takes the whole subtree
    // back -- which is what `WorldSnapshot` makes cheap and what a
    // reversible-command design would have made hard (`editor.h` says why).
    m_history.record(world, "Stamp");

    scene::SceneIoReport report;
    const core::InstanceId placed = scene::readStamp(world, text, parent, relative, &report);
    if (!placed.valid()) {
        // Nothing usable was built, so the step is taken back rather than
        // left: a step that undoes nothing eats a press of ctrl-Z, and undoing
        // it also removes whatever partial subtree the read managed before it
        // gave up.
        (void)m_history.undo(world);
        m_status = EditorStatus{"that stamp could not be read", true};
        return false;
    }

    // In front of the camera rather than at the origin, for the reason
    // `createInstance` places a new part there: something four kilometres from
    // the view is something nobody finds.
    //
    // **Through the pivot**, because the root of a stamp is a `Model` as often
    // as not -- grouping parts is what produces one -- and a `Model` has no
    // `CFrame` to write. See `pivotTo` above for what that cost.
    if (m_cameraAdopted) {
        const core::Mat3& basis = m_cameraCFrame.rotation;
        const Vec3 forward{-basis.m[2][0], -basis.m[2][1], -basis.m[2][2]};
        constexpr f32 kSpawnDistance = 8.0f;
        core::CFrameD spawn;
        spawn.position = m_cameraCFrame.position + core::toDVec3(forward * kSpawnDistance);
        pivotTo(world, placed, spawn);
    }

    // **A copy is a placement that forgets where it came from.** Same subtree,
    // no mark -- so it is written in full, and nothing that happens to the
    // stamp reaches it again.
    if (!linked)
        world.setStamp(placed, core::NameAtom{});

    inspector.select(placed);
    inspector.reveal(placed);
    m_status = EditorStatus{(linked ? "stamped " : "copied ") + relative, false};
    return true;
}

bool Editor::assignStampTo(scene::World& world, core::InstanceId root, core::InstanceId parent, std::string_view path,
                           std::string_view property, std::span<const core::InstanceId> targets)
{
    if (targets.empty() || property.empty())
        return false;

    const std::string relative = normalizeStampPath(path);
    const core::NameAtom mark = world.atoms().intern(relative);
    const core::NameAtom field = world.atoms().intern(property);

    // The one already in the world wins. Document order, so which one is found
    // is a fact about the tree rather than about pool layout -- and so two runs
    // of the same gesture on the same scene agree.
    //
    // **Searched BEFORE anything is recorded** (D134), because the search does
    // not mutate and because of what comes next: dropping the same material on
    // a part that already wears it must not record a step. `UndoStack::record`
    // clears the redo stack, so a step taken back afterwards has already
    // destroyed a real redo future and leaves a junk one behind -- the file's
    // own invariant is "a step that undoes nothing eats a press of ctrl-Z", and
    // this was the hole left in it.
    core::InstanceId subject;
    static thread_local std::vector<TreeRow> rows;
    collectTree(world, root, rows);
    for (const TreeRow& row : rows) {
        if (world.stampOf(row.id) == mark && !world.destroyed(row.id)) {
            subject = row.id;
            break;
        }
    }

    if (subject.valid()) {
        bool everyTargetAlready = true;
        core::usize live = 0;
        for (const core::InstanceId target : targets) {
            if (!world.alive(target) || world.destroyed(target))
                continue;
            ++live;
            const std::optional<scene::Value> held = world.getProperty(target, field);
            if (!held.has_value() || !std::holds_alternative<core::InstanceId>(*held) ||
                std::get<core::InstanceId>(*held) != subject) {
                everyTargetAlready = false;
                break;
            }
        }
        if (live > 0 && everyTargetAlready) {
            // Nothing to do, and saying so is the honest answer: the part is
            // wearing what was dropped on it.
            m_status = EditorStatus{"already " + relative, false};
            return true;
        }
    }

    // **Recorded before the world is touched**, so the placement and every write
    // it enables are one press of ctrl-Z. Two steps would mean undoing a drop
    // left a material in the world that nothing points at.
    m_history.record(world, "Assign");

    if (!subject.valid()) {
        std::string text;
        if (!platform::readTextFile(m_content.root() / std::filesystem::path(relative), text)) {
            (void)m_history.undo(world);
            m_status = EditorStatus{"that stamp is not there any more", true};
            return false;
        }

        scene::SceneIoReport report;
        subject = scene::readStamp(world, text, parent, relative, &report);
        if (!subject.valid()) {
            (void)m_history.undo(world);
            m_status = EditorStatus{"that stamp could not be read", true};
            return false;
        }
    }

    // **Not selected and not revealed.** Somebody dropping a material on a part
    // is looking at the part; replacing their selection with the thing they
    // dragged would take away what they were working on. Placing a stamp INTO
    // the world is a different gesture and does select, which is why this does
    // not share `instantiateStamp`.
    core::usize written = 0;
    for (const core::InstanceId target : targets) {
        if (!world.alive(target) || world.destroyed(target))
            continue;
        // `Unchanged` counts: a part that already pointed at this material was
        // asked for the same thing and got it. Treating it as a failure would
        // make dropping one material on two parts report a refusal because one
        // of them was already right.
        const scene::World::SetResult wrote = world.setProperty(target, field, scene::Value{subject});
        if (wrote == scene::World::SetResult::Changed || wrote == scene::World::SetResult::Unchanged)
            ++written;
    }

    if (written == 0) {
        // The class does not take it, or the setter refused the class of what
        // was dropped. Nothing changed, so nothing is left on the stack.
        (void)m_history.undo(world);
        m_status = EditorStatus{"nothing selected takes that", true};
        return false;
    }

    m_sceneDirty = true;
    m_status = EditorStatus{"assigned " + relative, false};
    return true;
}

bool Editor::reparent(scene::World& world, std::span<const core::InstanceId> ids, core::InstanceId newParent,
                      core::InstanceId root, Inspector& inspector)
{
    if (ids.empty())
        return false;

    // **Decided before anything is recorded**, so a drag that cannot move
    // anything leaves no undo step behind. A step that undoes nothing is worse
    // than no step: it eats a press of ctrl-Z and the second press takes back
    // something the person had stopped thinking about.
    const ReparentPlan plan = planReparent(world, ids, newParent, root);
    if (plan.targetRefuses) {
        m_status = EditorStatus{"nothing authored can live in that", true};
        return false;
    }

    if (plan.movable.empty()) {
        m_status =
            EditorStatus{plan.refused > 0 ? "nothing there can be moved into that" : "already there", plan.refused > 0};
        return false;
    }

    core::usize refused = plan.refused;
    m_history.record(world, plan.movable.size() == 1 ? "Reparent" : "Reparent " + std::to_string(plan.movable.size()));
    for (const core::InstanceId id : plan.movable) {
        if (world.setParent(id, newParent).has_value())
            ++refused;
    }

    inspector.pruneDead(world);
    inspector.onWorldRestored();
    // **Where it went, opened.** Dropping something into a collapsed folder and
    // watching it disappear is the same defect creating one inside an empty one
    // was, arriving through the other verb.
    inspector.reveal(plan.movable.front());

    std::string message = "moved " + std::to_string(plan.movable.size());
    if (refused > 0)
        message += ", refused " + std::to_string(refused);
    m_status = EditorStatus{message, false};
    return true;
}

bool Editor::reorder(scene::World& world, core::InstanceId child, core::u32 index, Inspector& inspector)
{
    const core::InstanceId parent = world.parentOf(child);
    if (!parent.valid())
        return false;

    // **Decided before anything is recorded**, the same rule `reparent` above
    // states: a drag that moves nothing must leave no undo step behind, because
    // a step that undoes nothing eats a press of ctrl-Z and clears the redo
    // stack on its way past (D134). `moveChild` would answer `Unchanged` and
    // there is no way to un-record afterwards, so the question is asked first.
    //
    // One walk answers both halves of it -- where the child stands now, and how
    // many places there are to stand -- and it is the same walk `moveChild`
    // makes, on a gesture nobody performs in a loop.
    core::u32 at = 0;
    core::u32 count = 0;
    bool found = false;
    for (core::InstanceId sibling = world.firstChild(parent); sibling.valid(); sibling = world.nextSibling(sibling)) {
        if (sibling == child) {
            at = count;
            found = true;
        }
        ++count;
    }
    if (!found)
        return false;

    if (index >= count) {
        m_status = EditorStatus{"cannot move there", true};
        return false;
    }
    if (index == at) {
        // Not an error. Dropping a row back where it started is a person
        // changing their mind, and a red status line for it would be the tool
        // scolding somebody for a gesture it invited.
        m_status = EditorStatus{"already there", false};
        return false;
    }

    m_history.record(world, "Reorder");
    if (world.moveChild(parent, child, index) != scene::World::MoveResult::Moved)
        return false;

    m_sceneDirty = true;
    inspector.reveal(child);
    m_status = EditorStatus{"reordered", false};
    return true;
}

bool Editor::deleteInstances(scene::World& world, std::span<const core::InstanceId> ids, core::InstanceId root,
                             Inspector& inspector)
{
    if (ids.empty())
        return false;
    if (ids.size() == 1)
        return deleteInstance(world, ids[0], root, inspector);

    std::vector<core::InstanceId> ordered;
    orderByTree(world, root, ids, ordered);

    std::vector<core::InstanceId> removable;
    for (const core::InstanceId id : ordered) {
        if (!isEngineOwned(world, id, root))
            removable.push_back(id);
    }

    if (removable.empty()) {
        m_status =
            EditorStatus{"that one belongs to the engine -- services and the world itself cannot be deleted", true};
        return false;
    }

    m_history.record(world, "Delete " + std::to_string(removable.size()) + " instances");

    core::usize removed = 0;
    for (const core::InstanceId id : removable) {
        // A parent destroyed earlier in the walk took its children with it, so
        // a child named separately is already gone -- and that is not an error.
        // Selecting a parent and its child and pressing delete means both, and
        // both is what happened.
        if (world.alive(id) && world.destroy(id))
            ++removed;
    }

    // A paused world runs no signal drain, so nothing else retires these and
    // they would go on answering `alive` -- the same reason `deleteInstance`
    // calls it.
    world.retireDestroyed();

    inspector.pruneDead(world);
    inspector.onWorldRestored();
    m_status = EditorStatus{"deleted " + std::to_string(removed) + " instance(s)", false};
    return true;
}

namespace {

// The shallowest instance that is an ancestor of, or equal to, every id.
//
// **Where a group goes.** Grouping four things from two branches has to put the
// container somewhere both of them can reach, and that is their common ancestor
// -- picking the first one's parent would silently move the other three into a
// branch nobody asked about.
[[nodiscard]] core::InstanceId commonParent(const scene::World& world, std::span<const core::InstanceId> ids,
                                            core::InstanceId root)
{
    core::InstanceId shared = core::InstanceId{};
    for (const core::InstanceId id : ids) {
        const core::InstanceId parent = world.parentOf(id);
        if (!parent.valid())
            continue;
        if (!shared.valid()) {
            shared = parent;
            continue;
        }
        if (shared == parent)
            continue;

        // Walk `shared` up until it covers `parent` too. Bounded by the tree's
        // depth, and `root` is the backstop for two branches that meet nowhere
        // -- which a world with more than one top-level tree can produce.
        core::InstanceId walk = shared;
        while (walk.valid() && !world.isAncestorOf(walk, parent))
            walk = world.parentOf(walk);
        shared = walk.valid() ? walk : root;
    }
    return shared;
}

} // namespace

bool Editor::groupSelection(scene::World& world, std::span<const core::InstanceId> ids, core::InstanceId root,
                            Inspector& inspector)
{
    if (ids.empty()) {
        m_status = EditorStatus{"select something to group", true};
        return false;
    }

    std::vector<core::InstanceId> ordered;
    orderByTree(world, root, ids, ordered);

    std::vector<core::InstanceId> movable;
    bool wantsModel = false;
    for (const core::InstanceId id : ordered) {
        if (!world.alive(id) || id == root || isEngineOwned(world, id, root))
            continue;
        // **A `Model` when anything has a transform.** A model has a pivot,
        // extents and a scale, all meaningless around four scripts -- and a
        // folder around four parts throws away the one thing grouping parts is
        // for.
        wantsModel = wantsModel || world.parts().find(id) != nullptr || world.models().find(id) != nullptr;
        movable.push_back(id);
    }

    if (movable.empty()) {
        m_status = EditorStatus{"nothing there can be grouped -- the world and its services stay where they are", true};
        return false;
    }

    const scene::ClassId containerClass = world.classes().findId(world.atoms().intern(wantsModel ? "Model" : "Folder"));
    if (containerClass == scene::InvalidClass) {
        m_status = EditorStatus{"this build has no class to group into", true};
        return false;
    }

    const core::InstanceId parent = commonParent(world, movable, root);

    // **Recorded before the create**, so one ctrl-Z takes the whole group back
    // rather than leaving an empty container behind.
    m_history.record(world, movable.size() == 1 ? "Group" : "Group " + std::to_string(movable.size()));

    const core::InstanceId container = world.create(containerClass);
    if (!container.valid()) {
        m_status = EditorStatus{"this build has no class to group into", true};
        return false;
    }
    world.setName(container, world.atoms().intern(wantsModel ? "Model" : "Folder"));
    (void)world.setParent(container, parent.valid() ? parent : root);

    core::usize moved = 0;
    for (const core::InstanceId id : movable) {
        // The container cannot be moved into itself, and neither can anything
        // ABOVE it -- which `commonParent` makes impossible by construction and
        // `setParent` refuses anyway. Counted rather than assumed.
        if (!world.setParent(id, container).has_value())
            ++moved;
    }

    inspector.pruneDead(world);
    inspector.onWorldRestored();
    // **The container, selected.** Grouping is a thing you do in order to then
    // move the group, so leaving the children selected would mean the next drag
    // undoes the reason you grouped them.
    inspector.select(container);
    inspector.reveal(container);
    m_status = EditorStatus{"grouped " + std::to_string(moved) + " instance(s)", false};
    return true;
}

bool Editor::ungroupSelection(scene::World& world, std::span<const core::InstanceId> ids, core::InstanceId root,
                              Inspector& inspector)
{
    if (ids.empty()) {
        m_status = EditorStatus{"select a group to take apart", true};
        return false;
    }

    std::vector<core::InstanceId> containers;
    for (const core::InstanceId id : ids) {
        // **Only something with children.** Ungrouping a part is not a thing,
        // and a verb that silently destroyed one would be the worst possible
        // reading of a key nobody meant to press.
        if (world.alive(id) && !isEngineOwned(world, id, root) && world.firstChild(id).valid())
            containers.push_back(id);
    }

    if (containers.empty()) {
        m_status = EditorStatus{"nothing selected has anything in it to take out", true};
        return false;
    }

    m_history.record(world, containers.size() == 1 ? "Ungroup" : "Ungroup " + std::to_string(containers.size()));

    std::vector<core::InstanceId> freed;
    for (const core::InstanceId container : containers) {
        const core::InstanceId parent = world.parentOf(container);

        // **Collected before any of them moves.** `firstChild`/`nextSibling` is
        // a live list, and reparenting while walking it drops every child after
        // the first -- which is the shape of bug that leaves three of five in a
        // container the editor then destroys.
        std::vector<core::InstanceId> children;
        for (core::InstanceId child = world.firstChild(container); child.valid(); child = world.nextSibling(child)) {
            children.push_back(child);
        }

        for (const core::InstanceId child : children) {
            if (!world.setParent(child, parent.valid() ? parent : root).has_value())
                freed.push_back(child);
        }

        // Only once it is empty. A container that kept a child the tree refused
        // to move is a container that still holds something, and destroying it
        // would take that something with it.
        if (!world.firstChild(container).valid())
            (void)world.destroy(container);
    }

    world.retireDestroyed();
    inspector.pruneDead(world);
    inspector.onWorldRestored();

    // What came out, selected -- because that is what somebody is now looking at
    // and what they are about to move.
    inspector.select(freed);
    if (!freed.empty())
        inspector.reveal(freed.front());

    m_status = EditorStatus{"took out " + std::to_string(freed.size()) + " instance(s)", false};
    return true;
}

bool Editor::duplicateInstances(scene::World& world, std::span<const core::InstanceId> ids, core::InstanceId root,
                                Inspector& inspector)
{
    if (ids.empty())
        return false;
    if (ids.size() == 1)
        return duplicateInstance(world, ids[0], root, inspector);

    std::vector<core::InstanceId> ordered;
    orderByTree(world, root, ids, ordered);

    std::vector<core::InstanceId> copyable;
    for (const core::InstanceId id : ordered) {
        if (!isEngineOwned(world, id, root) && world.parentOf(id).valid())
            copyable.push_back(id);
    }

    if (copyable.empty()) {
        m_status = EditorStatus{"nothing there can be duplicated", true};
        return false;
    }

    m_history.record(world, "Duplicate " + std::to_string(copyable.size()) + " instances");

    std::vector<core::InstanceId> copies;
    for (const core::InstanceId id : copyable) {
        const core::InstanceId copy = world.clone(id);
        if (!copy.valid())
            continue;
        (void)world.setParent(copy, world.parentOf(id));
        copies.push_back(copy);
    }

    // The copies, not the originals: the point of duplicating is to change what
    // came out, and a selection left on the source is a second click before
    // anything can be done to it.
    inspector.select(copies);
    if (!copies.empty())
        inspector.reveal(copies.front());
    m_status = EditorStatus{"duplicated " + std::to_string(copies.size()) + " instance(s)", false};
    return true;
}

bool Editor::renameInstance(scene::World& world, core::InstanceId id, core::InstanceId root, std::string_view name)
{
    if (!world.alive(id) || name.empty())
        return false;

    if (isEngineOwned(world, id, root)) {
        // **A script reaches a service by NAME** -- `game.Workspace` is a
        // lookup, not a keyword -- so renaming one breaks every line that does
        // it, in files nothing here can see.
        m_status = EditorStatus{"a service's name is how scripts find it, so it is not one to change", true};
        return false;
    }

    m_history.record(world, "Rename " + std::string(world.atoms().text(world.name(id))));
    world.setName(id, world.atoms().intern(name));
    m_status = EditorStatus{"renamed to " + std::string(name), false};
    return true;
}

void Editor::newScene(scene::World& world, Inspector& inspector)
{
    scene::clearScene(world);
    // See `load`: `clearScene` destroys, and a paused world never drains, so
    // without this the old scene lives on in the pools as unparented husks.
    world.retireDestroyed();

    // Undoing into a world that no longer exists is not undoing.
    m_history.clear();
    m_openScene.clear();
    // A scene nobody has touched yet. Whoever asked for this was asked about the
    // old one first, if there was anything to ask about.
    m_sceneDirty = false;
    inspector.select(core::InstanceId{});
    inspector.onWorldChanged();
    m_status = EditorStatus{"new scene -- untitled until you save it", false};
}

std::string Editor::normalizeScenePath(std::string_view typed)
{
    std::string path(typed);

    // Typed by a person into a box the dialog has already labelled `content/`,
    // so the prefix is the natural thing to type and the wrong thing to keep:
    // it resolves against the content root and makes `content/content/`. This
    // engine met that on its first real use (D068) and it left a scene nobody
    // could open beside one nobody meant to save.
    for (char& c : path) {
        if (c == '\\')
            c = '/';
    }
    while (!path.empty() && path.front() == '/')
        path.erase(path.begin());
    constexpr std::string_view kContentPrefix = "content/";
    while (path.compare(0, kContentPrefix.size(), kContentPrefix) == 0)
        path.erase(0, kContentPrefix.size());

    if (path.size() < kSceneExtension.size() ||
        path.compare(path.size() - kSceneExtension.size(), kSceneExtension.size(), kSceneExtension) != 0) {
        path += kSceneExtension;
    }
    return path;
}

bool Editor::sceneNameIsUsable(std::string_view typed) noexcept
{
    // A drive letter or a leading slash is a path out of the project, and `..`
    // is the same thing spelled to look like a name. Every segment has to be a
    // name the browser could have shown, which is the rule the New Folder box
    // already applies -- applied here too, because a Save box that accepts what
    // a New Folder box refuses is one rule with two answers.
    if (typed.empty() || typed.find(':') != std::string_view::npos)
        return false;

    std::size_t begin = 0;
    while (begin <= typed.size()) {
        const std::size_t end = typed.find('/', begin);
        const std::string_view segment = typed.substr(begin, end == std::string_view::npos ? end : end - begin);
        if (!ContentTree::isUsableName(segment))
            return false;
        if (end == std::string_view::npos)
            break;
        begin = end + 1;
    }
    return true;
}

bool Editor::saveSceneAs(scene::World& world, std::string_view relativePath)
{
    const std::string path = normalizeScenePath(relativePath);
    if (!sceneNameIsUsable(path)) {
        m_status = EditorStatus{"\"" + std::string(relativePath) + "\" is not a path inside content/", true};
        return false;
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
    m_history.clear();
    m_sceneDirty = false;
    // The status `load` set names the file; naming the scene is more useful,
    // because the browser is already showing the file.
    m_status = EditorStatus{"opened " + m_openScene, false};
    return true;
}

bool Editor::saveOpenScene(scene::World& world)
{
    if (m_openScene.empty()) {
        m_status = EditorStatus{"no scene is open -- open one from the content browser first", true};
        return false;
    }
    return save(world, m_content.root() / std::filesystem::path(m_openScene));
}

void Editor::reportImport(const ContentTree::ImportReport& report) noexcept
{
    if (report.imported.empty() && report.skipped.empty() && report.failed.empty() && report.companions.empty() &&
        report.missing.empty()) {
        return;
    }

    // **Counted, and the refusals named.** "Imported 3 files" is a sentence
    // nobody has to act on; "2 already here" is one they do, and the names are
    // what tells them which.
    std::string message = "imported " + std::to_string(report.imported.size()) + " file(s)";
    const auto listOf = [](const std::vector<std::string>& names) {
        std::string joined;
        for (const std::string& name : names) {
            if (!joined.empty())
                joined += ", ";
            joined += name;
        }
        return joined;
    };
    // **Counted apart**, because a person who dragged in one file and sees
    // "imported 7" would reasonably wonder what the other six are.
    if (!report.companions.empty())
        message += " and " + std::to_string(report.companions.size()) + " it needs";
    if (!report.skipped.empty())
        message += "; skipped " + listOf(report.skipped) + " (already here)";
    if (!report.failed.empty())
        message += "; could not read " + listOf(report.failed);
    // **The one that decides whether the model works.** A `.gltf` whose buffer
    // was not beside it imports perfectly and loads nothing, and this is the
    // only moment anybody can be told which file to go and find.
    if (!report.missing.empty())
        message += "; NOT beside it: " + listOf(report.missing) + " -- the model will not load without them";

    m_status = EditorStatus{message, !report.failed.empty() || !report.missing.empty()};
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

// How long the camera takes to reach what F asked it to frame. Long enough to
// read as a move rather than a cut, short enough that nobody waits for it.
constexpr core::f32 kFocusSeconds = 0.18f;

bool selectionBounds(const scene::World& world, std::span<const core::InstanceId> selection, core::DVec3& outCentre,
                     core::f64& outRadius)
{
    bool any = false;
    core::DVec3 lo{};
    core::DVec3 hi{};

    // Descendants included, so F on a `Model` frames the model. The walk is per
    // selected instance rather than over the world, so it costs what is
    // selected.
    std::vector<core::InstanceId> descendants;
    for (const core::InstanceId id : selection) {
        if (!id.valid() || !world.alive(id))
            continue;

        descendants.clear();
        world.collectDescendants(id, descendants);
        descendants.push_back(id);

        for (const core::InstanceId member : descendants) {
            const scene::PartComponent* part = world.parts().find(member);
            if (part == nullptr)
                continue;

            // The rotated box's axis-aligned extent, which is the same absolute
            // -value trick the partitioner uses: a turned crate's reach along
            // each world axis is its half-size dotted with the absolute row.
            const core::Mat3& r = part->cframe.rotation;
            const core::Vec3 h{part->size.x * 0.5f, part->size.y * 0.5f, part->size.z * 0.5f};
            const auto extent = [&](int axis) {
                return static_cast<core::f64>(std::abs(r.m[0][axis]) * h.x + std::abs(r.m[1][axis]) * h.y +
                                              std::abs(r.m[2][axis]) * h.z);
            };
            const core::DVec3 c = part->cframe.position;
            const core::DVec3 e{extent(0), extent(1), extent(2)};

            if (!any) {
                lo = {c.x - e.x, c.y - e.y, c.z - e.z};
                hi = {c.x + e.x, c.y + e.y, c.z + e.z};
                any = true;
                continue;
            }
            lo = {std::min(lo.x, c.x - e.x), std::min(lo.y, c.y - e.y), std::min(lo.z, c.z - e.z)};
            hi = {std::max(hi.x, c.x + e.x), std::max(hi.y, c.y + e.y), std::max(hi.z, c.z + e.z)};
        }
    }

    if (!any)
        return false;

    outCentre = {(lo.x + hi.x) * 0.5, (lo.y + hi.y) * 0.5, (lo.z + hi.z) * 0.5};
    const core::DVec3 half{(hi.x - lo.x) * 0.5, (hi.y - lo.y) * 0.5, (hi.z - lo.z) * 0.5};
    // The sphere around the box rather than the box, because the camera may be
    // looking at it from any angle and a half-width is only the right distance
    // from one of them.
    outRadius = std::sqrt(half.x * half.x + half.y * half.y + half.z * half.z);
    return true;
}

core::CFrameD framedCamera(const core::CFrameD& current, core::DVec3 centre, core::f64 radius, f32 fieldOfViewDegrees)
{
    // Never zero: a `Part` of no size, or a selection of one point, would put
    // the camera exactly on it.
    constexpr core::f64 kSmallest = 0.5;
    // Room around the thing, so it is framed rather than filling the panel edge
    // to edge.
    constexpr core::f64 kMargin = 1.35;

    const core::f64 half = static_cast<core::f64>(fieldOfViewDegrees) * 0.5 * 3.14159265358979323846 / 180.0;
    const core::f64 sine = std::sin(half);
    const core::f64 wanted = radius < kSmallest ? kSmallest : radius;
    const core::f64 distance = sine > 1e-6 ? (wanted * kMargin) / sine : wanted * 4.0;

    // `Mat3`'s columns are right, up and BACK, so backing off is +back.
    const core::Mat3& basis = current.rotation;
    const core::DVec3 back{static_cast<core::f64>(basis.m[2][0]), static_cast<core::f64>(basis.m[2][1]),
                           static_cast<core::f64>(basis.m[2][2])};

    core::CFrameD framed;
    framed.rotation = current.rotation;
    framed.position = {centre.x + back.x * distance, centre.y + back.y * distance, centre.z + back.z * distance};
    return framed;
}

void Editor::focusCamera(core::DVec3 centre, core::f64 radius) noexcept
{
    if (!m_cameraAdopted)
        return;
    m_focusTarget = framedCamera(m_cameraCFrame, centre, radius).position;
    m_focusRemaining = kFocusSeconds;
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

    // **Taking the controls cancels the focus.** A tool that kept sliding the
    // view after somebody started flying is a tool arguing with them, and the
    // person's input is always the more recent answer.
    if (m_focusRemaining > 0.0f) {
        if (lookDelta.x != 0.0f || lookDelta.y != 0.0f || move.x != 0.0f || move.y != 0.0f || move.z != 0.0f) {
            m_focusRemaining = 0.0f;
        }
        else {
            const f32 step = dt < m_focusRemaining ? dt : m_focusRemaining;
            const core::f64 t = m_focusRemaining > 0.0f ? static_cast<core::f64>(step / m_focusRemaining) : 1.0;
            m_cameraCFrame.position = {
                m_cameraCFrame.position.x + (m_focusTarget.x - m_cameraCFrame.position.x) * t,
                m_cameraCFrame.position.y + (m_focusTarget.y - m_cameraCFrame.position.y) * t,
                m_cameraCFrame.position.z + (m_focusTarget.z - m_cameraCFrame.position.z) * t,
            };
            m_focusRemaining -= step;
            return m_cameraCFrame;
        }
    }

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

// --- The manipulators -------------------------------------------------------

namespace {

// How many pixels long an arm is drawn, and therefore how big the whole gizmo
// is. Constant on screen: a handle that shrank into nothing as you flew away
// would be a handle you could not grab.
constexpr f32 kGizmoPixels = 90.0f;

[[nodiscard]] f32 snapTo(f32 value, f32 step) noexcept
{
    if (step <= 0.0f)
        return value;
    return std::round(value / step) * step;
}

} // namespace

void Editor::setGizmoMode(GizmoMode mode) noexcept
{
    // Not mid-drag. Changing what a drag MEANS half way through it is not
    // something a person can have meant, and the drag's recorded start would be
    // a start for the wrong operation.
    if (m_drag.has_value())
        return;
    m_gizmoMode = mode;
    m_preferencesDirty = true;
}

void Editor::setGizmoLocal(bool local) noexcept
{
    if (m_drag.has_value())
        return;
    m_gizmoLocal = local;
    m_preferencesDirty = true;
}

f32 Editor::snapStep(GizmoMode mode) const noexcept
{
    return m_snapStep[static_cast<core::usize>(mode)];
}

void Editor::setSnapStep(GizmoMode mode, f32 step) noexcept
{
    m_snapStep[static_cast<core::usize>(mode)] = step > 0.0f ? step : 0.0f;
    m_preferencesDirty = true;
}

namespace {

// Where the manipulator sits for an instance, in WORLD space, or nothing when
// the instance is not anywhere.
//
// **Four kinds, and each is located the way it is defined** (S5.2). A part is
// its own `CFrame`; a camera is too. An attachment's `CFrame` is relative to the
// part it is on, so the world one is the derived `WorldCFrame` the mirror keeps.
// A `Model` has no transform at all and is located by its PIVOT, which is what
// `PivotTo` moves and therefore the only point a gizmo on one could honestly be.
[[nodiscard]] std::optional<core::CFrameD> gizmoTransformOf(const scene::World& world, core::InstanceId id)
{
    if (const scene::PartComponent* part = world.parts().find(id); part != nullptr)
        return part->cframe;
    if (const scene::CameraComponent* camera = world.cameras().find(id); camera != nullptr)
        return camera->cframe;
    if (const scene::AttachmentComponent* attachment = world.attachments().find(id); attachment != nullptr)
        return attachment->worldCFrame;
    if (world.models().find(id) != nullptr)
        return scene::pivotOf(world, id);
    return std::nullopt;
}

} // namespace

// Puts one dragged instance at `after`, in world space, by whatever route its
// kind is transformed through (S5.2).
//
// **Through the inspector's queue in every case**, which is what keeps a gizmo
// drag one undo step and one safe point however many kinds are in the selection
// -- and what stops a `Model` needing its own history handling.
void Editor::applyDragTransform(scene::World& world, Inspector& inspector, core::usize index,
                                const core::CFrameD& after)
{
    const GizmoDrag& drag = *m_drag;
    const core::InstanceId id = drag.targets[index];
    const core::NameAtom cframeName = world.atoms().intern("CFrame");

    switch (drag.kinds[index]) {
    case DragKind::Attachment:
        // **Divided back through the parent**, because an `Attachment.CFrame` is
        // relative to the part it is on. Writing the world frame straight in
        // would move a bone by the part's own transform on top of the drag --
        // which on a character ten metres out is a bone ten metres away.
        inspector.enqueue(id, cframeName, scene::Value{core::inverse(drag.parents[index]) * after});
        return;

    case DragKind::Model: {
        // A `Model` has no transform, so moving it is moving everything under
        // it by the same delta -- which is exactly what `PivotTo` means and the
        // only thing that keeps the parts' relative layout.
        const core::CFrameD delta = after * core::inverse(drag.before[index]);
        std::vector<core::InstanceId> descendants;
        world.collectDescendants(id, descendants);
        for (const core::InstanceId descendant : descendants) {
            const scene::PartComponent* part = world.parts().find(descendant);
            if (part == nullptr)
                continue;
            inspector.enqueue(descendant, cframeName, scene::Value{delta * part->cframe});
        }
        return;
    }

    case DragKind::Part:
    case DragKind::Camera:
    default:
        // Both own a world `CFrame` outright, and both expose it as `CFrame`.
        inspector.enqueue(id, cframeName, scene::Value{after});
        return;
    }
}

std::optional<GizmoFrame> Editor::gizmoFrame(const scene::World& world, const Inspector& inspector) const
{
    if (!editing(m_run) || !m_hasCamera)
        return std::nullopt;

    const core::InstanceId primary = inspector.selection();
    if (!primary.valid() || !world.alive(primary))
        return std::nullopt;

    // **Whatever the primary IS, if it is somewhere** (S5.2). The manipulator
    // read the part pool and nothing else, so selecting a `Camera`, an
    // `Attachment` or a `Model` gave no gizmo at all -- and the two verbs an
    // editor has for moving something are the gizmo and typing numbers into the
    // grid.
    const std::optional<core::CFrameD> located = gizmoTransformOf(world, primary);
    if (!located.has_value())
        return std::nullopt;

    GizmoFrame frame;
    frame.transform.position = located->position;

    // **The middle of the selection, when asked for** (S5.17). Over one instance
    // this is the instance, so the control does nothing there; over forty it is
    // the difference between rotating a row of columns about the one you clicked
    // last and rotating it about itself.
    //
    // The mean of the transforms rather than the centre of the bounding box: a
    // box's centre moves when one part is scaled, so a gizmo on it would drift
    // during a scale drag -- and a handle that moves while you hold it is a
    // handle that does not track the pointer.
    if (m_gizmoOrigin == GizmoOrigin::Centre && inspector.selectionCount() > 1) {
        core::DVec3 sum{};
        core::usize counted = 0;
        for (const core::InstanceId id : inspector.selectionSet()) {
            if (const std::optional<core::CFrameD> at = gizmoTransformOf(world, id); at.has_value()) {
                sum = sum + at->position;
                ++counted;
            }
        }
        if (counted > 0) {
            const auto divisor = static_cast<core::f64>(counted);
            frame.transform.position = core::DVec3{sum.x / divisor, sum.y / divisor, sum.z / divisor};
        }
    }
    // World axes unless somebody asked for the part's own. A rotated crate is
    // unusable in world space and a wall is unusable in local, which is why this
    // is a choice rather than a decision made here.
    //
    // **Except for SCALE, which is always the part's own, and that is not a
    // preference.** A `Size` is three numbers in the part's own space -- there
    // is no world-space size to change -- so a world-axis scale arm on a turned
    // crate would point one way and grow it another. Unity's scale tool ignores
    // the same toggle for the same reason, and the alternative is a handle that
    // lies about what it does.
    const bool local = m_gizmoLocal || m_gizmoMode == GizmoMode::Scale;
    frame.transform.rotation = local ? located->rotation : core::Mat3{};
    frame.size = metresPerPixel(m_projection, m_viewport, m_cameraOrigin, frame.transform.position) * kGizmoPixels;

    if (!(frame.size > 0.0f))
        return std::nullopt;
    return frame;
}

std::optional<GizmoHandle> Editor::gizmoHandle() const noexcept
{
    if (m_drag.has_value())
        return m_drag->handle;
    return m_hover;
}

void Editor::setPointer(core::Vec2 pixelInViewport, bool pressed, bool down) noexcept
{
    m_pointer = pixelInViewport;
    m_pointerPressed = pressed;
    m_pointerDown = down;
}

// --- The brush (F1) ----------------------------------------------------------

void Editor::setTool(Tool tool) noexcept
{
    // Refused mid-stroke, exactly as `setGizmoMode` is refused mid-drag.
    if (m_stroke.has_value())
        return;
    m_tool = tool;
    m_preferencesDirty = true;
}

void Editor::setBrushRadius(f32 metres) noexcept
{
    // A radius of zero stamps nothing and a huge one would ask for a million
    // voxel writes in a frame, so both ends are clamped rather than refused --
    // a slider that stops is better than one that does nothing at its end.
    m_brush.radius = std::clamp(metres, 0.25f, 64.0f);
    m_preferencesDirty = true;
}

void Editor::setBrushSpacing(f32 fraction) noexcept
{
    // Below about a tenth the stamps overlap so heavily that a drag costs
    // hundreds of edits and looks identical; above one they stop overlapping at
    // all and a stroke becomes a dotted line.
    m_brush.spacing = std::clamp(fraction, 0.1f, 1.0f);
    m_preferencesDirty = true;
}

void Editor::setBrushStrength(f32 strength) noexcept
{
    // A strength of zero is a tool that does nothing and one of one is a tool
    // with no feel, so both ends are clamped inside rather than at the extremes.
    m_brush.strength = std::clamp(strength, 0.02f, 1.0f);
    m_preferencesDirty = true;
}

void Editor::setBrushMaterial(core::u8 material) noexcept
{
    // **Never zero.** Zero means erase to `fillBall`, and a material picker
    // whose first entry deleted the hillside would be the worst possible
    // reading of one shared convention -- erasing is the `erase` flag.
    m_brush.material = material == 0 ? 1 : material;
    m_preferencesDirty = true;
}

namespace {

// The terrain a click can reach: the one under the root the viewport is
// drawing.
//
// A walk of the root's children rather than the first entry of the pool,
// because a stamp stage has a world of its own and its terrain is not the
// host's -- and because a pool's first entry is an allocation order, which is
// not a fact anybody clicking on the ground has in mind.
[[nodiscard]] core::InstanceId terrainUnder(const scene::World& world, core::InstanceId root)
{
    if (!root.valid())
        return {};
    for (core::InstanceId child = world.firstChild(root); child.valid(); child = world.nextSibling(child)) {
        if (world.terrains().find(child) != nullptr)
            return child;
    }
    return {};
}

} // namespace

bool Editor::driveSculpt(scene::World& world, core::InstanceId root, Inspector& inspector)
{
    // **The aim is cleared first, every frame.** It is what the ring is drawn
    // from, and a stale one would leave a brush hanging in the air over a tool
    // that is no longer the brush.
    m_brushAim.reset();

    // **Looked for every frame, before the tool is even consulted.** The toolbar
    // shows the brush only when there is ground to use it on, and it is drawn
    // from this -- so the answer has to be current whichever tool is selected.
    const core::InstanceId terrainId = terrainUnder(world, root);
    scene::TerrainComponent* terrain = terrainId.valid() ? world.terrains().find(terrainId) : nullptr;
    m_hasTerrain = terrain != nullptr;

    if (m_tool == Tool::Select) {
        m_stroke.reset();
        return false;
    }

    if (terrain == nullptr) {
        m_stroke.reset();
        // **A tool with nothing to act on does not eat the click.** Somebody who
        // left the brush selected and clicked a part meant to select the part,
        // and a world with no terrain in it cannot have meant anything else.
        return false;
    }

    // **Against the frozen field while a stroke is running**, against the live
    // one while merely hovering -- so the ring follows the ground as it is, and
    // the stamps go where the stroke was aimed when it began. `Stroke::aimField`
    // says at length why.
    const PickRay ray = rayThrough(m_pointer);
    const asset::TerrainField& aimAt = m_stroke.has_value() ? m_stroke->aimField : terrain->field;
    // **Cast in the FIELD's space and answered in the world's.** A terrain can
    // be moved, and the field knows nothing about that -- the origin is applied
    // by its consumers rather than baked into every tile. So the ray goes in
    // with the origin subtracted and the hit comes back with it added, which is
    // the only place in the brush that has to know a terrain has a position.
    const core::DVec3 localOrigin{ray.origin.x - terrain->origin.x, ray.origin.y - terrain->origin.y,
                                  ray.origin.z - terrain->origin.z};
    m_brushAim = asset::raycastField(aimAt, localOrigin, ray.direction, BrushReach);
    if (m_brushAim.has_value()) {
        m_brushAim->position.x += terrain->origin.x;
        m_brushAim->position.y += terrain->origin.y;
        m_brushAim->position.z += terrain->origin.z;
    }
    else if (m_brushPlaneLock) {
        // **The plane, when the ray met no ground.** Without this a brush over
        // an empty field stamps nothing anywhere, which is a tool that does not
        // work rather than a tool with an edge case.
        //
        // Held at the stroke's own height while one is running, so extending a
        // hillside past its edge continues it instead of dropping to the
        // origin; at the terrain's origin otherwise.
        const double planeY = m_stroke.has_value() ? static_cast<double>(m_stroke->plane) : terrain->origin.y;
        // A ray parallel to the plane meets it nowhere, and one pointing away
        // meets it behind the camera. Both are "no aim" rather than a hit at a
        // negative distance.
        if (std::abs(static_cast<double>(ray.direction.y)) > 1e-6) {
            const double along = (planeY - ray.origin.y) / static_cast<double>(ray.direction.y);
            if (along > 0.0 && along <= BrushReach) {
                asset::TerrainHit hit;
                hit.position = core::DVec3{ray.origin.x + static_cast<double>(ray.direction.x) * along, planeY,
                                           ray.origin.z + static_cast<double>(ray.direction.z) * along};
                hit.normal = core::Vec3{0.0f, 1.0f, 0.0f};
                hit.distance = along;
                m_brushAim = hit;
            }
        }
    }

    // The button came up, or the stroke ran out of ground under it.
    if (m_stroke.has_value() && !m_pointerDown) {
        // **An editor says what it did.** The manipulator has no equivalent
        // because a drag's result is on screen; a stroke's is a number of edits
        // nobody can count by looking, and it is the cheapest evidence that the
        // brush did what the drag asked rather than one stamp or a thousand.
        m_lastStrokeStamps = m_stroke->stamps;
        m_stroke.reset();
        // The release belongs to the brush: without this the same click that
        // finished a stroke falls through and selects whatever is under it.
        m_pending.reset();
        return true;
    }

    if (!m_brushAim.has_value()) {
        // Over the sky. A stroke already running keeps running -- a drag that
        // crosses a gap in the ground is one stroke, not two -- but it stamps
        // nothing this frame.
        if (m_stroke.has_value())
            return true;
        return false;
    }

    if (!m_stroke.has_value()) {
        if (!m_pointerPressed)
            // Hovering. The ring is drawn from `m_brushAim`, and the pointer is
            // still the manipulator's and the pick's.
            return false;

        // **One undo step for the whole stroke**, recorded before the first
        // stamp writes anything. A terrain snapshot is a vector of shared
        // pointers to tiles nobody is about to change, so this costs a copy of
        // the index and not a copy of the ground (ADR 0067).
        Stroke stroke;
        stroke.terrain = terrainId;
        stroke.gesture = inspector.beginGesture();
        stroke.last = m_brushAim->position;
        stroke.aimField = terrain->field;
        // Where `Flatten` levels to. Captured once, here, so a drag across a
        // hillside levels it to where the stroke began rather than chasing its
        // own result downhill.
        stroke.plane = static_cast<f32>(m_brushAim->position.y);
        m_history.record(world, strokeLabel(m_tool, m_brush.op), stroke.gesture);
        m_stroke = stroke;

        applyBrushAt(*terrain, m_brushAim->position);
        m_stroke->stamps += 1;
        m_pending.reset();
        return true;
    }

    // **A drag in progress, walked in metres.** Every stamp between the last one
    // and where the pointer is now, so the stroke is a function of where the
    // pointer went rather than of how many frames it took to get there.
    //
    // `m_stroke->last` is the last STAMP, not last frame's pointer, and it only
    // moves when a stamp happens -- which is what makes a slow drag at 120 Hz
    // and a fast one at 30 leave the same ground rather than merely similar
    // ground. Advancing it every frame was the defect: at a high framerate every
    // step was shorter than one stamp, so the whole drag stamped once.
    const auto radius = static_cast<double>(m_brush.radius);
    const auto spacing = static_cast<double>(m_brush.spacing);
    if (!strokeAdvanced(m_stroke->last, m_brushAim->position, radius, spacing)) {
        m_pending.reset();
        return true;
    }

    // **The first entry IS the last stamp**, because the walk starts at `from`
    // -- so it is skipped rather than re-applied, and the anchor moves to the
    // last entry rather than to the pointer. The pointer is ahead of the last
    // stamp by the fraction of a step not yet walked, and that fraction is
    // carried into the next frame rather than dropped.
    const std::vector<core::DVec3> stamps = strokeStamps(m_stroke->last, m_brushAim->position, radius, spacing);
    for (core::usize at = 1; at < stamps.size(); ++at) {
        applyBrushAt(*terrain, stamps[at]);
        m_stroke->stamps += 1;
    }
    if (!stamps.empty())
        m_stroke->last = stamps.back();

    // The gesture is still the same one, so the undo step above is still the
    // step this belongs to; nothing more to record.
    m_pending.reset();
    return true;
}

void Editor::applyBrushAt(scene::TerrainComponent& terrain, core::DVec3 worldAt)
{
    // Back into the field's own space, for the reason the raycast above goes the
    // other way: the field has no idea where it sits.
    const core::DVec3 at{worldAt.x - terrain.origin.x, worldAt.y - terrain.origin.y, worldAt.z - terrain.origin.z};
    const auto radius = static_cast<double>(m_brush.radius);
    const bool box = m_brush.shape == BrushShape::Box;
    // A box the brush's width, so the two shapes cover the same ground and
    // switching between them is a change of edge rather than of size.
    const auto side = static_cast<f32>(radius * 2.0);
    const core::Vec3 extent{side, side, side};

    if (m_tool == Tool::Paint) {
        asset::paintBall(terrain.field, at, radius, m_brush.material);
    }
    else {
        switch (m_brush.op) {
        case BrushOp::Add:
            if (box)
                asset::fillBlock(terrain.field, at, extent, m_brush.material);
            else
                asset::fillBall(terrain.field, at, radius, m_brush.material);
            break;
        case BrushOp::Subtract:
            if (box)
                asset::fillBlock(terrain.field, at, extent, 0);
            else
                asset::fillBall(terrain.field, at, radius, 0);
            break;
        case BrushOp::Smooth:
            // **Round whichever shape is selected.** Smoothing walks columns
            // rather than filling a volume, and a square blur leaves visible
            // corners in ground that is supposed to be getting softer.
            asset::smoothBall(terrain.field, at, radius, m_brush.strength);
            break;
        case BrushOp::Flatten:
            asset::flattenBall(
                terrain.field, at, radius,
                static_cast<f32>((m_stroke.has_value() ? static_cast<double>(m_stroke->plane) : worldAt.y) -
                                 terrain.origin.y),
                m_brush.strength);
            break;
        }
    }
    // **Bumped here and nowhere else**, so the renderer and the physics mirror
    // both learn about a stamp through the one path they already read.
    terrain.fieldRevision += 1;
    m_sceneDirty = true;
}

// --- Making ground exist -----------------------------------------------------

core::InstanceId Editor::workspaceUnder(const scene::World& world, core::InstanceId root) const
{
    if (!root.valid()) {
        return {};
    }
    if (world.workspaces().find(root) != nullptr) {
        return root;
    }
    for (core::InstanceId child = world.firstChild(root); child.valid(); child = world.nextSibling(child)) {
        if (world.workspaces().find(child) != nullptr) {
            return child;
        }
    }
    return {};
}

core::InstanceId Editor::terrainIn(const scene::World& world, core::InstanceId root) const
{
    return terrainUnder(world, workspaceUnder(world, root));
}

core::InstanceId Editor::createTerrain(scene::World& world, core::InstanceId rootOrWorkspace, Inspector& inspector)
{
    // **Resolved here rather than trusted from the caller**, because the shell's
    // panels hold the Explorer's root and that is the `DataModel`.
    const core::InstanceId root = workspaceUnder(world, rootOrWorkspace);
    if (const core::InstanceId existing = terrainUnder(world, root); existing.valid()) {
        // Already there. Selecting it is more useful than refusing: somebody who
        // pressed the button wants to be looking at the terrain either way.
        inspector.select(existing);
        return existing;
    }
    if (!root.valid()) {
        return {};
    }

    const scene::ClassId terrainClass = world.classes().findId(world.atoms().intern("Terrain"));
    if (terrainClass == scene::InvalidClass) {
        return {};
    }

    m_history.record(world, "Create Terrain");
    const core::InstanceId id = world.create(terrainClass);
    if (!id.valid()) {
        return {};
    }
    world.setName(id, world.atoms().intern("Terrain"));
    if (world.setParent(id, root).has_value()) {
        world.destroy(id);
        return {};
    }

    inspector.select(id);
    m_sceneDirty = true;
    return id;
}

bool Editor::generateGround(scene::World& world, core::InstanceId rootOrWorkspace, Inspector& inspector, f32 size,
                            f32 height, core::u8 material)
{
    const core::InstanceId id = createTerrain(world, rootOrWorkspace, inspector);
    scene::TerrainComponent* terrain = id.valid() ? world.terrains().find(id) : nullptr;
    if (terrain == nullptr || !(size > 0.0f) || material == 0) {
        return false;
    }

    m_history.record(world, "Generate Ground");

    // **Written into the height layer directly rather than carved as a box.**
    //
    // The box was the obvious thing and it was two hundred times slower: making
    // ground that reaches the floor means a block reaching below it, which makes
    // every column's promotion examination walk the whole reserved range for a
    // result that is one number. A 128 m square took 225 milliseconds.
    //
    // `fillFlat` says the same thing in the encoding's own terms -- one column,
    // one height, one material -- and produces a field identical to what the box
    // produced.
    const core::DVec3 centre{terrain->origin.x, 0.0, terrain->origin.z};
    asset::fillFlat(terrain->field, core::DVec3{centre.x - terrain->origin.x, 0.0, centre.z - terrain->origin.z}, size,
                    height, material);

    terrain->fieldRevision += 1;
    m_sceneDirty = true;
    return true;
}

bool Editor::clearTerrain(scene::World& world, core::InstanceId root, Inspector& inspector)
{
    (void)inspector;
    const core::InstanceId id = terrainIn(world, root);
    scene::TerrainComponent* terrain = id.valid() ? world.terrains().find(id) : nullptr;
    if (terrain == nullptr) {
        return false;
    }
    if (terrain->field.tileCount() == 0 && terrain->field.brickCount() == 0) {
        // Nothing to clear. Refused rather than recorded, because a step that
        // undoes nothing eats a press of ctrl-Z.
        return false;
    }

    m_history.record(world, "Clear Terrain");
    terrain->field = asset::TerrainField(terrain->field.settings());
    terrain->fieldRevision += 1;
    m_sceneDirty = true;
    return true;
}

bool Editor::driveGizmo(scene::World& world, Inspector& inspector)
{
    const std::optional<GizmoFrame> frame = gizmoFrame(world, inspector);

    // The button came up, or the world stopped being editable under a drag.
    if (m_drag.has_value() && (!m_pointerDown || !frame.has_value())) {
        if (inspector.gesture() == m_drag->gesture)
            inspector.endGesture();
        m_drag.reset();
        // The release belongs to the gizmo too: without this the same click
        // that finished a drag would fall through and select whatever the
        // pointer ended up over.
        m_pending.reset();
        return true;
    }

    if (!frame.has_value()) {
        m_hover.reset();
        return false;
    }

    const PickRay ray = rayThrough(m_pointer);

    if (!m_drag.has_value()) {
        m_hover = pickGizmo(ray, *frame, m_gizmoMode);
        if (!m_pointerPressed || !m_hover.has_value())
            return false;

        GizmoDrag drag;
        drag.handle = *m_hover;
        drag.frame = *frame;

        if (m_gizmoMode == GizmoMode::Rotate) {
            const std::optional<f32> angle = gizmoDragAngle(ray, *frame, drag.handle);
            if (!angle.has_value())
                return false;
            drag.startAngle = *angle;
        }
        else {
            const std::optional<core::DVec3> point = gizmoDragPoint(ray, *frame, drag.handle);
            if (!point.has_value())
                return false;
            drag.startPoint = *point;
        }

        // Every selected instance that HAS a transform, and what it was. The
        // ones that do not are simply not moved rather than being an error: a
        // selection may hold a folder and a part, and dragging the part is a
        // thing somebody meant.
        for (const core::InstanceId id : inspector.selectionSet()) {
            const std::optional<core::CFrameD> at = gizmoTransformOf(world, id);
            if (!at.has_value())
                continue;

            const scene::PartComponent* part = world.parts().find(id);
            DragKind kind = DragKind::Part;
            core::CFrameD parent;
            if (part != nullptr) {
                kind = DragKind::Part;
            }
            else if (world.cameras().find(id) != nullptr) {
                kind = DragKind::Camera;
            }
            else if (world.attachments().find(id) != nullptr) {
                kind = DragKind::Attachment;
                // What the local `CFrame` is relative to. Derived from the two
                // frames the mirror already keeps rather than looked up through
                // the tree, so a bone under a bone is right for free.
                if (const scene::AttachmentComponent* attachment = world.attachments().find(id);
                    attachment != nullptr) {
                    parent = attachment->worldCFrame * core::inverse(attachment->cframe);
                }
            }
            else {
                kind = DragKind::Model;
            }

            drag.targets.push_back(id);
            drag.before.push_back(*at);
            drag.sizes.push_back(part != nullptr ? part->size : core::Vec3{1.0f, 1.0f, 1.0f});
            drag.kinds.push_back(kind);
            drag.parents.push_back(parent);
        }
        if (drag.targets.empty())
            return false;

        // One gesture for the whole drag, so it is one undo step however many
        // frames and however many instances it writes.
        drag.gesture = inspector.beginGesture();
        m_drag = std::move(drag);

        // The press was the gizmo's. Whatever pick the panel queued for the same
        // click is not somebody asking to select something else.
        m_pending.reset();
        return true;
    }

    // --- A drag in progress --------------------------------------------------
    GizmoDrag& drag = *m_drag;
    m_hover = drag.handle;

    // `CFrame` is interned by `applyDragTransform`, which is where every
    // transform write now goes -- four kinds write it four ways, and the one
    // place that knows which is the one that names the property.
    const core::NameAtom sizeName = world.atoms().intern("Size");

    if (m_gizmoMode == GizmoMode::Rotate) {
        const std::optional<f32> angle = gizmoDragAngle(ray, drag.frame, drag.handle);
        if (!angle.has_value())
            return true;

        f32 turned = *angle - drag.startAngle;
        // The short way round, so a ring crossing its own seam does not spin the
        // selection by a whole turn in one frame.
        while (turned > 3.14159265f)
            turned -= 6.28318531f;
        while (turned < -3.14159265f)
            turned += 6.28318531f;
        if (snapping())
            turned = snapTo(turned, snapStep(GizmoMode::Rotate) * 3.14159265f / 180.0f);

        Vec3 axes[3];
        {
            const core::Mat3& basis = drag.frame.transform.rotation;
            for (int index = 0; index < 3; ++index)
                axes[index] = core::normalize(Vec3{basis.m[index][0], basis.m[index][1], basis.m[index][2]});
        }
        const core::Mat3 turn = core::fromAxisAngle(axes[drag.handle.axis], turned);
        const core::DVec3 pivot = drag.frame.transform.position;

        for (core::usize index = 0; index < drag.targets.size(); ++index) {
            const core::CFrameD& before = drag.before[index];
            core::CFrameD after;
            after.rotation = turn * before.rotation;
            // Around the gizmo's pivot rather than each part's own, so a
            // selection turns as one body -- which is what "rotate these" means
            // and what turning each in place would not be.
            const Vec3 offset = core::toVec3(before.position - pivot);
            after.position = pivot + core::toDVec3(turn * offset);
            applyDragTransform(world, inspector, index, after);
        }
        return true;
    }

    const std::optional<core::DVec3> point = gizmoDragPoint(ray, drag.frame, drag.handle);
    if (!point.has_value())
        return true;

    core::DVec3 delta = *point - drag.startPoint;

    if (m_gizmoMode == GizmoMode::Translate) {
        if (snapping()) {
            // **Snapped in the GIZMO's frame, and this used to say so while
            // doing the opposite.** It quantised each WORLD component, which is
            // right for a world-axis drag and wrong for every other one: an arm
            // in local space points diagonally through the world, so rounding
            // x, y and z apart takes the motion OFF the arm. A person dragging
            // a turned crate saw it wander -- reported as "não segue exatamente
            // a reta, vai todo estranho para o sentido da seta".
            //
            // Expressed in the gizmo's own basis, snapped there and turned
            // back, an axis drag stays on its axis and a plane drag stays in
            // its plane, because a component that was zero rounds to zero. In
            // world mode the basis is the identity and this is exactly what it
            // was.
            const f32 step = snapStep(GizmoMode::Translate);
            Vec3 axes[3];
            const core::Mat3& basis = drag.frame.transform.rotation;
            for (int index = 0; index < 3; ++index)
                axes[index] = core::normalize(Vec3{basis.m[index][0], basis.m[index][1], basis.m[index][2]});

            const Vec3 local = core::toVec3(delta);
            const Vec3 snapped{snapTo(core::dot(local, axes[0]), step), snapTo(core::dot(local, axes[1]), step),
                               snapTo(core::dot(local, axes[2]), step)};
            delta = core::toDVec3(axes[0] * snapped.x + axes[1] * snapped.y + axes[2] * snapped.z);
        }
        for (core::usize index = 0; index < drag.targets.size(); ++index) {
            core::CFrameD after = drag.before[index];
            after.position = after.position + delta;
            applyDragTransform(world, inspector, index, after);
        }
        return true;
    }

    // Scale. The axis handle grows one dimension, the middle grows all three,
    // and the amount is the drag measured along the axis in gizmo sizes -- so a
    // drag of one arm's length doubles it whatever the part started at.
    Vec3 factor{1.0f, 1.0f, 1.0f};
    const f32 reach = drag.frame.size > 0.0f ? drag.frame.size : 1.0f;
    if (drag.handle.uniform) {
        const auto along = static_cast<f32>(delta.x + delta.y + delta.z) / reach;
        const f32 scale = 1.0f + along;
        factor = Vec3{scale, scale, scale};
    }
    else {
        Vec3 axes[3];
        const core::Mat3& basis = drag.frame.transform.rotation;
        for (int index = 0; index < 3; ++index)
            axes[index] = core::normalize(Vec3{basis.m[index][0], basis.m[index][1], basis.m[index][2]});
        const f32 along = core::dot(core::toVec3(delta), axes[drag.handle.axis]) / reach;
        const f32 scale = 1.0f + along;
        if (drag.handle.axis == 0)
            factor.x = scale;
        else if (drag.handle.axis == 1)
            factor.y = scale;
        else
            factor.z = scale;
    }

    for (core::usize index = 0; index < drag.targets.size(); ++index) {
        const Vec3 was = drag.sizes[index];
        Vec3 now{was.x * factor.x, was.y * factor.y, was.z * factor.z};
        if (snapping()) {
            const f32 step = snapStep(GizmoMode::Scale);
            now = Vec3{snapTo(now.x, step), snapTo(now.y, step), snapTo(now.z, step)};
        }
        // A part with no thickness has no faces and cannot be picked back, so a
        // drag through zero stops at the smallest thing that is still a thing
        // rather than turning the part inside out.
        constexpr f32 kMinimum = 0.01f;
        now = Vec3{now.x < kMinimum ? kMinimum : now.x, now.y < kMinimum ? kMinimum : now.y,
                   now.z < kMinimum ? kMinimum : now.z};
        inspector.enqueue(drag.targets[index], sizeName, scene::Value{now});
    }
    return true;
}

// The manipulator, drawn where the selection is.
//
// **Lines, because `DebugDraw` is a line list and stays one.** Its own header
// says solid shapes arrive with the milestone that needs them, and a manipulator
// does not: an arrow made of an arm and four barbs reads as an arrow, and a ring
// of segments reads as a ring. What a filled cone would buy is not worth a second
// pipeline in the debug path.
//
// **Camera-relative, like the selection outline beside it and for the same
// reason**: `DebugDraw::rebaseTo` subtracts in f32, so a submission in world
// coordinates quantises the absolute metre value before the camera comes off it
// -- about half a millimetre four kilometres out, on the one thing in the frame
// somebody is trying to place precisely.
void submitGizmo(const GizmoFrame& frame, GizmoMode mode, std::optional<GizmoHandle> active, core::DVec3 cameraOrigin,
                 render::DebugDraw& draw)
{
    using core::Vec3;

    const core::Mat3& basis = frame.transform.rotation;
    Vec3 axes[3];
    for (int index = 0; index < 3; ++index)
        axes[index] = core::normalize(Vec3{basis.m[index][0], basis.m[index][1], basis.m[index][2]});

    // The gizmo's centre in the space the debug pass draws in. Every point below
    // is this plus a metre offset, so the f64 subtraction happens once.
    const Vec3 centre = core::toVec3(frame.transform.position - cameraOrigin);
    const f32 size = frame.size;

    // X red, Y green, Z blue -- the convention `DebugDraw::axes` already uses
    // and the one every editor shares. The one under the pointer goes yellow,
    // which is the only feedback a manipulator needs and the one it cannot do
    // without.
    const render::DebugColor axisColor[3] = {
        render::DebugColor::fromLinear(0.90f, 0.25f, 0.25f),
        render::DebugColor::fromLinear(0.30f, 0.85f, 0.30f),
        render::DebugColor::fromLinear(0.30f, 0.50f, 0.95f),
    };
    const render::DebugColor hot = render::DebugColor::fromLinear(1.0f, 0.85f, 0.15f);
    const render::DebugColor white = render::DebugColor::fromLinear(0.95f, 0.95f, 0.95f);

    const auto lit = [active](core::u8 axis, bool plane, bool uniform) {
        return active.has_value() && active->axis == axis && active->plane == plane && active->uniform == uniform;
    };

    if (mode == GizmoMode::Rotate) {
        constexpr int kSegments = 48;
        for (core::u8 axis = 0; axis < 3; ++axis) {
            const Vec3 u = axes[(axis + 1) % 3] * size;
            const Vec3 v = axes[(axis + 2) % 3] * size;
            const render::DebugColor colour = lit(axis, false, false) ? hot : axisColor[axis];
            Vec3 previous = centre + u;
            for (int step = 1; step <= kSegments; ++step) {
                const f32 angle = 6.28318531f * static_cast<f32>(step) / static_cast<f32>(kSegments);
                const Vec3 point = centre + u * std::cos(angle) + v * std::sin(angle);
                draw.line(previous, point, colour);
                previous = point;
            }
        }
        return;
    }

    for (core::u8 axis = 0; axis < 3; ++axis) {
        const Vec3 direction = axes[axis];
        const Vec3 tip = centre + direction * size;
        const render::DebugColor colour = lit(axis, false, false) ? hot : axisColor[axis];

        // The arm starts clear of the centre handle, so the two do not draw over
        // each other and the gap says where one ends.
        draw.line(centre + direction * (size * 0.12f), tip, colour);

        const Vec3 side = axes[(axis + 1) % 3];
        const Vec3 other = axes[(axis + 2) % 3];
        if (mode == GizmoMode::Translate) {
            // Four barbs back from the tip: an arrowhead, in lines.
            const Vec3 back = tip - direction * (size * 0.16f);
            const f32 spread = size * 0.06f;
            draw.line(tip, back + side * spread, colour);
            draw.line(tip, back - side * spread, colour);
            draw.line(tip, back + other * spread, colour);
            draw.line(tip, back - other * spread, colour);

            // The plane square, at the corner between the OTHER two axes -- so
            // the one drawn in the XY corner moves in X and Y, and its handle is
            // named by the axis it does not move along.
            const core::u8 normal = (axis + 2) % 3;
            const Vec3 a = axes[(normal + 1) % 3];
            const Vec3 b = axes[(normal + 2) % 3];
            const f32 inner = size * 0.25f;
            const f32 outer = size * 0.55f;
            const render::DebugColor planeColour = lit(normal, true, false) ? hot : axisColor[normal];
            const Vec3 corner[4] = {
                centre + a * inner + b * inner,
                centre + a * outer + b * inner,
                centre + a * outer + b * outer,
                centre + a * inner + b * outer,
            };
            for (int edge = 0; edge < 4; ++edge)
                draw.line(corner[edge], corner[(edge + 1) % 4], planeColour);
        }
        else {
            // A small open box at the tip, which is what a scale handle looks
            // like everywhere and what tells it apart from an arrow at a glance.
            const f32 box = size * 0.05f;
            const Vec3 a = side * box;
            const Vec3 b = other * box;
            const Vec3 face[4] = {tip + a + b, tip + a - b, tip - a - b, tip - a + b};
            for (int edge = 0; edge < 4; ++edge)
                draw.line(face[edge], face[(edge + 1) % 4], colour);
        }
    }

    // The centre: uniform scale, or a screen-space drag for translate. Drawn as
    // a small box so it reads as a handle rather than as the place the arms
    // happen to meet.
    const f32 middle = size * 0.09f;
    const render::DebugColor centreColour = active.has_value() && active->uniform ? hot : white;
    draw.wireBox(centre, Vec3{middle, middle, middle}, centreColour);
}

std::optional<PickHit> Editor::resolvePick(const scene::World& world, core::InstanceId root,
                                           Inspector& inspector) noexcept
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

    const PickRay ray = rayThrough(request.pixel);
    std::optional<PickHit> hit = pickNearest(world, root, ray);

    // **What is not a part** (S5.1). Picking walked the part pool and nothing
    // else, so a `Camera`, a `PointLight`, an `Attachment` and a `Ragdoll` could
    // be reached only through the Explorer -- and the one you want to move is
    // the one you can see.
    //
    // A marker wins over geometry when the ray passes within its radius AND it
    // is not behind the solid hit: a marker is an aiming target rather than a
    // shape, so being smaller must not make it harder to click, and being behind
    // a wall must still make it unreachable.
    static std::vector<PickMarker> markers;
    collectPickMarkers(world, root, markers);
    if (const std::optional<PickHit> marker = pickMarker(
            markers, ray, kPickMarkerRadius, hit.has_value() ? hit->distance : std::numeric_limits<f32>::infinity());
        marker.has_value()) {
        hit = marker;
    }

    // **A click selects the thing, not the part it is made of** (S5.3). A
    // `Model` is something somebody made in order to move it as one, so
    // selecting the wheel of a car hands back the opposite of what the grouping
    // was for. Double-clicking drills in, and a click outside what was drilled
    // comes back out.
    if (hit.has_value()) {
        const core::InstanceId resolved = resolveSelection(world, root, hit->instance, m_drilled);

        if (request.opening) {
            // **Opening what was RESOLVED, not what was hit.** Double-clicking a
            // wheel opens the car it is part of; a second double-click then
            // opens whatever is inside that, one level per gesture.
            m_drilled = resolved != hit->instance ? resolved : hit->instance;
        }
        else if (m_drilled.valid() && !world.isAncestorOf(m_drilled, resolved) && resolved != m_drilled) {
            // Clicked outside what was open, so it is closed. Otherwise a drill
            // would be permanent and the rule would be off for the rest of the
            // session.
            m_drilled = core::InstanceId{};
        }

        hit = PickHit{resolveSelection(world, root, hit->instance, m_drilled), hit->distance};
    }
    else if (!request.additive) {
        // Empty space closes it too, for the same reason it deselects.
        m_drilled = core::InstanceId{};
    }

    // **Ctrl adds and removes; a plain click replaces.** The same gesture the
    // Explorer's rows use, because it is the same question asked of a different
    // surface -- and somebody who has ctrl-clicked four parts in the tree will
    // try it in the viewport within the minute.
    if (request.additive) {
        // Ctrl on empty space keeps what is selected. Deselecting everything is
        // what a plain click means, and a modifier that means "add" cannot also
        // mean "clear".
        if (hit.has_value()) {
            inspector.toggle(hit->instance);
            // Revealed whether the toggle added or removed it: either way the
            // row somebody just acted on is the one they want to see.
            inspector.reveal(hit->instance);
        }
        return hit;
    }

    // Clicking empty space deselects. See the header: leaving the last thing
    // selected is how somebody edits the object they believed they had let go
    // of.
    inspector.select(hit.has_value() ? hit->instance : core::InstanceId{});
    // **And the tree goes to it.** Clicking a part in the viewport and then
    // hunting for its row through four closed folders is the editor knowing
    // where something is and not saying. The reveal opens the way down and the
    // Explorer scrolls the row into view, which is what every tool with a
    // viewport and a tree does.
    if (hit.has_value())
        inspector.reveal(hit->instance);
    return hit;
}

} // namespace luaug::app
