#include <luaug/app/editor.h>
#include <luaug/core/json.h>
#include <luaug/core/json_writer.h>
#include <luaug/platform/file.h>
#include <luaug/render/debug_draw.h>
#include <luaug/rhi/device.h>
#include <luaug/scene/class_registry.h>
#include <luaug/scene/scene_file.h>
#include <luaug/scene/world.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace luaug::app {
using core::Vec3;
namespace {
// The seed a stage's world is built with. A constant, because nothing in a
// stage is simulated and nothing in it reads the generator -- and a seed drawn
// from anywhere else would make a stamp's bytes depend on when it was opened.
constexpr core::u64 kStageSeed = 0x5741'4D50u;
// The content tree's, for the same reason: nothing in it is simulated.
constexpr core::u64 kContentSeed = 0x434F'4E54u;

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
    // **The stamps the scene names, read through this editor's content root.**
    // `scene` is L3 and has no filesystem; a scene loaded without this opens
    // with its stamped instances missing and a count saying so.
    if (const std::optional<core::EngineError> error = scene::readScene(world, text, &report, stampSource());
        error.has_value()) {
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

void Editor::rememberState(const std::filesystem::path& stateDirectory) const
{
    // JSON of one field rather than the bare path, because the second thing an
    // editor wants to remember arrives sooner than anybody expects and a file
    // that is only a string has nowhere to put it. It arrived: folder colours.
    core::JsonWriter writer;
    writer.beginObject();
    writer.field("openScene", m_openScene);
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

    const core::JsonValue colors = document.root()["folderColors"];
    if (colors.type() != core::JsonType::Object)
        return;
    for (core::usize index = 0; index < colors.size(); ++index) {
        const std::string_view path = colors.keyAt(index);
        if (const std::optional<core::Color3> color = parseHexColor(colors[path].asString()); color.has_value())
            m_contentColors.emplace(std::string(path), *color);
    }
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
    m_stamp = StampSession{relative, root, false};

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

bool Editor::saveStamp()
{
    if (!m_stamp.open() || m_stage == nullptr || !m_stage->world().alive(m_stamp.root)) {
        m_status = EditorStatus{"there is no stamp open to save", true};
        return false;
    }

    scene::SceneIoReport report;
    const std::string text = scene::writeStamp(m_stage->world(), m_stamp.root, &report);
    const std::filesystem::path absolute = m_content.root() / std::filesystem::path(m_stamp.path);
    if (!platform::createDirectories(absolute.parent_path()) || !platform::writeTextFile(absolute, text)) {
        m_status = EditorStatus{"could not write " + m_stamp.path, true};
        return false;
    }

    m_stamp.dirty = false;
    m_status = EditorStatus{"saved " + m_stamp.path + " (" + std::to_string(report.instances) + " instance(s))", false};
    return true;
}

bool Editor::closeStamp(Inspector& inspector, bool save)
{
    if (!m_stamp.open())
        return false;

    const std::string closed = m_stamp.path;
    const bool wrote = save && saveStamp();

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
    if (m_cameraAdopted) {
        const core::Mat3& basis = m_cameraCFrame.rotation;
        const Vec3 forward{-basis.m[2][0], -basis.m[2][1], -basis.m[2][2]};
        constexpr f32 kSpawnDistance = 8.0f;
        core::CFrameD placed_at;
        placed_at.position = m_cameraCFrame.position + core::toDVec3(forward * kSpawnDistance);
        (void)world.setProperty(placed, world.atoms().intern("CFrame"), scene::Value{placed_at});
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

    // Undoing into a world that no longer exists is not undoing.
    m_history.clear();
    m_openScene.clear();
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

void Editor::openContentTree(scene::ClassRegistry& classes, scene::EnumRegistry& enums, core::AtomTable& atoms)
{
    m_content_ = ContentTreeWorld{};
    if (m_content.root().empty())
        return;

    m_content_.world = std::make_unique<scene::World>(classes, enums, atoms, kContentSeed);

    // A `Folder` for a root, because that is what it is: a place to put things.
    // Named `Content`, which is what the Explorer shows and what a path into it
    // starts with.
    const scene::ClassId folder = classes.findId(atoms.intern("Folder"));
    m_content_.root = m_content_.world->create(folder);
    if (!m_content_.root.valid()) {
        m_content_ = ContentTreeWorld{};
        return;
    }
    m_content_.world->setName(m_content_.root, atoms.intern("Content"));

    // **A project with no tree yet is not an error.** Every project written
    // before this had none, and one that greeted them with a message would be
    // wrong about all of them.
    std::string text;
    if (!platform::readTextFile(m_content.root() / std::filesystem::path(ContentTreeFile), text))
        return;

    // The file's root IS the content root, so its own children are what gets
    // built -- the same shape `readScene` gives the Workspace.
    scene::SceneIoReport report;
    const core::InstanceId built =
        scene::readStamp(*m_content_.world, text, m_content_.root, std::string(ContentTreeFile), &report);
    if (!built.valid()) {
        m_status = EditorStatus{"the content tree could not be read", true};
        return;
    }

    // What came back is one instance under the root; its children are the
    // tree. Reparented up and dropped, so the file's own root does not become a
    // second `Content` inside `Content`.
    std::vector<core::InstanceId> moved;
    for (core::InstanceId child = m_content_.world->firstChild(built); child.valid();
         child = m_content_.world->nextSibling(child)) {
        moved.push_back(child);
    }
    for (const core::InstanceId child : moved)
        (void)m_content_.world->setParent(child, m_content_.root);
    (void)m_content_.world->destroy(built);
    m_content_.world->retireDestroyed();
    m_content_.world->setStamp(m_content_.root, core::NameAtom{});
}

bool Editor::saveContentTree()
{
    if (m_content_.world == nullptr || !m_content_.root.valid() || m_content.root().empty())
        return false;

    scene::SceneIoReport report;
    const std::string text = scene::writeStamp(*m_content_.world, m_content_.root, &report);
    const std::filesystem::path path = m_content.root() / std::filesystem::path(ContentTreeFile);
    if (!platform::createDirectories(path.parent_path()) || !platform::writeTextFile(path, text)) {
        m_status = EditorStatus{"could not write the content tree", true};
        return false;
    }
    return true;
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

[[nodiscard]] core::DVec3 snapTo(core::DVec3 value, f32 step) noexcept
{
    if (step <= 0.0f)
        return value;
    const auto quantum = static_cast<core::f64>(step);
    return core::DVec3{std::round(value.x / quantum) * quantum, std::round(value.y / quantum) * quantum,
                       std::round(value.z / quantum) * quantum};
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
}

void Editor::setGizmoLocal(bool local) noexcept
{
    if (m_drag.has_value())
        return;
    m_gizmoLocal = local;
}

f32 Editor::snapStep(GizmoMode mode) const noexcept
{
    return m_snapStep[static_cast<core::usize>(mode)];
}

void Editor::setSnapStep(GizmoMode mode, f32 step) noexcept
{
    m_snapStep[static_cast<core::usize>(mode)] = step > 0.0f ? step : 0.0f;
}

std::optional<GizmoFrame> Editor::gizmoFrame(const scene::World& world, const Inspector& inspector) const
{
    if (!editing(m_run) || !m_hasCamera)
        return std::nullopt;

    const core::InstanceId primary = inspector.selection();
    if (!primary.valid() || !world.alive(primary))
        return std::nullopt;

    const scene::PartComponent* part = world.parts().find(primary);
    if (part == nullptr)
        return std::nullopt;

    GizmoFrame frame;
    frame.transform.position = part->cframe.position;
    // World axes unless somebody asked for the part's own. A rotated crate is
    // unusable in world space and a wall is unusable in local, which is why this
    // is a choice rather than a decision made here.
    frame.transform.rotation = m_gizmoLocal ? part->cframe.rotation : core::Mat3{};
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
            const scene::PartComponent* part = world.parts().find(id);
            if (part == nullptr)
                continue;
            drag.targets.push_back(id);
            drag.before.push_back(part->cframe);
            drag.sizes.push_back(part->size);
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

    const core::NameAtom cframeName = world.atoms().intern("CFrame");
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
            inspector.enqueue(drag.targets[index], cframeName, scene::Value{after});
        }
        return true;
    }

    const std::optional<core::DVec3> point = gizmoDragPoint(ray, drag.frame, drag.handle);
    if (!point.has_value())
        return true;

    core::DVec3 delta = *point - drag.startPoint;

    if (m_gizmoMode == GizmoMode::Translate) {
        if (snapping()) {
            // Snapped in the GIZMO's frame rather than the world's, so a local
            // drag lands on the part's own grid. In world space the two are the
            // same thing.
            delta = snapTo(delta, snapStep(GizmoMode::Translate));
        }
        for (core::usize index = 0; index < drag.targets.size(); ++index) {
            core::CFrameD after = drag.before[index];
            after.position = after.position + delta;
            inspector.enqueue(drag.targets[index], cframeName, scene::Value{after});
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

    // **Ctrl adds and removes; a plain click replaces.** The same gesture the
    // Explorer's rows use, because it is the same question asked of a different
    // surface -- and somebody who has ctrl-clicked four parts in the tree will
    // try it in the viewport within the minute.
    if (request.additive) {
        // Ctrl on empty space keeps what is selected. Deselecting everything is
        // what a plain click means, and a modifier that means "add" cannot also
        // mean "clear".
        if (hit.has_value())
            inspector.toggle(hit->instance);
        return hit;
    }

    // Clicking empty space deselects. See the header: leaving the last thing
    // selected is how somebody edits the object they believed they had let go
    // of.
    inspector.select(hit.has_value() ? hit->instance : core::InstanceId{});
    return hit;
}

} // namespace luaug::app
