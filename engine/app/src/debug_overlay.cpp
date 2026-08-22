#include "luaug/app/debug_overlay.h"

#if LUAUG_DEBUG_UI

#include "luaug/app/backends.h"
#include "luaug/core/log.h"
#include "luaug/core/math.h"
#include "luaug/core/text_key.h"
#include "luaug/platform/sdl_interop.h"
#include "luaug/platform/window.h"
#include "luaug/rhi/device.h"
#include "luaug/rhi/sdlgpu_interop.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/enum_registry.h"
#include "luaug/scene/value.h"
#include "luaug/scene/world.h"

#include <array>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <imgui_internal.h>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>
#include <vector>

#endif

namespace luaug::app {

#if LUAUG_DEBUG_UI

namespace {

// Bound at construction, read while drawing. These sit beside ImGui's own
// process-wide context rather than inside the class for two reasons: that
// context already makes a second live overlay meaningless, and keeping them out
// of the header is what lets the header stay free of SDL and of a layout that
// changes with the build profile.
//
// Main-thread only, like everything else that touches SDL's event queue.
platform::Window* g_window = nullptr;
const rhi::IDevice* g_device = nullptr;

// Frame time, sampled and held; everything else read directly.
//
// The rule this panel started with -- "nothing here is sampled or estimated" --
// is right for the three static facts below and was wrong for the one value
// that changes every frame. Printed raw at 60 Hz it cannot be read at all: the
// human reported it twice, and could only read the panel by pausing a frame.
//
// A held mean is also MORE honest about what the engine costs than a number
// that trembles, because windowed frames present through the swapchain and the
// last digits are VSync and the compositor rather than engine work. The worst
// frame in the window is printed beside it, since a hitch is what a developer
// is actually looking for and a mean is precisely the statistic that hides one.
//
// `frame.index` is gone. A bare counter at 60 Hz is unreadable by construction
// and answers nothing the frame time does not -- a stalled engine stops drawing
// this panel at all. The number still exists where it is used: the capture
// stream names its frames, and the baseline collector counts them.
struct FrameTimeMeter
{
    // Four hertz, the slow end of a readable range rather than the fast one: a
    // four-digit number that changes faster than this is legible only in
    // principle, which is the defect being fixed.
    static constexpr double kWindowSeconds = 0.25;

    double elapsed = 0.0;
    double sum = 0.0;
    double worst = 0.0;
    unsigned frames = 0;

    // What is displayed, replaced only when a window closes.
    double meanMs = 0.0;
    double worstMs = 0.0;
    double perSecond = 0.0;
    bool primed = false;

