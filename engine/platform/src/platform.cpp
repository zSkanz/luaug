#include "luaug/platform/platform.h"

#include <chrono>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>

#include "luaug/core/text_key.h"

namespace luaug::platform
{
namespace
{

bool g_initialized = false;

// SDL_GetBasePath's result never changes for a process, and the derived paths
// are used per frame once shaders and content load, so they are resolved once.
Paths& pathsSlot()
{
    static Paths paths;
    return paths;
}

void resolvePaths()
{
    Paths& paths = pathsSlot();

    // Null on platforms that do not implement it; the working directory is the
    // same fallback the M0 host used, and is what CTest provides.
    if (const char* base = SDL_GetBasePath(); base != nullptr)
    {
        paths.executableDir = std::filesystem::path(base);
    }
    else
    {
        std::error_code ec;
        paths.executableDir = std::filesystem::current_path(ec);
    }

    paths.contentDir = paths.executableDir / "content";
}

} // namespace

std::optional<core::EngineError> init(const InitOptions& options)
{
    if (g_initialized)
        return std::nullopt;

    if (options.headless)
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");

    // SDL_INIT_VIDEO implies SDL_INIT_EVENTS, so the window and the event pump
    // come up together -- which is the only combination this module offers.
    if (!SDL_Init(SDL_INIT_VIDEO))
        return core::makeError(LUAUG_TR("platform.err.init_failed"), {}, SDL_GetError());

    resolvePaths();
    g_initialized = true;
    return std::nullopt;
}

void shutdown()
{
    if (!g_initialized)
        return;

    SDL_Quit();
    g_initialized = false;
}

bool isInitialized() noexcept
{
    return g_initialized;
}

u64 nowNs() noexcept
{
    // Deliberately not SDL_GetTicksNS: SDL's tick clock counts from SDL library
    // initialization and reads 0 before it, so anything measuring startup --
    // or any test that runs before a window exists -- would silently get zero
    // and a first frame of impossible duration. steady_clock is monotonic by
    // standard guarantee, needs no bring-up, and has the same resolution on
    // every platform we target.
    const auto since = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(since).count());
}

const Paths& paths()
{
    // Resolvable without a video subsystem, so a tool or a test that only wants
    // to find content does not have to bring up a window stack first.
    if (pathsSlot().executableDir.empty())
        resolvePaths();

    return pathsSlot();
}

} // namespace luaug::platform
