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
#include <deque>
#include <filesystem>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <imgui_internal.h>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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
std::vector<bool> g_openAtDepth;
std::vector<const scene::PropertyDesc*> g_properties;

// A flat preorder list plus one open flag per depth is the whole of the
// collapse state: preorder guarantees the most recent row one level up IS this
// row's parent, so a closed parent leaves its flag false and every descendant
// reads it before drawing.
//
// Drawn flat -- Indent/Unindent rather than nested TreePush -- because the row
// order comes from `collectTree` and not from the recursion. That is what keeps
// the drawn order the world's order rather than the drawing code's.
void drawExplorer(scene::World& world, core::InstanceId root, Inspector& inspector)
{
    collectTree(world, root, g_rows);
    g_openAtDepth.clear();

    const float indentSpacing = ImGui::GetStyle().IndentSpacing;

    for (const TreeRow& row : g_rows) {
        if (g_openAtDepth.size() <= row.depth)
            g_openAtDepth.resize(row.depth + 1, false);

        if (row.depth > 0 && !g_openAtDepth[row.depth - 1]) {
            // Collapsed under a closed ancestor. The flag still has to be
            // written, or this row's own children would read whatever the
            // previous subtree left behind at this depth.
            g_openAtDepth[row.depth] = false;
            continue;
        }

        const float indent = static_cast<float>(row.depth) * indentSpacing;
        if (indent > 0.0f)
            ImGui::Indent(indent);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (world.childCount(row.id) == 0)
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (row.id == inspector.selection())
            flags |= ImGuiTreeNodeFlags_Selected;

        const std::string_view instanceName = world.atoms().text(world.name(row.id));
        const scene::ClassDescriptor* classDescriptor = world.classes().find(world.classOf(row.id));
        const std::string_view className =
            classDescriptor != nullptr ? world.atoms().text(classDescriptor->name) : std::string_view("?");

        // The services under `game` are what anyone opening this wants to see;
        // deeper than that is a project's own tree and is its business.
        if (row.depth < 2)
            ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);

        ImGui::PushID(static_cast<int>(row.id.index));
        const bool open = ImGui::TreeNodeEx("row", flags, "%.*s  (%.*s)", static_cast<int>(instanceName.size()),
                                            instanceName.data(), static_cast<int>(className.size()), className.data());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            inspector.select(row.id);
        if (open && (flags & ImGuiTreeNodeFlags_NoTreePushOnOpen) == 0)
            ImGui::TreePop();
        ImGui::PopID();

        if (indent > 0.0f)
            ImGui::Unindent(indent);

        g_openAtDepth[row.depth] = open;
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

    ImGui::SameLine();
    if (ImGui::Button("save"))
        commands.save = true;

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

// The fly camera, on the right mouse button.
//
// RIGHT rather than left, because left is select and an editor where looking
// around also changes what you have selected is unusable. Held rather than
// toggled, because a mode you can forget you are in is how somebody loses a
// minute wondering why their mouse does nothing.
//
// It reads ImGui's input rather than the engine's: ImGui already owns this
// window's mouse and keyboard while the pointer is in it, and asking the
// engine's input layer would mean two owners disagreeing about whether a
// keystroke was consumed.
void driveEditorCamera(Editor& editor, bool overViewport)
{
    const ImGuiIO& io = ImGui::GetIO();

    // The scroll wheel changes speed rather than dollying the camera. A dolly
    // duplicates what W and S already do; a speed control is the thing an open
    // world of four kilometres and a room of four metres actually need
    // different values of.
    if (overViewport && io.MouseWheel != 0.0f) {
        const f32 factor = io.MouseWheel > 0.0f ? 1.25f : 0.8f;
        editor.setCameraSpeed(editor.cameraSpeed() * factor);
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        // Still drive with a zero delta: the camera has to keep moving while
        // the keys are held and the mouse is still, and returning early here
        // would make WASD work only while the mouse was in motion.
        if (overViewport)
            editor.driveCamera({}, {}, io.DeltaTime);
        return;
    }

    // The drag has to have STARTED over the viewport. Otherwise dragging out of
    // another panel and across this one flings the camera.
    if (!overViewport && !ImGui::IsMouseDragging(ImGuiMouseButton_Right))
        return;

    const auto axis = [](ImGuiKey positive, ImGuiKey negative) -> f32 {
        return (ImGui::IsKeyDown(positive) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(negative) ? 1.0f : 0.0f);
    };

    const core::Vec3 move{
        axis(ImGuiKey_D, ImGuiKey_A),
        axis(ImGuiKey_E, ImGuiKey_Q),
        axis(ImGuiKey_W, ImGuiKey_S),
    };

    const f32 sprint = ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 4.0f : 1.0f;
    editor.driveCamera(core::Vec2{io.MouseDelta.x, io.MouseDelta.y}, move * sprint, io.DeltaTime);
}

// The 3D view.
//
// The panel IS the image: no padding, because a margin of window background
// around a rendered world reads as a bug rather than as a frame. Its rectangle
// is handed to the editor every frame because that rectangle is the only thing
// that maps a mouse position onto a ray.
void drawViewport(Editor& editor, rhi::TextureHandle texture, EditorCommands& commands)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool open = ImGui::Begin("viewport");
    ImGui::PopStyleVar();

    if (open) {
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

            driveEditorCamera(editor, overImage);
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
    ImGui::DockBuilderDockWindow("console", bottom);

    ImGui::DockBuilderFinish(dockspace);
}

// The editor's furniture. The panels inside it are the overlay's own, which is
// the whole argument of ADR 0046: what an editor mostly is, this engine already
// had.
void drawEditorShell(const Frame& frame, scene::World* world, core::InstanceId root, Inspector* inspector,
                     script::ScriptRuntime* runtime, Editor* editor, rhi::TextureHandle viewport, bool& laidOut,
                     EditorCommands& commands)
{
    // A transparent central node, so a layout that has not been built yet shows
    // the frame underneath instead of a slab of grey.
    const ImGuiID dockspace =
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

    // `DockBuilderGetNode` answers null until the dockspace exists, and a saved
    // layout has already put windows into it by the time it does -- so "nobody
    // has arranged this yet" is the node having no split and no window, which is
    // exactly the state a first launch is in.
    if (!laidOut) {
        laidOut = true;
        const ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspace);
        if (node == nullptr || (!node->IsSplitNode() && node->Windows.Size == 0))
            buildDefaultLayout(dockspace);
    }

    if (editor != nullptr)
        drawViewport(*editor, viewport, commands);

    if (ImGui::Begin("explorer")) {
        if (world != nullptr && inspector != nullptr)
            drawExplorer(*world, root, *inspector);
    }
    ImGui::End();

    if (ImGui::Begin("properties")) {
        if (world != nullptr && inspector != nullptr) {
            drawProperties(*world, *inspector);
            drawWriteLog(*world, *inspector);
        }
    }
    ImGui::End();

    // Draws with no VM for the same reason it does in the overlay: the LOG half
    // is what somebody wants when the VM failed to boot.
    if (ImGui::Begin("console"))
        drawConsole(runtime);
    ImGui::End();

    if (ImGui::Begin("stats")) {
        drawStats(frame);
        if (runtime != nullptr)
            drawMemory(*runtime);
    }
    ImGui::End();
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
                drawExplorer(*world, root, *inspector);
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
    for (const SDL_Event& raw : platform::rawEvents())
        ImGui_ImplSDL3_ProcessEvent(&raw);

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
        drawEditorShell(frame, world_, root_, inspector_, runtime_, editor_, viewportTexture_, layoutBuilt_, commands_);
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
