#include <luaug/app/editor.h>
#include <luaug/core/json.h>
#include <luaug/core/json_writer.h>
#include <luaug/platform/file.h>
#include <luaug/rhi/device.h>
#include <luaug/scene/class_registry.h>
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
    // something a system made is not somewhere a person authors into.
    if (world.generated(parent)) {
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
    // to change it.
    inspector.select(made);

    m_status = EditorStatus{"added a " + std::string(world.atoms().text(descriptor->name)), false};
    (void)root;
    return true;
}

bool Editor::authorable(const scene::World& world, core::InstanceId id, core::InstanceId root)
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

    return !isEngineOwned(world, id, root);
}

bool Editor::reparent(scene::World& world, std::span<const core::InstanceId> ids, core::InstanceId newParent,
                      core::InstanceId root, Inspector& inspector)
{
    if (ids.empty() || !world.alive(newParent))
        return false;

    if (newParent != root && !authorable(world, newParent, root)) {
        m_status = EditorStatus{"nothing authored can live in that", true};
        return false;
    }

    // **Decided before anything is recorded**, so a drag that cannot move
    // anything leaves no undo step behind. A step that undoes nothing is worse
    // than no step: it eats a press of ctrl-Z and the second press takes back
    // something the person had stopped thinking about.
    //
    // Document order rather than click order, so the result is a function of
    // the SET. It is also the order that keeps a parent ahead of its own child,
    // which stops a move of both depending on which was reached first.
    std::vector<core::InstanceId> ordered;
    orderByTree(world, root, ids, ordered);

    std::vector<core::InstanceId> movable;
    core::usize refused = 0;
    for (const core::InstanceId id : ordered) {
        if (isEngineOwned(world, id, root)) {
            ++refused;
            continue;
        }
        // A cycle: onto itself, or into its own subtree. `World::setParent`
        // refuses both and this asks the SAME function rather than carrying a
        // second copy of the rule.
        if (id == newParent || world.isAncestorOf(id, newParent)) {
            ++refused;
            continue;
        }
        // Already there. Not a refusal -- a parent earlier in the walk has
        // taken its children with it, and re-parenting a child to where it
        // already is would only move it to the end of the sibling list.
        if (world.parentOf(id) == newParent)
            continue;
        movable.push_back(id);
    }

    if (movable.empty()) {
        m_status = EditorStatus{refused > 0 ? "nothing there can be moved into that" : "already there", refused > 0};
        return false;
    }

    m_history.record(world, movable.size() == 1 ? "Reparent" : "Reparent " + std::to_string(movable.size()));
    for (const core::InstanceId id : movable) {
        if (world.setParent(id, newParent).has_value())
            ++refused;
    }

    inspector.pruneDead(world);
    inspector.onWorldRestored();

    std::string message = "moved " + std::to_string(movable.size());
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

bool Editor::saveSceneAs(const scene::World& world, std::string_view relativePath)
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
