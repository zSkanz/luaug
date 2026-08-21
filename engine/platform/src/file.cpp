#include "luaug/platform/file.h"

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

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

} // namespace luaug::platform
