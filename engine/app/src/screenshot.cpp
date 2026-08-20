// The host's screenshot path is now a two-line forward: `asset::writePng` owns
// the encoding and the file, which is what this file's header said would happen
// once image IO became a real subsystem (M4).
#include "luaug/app/screenshot.h"

#include "luaug/asset/image.h"

namespace luaug::app
{

std::optional<core::EngineError> writePng(
    const std::filesystem::path& path, std::span<const std::byte> pixels, core::u32 width, core::u32 height)
{
    return asset::writePng(path, pixels, width, height);
}

} // namespace luaug::app
