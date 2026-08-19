#include "luaug/app/screenshot.h"

#include <array>
#include <string>

#include "luaug/core/i18n.h"
#include "luaug/core/text_key.h"

// stb_image_write's implementation is compiled once, here. `app` is its only
// consumer today; when a second one appears it moves to a translation unit of
// its own rather than being defined twice.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>

namespace luaug::app
{
namespace
{

// STBI_WRITE_NO_STDIO, so stb hands us the encoded bytes and we do the file
// ourselves. That is deliberate: stb's own stdio path takes a `const char*`,
// which silently mangles a non-ASCII path on Windows, and a user whose project
// lives under an accented directory name would get a screenshot that vanished
// with no error. std::filesystem::path knows how to open itself.
struct EncodedPng
{
    std::string bytes;
};

void appendEncoded(void* context, void* data, int size)
{
    auto* encoded = static_cast<EncodedPng*>(context);
    encoded->bytes.append(static_cast<const char*>(data), static_cast<std::size_t>(size));
}

} // namespace

std::optional<core::EngineError> writePng(
    const std::filesystem::path& path, std::span<const std::byte> pixels, core::u32 width, core::u32 height)
{
    const auto expected = static_cast<std::size_t>(width) * height * 4u;
    if (width == 0 || height == 0 || pixels.size() < expected)
    {
        return core::makeError(LUAUG_TR("engine.screenshot.err.bad_pixels"), {},
            "expected " + std::to_string(expected) + " bytes, got " + std::to_string(pixels.size()));
    }

    EncodedPng encoded;
    const int stride = static_cast<int>(width) * 4;
    if (stbi_write_png_to_func(&appendEncoded, &encoded, static_cast<int>(width), static_cast<int>(height), 4,
            pixels.data(), stride)
        == 0)
    {
        return core::makeError(LUAUG_TR("engine.screenshot.err.encode_failed"));
    }

    std::error_code ec;
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path(), ec);

    std::FILE* file = nullptr;
#ifdef _WIN32
    // The wide path is the only one that survives a non-ASCII directory name.
    if (_wfopen_s(&file, path.c_str(), L"wb") != 0)
        file = nullptr;
#else
    file = std::fopen(path.c_str(), "wb");
#endif
    if (file == nullptr)
    {
        const std::array<core::I18nArg, 1> args{core::I18nArg{"path", path.string()}};
        return core::makeError(LUAUG_TR("engine.screenshot.err.open_failed"), args);
    }

    const std::size_t written = std::fwrite(encoded.bytes.data(), 1, encoded.bytes.size(), file);
    const bool closed = std::fclose(file) == 0;

    if (written != encoded.bytes.size() || !closed)
    {
        const std::array<core::I18nArg, 1> args{core::I18nArg{"path", path.string()}};
        return core::makeError(LUAUG_TR("engine.screenshot.err.write_failed"), args);
    }

    return std::nullopt;
}

} // namespace luaug::app
