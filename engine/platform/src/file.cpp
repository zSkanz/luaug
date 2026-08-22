#include "luaug/platform/file.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_thread.h>
#include <atomic>
#include <cstdint>
#include <string>

namespace luaug::platform {
namespace {

// u8string(), not string(): see the header. The reinterpret_cast is the
// standard-sanctioned way back from char8_t, and is a no-op at runtime.
[[nodiscard]] std::string toUtf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

// SDL_LoadFile handles a stream whose size is not known in advance by growing
// its buffer -- which is not hypothetical here: an APK asset compressed by aapt
// is read through SDL's fallback input-stream path, and asking for its size
// first is exactly the case that would return -1.
[[nodiscard]] void* loadWhole(const std::filesystem::path& path, std::size_t& size)
{
    return SDL_LoadFile(toUtf8(path).c_str(), &size);
}

// A temporary name in the TARGET'S OWN directory. The atomicity `writeFile`
// promises comes from the rename, and a rename is only atomic within one
// filesystem -- a temporary in the system temp directory would cross a volume
// on Windows and turn the replace into a copy.
//
// The name has to be unique against every other writer that could be aiming at
// the same target, or two of them would share a temporary and one would rename
// the other's half-written bytes over the file. An OS thread id is unique
// across the machine, not just the process, so it plus a per-process counter
// settles that without reading a clock.
[[nodiscard]] std::filesystem::path temporaryNameFor(const std::filesystem::path& target)
{
    static std::atomic<std::uint64_t> sequence{0};

    std::string suffix = ".luaug-";
    suffix += std::to_string(static_cast<std::uint64_t>(SDL_GetCurrentThreadID()));
    suffix += '-';
    suffix += std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
    suffix += ".tmp";

    std::filesystem::path temporary = target;
    temporary += suffix;
    return temporary;
}

// The write half of `writeFile` and `writeTextFile`, which differ only in how
// the caller spells its bytes.
[[nodiscard]] bool writeWhole(const std::filesystem::path& path, const void* data, std::size_t size)
{
    // An empty path would otherwise become a bare temporary in the working
    // directory, and then fail to rename onto nothing.
    if (path.empty()) {
        return false;
    }

    const std::string temporary = toUtf8(temporaryNameFor(path));
    if (!SDL_SaveFile(temporary.c_str(), data, size)) {
        // The open may have failed, or the write may have stopped partway and
        // left a file behind. Removing unconditionally is correct either way,
        // and a failure to remove is not something the caller can act on.
        (void)SDL_RemovePath(temporary.c_str());
        return false;
    }

    // Also the only check that the target is writable at all: a directory, or a
    // path whose parent does not exist, fails HERE rather than corrupting
    // anything, and the temporary is collected on the way out.
    if (!SDL_RenamePath(temporary.c_str(), toUtf8(path).c_str())) {
        (void)SDL_RemovePath(temporary.c_str());
        return false;
    }

    return true;
}

} // namespace

bool readFile(const std::filesystem::path& path, std::vector<std::byte>& out)
{
    std::size_t size = 0;
    void* data = loadWhole(path, size);
    if (data == nullptr)
        return false;

    const auto* const bytes = static_cast<const std::byte*>(data);
    out.assign(bytes, bytes + size);
    SDL_free(data);
    return true;
}

bool readTextFile(const std::filesystem::path& path, std::string& out)
{
    std::size_t size = 0;
    void* data = loadWhole(path, size);
    if (data == nullptr)
        return false;

    out.assign(static_cast<const char*>(data), size);
    SDL_free(data);
    return true;
}

bool fileExists(const std::filesystem::path& path)
{
    // Open and close. `SDL_IOFromFile` routes through the asset manager on
    // Android, so this answers correctly inside an APK where a stat cannot --
    // and unlike `SDL_LoadFile` it does not move the file's bytes to find out.
    SDL_IOStream* const stream = SDL_IOFromFile(toUtf8(path).c_str(), "rb");
    if (stream == nullptr) {
        return false;
    }
    (void)SDL_CloseIO(stream);
    return true;
}

bool writeFile(const std::filesystem::path& path, std::span<const std::byte> bytes)
{
    return writeWhole(path, bytes.data(), bytes.size());
}

bool writeTextFile(const std::filesystem::path& path, std::string_view text)
{
    return writeWhole(path, text.data(), text.size());
}

bool createDirectories(const std::filesystem::path& path)
{
    if (path.empty()) {
        return false;
    }

    // SDL_CreateDirectory already walks the missing parents, and reports
    // success when the directory is already there -- which is the case a caller
    // saving over an existing project hits every time.
    return SDL_CreateDirectory(toUtf8(path).c_str());
}

} // namespace luaug::platform
