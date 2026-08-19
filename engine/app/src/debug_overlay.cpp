#include "luaug/app/debug_overlay.h"

#if LUAUG_DEBUG_UI

#include <array>
#include <string_view>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include "luaug/app/backends.h"
#include "luaug/core/log.h"
#include "luaug/core/text_key.h"
#include "luaug/platform/sdl_interop.h"
#include "luaug/platform/window.h"
#include "luaug/rhi/device.h"
#include "luaug/rhi/sdlgpu_interop.h"

#endif

namespace luaug::app
{

#if LUAUG_DEBUG_UI

namespace
{

// Bound at construction, read while drawing. These sit beside ImGui's own
// process-wide context rather than inside the class for two reasons: that
// context already makes a second live overlay meaningless, and keeping them out
// of the header is what lets the header stay free of SDL and of a layout that
// changes with the build profile.
//
// Main-thread only, like everything else that touches SDL's event queue.
platform::Window* g_window = nullptr;
const rhi::IDevice* g_device = nullptr;

// Four facts the host already knows. Nothing here is sampled or estimated:
// `core` has no counters yet, and a profiler panel reporting numbers the engine
// cannot actually measure is worse than no panel.
void drawPanel(const Frame& frame)
{
    ImGui::SetNextWindowBgAlpha(0.75f);
    ImGui::SetNextWindowSize(ImVec2(280.0f, 0.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("LuauG"))
    {
        ImGui::Text("frame %llu", static_cast<unsigned long long>(frame.index));

        // Guarded because the first frame has no previous one to measure
        // against, and dividing by its zero would print inf on every start.
        const double milliseconds = frame.renderDt * 1000.0;
        const double perSecond = frame.renderDt > 0.0 ? 1.0 / frame.renderDt : 0.0;
        ImGui::Text("%.2f ms (%.0f fps)", milliseconds, perSecond);

        const std::string_view backend = backendName(g_device->backend());
        ImGui::Text("backend %.*s", static_cast<int>(backend.size()), backend.data());

        const platform::WindowSize size = platform::windowPixelSize(*g_window);
        ImGui::Text("drawable %d x %d", size.width, size.height);
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

    if (ImGui::GetCurrentContext() != nullptr)
    {
        core::log(core::LogLevel::Warn, LUAUG_TR("engine.overlay.warn.already_running"));
        return;
    }

    // The pipeline ImGui builds is compiled against one colour format, and the
    // only source of the right one is the device-window pair. An unclaimed
    // window answers INVALID here rather than at the first draw, so the
    // ordering requirement is checked where it can still be explained.
    const SDL_GPUTextureFormat colorFormat = SDL_GetGPUSwapchainTextureFormat(gpuDevice, sdlWindow);
    if (colorFormat == SDL_GPU_TEXTUREFORMAT_INVALID)
    {
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

    if (!ImGui_ImplSDL3_InitForSDLGPU(sdlWindow))
    {
        ImGui::DestroyContext();
        core::log(core::LogLevel::Warn, LUAUG_TR("engine.overlay.warn.init_failed"));
        return;
    }

    ImGui_ImplSDLGPU3_InitInfo info{};
    info.Device = gpuDevice;
    info.ColorTargetFormat = colorFormat;
    info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;

    if (!ImGui_ImplSDLGPU3_Init(&info))
    {
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

    for (const platform::Event& event : events)
    {
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
    drawPanel(frame);
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

DebugOverlay::DebugOverlay(platform::Window&, rhi::IDevice&) {}

DebugOverlay::~DebugOverlay() = default;

void DebugOverlay::handleEvents(std::span<const platform::Event>) {}

void DebugOverlay::render(rhi::ICmdList&, rhi::TextureHandle, const Frame&) {}

#endif

} // namespace luaug::app