    void accumulate(double renderDt) noexcept
    {
        // The first frame has no previous one to measure against. Its zero is
        // kept out of the mean rather than divided by, which is the same guard
        // the raw print needed and for the same reason.
        if (renderDt > 0.0) {
            sum += renderDt;
            worst = renderDt > worst ? renderDt : worst;
            ++frames;
        }

        elapsed += renderDt;
        if (elapsed < kWindowSeconds || frames == 0)
            return;

        const double mean = sum / static_cast<double>(frames);
        meanMs = mean * 1000.0;
        worstMs = worst * 1000.0;
        perSecond = 1.0 / mean;
        primed = true;

        elapsed = 0.0;
        sum = 0.0;
        worst = 0.0;
        frames = 0;
    }
};

// Accumulates only while the panel is drawing, which is what makes the window
// it reports the window it displayed.
FrameTimeMeter g_frameTime;

// Three facts the host already knows, plus the sampled frame time above.
void drawStats(const Frame& frame)
{
    g_frameTime.accumulate(frame.renderDt);

    // Dashes rather than a made-up 0.00 before the first window closes: a
    // quarter second of "no measurement yet" is honest and 0.00 ms is not.
    if (g_frameTime.primed) {
        ImGui::Text("%.2f ms (%.0f fps)  worst %.2f ms", g_frameTime.meanMs, g_frameTime.perSecond,
                    g_frameTime.worstMs);
    }
    else {
        ImGui::TextUnformatted("-- ms (-- fps)");
    }

    const std::string_view backend = backendName(g_device->backend());
    ImGui::Text("backend %.*s", static_cast<int>(backend.size()), backend.data());

    const platform::WindowSize size = platform::windowPixelSize(*g_window);
    ImGui::Text("drawable %d x %d", size.width, size.height);
}

// Reused across frames rather than rebuilt: the panel fills these once per
// frame for as long as it is open, and a debug overlay that allocates a whole
// tree every frame is a profile artefact somebody eventually has to explain.
std::vector<TreeRow> g_rows;
// The rows a person could actually see if they scrolled: `g_rows` minus every
// subtree under a closed ancestor. Computed before anything is drawn, because a
// clipper needs to know how many rows exist before it decides which to draw.
std::vector<TreeRow> g_visible;
// Which instances are expanded, and which have been seen at all. Held here
// rather than in ImGui's own tree state, because a clipped row's widget never
// runs and therefore has no state to hold.
std::unordered_set<core::u32> g_open;
std::unordered_set<core::u32> g_openKnown;
// Which world the two sets above describe. They are keyed by instance INDEX and
// indices are recycled, so carrying them across a world would let a new instance
// inherit whether a dead one was expanded.
core::u64 g_explorerWorld = 0;
// Whether the right-drag in progress BEGAN over the viewport image. Latched on
// the press, because that is the only moment the question can be answered: once
// the pointer is held it stops reporting a position, and asking afterwards lets
// a right-click in any panel become a camera turn.
bool g_lookLatched = false;
std::vector<const scene::PropertyDesc*> g_properties;

// The instance tree, virtualised.
//
// **Every row used to be drawn every frame**, which on the flagship is 4,300
// `TreeNodeEx` calls sixty times a second for a panel showing thirty of them.
// The content browser beside this one clips, and applying that standard to one
// panel and not the other was an inconsistency rather than a decision.
//
// Clipping a TREE is not clipping a list, and the difference is the reason this
// is longer than it was. A clipper skips rows, and `TreeNodeEx` keeps its own
// open/closed state -- so a row nobody drew has no state, and its children would
// not know whether to appear. So the open set is held HERE, keyed by instance,
// the visible rows are computed before anything is drawn, and the clipper runs
// over that flat list. Which is what a virtualised tree view is.
// `commands` and `dialogs` are null in the F3 overlay, and that is the
// distinction rather than a convenience: **the overlay inspects and the editor
// edits.** Offering Delete over a running game would be offering an edit whose
// only result is a world nobody can put back.
void drawExplorer(scene::World& world, core::InstanceId root, Inspector& inspector, EditorCommands* commands,
                  EditorDialogs* dialogs)
{
    collectTree(world, root, g_rows);

    // **A new world opens collapsed.** Loading a scene, starting a new one or
    // stopping a play session all arrive here as a world nobody has expanded
    // anything in yet -- and a tree that remembered would be showing somebody
    // the shape of the scene they just closed.
    if (g_explorerWorld != inspector.worldGeneration()) {
        g_explorerWorld = inspector.worldGeneration();
        g_open.clear();
        g_openKnown.clear();
    }

    // Preorder, so an ancestor is always decided before its descendants. A row
    // is visible when every ancestor above it is open; `hiddenBelow` remembers
    // the shallowest closed depth rather than re-walking upwards per row.
    g_visible.clear();
    u32 hiddenBelow = std::numeric_limits<u32>::max();
    for (const TreeRow& row : g_rows) {
        if (row.depth > hiddenBelow)
            continue;
        hiddenBelow = std::numeric_limits<u32>::max();

        g_visible.push_back(row);

        const bool hasChildren = world.childCount(row.id) > 0;
        // The services under `game` are what anyone opening this wants to see;
        // deeper than that is a project's own tree and is its business. Seeded
        // once per instance rather than every frame, so collapsing one stays
        // collapsed.
        if (hasChildren && !g_openKnown.contains(row.id.index)) {
            g_openKnown.insert(row.id.index);
            // **The root only.** Its children are the services, which is the
            // list somebody wants on opening; what is INSIDE them is the scene,
            // and showing all of it means scrolling past a world to find the
            // thing you came for.
            if (row.depth == 0)
                g_open.insert(row.id.index);
        }
        if (hasChildren && !g_open.contains(row.id.index))
            hiddenBelow = row.depth;
    }

    const float indentSpacing = ImGui::GetStyle().IndentSpacing;

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(g_visible.size()));
    while (clipper.Step()) {
        for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
            const TreeRow& row = g_visible[static_cast<std::size_t>(index)];
            const bool hasChildren = world.childCount(row.id) > 0;

            const float indent = static_cast<float>(row.depth) * indentSpacing;
            if (indent > 0.0f)
                ImGui::Indent(indent);

            ImGui::PushID(static_cast<int>(row.id.index));

            // The arrow is its own control rather than part of the row, because
            // opening a thing and selecting it are different intentions and a
            // tree that conflates them makes browsing destructive.
            if (hasChildren) {
                if (ImGui::ArrowButton("toggle", g_open.contains(row.id.index) ? ImGuiDir_Down : ImGuiDir_Right)) {
                    if (!g_open.insert(row.id.index).second)
                        g_open.erase(row.id.index);
                }
            }
            else {
                ImGui::Dummy(ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
            }
            ImGui::SameLine();

            const std::string_view instanceName = world.atoms().text(world.name(row.id));
            const scene::ClassDescriptor* classDescriptor = world.classes().find(world.classOf(row.id));
            const std::string_view className =
                classDescriptor != nullptr ? world.atoms().text(classDescriptor->name) : std::string_view("?");

            char label[192];
            (void)std::snprintf(label, sizeof(label), "%.*s  (%.*s)", static_cast<int>(instanceName.size()),
                                instanceName.data(), static_cast<int>(className.size()), className.data());

            if (ImGui::Selectable(label, inspector.isSelected(row.id))) {
                // Ctrl adds and removes, shift takes the run from the primary
                // to here, a plain click replaces. The range is taken over
                // `g_visible` rather than over the whole tree, because a range
                // somebody drew with the mouse across a collapsed branch would
                // pick up rows they cannot see.
                const ImGuiIO& io = ImGui::GetIO();
                if (io.KeyCtrl) {
                    inspector.toggle(row.id);
                }
                else if (io.KeyShift && inspector.selection().valid()) {
                    selectVisibleRange(inspector, g_visible, inspector.selection(), row.id);
                }
                else {
                    inspector.select(row.id);
                }
            }

            // **Right-clicking selects first, unless this row is already part
            // of the selection.** A menu that acted on whatever was selected
            // before would delete the wrong thing the first time somebody
            // right-clicked without looking -- and one that replaced the
            // selection would throw away the four things they had just picked
            // in order to act on one of them.
            if (commands != nullptr && dialogs != nullptr && ImGui::BeginPopupContextItem("row-menu")) {
                if (!inspector.isSelected(row.id))
                    inspector.select(row.id);

                const bool engineOwned = Editor::isEngineOwned(world, row.id, root);
                if (ImGui::MenuItem("Rename...", nullptr, false, !engineOwned)) {
                    dialogs->renameTarget = row.id;
                    dialogs->renameContentPath.clear();
                    dialogs->renameSeed = std::string(instanceName);
                    dialogs->renameInstance = true;
                }
                // Greyed rather than refused afterwards. The rule lives in
                // `Editor` because it is a rule about the world; the menu
                // reflects it so nobody presses a thing that cannot happen.
                if (ImGui::MenuItem("Duplicate", nullptr, false, !engineOwned))
                    commands->duplicateInstance = row.id;
                ImGui::Separator();
                if (ImGui::MenuItem("Delete", nullptr, false, !engineOwned))
                    commands->deleteInstance = row.id;
                ImGui::EndPopup();
            }

            ImGui::PopID();

            if (indent > 0.0f)
                ImGui::Unindent(indent);
        }
    }
}

// One widget per `ValueType` and no code per class (Decision 16): everything
// this needs arrives in the `PropertyDesc` it is handed.
//
// Nothing here writes. Every branch that accepts an edit enqueues it, and the
// queue drains at the next FrameStart (Decision 15) through
// `World::setProperty` and nothing else (Decision 14).
void drawEditor(scene::World& world, Inspector& inspector, core::InstanceId id, const scene::PropertyDesc& descriptor)
{
    const std::optional<scene::Value> current = world.getProperty(id, descriptor.name);
    if (!current.has_value()) {
        // The class declares the property and the world cannot read it: a null
        // getter. Shown rather than skipped, because a complete view of the
        // descriptor tables is the entire claim this panel makes.
        ImGui::TextUnformatted("<unreadable>");
        return;
    }

    // `editorFor` answers from the DECLARED type, and the variant holds what the
    // property actually has right now. Those disagree whenever a value is absent
    // -- `scene::Value`'s own comment names the two cases, an unset attribute and
    // a nil Instance reference -- and every branch below reaches for its
    // alternative with `std::get`, which throws rather than returning.
    //
    // Guarded here rather than in each branch: there are eight of them, they all
    // have this shape, and an absent value has no editor whatever its type says.
    // Found by a human clicking `go` on `RunService.Parent`, which selects the
    // DataModel -- the one instance in the world whose own Parent is nil -- and
    // took the host down with an uncaught `std::bad_variant_access`.
    if (std::holds_alternative<std::monostate>(*current)) {
        ImGui::TextUnformatted("nil");
        return;
    }

    const EditorKind kind = editorFor(descriptor.type);
    ImGui::PushID(static_cast<int>(descriptor.name.id));
    ImGui::SetNextItemWidth(-FLT_MIN);

    // Handled before the disabled block, because its one interaction is a
    // selection rather than a write -- following a reference is how you reach
    // an instance the tree has collapsed away.
    if (kind == EditorKind::InstanceRef) {
        const core::InstanceId reference = std::get<core::InstanceId>(*current);
        const std::string text = formatValue(world, *current);
        ImGui::TextUnformatted(text.c_str());
        if (reference.valid() && world.alive(reference)) {
            ImGui::SameLine();
            if (ImGui::SmallButton("go"))
                inspector.select(reference);
        }
        ImGui::PopID();
        return;
    }

    // `readOnly` is honoured HERE and not only by the setter: a field that
    // takes a drag the world then refuses is a UI making a claim it cannot
    // keep, and the refusal arrives a frame later with nothing attaching it to
    // the gesture that caused it.
    const bool locked = !editable(descriptor);
    if (locked)
        ImGui::BeginDisabled();

    switch (kind) {
    case EditorKind::Checkbox: {
        bool value = std::get<bool>(*current);
        if (ImGui::Checkbox("##value", &value))
            inspector.enqueue(id, descriptor.name, scene::Value{value});
        break;
    }
    case EditorKind::Number: {
        f64 value = std::get<f64>(*current);
        if (ImGui::DragScalar("##value", ImGuiDataType_Double, &value, 0.01f, nullptr, nullptr, "%.4f"))
            inspector.enqueue(id, descriptor.name, scene::Value{value});
        break;
    }
    case EditorKind::Text: {
        const std::string& text = std::get<std::string>(*current);
        char buffer[256]{};
        if (text.size() + 1 > sizeof(buffer)) {
            // Editing through a buffer that cannot hold the value would write a
            // truncated string back on the first Enter. Shown, not offered.
            ImGui::TextUnformatted(text.c_str());
            break;
        }
        std::snprintf(buffer, sizeof(buffer), "%s", text.c_str());
        if (ImGui::InputText("##value", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue))
            inspector.enqueue(id, descriptor.name, scene::Value{std::string(buffer)});
        break;
    }
    case EditorKind::Vector3: {
        const core::Vec3 value = std::get<core::Vec3>(*current);
        float components[3]{value.x, value.y, value.z};
        if (ImGui::DragFloat3("##value", components, 0.01f))
            inspector.enqueue(id, descriptor.name,
                              scene::Value{core::Vec3{components[0], components[1], components[2]}});
        break;
    }
    case EditorKind::CFrame: {
        core::CFrameD value = std::get<core::CFrameD>(*current);
        f64 position[3]{value.position.x, value.position.y, value.position.z};
        if (ImGui::DragScalarN("##value", ImGuiDataType_Double, position, 3, 0.01f, nullptr, nullptr, "%.3f")) {
            value.position = core::DVec3{position[0], position[1], position[2]};
            inspector.enqueue(id, descriptor.name, scene::Value{value});
        }
        // The basis is shown and never edited. A 3x3 rotation has no honest
        // widget, and round-tripping it through Euler angles would rewrite the
        // matrix on every frame the panel is open -- a property-changed fire
        // per frame for a value nobody touched.
        for (int axis = 0; axis < 3; ++axis) {
            ImGui::Text("%.3f %.3f %.3f", static_cast<f64>(value.rotation.m[axis][0]),
                        static_cast<f64>(value.rotation.m[axis][1]), static_cast<f64>(value.rotation.m[axis][2]));
        }
        break;
    }
    case EditorKind::Color: {
        const core::Color3 value = std::get<core::Color3>(*current);
        float components[3]{value.r, value.g, value.b};
        // Float and HDR because api-design.md 2.3 leaves the range open: a
        // picker that clamped to [0, 1] would silently rewrite a light's
        // intensity the first time anyone looked at it.
        if (ImGui::ColorEdit3("##value", components, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
            inspector.enqueue(id, descriptor.name,
                              scene::Value{core::Color3{components[0], components[1], components[2]}});
        break;
    }
    case EditorKind::Vector2: {
        const core::Vec2 value = std::get<core::Vec2>(*current);
        float components[2]{value.x, value.y};
        if (ImGui::DragFloat2("##value", components, 0.5f))
            inspector.enqueue(id, descriptor.name, scene::Value{core::Vec2{components[0], components[1]}});
        break;
    }
    case EditorKind::UDim: {
        const core::UDim value = std::get<core::UDim>(*current);
        float scale = value.scale;
        float offset = value.offset;
        // Two widgets rather than a DragFloat2, because the two halves are not
        // the same quantity: a scale moves in hundredths of a parent and an
        // offset moves in pixels, and one shared step makes one of them useless.
        bool changed = ImGui::DragFloat("##scale", &scale, 0.01f);
        changed = ImGui::DragFloat("##offset", &offset, 1.0f) || changed;
        if (changed)
            inspector.enqueue(id, descriptor.name, scene::Value{core::UDim{scale, offset}});
        break;
    }
    case EditorKind::UDim2: {
        const core::UDim2 value = std::get<core::UDim2>(*current);
        float scales[2]{value.x.scale, value.y.scale};
        float offsets[2]{value.x.offset, value.y.offset};
        bool changed = ImGui::DragFloat2("##scale", scales, 0.01f);
        changed = ImGui::DragFloat2("##offset", offsets, 1.0f) || changed;
        if (changed) {
            inspector.enqueue(
                id, descriptor.name,
                scene::Value{core::UDim2{core::UDim{scales[0], offsets[0]}, core::UDim{scales[1], offsets[1]}}});
        }
        break;
    }
    case EditorKind::Rect: {
        const core::Rect value = std::get<core::Rect>(*current);
        float components[4]{value.min.x, value.min.y, value.max.x, value.max.y};
        if (ImGui::DragFloat4("##value", components, 1.0f)) {
            inspector.enqueue(id, descriptor.name,
                              scene::Value{core::Rect{core::Vec2{components[0], components[1]},
                                                      core::Vec2{components[2], components[3]}}});
        }
        break;
    }
    case EditorKind::EnumCombo: {
        const scene::EnumValue value = std::get<scene::EnumValue>(*current);

        // The domain comes from the DESCRIPTOR, not from the value in the field.
        // Reading it off the value made the combo depend on the instance being
        // there and holding something -- an unset enum property offered no items
        // at all, and there was no way to ask what a property accepts without
        // creating one first. `enumDomainOf` answers from the class.
        //
        // The value's own enum is the fallback and not the source: a hand-built
        // registry (the fixtures) may declare a property without naming its
        // enum, and a field that then rendered nothing would be a regression
        // dressed as a refactor.
        scene::EnumId domain = enumDomainOf(world.enums(), descriptor);
        if (domain == scene::InvalidEnum)
            domain = value.enumId;

        const scene::EnumDescriptor* enumDescriptor = world.enums().find(domain);
        const std::string preview = formatValue(world, *current);
        if (enumDescriptor == nullptr) {
            ImGui::TextUnformatted(preview.c_str());
            break;
        }
        if (ImGui::BeginCombo("##value", preview.c_str())) {
            // Declaration order, which is `GetEnumItems`'s documented order and
            // therefore not something a panel gets to re-sort either.
            for (const scene::EnumItemDesc& item : enumDescriptor->items) {
                const std::string itemName(world.atoms().text(item.name));
                const bool selected = domain == value.enumId && item.value == value.value;
                if (ImGui::Selectable(itemName.c_str(), selected))
                    inspector.enqueue(id, descriptor.name, scene::Value{scene::EnumValue{domain, item.value}});
            }
            ImGui::EndCombo();
        }
        break;
    }
    case EditorKind::InstanceRef:
    case EditorKind::ReadOnlyText: {
        // The floor every `ValueType` falls back to, so that one with no editor
        // of its own is still inspectable rather than absent (M4 brief,
        // entering risk 6).
        const std::string text = formatValue(world, *current);
        ImGui::TextUnformatted(text.c_str());
        break;
    }
    }

    if (locked)
        ImGui::EndDisabled();

    ImGui::PopID();
}

// The gesture this panel currently owns, or zero. File-static for the same
// reason the row buffers above are: the panel is one function called once a
// frame, and its state between frames has nowhere else to live.
core::u64 g_propertyGesture = 0;

void drawProperties(scene::World& world, Inspector& inspector)
{
    const core::InstanceId selected = inspector.selection();
    if (!selected.valid() || !world.alive(selected)) {
        ImGui::TextUnformatted("nothing selected");
        return;
    }

    // One loop over the descriptor tables. There is no switch on a class name
    // anywhere below this line, which is Decision 16's whole claim.
    collectProperties(world.classes(), world.classOf(selected), g_properties);

    if (ImGui::BeginTable("properties", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("property", ImGuiTableColumnFlags_WidthStretch, 0.45f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.55f);

        for (const scene::PropertyDesc* descriptor : g_properties) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            const std::string_view propertyName = world.atoms().text(descriptor->name);
            ImGui::Text("%.*s", static_cast<int>(propertyName.size()), propertyName.data());

            // The IDL's own prose for this property, which now rides on the
            // descriptor rather than staying in a file nothing at runtime reads
            // (`class_registry.h` says why it is prose and not a catalog key).
            // Wrapped, because these are paragraphs and an unwrapped tooltip is
            // one line as wide as the sentence.
            if (descriptor->doc[0] != 0 && ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
                ImGui::TextUnformatted(descriptor->doc);
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }

            if (const char* tag = propertyTag(*descriptor); tag != nullptr) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", tag);
                // The tooltip carries the part a three-character tag cannot: an
                // inert property is not broken and not read-only, it is waiting
                // for the milestone that renders it.
                if (descriptor->inert && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("stored and read back faithfully; nothing in this build acts on it yet");
                }
            }

            ImGui::TableSetColumnIndex(1);
            drawEditor(world, inspector, selected, *descriptor);
        }
        ImGui::EndTable();
    }

    // **A drag in this panel is ONE edit**, and this is what tells the undo
    // stack so. ImGui keeps exactly one item active at a time, so "something in
    // this window is being held" is the whole of the question -- which is why
    // this is four lines here rather than a pair of calls repeated through
    // twelve widget branches, three of which draw more than one widget and
    // would each have got it subtly wrong.
    //
    // The panel closes only the gesture it opened. The manipulators open their
    // own, and a properties panel that ended somebody else's drag because
    // nothing in it was focused would be worse than not tracking one at all.
    const bool holding = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsAnyItemActive();
    if (holding && g_propertyGesture == 0) {
        g_propertyGesture = inspector.beginGesture();
    }
    else if (!holding && g_propertyGesture != 0) {
        if (inspector.gesture() == g_propertyGesture)
            inspector.endGesture();
        g_propertyGesture = 0;
    }
}

// The one frame of latency Decision 15 buys determinism with, said out loud,
// plus what the last few writes actually did. A refusal that is not reported is
// a value that snaps back with no explanation.
void drawWriteLog(scene::World& world, const Inspector& inspector)
{
    if (inspector.pendingCount() > 0)
        ImGui::Text("%zu write(s) queued for the next frame start", inspector.pendingCount());

    const std::span<const WriteOutcome> outcomes = inspector.outcomes();
    if (outcomes.empty())
        return;

    if (!ImGui::CollapsingHeader("recent writes"))
        return;

    for (const WriteOutcome& outcome : outcomes) {
        const std::string_view propertyName = world.atoms().text(outcome.property);
        ImGui::Text("%.*s: %s", static_cast<int>(propertyName.size()), propertyName.data(),
                    setResultLabel(outcome.result));
    }
}

// --- The console and the memory table (D017) ---------------------------------
//
// `architecture.md` §app names five panes for the DebugShell -- explorer,
// properties, profiler with the memcat table, log/REPL, streaming map, physics
// wireframe -- and two of them had never been written. The audit that found that
// is what D017 is; this is the pane.

// The last few hundred log lines, and the sink that fills them.
//
// Bounded and dropping the oldest, because a shell that grew with the log would
// be a memory leak with a scrollbar. Process-global like the sink it installs:
// `core::setLogSink` takes one function and there is one console.
struct ConsoleLog
{
    static constexpr core::usize kMaxLines = 400;

    struct Line
    {
        core::LogLevel level = core::LogLevel::Info;
        std::string text;
    };

    std::mutex mutex;
    std::deque<Line> lines;
    bool installed = false;
    // The sink that was there first. Chained rather than replaced, so the
    // console pane and the log FILE both get every line -- a shell that ate the
    // log would be the last place anybody looked for it.
    core::LogSink previous;
};

ConsoleLog& console()
{
    static ConsoleLog instance;
    return instance;
}

void drawMemory(script::ScriptRuntime& runtime)
{
    const std::vector<script::ScriptRuntime::MemoryCategory> rows = runtime.memoryByCategory();

    core::usize total = 0;
    for (const auto& row : rows)
        total += row.bytes;
    ImGui::Text("script heap: %.1f KB across %d categories", static_cast<double>(total) / 1024.0,
                static_cast<int>(rows.size()));

    if (!ImGui::BeginTable("memcat", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
        return;

    ImGui::TableSetupColumn("cat", ImGuiTableColumnFlags_WidthFixed, 32.0f);
    ImGui::TableSetupColumn("what");
    ImGui::TableSetupColumn("KB", ImGuiTableColumnFlags_WidthFixed, 64.0f);
    ImGui::TableHeadersRow();

    for (const auto& row : rows) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%u", row.category);
        ImGui::TableNextColumn();
        // A category with no name is one the pool assigned and whose script has
        // since been replaced -- worth showing as a number rather than hiding,
        // because that is exactly the leak the table is for.
        ImGui::TextUnformatted(row.name.empty() ? "(recycled)" : std::string(row.name).c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%.1f", static_cast<double>(row.bytes) / 1024.0);
    }
    ImGui::EndTable();
}

void drawConsole(script::ScriptRuntime* runtime)
{
    ConsoleLog& log = console();

    if (ImGui::BeginChild("log", ImVec2(0.0f, 160.0f), ImGuiChildFlags_Borders)) {
        std::lock_guard<std::mutex> lock(log.mutex);
        for (const ConsoleLog::Line& line : log.lines) {
            const ImVec4 colour = line.level == core::LogLevel::Error   ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f)
                                  : line.level == core::LogLevel::Warn  ? ImVec4(1.0f, 0.85f, 0.4f, 1.0f)
                                  : line.level == core::LogLevel::Debug ? ImVec4(0.6f, 0.65f, 0.75f, 1.0f)
                                                                        : ImVec4(0.85f, 0.88f, 0.92f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, colour);
            ImGui::TextUnformatted(line.text.c_str());
            ImGui::PopStyleColor();
        }
        // Only while already at the bottom, so scrolling back to read something
        // is not undone by the next log line.
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();

    static std::array<char, 512> input{};
    ImGui::SetNextItemWidth(-1.0f);
    const bool submitted = ImGui::InputText("##repl", input.data(), input.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    if (!submitted)
        return;

    const std::string_view source{input.data()};
    if (!source.empty() && runtime != nullptr) {
        // Echoed first, so the log reads as a session rather than as a list of
        // answers with no questions.
        core::logText(core::LogLevel::Info, std::string("> ").append(source));
        if (const std::optional<core::EngineError> error = runtime->evaluate(source); error.has_value())
            core::logText(core::LogLevel::Error, error->message);
    }
    input.fill(0);
    ImGui::SetKeyboardFocusHere(-1);
}

// Play, pause and step, above the image they act on.
//
// **There is no Stop, and its absence is deliberate** (D058). Stop means "put
// the world back the way it was before I pressed play", and this engine cannot
// remember an edited world yet -- nothing can serialize one. A Stop that
// silently rebuilt from the scripts would throw away whatever somebody had
// changed, which is worse than a button that is not there.
// The transport, in the order and the shape Unity and Unreal both use.
//
// **Play and stop are one button because they are opposites**; pause is a
// different question and gets its own. A single toggle cannot answer both --
// pressing play asks "run my game", pressing stop asks "give me my world back",
// and a button that means one of them while showing the other is the first
// thing a person notices.
void drawTransport(Editor& editor, EditorCommands& commands)
{
    const RunState run = editor.runState();
    const bool inPlay = editor.inPlayMode();

    if (ImGui::Button(inPlay ? "stop" : "play"))
        commands.play = !inPlay;
    ImGui::SetItemTooltip(inPlay ? "leave play mode and put the world back where you pressed play"
                                 : "run the game, remembering the world first");

    // Only inside play mode, because pausing is a thing that happens to a
    // running game. Disabled rather than hidden: a control that appears and
    // disappears moves the ones beside it.
    ImGui::SameLine();
    ImGui::BeginDisabled(!inPlay);
    if (ImGui::Button(run == RunState::Paused ? "resume" : "pause"))
        commands.pause = run != RunState::Paused;
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(run == RunState::Playing);
    if (ImGui::Button("step"))
        editor.requestStep();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("advance exactly one simulation tick");

    // A world to start in. Beside save rather than in the content browser,
    // because "give me somewhere to begin" is a thing you do to the WORLD and
    // the browser is about files.
    ImGui::SameLine();
    if (ImGui::Button("new"))
        commands.newScene = true;
    ImGui::SetItemTooltip("empty the scene and start over -- anything unsaved is gone");

    ImGui::SameLine();
    // **Save asks for a name when there is nothing to overwrite.** An editor
    // that invented one would put somebody's first hour of work in a file they
    // did not choose and cannot find, which is the whole reason every editor
    // has this dialog. The dialog itself lives at the shell's level, because
    // File > Save Scene As has to reach the same one.
    const bool untitled = editor.openScenePath().empty();
    if (ImGui::Button(untitled ? "save as..." : "save")) {
        if (untitled)
            commands.wantSaveAs = true;
        else
            commands.save = true;
    }
    if (!untitled)
        ImGui::SetItemTooltip("%s", editor.openScenePath().c_str());

    ImGui::SameLine();
    switch (run) {
    case RunState::Playing:
        ImGui::TextDisabled("playing");
        break;
    case RunState::Paused:
        ImGui::TextDisabled("paused in play mode -- stop to get your world back");
        break;
    case RunState::Editing:
        ImGui::TextDisabled("editing | right-drag to look, WASD/QE to fly, wheel for speed (%.0f m/s)",
                            static_cast<double>(editor.cameraSpeed()));
        break;
    }

    // Whatever the last save or stop had to say, on the line under the buttons
    // rather than in a modal: somebody pressing save twice a minute should not
    // have to dismiss anything.
    if (!editor.status().message.empty()) {
        const ImVec4 colour =
            editor.status().failed ? ImVec4(1.0f, 0.45f, 0.35f, 1.0f) : ImVec4(0.55f, 0.75f, 0.55f, 1.0f);
        ImGui::TextColored(colour, "%s", editor.status().message.c_str());
    }
}

// What the mouse and keyboard are asking the viewport for.
//
// **It reports and does not act.** The camera is driven by the frame loop,
// because turning it puts the pointer into relative mode and in relative mode
// ImGui has no absolute position to difference -- so `io.MouseDelta` is zero
// exactly when the camera needs it. The motion comes from the platform's own
// events; what a UI callback can still answer is whether somebody is asking.
//
// RIGHT button, not left: left is select, and an editor where looking around
// changes what you have selected is unusable. Held rather than toggled, because
// a mode you can forget you are in is how a minute goes missing.
void reportLookInput(Editor& editor, bool overViewport)
{
    const ImGuiIO& io = ImGui::GetIO();

    // The wheel changes SPEED rather than dollying. A dolly duplicates what W
    // and S already do; a speed control is the thing a four-kilometre world and
    // a four-metre room need different values of.
    if (overViewport && io.MouseWheel != 0.0f)
        editor.setCameraSpeed(editor.cameraSpeed() * (io.MouseWheel > 0.0f ? 1.25f : 0.8f));

    // **Latched on the press, and only there.** The first version asked "over
    // the viewport OR dragging", and `IsMouseDragging` becomes true a pixel
    // after a press ANYWHERE -- so a right-click in the explorer turned into a
    // camera turn: the pointer locked, motion stopped reaching ImGui, and the
    // context menu that click was asking for never appeared.
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        g_lookLatched = overViewport;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
        g_lookLatched = false;

    Editor::LookInput look;
    look.active = g_lookLatched;

    if (look.active) {
        const auto axis = [](ImGuiKey positive, ImGuiKey negative) -> f32 {
            return (ImGui::IsKeyDown(positive) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(negative) ? 1.0f : 0.0f);
        };
        const f32 sprint = ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 4.0f : 1.0f;
        look.move =
            core::Vec3{axis(ImGuiKey_D, ImGuiKey_A), axis(ImGuiKey_E, ImGuiKey_Q), axis(ImGuiKey_W, ImGuiKey_S)} *
            sprint;
    }

    editor.setLookInput(look);
}

// The 3D view.
//
// The panel IS the image: no padding, because a margin of window background
// around a rendered world reads as a bug rather than as a frame. Its rectangle
// is handed to the editor every frame because that rectangle is the only thing
// that maps a mouse position onto a ray.
void drawViewport(Editor& editor, rhi::TextureHandle texture, EditorCommands& commands, bool& open)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin("viewport", &open);
    ImGui::PopStyleVar();

    if (visible) {
        drawTransport(editor, commands);

        const ImVec2 size = ImGui::GetContentRegionAvail();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        editor.setViewport(ViewportRect{origin.x, origin.y, size.x, size.y});

        SDL_GPUTexture* native = texture.valid() ? rhi::nativeTexture(*g_device, texture) : nullptr;
        if (native != nullptr && size.x >= 1.0f && size.y >= 1.0f) {
            ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(native)), size);

            // Hovering the IMAGE, not the window: a click on the tab, the
            // border or the space beside a letterboxed image is not a click on
            // the world, and treating it as one deselects whatever the person
            // was working on.
            const bool overImage = ImGui::IsItemHovered();
            if (overImage && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                const ImVec2 mouse = ImGui::GetMousePos();
                editor.requestPick(core::Vec2{mouse.x - origin.x, mouse.y - origin.y});
            }

            reportLookInput(editor, overImage);
        }
    }
    ImGui::End();
}

// The arrangement somebody gets the first time they open the editor.
//
// Without this every panel is placed at ImGui's default position, which is the
// same position, so the first launch is five windows in a pile with the
// viewport at the bottom of it -- which is what the first run of this shell
// actually looked like. A dockspace does not arrange anything by itself; it
// only makes arranging possible.
//
// Built once, and only when there is no saved layout: `DockBuilderRemoveNode`
// would throw away the arrangement somebody chose.
void buildDefaultLayout(ImGuiID dockspace)
{
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->Size);

    // The centre is the world and everything else is furniture around it, which
    // is the one thing every editor of this shape agrees on.
    ImGuiID centre = dockspace;
    const ImGuiID left = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, 0.22f, nullptr, &centre);
    const ImGuiID right = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.28f, nullptr, &centre);
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.26f, nullptr, &centre);

    ImGui::DockBuilderDockWindow("viewport", centre);
    ImGui::DockBuilderDockWindow("explorer", left);
    ImGui::DockBuilderDockWindow("properties", right);
    ImGui::DockBuilderDockWindow("stats", right);
    // Content first, so it is the tab that opens. The two share a node on
    // purpose -- they are both "the thing under the viewport" and neither
    // deserves permanent floor space -- but which one greets somebody is a
    // decision rather than a consequence of call order, so it is also set
    // explicitly below.
    ImGui::DockBuilderDockWindow("content", bottom);
    ImGui::DockBuilderDockWindow("console", bottom);

    ImGui::DockBuilderFinish(dockspace);
}

