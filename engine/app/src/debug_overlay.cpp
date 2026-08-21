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
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
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
        const scene::EnumDescriptor* enumDescriptor = world.enums().find(value.enumId);
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
                if (ImGui::Selectable(itemName.c_str(), item.value == value.value))
                    inspector.enqueue(id, descriptor.name, scene::Value{scene::EnumValue{value.enumId, item.value}});
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

void drawShell(const Frame& frame, scene::World* world, core::InstanceId root, Inspector* inspector)
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
    }
    ImGui::End();
}

} // namespace

DebugOverlay::DebugOverlay(platform::Window& window, rhi::IDevice& device)
{
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
    // be launched from. Remembered window positions are not state this engine
    // has decided to keep.
    io.IniFilename = nullptr;
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
    drawShell(frame, world_, root_, inspector_);
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

#else

// ADR 0011: a shipping build contains no ImGui, so the overlay contains no
// behaviour. The class keeps its shape and its signatures -- that is what lets
// the frame loop call it without an #ifdef -- and active() answers false, which
// is how anything that asks finds out there is nothing here.

DebugOverlay::DebugOverlay(platform::Window&, rhi::IDevice&)
{}

DebugOverlay::~DebugOverlay() = default;

void DebugOverlay::handleEvents(std::span<const platform::Event>)
{}

void DebugOverlay::render(rhi::ICmdList&, rhi::TextureHandle, const Frame&)
{}

#endif

} // namespace luaug::app
