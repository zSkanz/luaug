#include "luaug/platform/platform.h"

#include "luaug/core/text_key.h"
#include "luaug/platform/async_io.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_platform_defines.h>
#include <chrono>
#include <string>
#include <vector>

#if defined(_WIN32)
// windows.h FIRST: psapi.h declares its functions with the Win32 typedefs and
// does not include them itself, so the other order is two hundred syntax errors
// inside a system header.
//
// `clang-format off` is what holds it. `IncludeBlocks: Regroup` merges blocks
// and sorts them, so a blank line does not survive and `psapi` sorts first --
// which is exactly the broken order, and the gate would reinstate it on every
// run.
// clang-format off
#include <windows.h>
#include <psapi.h>
// `SetCurrentProcessExplicitAppUserModelID` lives here, and shobjidl_core.h has
// the same ordering requirement psapi.h does.
#include <shobjidl_core.h>
// clang-format on
#elif defined(__linux__)
#include <cstdio>
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#endif

namespace luaug::platform {
namespace {

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

#ifdef SDL_PLATFORM_ANDROID
    // An APK has no executable directory and its assets are not a filesystem:
    // they are zip entries the package manager serves through AAssetManager.
    // SDL reaches them by opening a *relative* path -- an absolute one is sent
    // to the C runtime instead and finds nothing -- so the content directory is
    // deliberately the bare relative name that matches the Gradle project's
    // `assets/content/` tree, and everything that reads it must go through
    // platform::readFile (file.h).
    //
    // SDL_GetBasePath() answers "./" here. That is not used: the "./" prefix
    // survives into the lookup key and AAssetManager indexes exact names.
    paths.executableDir = std::filesystem::path(".");
    paths.contentDir = std::filesystem::path("content");
#else
    // Null on platforms that do not implement it; the working directory is the
    // same fallback the M0 host used, and is what CTest provides.
    if (const char* base = SDL_GetBasePath(); base != nullptr) {
        paths.executableDir = std::filesystem::path(base);
    }
    else {
        std::error_code ec;
        paths.executableDir = std::filesystem::current_path(ec);
    }

    paths.contentDir = paths.executableDir / "content";
#endif
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
    //
    // SDL_INIT_GAMEPAD is separate and is asked for as a SECOND call rather than
    // ORed into the first, because a machine with no controller subsystem --
    // which is every CI container -- must still get a window. A failure here is
    // logged by SDL and ignored: the engine runs, and no gamepad event arrives,
    // which is exactly what having no gamepad means.
    if (!SDL_Init(SDL_INIT_VIDEO))
        return core::makeError(LUAUG_TR("platform.err.init_failed"), {}, SDL_GetError());
    (void)SDL_InitSubSystem(SDL_INIT_GAMEPAD);

    resolvePaths();
    g_initialized = true;
    return std::nullopt;
}

void shutdown()
{
    if (!g_initialized)
        return;

    // BEFORE `SDL_Quit`, and it has to be: the async IO service holds an SDL
    // queue it must drain and destroy, and a submitter thread it must join.
    // Leaving that to the static destructor means joining a thread after SDL is
    // gone -- and, if nothing joins it at all, a joinable `std::thread` destroyed
    // at exit, which is `std::terminate` and reads as a crash.
    shutdownIo();

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

u64 residentBytes() noexcept
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) == 0) {
        return 0;
    }
    return static_cast<u64>(counters.WorkingSetSize);
#elif defined(__linux__)
    // `statm` rather than `status`: two integers on one line, in pages, with
    // no parsing beyond the second field. `status`'s VmRSS is the same number
    // behind a line search and a unit suffix.
    std::FILE* const statm = std::fopen("/proc/self/statm", "rb");
    if (statm == nullptr) {
        return 0;
    }
    unsigned long long total = 0;
    unsigned long long resident = 0;
    const int read = std::fscanf(statm, "%llu %llu", &total, &resident);
    (void)std::fclose(statm);
    if (read != 2) {
        return 0;
    }
    return static_cast<u64>(resident) * static_cast<u64>(::sysconf(_SC_PAGESIZE));
#elif defined(__APPLE__)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) !=
        KERN_SUCCESS) {
        return 0;
    }
    return static_cast<u64>(info.resident_size);
#else
    return 0;
#endif
}

std::vector<std::byte> applicationIconBytes()
{
#if defined(_WIN32)
    // Ordinal 1, which is what `luaug.rc` names and what `luaug build` replaces.
    // The GROUP is what an executable's icon actually is: a directory of sizes,
    // each naming an `RT_ICON` of its own.
    const HRSRC groupHandle = ::FindResourceW(nullptr, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(14 /* RT_GROUP_ICON */));
    if (groupHandle == nullptr)
        return {};
    const HGLOBAL group = ::LoadResource(nullptr, groupHandle);
    if (group == nullptr)
        return {};
    const auto* directory = static_cast<const unsigned char*>(::LockResource(group));
    if (directory == nullptr)
        return {};

    const auto count = static_cast<u32>(directory[4]) | (static_cast<u32>(directory[5]) << 8);
    if (count == 0)
        return {};

    // The LARGEST entry, because this is a window icon and the compositor
    // downscales. A group directory entry is 14 bytes: width, height, colours,
    // reserved, planes, bit count, byte count, and the id of its `RT_ICON`.
    u32 bestId = 0;
    u32 bestPixels = 0;
    for (u32 index = 0; index < count; ++index) {
        const unsigned char* entry = directory + 6 + static_cast<std::size_t>(index) * 14u;
        // Zero means 256 in an icon directory, which is the size that matters
        // most here and the one a naive read discards.
        const u32 width = entry[0] == 0 ? 256u : entry[0];
        const u32 height = entry[1] == 0 ? 256u : entry[1];
        const u32 id = static_cast<u32>(entry[12]) | (static_cast<u32>(entry[13]) << 8);
        if (width * height > bestPixels) {
            bestPixels = width * height;
            bestId = id;
        }
    }
    if (bestId == 0)
        return {};

    const HRSRC iconHandle = ::FindResourceW(nullptr, MAKEINTRESOURCEW(bestId), MAKEINTRESOURCEW(3 /* RT_ICON */));
    if (iconHandle == nullptr)
        return {};
    const DWORD size = ::SizeofResource(nullptr, iconHandle);
    const HGLOBAL icon = ::LoadResource(nullptr, iconHandle);
    if (icon == nullptr || size == 0)
        return {};
    const auto* bytes = static_cast<const std::byte*>(::LockResource(icon));
    if (bytes == nullptr)
        return {};

    return std::vector<std::byte>(bytes, bytes + size);
#else
    return {};
#endif
}

void setApplicationId([[maybe_unused]] std::string_view id)
{
#if defined(_WIN32)
    if (id.empty())
        return;

    // The API takes UTF-16 and an id is ASCII reverse-DNS, so the widening is
    // the whole conversion. Failure is ignored on purpose: the shell has
    // already decided this process's identity by the time a window exists, and
    // a game that cannot set it still runs.
    std::wstring wide;
    wide.reserve(id.size());
    for (const char c : id)
        wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    (void)::SetCurrentProcessExplicitAppUserModelID(wide.c_str());
#endif
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