// A short label for a kind, so a row says what it is without an icon set.
[[nodiscard]] const char* contentKindLabel(ContentKind kind) noexcept
{
    switch (kind) {
    case ContentKind::Folder:
        return "dir";
    case ContentKind::Scene:
        return "scene";
    case ContentKind::Mesh:
        return "mesh";
    case ContentKind::Texture:
        return "tex";
    case ContentKind::Chunk:
        return "chunk";
    case ContentKind::Other:
        break;
    }
    return "";
}

// The project's assets, and the panel from which a scene is opened.
//
// **Virtualised**, and that is a measurement rather than a flourish: the quality
// bar for this was given as Unity and Unreal, and a Content Browser is judged on
// a tree of thousands. `ImGuiListClipper` draws only the rows a person can see,
// so a folder of ten thousand meshes costs the same as a folder of ten.
// `drawExplorer` beside this one does NOT do that yet, which is a thing to fix
// rather than a precedent to follow.
void drawContent(Editor& editor, EditorCommands& commands, bool& open, EditorDialogs& dialogs)
{
    if (!ImGui::Begin("content", &open)) {
        ImGui::End();
        return;
    }

    ContentTree& tree = editor.content();

    ImGui::BeginDisabled(tree.atRoot());
    if (ImGui::Button("up"))
        (void)tree.leave();
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("refresh"))
        (void)tree.refresh();

    ImGui::SameLine();
    // The shell's dialog, so the toolbar button and the folder's own
    // right-click menu reach the same one.
    if (ImGui::Button("new folder"))
        dialogs.newFolder = true;

    ImGui::SameLine();
    ImGui::TextDisabled("content/%s", tree.currentFolder().c_str());

    ImGui::Separator();

    if (ImGui::BeginChild("entries")) {
        const std::vector<ContentEntry>& entries = tree.entries();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(entries.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const ContentEntry& entry = entries[static_cast<std::size_t>(row)];
                ImGui::PushID(row);

                const bool isOpenScene = entry.kind == ContentKind::Scene && entry.path == editor.openScenePath();
                if (isOpenScene)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.85f, 0.55f, 1.0f));

                // Double-click, because a single click is how somebody browses
                // and opening a scene throws away what is in the world.
                if (ImGui::Selectable(entry.name.c_str(), isOpenScene, ImGuiSelectableFlags_AllowDoubleClick) &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (entry.kind == ContentKind::Folder)
                        (void)tree.enter(entry.name);
                    else if (entry.kind == ContentKind::Scene)
                        commands.openScene = entry.path;
                }

                if (isOpenScene)
                    ImGui::PopStyleColor();

                if (ImGui::BeginPopupContextItem("entry-menu")) {
                    if (entry.kind == ContentKind::Scene && ImGui::MenuItem("Open"))
                        commands.openScene = entry.path;
                    if (ImGui::MenuItem("Rename...")) {
                        dialogs.renameTarget = {};
                        dialogs.renameContentPath = entry.path;
                        dialogs.renameSeed = ContentTree::stemOf(entry);
                        dialogs.renameContent = true;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Delete"))
                        dialogs.deleteContentPath = entry.path;
                    ImGui::EndPopup();
                }

                if (const char* label = contentKindLabel(entry.kind); label[0] != '\0') {
                    ImGui::SameLine(0.0f, 12.0f);
                    ImGui::TextDisabled("%s", label);
                }
                ImGui::PopID();
            }
        }

        // The space below the rows. Right-clicking nothing is how somebody asks
        // about the FOLDER rather than about a thing in it.
        if (ImGui::BeginPopupContextWindow("folder-menu",
                                           ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("New Folder..."))
                dialogs.newFolder = true;
            if (ImGui::MenuItem("Refresh"))
                (void)tree.refresh();
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

// The application menu, which every engine has and which this one did not.
//
// **A menu bar is not decoration: it is where a person looks for a thing they
// cannot see.** A panel closed by accident, a scene saved under a new name, the
// preferences -- none of those have anywhere else to live, and an editor that
// puts them only on toolbar buttons is one where closing a panel is permanent.
//
// Drawn BEFORE the dockspace, because `DockSpaceOverViewport` measures the work
// area and a menu bar declared after it would overlap the panels by its own
// height.
void drawMenuBar(Editor& editor, EditorPanels& panels, EditorCommands& commands, EditorDialogs& dialogs)
{
    if (!ImGui::BeginMainMenuBar())
        return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Scene"))
            commands.newScene = true;
        ImGui::Separator();
        // Greyed rather than hidden when there is nothing to save to: the item
        // has to be where somebody expects it even when it cannot act, or they
        // conclude the editor cannot do it at all.
        if (ImGui::MenuItem("Save Scene", nullptr, false, !editor.openScenePath().empty()))
            commands.save = true;
        if (ImGui::MenuItem("Save Scene As..."))
            dialogs.saveAs = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Exit"))
            commands.quit = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        // Named after what they will undo. "Undo" alone leaves somebody to find
        // out by pressing it, which for a delete is finding out too late.
        const std::string undoLabel =
            editor.history().canUndo() ? "Undo " + std::string(editor.history().undoLabel()) : std::string("Undo");
        const std::string redoLabel =
            editor.history().canRedo() ? "Redo " + std::string(editor.history().redoLabel()) : std::string("Redo");

        if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, editor.history().canUndo()))
            commands.undo = true;
        if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, editor.history().canRedo()))
            commands.redo = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Preferences..."))
            dialogs.preferences = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Window")) {
        ImGui::MenuItem("Explorer", nullptr, &panels.explorer);
        ImGui::MenuItem("Properties", nullptr, &panels.properties);
        ImGui::MenuItem("Viewport", nullptr, &panels.viewport);
        ImGui::MenuItem("Content", nullptr, &panels.content);
        ImGui::MenuItem("Console", nullptr, &panels.console);
        ImGui::MenuItem("Stats", nullptr, &panels.stats);
        ImGui::Separator();
        // Not "close everything": somebody who has lost a panel behind another
        // wants the arrangement back, not an empty window.
        if (ImGui::MenuItem("Reset Layout"))
            commands.resetLayout = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About LuauG"))
            dialogs.about = true;
        ImGui::EndMenu();
    }

    // Which scene this is, right-aligned. The menu bar is the best-placed thing
    // to answer that, and it is the question somebody asks just before saving
    // over something.
    const std::string open = editor.openScenePath().empty() ? std::string("untitled") : editor.openScenePath();
    const float width = ImGui::CalcTextSize(open.c_str()).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - width - 16.0f);
    ImGui::TextDisabled("%s", open.c_str());

    ImGui::EndMainMenuBar();
}

// The dialogs, at the shell's level rather than inside a panel.
//
// A popup belongs to the window that opened it, so one living inside the
// viewport could not be opened from the menu -- which is exactly what a Save As
// has to be. Modal, because each of these is a question with an answer, and one
// left half-answered behind a panel is one somebody loses track of.
void drawEditorDialogs(Editor& editor, EditorCommands& commands, EditorDialogs& dialogs)
{
    if (dialogs.saveAs) {
        dialogs.saveAs = false;
        ImGui::OpenPopup("Save Scene As");
    }
    if (dialogs.preferences) {
        dialogs.preferences = false;
        ImGui::OpenPopup("Preferences");
    }
    if (dialogs.about) {
        dialogs.about = false;
        ImGui::OpenPopup("About LuauG");
    }

    ImGui::SetNextWindowSize(ImVec2(470.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_NoResize)) {
        static std::array<char, 160> path{};
        ImGui::TextDisabled("Scenes live under the project's content directory.");
        ImGui::Spacing();

        ImGui::TextUnformatted("content/");
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::SetNextItemWidth(330.0f);
        const bool submitted =
            ImGui::InputText("##scene-path", path.data(), path.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        const std::string typed(path.data());

        // The RESOLVED path, not the typed one. The label above the box already
        // says `content/`, so `content/scenes/main` is the natural thing to
        // type and used to be saved verbatim into `content/content/` -- D068,
        // found by the first person to use this dialog. Showing what will
        // actually be written is what makes the normalisation visible instead
        // of surprising.
        const std::string resolved = Editor::normalizeScenePath(typed);
        const bool usable = !typed.empty() && Editor::sceneNameIsUsable(resolved);
        if (usable)
            ImGui::TextDisabled("saves as content/%s", resolved.c_str());
        else if (typed.empty())
            ImGui::TextDisabled("saves as content/…");
        else
            ImGui::TextDisabled("not a path inside content/");

        ImGui::Spacing();
        ImGui::BeginDisabled(!usable);
        const bool accepted = ImGui::Button("Save", ImVec2(120.0f, 0.0f));
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            path.fill(0);
            ImGui::CloseCurrentPopup();
        }

        if ((submitted || accepted) && usable) {
            commands.saveAs = typed;
            path.fill(0);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(470.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Preferences", nullptr, ImGuiWindowFlags_NoResize)) {
        // Deliberately small and deliberately real. An empty preferences window
        // is a promise; one holding the setting somebody actually reaches for is
        // a place the next setting knows where to go.
        ImGui::SeparatorText("Viewport");
        f32 speed = editor.cameraSpeed();
        if (ImGui::DragFloat("Camera speed (m/s)", &speed, 0.5f, 0.1f, 2000.0f, "%.1f"))
            editor.setCameraSpeed(speed);
        ImGui::TextDisabled("The scroll wheel changes this while flying, too.");

        ImGui::Spacing();
        ImGui::SeparatorText("Where these live");
        ImGui::TextWrapped("The panel layout and the scene you had open are per-person and are kept in the project's "
                           ".luaug directory. What a RUN of the project starts with is the project's own decision, "
                           "and lives in luaug.toml.");

        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(120.0f, 0.0f)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (dialogs.renameInstance || dialogs.renameContent) {
        ImGui::OpenPopup("Rename");
    }
    if (dialogs.newFolder) {
        dialogs.newFolder = false;
        ImGui::OpenPopup("New Folder");
    }
    if (!dialogs.deleteContentPath.empty())
        ImGui::OpenPopup("Delete");

    ImGui::SetNextWindowSize(ImVec2(400.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Rename", nullptr, ImGuiWindowFlags_NoResize)) {
        static std::array<char, 128> name{};
        // Seeded once, on the frame the dialog opens. Copying every frame would
        // overwrite what the person is typing with what they started from.
        if (dialogs.renameInstance || dialogs.renameContent) {
            name.fill(0);
            const std::size_t count = std::min(dialogs.renameSeed.size(), name.size() - 1);
            std::memcpy(name.data(), dialogs.renameSeed.data(), count);
            dialogs.renameInstance = false;
            dialogs.renameContent = false;
        }

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SetKeyboardFocusHere();
        const bool submitted =
            ImGui::InputText("##rename", name.data(), name.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        const std::string typed(name.data());

        ImGui::Spacing();
        ImGui::BeginDisabled(typed.empty());
        const bool accepted = ImGui::Button("Rename", ImVec2(120.0f, 0.0f));
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            dialogs.renameTarget = {};
            dialogs.renameContentPath.clear();
            ImGui::CloseCurrentPopup();
        }

        if ((submitted || accepted) && !typed.empty()) {
            if (dialogs.renameTarget.valid()) {
                commands.renameInstance = dialogs.renameTarget;
                commands.renameInstanceTo = typed;
            }
            else if (!dialogs.renameContentPath.empty()) {
                commands.renameContent = dialogs.renameContentPath;
                commands.renameContentTo = typed;
            }
            dialogs.renameTarget = {};
            dialogs.renameContentPath.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(400.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("New Folder", nullptr, ImGuiWindowFlags_NoResize)) {
        static std::array<char, 128> folder{};
        ImGui::SetNextItemWidth(-1.0f);
        const bool submitted =
            ImGui::InputText("##folder", folder.data(), folder.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        const std::string typed(folder.data());

        ImGui::Spacing();
        // Greyed before the press rather than refused after it: a name a
        // filesystem cannot carry is knowable while it is being typed.
        ImGui::BeginDisabled(!ContentTree::isUsableName(typed));
        const bool accepted = ImGui::Button("Create", ImVec2(120.0f, 0.0f));
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            folder.fill(0);
            ImGui::CloseCurrentPopup();
        }

        if ((submitted || accepted) && ContentTree::isUsableName(typed)) {
            commands.createFolder = typed;
            folder.fill(0);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Delete", nullptr, ImGuiWindowFlags_NoResize)) {
        // **Named, not counted.** "Delete 1 item?" is a question nobody can
        // answer; the path is what tells somebody whether they meant it.
        ImGui::TextWrapped("Delete content/%s?", dialogs.deleteContentPath.c_str());
        ImGui::Spacing();
        ImGui::TextDisabled("This removes the file from disk. A folder goes with everything in it, and there is no "
                            "undo for either.");
        ImGui::Spacing();

        if (ImGui::Button("Delete", ImVec2(120.0f, 0.0f))) {
            commands.deleteContent = dialogs.deleteContentPath;
            dialogs.deleteContentPath.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            dialogs.deleteContentPath.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(430.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("About LuauG", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextUnformatted("LuauG");
        ImGui::TextDisabled("An open-source game engine scripted in Luau.");
        ImGui::Spacing();
        ImGui::TextDisabled("Apache-2.0. The editor is post-v1 phase 1.");
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(120.0f, 0.0f)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// The editor's furniture. The panels inside it are the overlay's own, which is
// the whole argument of ADR 0046: what an editor mostly is, this engine already
// had.
void drawEditorShell(const Frame& frame, scene::World* world, core::InstanceId root, Inspector* inspector,
                     script::ScriptRuntime* runtime, Editor* editor, rhi::TextureHandle viewport, bool& laidOut,
                     EditorCommands& commands, EditorPanels& panels, EditorDialogs& dialogs)
{
    // Before the dockspace. `DockSpaceOverViewport` measures the work area, and
    // a menu bar declared after it would sit on top of the panels by its own
    // height.
    if (editor != nullptr)
        drawMenuBar(*editor, panels, commands, dialogs);

    // A transparent central node, so a layout that has not been built yet shows
    // the frame underneath instead of a slab of grey.
    const ImGuiID dockspace =
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

    // `DockBuilderGetNode` answers null until the dockspace exists, and a saved
    // layout has already put windows into it by the time it does -- so "nobody
    // has arranged this yet" is the node having no split and no window, which is
    // exactly the state a first launch is in.
    bool builtThisFrame = false;
    if (!laidOut || commands.resetLayout) {
        const bool asked = commands.resetLayout;
        commands.resetLayout = false;
        laidOut = true;
        const ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspace);
        // Asked for, or never arranged. `DockBuilderRemoveNode` throws away an
        // arrangement somebody chose, so it only runs when they said so or when
        // there is nothing to throw away.
        if (asked || node == nullptr || (!node->IsSplitNode() && node->Windows.Size == 0)) {
            buildDefaultLayout(dockspace);
            builtThisFrame = true;
            if (asked)
                panels = EditorPanels{};
        }
    }

    if (editor != nullptr) {
        if (panels.viewport)
            drawViewport(*editor, viewport, commands, panels.viewport);
        if (panels.content)
            drawContent(*editor, commands, panels.content, dialogs);
    }

    if (panels.explorer) {
        if (ImGui::Begin("explorer", &panels.explorer)) {
            if (world != nullptr && inspector != nullptr)
                drawExplorer(*world, root, *inspector, &commands, &dialogs);
        }
        ImGui::End();
    }

    if (panels.properties) {
        if (ImGui::Begin("properties", &panels.properties)) {
            if (world != nullptr && inspector != nullptr) {
                drawProperties(*world, *inspector);
                drawWriteLog(*world, *inspector);
            }
        }
        ImGui::End();
    }

    // Draws with no VM for the same reason it does in the overlay: the LOG half
    // is what somebody wants when the VM failed to boot.
    if (panels.console) {
        if (ImGui::Begin("console", &panels.console))
            drawConsole(runtime);
        ImGui::End();
    }

    if (panels.stats) {
        if (ImGui::Begin("stats", &panels.stats)) {
            drawStats(frame);
            if (runtime != nullptr)
                drawMemory(*runtime);
        }
        ImGui::End();
    }

    // **Escape lets go of the selection, from anywhere.** Deselecting is a thing
    // a person does constantly and it needs a key that works wherever they are
    // looking -- the explorer, the viewport, the content browser.
    //
    // Only when nothing else has a claim on it, and the order matters: a modal
    // is closed by Escape and a text field cancels its edit with it, and taking
    // the key from either would make the shell's own dialogs unclosable. So it
    // is asked for last, after everything that could have wanted it.
    const bool popupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive() && !popupOpen) {
        // **In play mode Escape stops**, which is what Unreal does and what
        // this editor now needs rather than merely wants. A running game holds
        // the pointer, and while it does the panels do not see the mouse at all
        // (D069) -- so a project that never hands the cursor back would have no
        // way to reach the stop button. The keyboard still arrives, and a
        // transport somebody cannot reach is not a transport.
        //
        // The game sees the key too. That is the same arrangement Unreal ships
        // and the honest one: while you are testing, the tool keeps one key.
        if (editor != nullptr && editor->inPlayMode())
            commands.play = false;
        else
            commands.clearSelection = true;
    }

    // Ctrl+Z and Ctrl+Y, under the same rule Escape is: not while a field has
    // the keyboard, because Ctrl+Z inside a text box is the box's own undo and
    // taking it would make typing a name unrecoverable.
    if (!ImGui::IsAnyItemActive() && !popupOpen && ImGui::GetIO().KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
            commands.undo = true;
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
            commands.redo = true;
    }

    if (commands.wantSaveAs) {
        commands.wantSaveAs = false;
        dialogs.saveAs = true;
    }
    if (editor != nullptr)
        drawEditorDialogs(*editor, commands, dialogs);

    // After every panel has been declared, because focusing a window ImGui has
    // not seen this frame does nothing. Only on the frame the default layout
    // was built: a person who later chose the console should find the console.
    if (builtThisFrame)
        ImGui::SetWindowFocus("content");
}

void drawShell(const Frame& frame, scene::World* world, core::InstanceId root, Inspector* inspector,
               script::ScriptRuntime* runtime)
{
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::SetNextWindowSize(ImVec2(420.0f, 520.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("LuauG")) {
        drawStats(frame);

        // A host with no world is a normal state -- `--version`, the render
        // gates, a test with no scene -- and it gets the stats panel it has
        // always had.
        if (world != nullptr && inspector != nullptr) {
            ImGui::SeparatorText("explorer");
            if (ImGui::BeginChild("explorer", ImVec2(0.0f, 200.0f), ImGuiChildFlags_Borders))
                drawExplorer(*world, root, *inspector, nullptr, nullptr);
            ImGui::EndChild();

            ImGui::SeparatorText("properties");
            drawProperties(*world, *inspector);
            drawWriteLog(*world, *inspector);
        }

        if (runtime != nullptr) {
            ImGui::SeparatorText("memory");
            drawMemory(*runtime);
        }

        // The console draws even with no VM: the LOG half is the half a person
        // wants when the VM failed to boot, which is the moment they most want
        // it. The input line simply does nothing.
        ImGui::SeparatorText("console");
        drawConsole(runtime);
    }
    ImGui::End();
}

} // namespace

DebugOverlay::DebugOverlay(platform::Window& window, rhi::IDevice& device, Shell shell, std::string layoutPath)
    : shell_(shell), layoutPath_(std::move(layoutPath))
{
    // The editor IS the application, so it is up from the first frame. F3 still
    // works and still hides it, which is the cheapest way to look at the world
    // without the furniture.
    visible_ = shell_ == Shell::Editor;

    SDL_Window* sdlWindow = platform::nativeWindow(window);
    SDL_GPUDevice* gpuDevice = rhi::nativeDevice(device);

    // Not a failure and not worth a message: `--rhi=capture` and `--rhi=null`
    // have nothing to draw with, and answering false from active() is the
    // entire contract for that case.
    if (sdlWindow == nullptr || gpuDevice == nullptr)
        return;

    if (ImGui::GetCurrentContext() != nullptr) {
        core::log(core::LogLevel::Warn, LUAUG_TR("engine.overlay.warn.already_running"));
        return;
    }

    // The pipeline ImGui builds is compiled against one colour format, and the
    // only source of the right one is the device-window pair. An unclaimed
    // window answers INVALID here rather than at the first draw, so the
    // ordering requirement is checked where it can still be explained.
    const SDL_GPUTextureFormat colorFormat = SDL_GetGPUSwapchainTextureFormat(gpuDevice, sdlWindow);
    if (colorFormat == SDL_GPU_TEXTUREFORMAT_INVALID) {
        core::log(core::LogLevel::Warn, LUAUG_TR("engine.overlay.warn.window_not_claimed"));
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    // Docking is why ADR 0011 pins the docking tag rather than the release one.
    // No dockspace host window is created: one panel does not need one, and the
    // editor that would is not in v1 (R15).
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // Otherwise ImGui writes imgui.ini into the working directory, which under
    // CTest is the source tree (R14) and for a game is wherever it happened to
    // be launched from. Remembered window positions are not state a GAME has
    // decided to keep -- but they are exactly what an editor owes somebody who
    // arranged their panels once, so `Shell::Editor` names a file inside the
    // project it opened.
    io.IniFilename = nullptr;
    if (shell_ == Shell::Editor && !layoutPath_.empty()) {
        // ImGui writes the file and never the directory above it, and a project
        // that has not been built yet has no `.luaug/`. Failing here would mean
        // a layout that silently never persists, which is worse than a layout
        // that never existed.
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(layoutPath_).parent_path(), ec);
        io.IniFilename = layoutPath_.c_str();
    }
    ImGui::StyleColorsDark();

    if (!ImGui_ImplSDL3_InitForSDLGPU(sdlWindow)) {
        ImGui::DestroyContext();
        core::log(core::LogLevel::Warn, LUAUG_TR("engine.overlay.warn.init_failed"));
        return;
    }

    ImGui_ImplSDLGPU3_InitInfo info{};
    info.Device = gpuDevice;
    info.ColorTargetFormat = colorFormat;
    info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;

    if (!ImGui_ImplSDLGPU3_Init(&info)) {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        core::log(core::LogLevel::Warn, LUAUG_TR("engine.overlay.warn.init_failed"));
        return;
    }

    g_window = &window;
    g_device = &device;
    active_ = true;
}

DebugOverlay::~DebugOverlay()
{
    if (!active_)
        return;

    // Renderer first: it releases GPU objects through the device, which is
    // still alive because the constructor's contract says it must be.
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    g_window = nullptr;
    g_device = nullptr;
}

void DebugOverlay::handleEvents(std::span<const platform::Event> events)
{
    if (!active_)
        return;

    // ImGui models far more input than the engine does -- text, mouse capture,
    // window focus -- so it reads the untranslated stream. That stream existing
    // at all is what sdl_interop.h is for.
    // **While the pointer is held, the UI does not see the mouse.** Relative
    // mode hides the cursor and stops moving it, and SDL goes on posting motion
    // with a logical position it accumulates from the deltas -- so without this
    // the invisible cursor walks across the panels, highlighting rows in the
    // explorer and hovering buttons nobody is pointing at.
    //
    // **Two holders, and they need different things.** The EDITOR holds it
    // while a right-drag turns the fly camera (D063), and there only motion is
    // withheld: the button going UP still has to arrive, or ImGui believes it
    // is held forever and the turn never ends. The GAME holds it for as long as
    // somebody is playing (D069), and there the mouse is not the UI's at all --
    // motion, wheel and button PRESSES are all withheld, because a player
    // turning their head must not be clicking the explorer at the same time.
    //
    // A release is delivered in both cases and for the same reason: ImGui can
    // never be left believing a button it was never told about is down.
    const bool looking = editor_ != nullptr && editor_->lookInput().active;
    for (const SDL_Event& raw : platform::rawEvents()) {
        const bool motion = raw.type == SDL_EVENT_MOUSE_MOTION;
        if (looking && motion)
            continue;
        if (gameHoldsPointer_ &&
            (motion || raw.type == SDL_EVENT_MOUSE_WHEEL || raw.type == SDL_EVENT_MOUSE_BUTTON_DOWN)) {
            continue;
        }
        ImGui_ImplSDL3_ProcessEvent(&raw);
    }

    // **And the cursor is nowhere**, which withholding motion alone does not
    // say: ImGui would keep the last position it was told about, so whichever
    // row the cursor happened to be over when play was pressed would stay lit
    // for the whole session. `-FLT_MAX` is ImGui's own spelling of "there is no
    // mouse". The SDL3 backend will not overwrite it while relative mode is on
    // -- `ImGui_ImplSDL3_UpdateMouseData` guards its global-state fallback with
    // exactly that -- so this holds for the frame.
    if (gameHoldsPointer_)
        ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);

    for (const platform::Event& event : events) {
        // Repeats excluded: holding F3 down should not strobe the panel.
        if (event.type == platform::EventType::KeyDown && event.key == platform::Key::F3 && !event.repeat)
            visible_ = !visible_;
    }
}

void DebugOverlay::render(rhi::ICmdList& cmd, rhi::TextureHandle target, const Frame& frame)
{
    if (!active_ || !visible_ || !target.valid())
        return;

    // The frame currently being recorded. Null means the caller is outside
    // beginFrame()/submitAndPresent(), where there is nothing to draw into.
    SDL_GPUCommandBuffer* buffer = rhi::nativeCommandBuffer(*g_device);
    if (buffer == nullptr)
        return;

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    if (shell_ == Shell::Editor)
        drawEditorShell(frame, world_, root_, inspector_, runtime_, editor_, viewportTexture_, layoutBuilt_, commands_,
                        panels_, dialogs_);
    else
        drawShell(frame, world_, root_, inspector_, runtime_);
    ImGui::Render();

    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData == nullptr)
        return;

    // Mandatory, and mandatory HERE: this is where the vertex and index buffers
    // are uploaded, and a copy cannot run inside a render pass. The backend
    // header says so in capitals for the same reason our seam does.
    ImGui_ImplSDLGPU3_PrepareDrawData(drawData, buffer);

    // Load, not Clear: the overlay is drawn on top of a finished frame.
    const std::array<rhi::ColorAttachment, 1> colors{rhi::ColorAttachment{
        .texture = target,
        .loadOp = rhi::LoadOp::Load,
        .storeOp = rhi::StoreOp::Store,
    }};

    cmd.pushDebugGroup("debug-overlay");
    cmd.beginRenderPass({.colorAttachments = colors, .debugName = "imgui"});

    // Opened through the seam a line ago, so this is the pass just begun -- the
    // device owns exactly one command list, which is the one `cmd` refers to.
    if (SDL_GPURenderPass* pass = rhi::nativeRenderPass(*g_device); pass != nullptr)
        ImGui_ImplSDLGPU3_RenderDrawData(drawData, buffer, pass);

    cmd.endRenderPass();
    cmd.popDebugGroup();
}

void DebugOverlay::captureLog()
{
    ConsoleLog& log = console();
    if (log.installed)
        return;
    log.installed = true;

    log.previous = core::setLogSink([](core::LogLevel level, std::string_view text) {
        ConsoleLog& sink = console();
        {
            std::lock_guard<std::mutex> lock(sink.mutex);
            sink.lines.push_back(ConsoleLog::Line{level, std::string(text)});
            while (sink.lines.size() > ConsoleLog::kMaxLines)
                sink.lines.pop_front();
        }
        // Chained rather than replaced: the console pane and the log FILE both
        // get every line. A shell that ate the log would be the last place
        // anybody looked for it.
        if (sink.previous)
            sink.previous(level, text);
    });
}

#else

// ADR 0011: a shipping build contains no ImGui, so the overlay contains no
// behaviour. The class keeps its shape and its signatures -- that is what lets
// the frame loop call it without an #ifdef -- and active() answers false, which
// is how anything that asks finds out there is nothing here.

DebugOverlay::DebugOverlay(platform::Window&, rhi::IDevice&, Shell, std::string)
{}

DebugOverlay::~DebugOverlay() = default;

void DebugOverlay::handleEvents(std::span<const platform::Event>)
{}

void DebugOverlay::render(rhi::ICmdList&, rhi::TextureHandle, const Frame&)
{}

// Nothing to capture INTO: the ring buffer and the console pane that reads it
// live in the half of this file that ImGui compiles. Leaving the process log
// sink alone is the whole behaviour -- the log FILE keeps every line, which is
// where a shipping build's log was always going to be read from.
void DebugOverlay::captureLog()
{}

#endif

} // namespace luaug::app
