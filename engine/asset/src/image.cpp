#include "luaug/asset/image.h"

#include "luaug/core/i18n.h"
#include "luaug/core/text_key.h"

#include <array>
#include <cstdio>
#include <string>

// stb's two image translation units are compiled once, here. They were in `app`
// until this module existed, which is what `screenshot.h` said would happen.
//
// STBI_NO_STDIO on the read side is not a preference: every decode this engine
// performs is from memory -- a glTF's embedded image is a span in the middle of
// a buffer, never a file of its own -- and letting stb open files would add a
// path-handling surface with no caller.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_FAILURE_USERMSG
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>

namespace luaug::asset {
namespace {

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

std::optional<core::EngineError> decodeImage(std::span<const std::byte> encoded, Image& out)
{
    out = Image{};

    if (encoded.empty()) {
        return core::makeError(LUAUG_TR("asset.image.err.decode_failed"), {}, "empty input");
    }
    // stb takes an `int` length, and an image larger than 2 GiB would wrap it
    // into a negative -- which stb reads as a much smaller buffer and decodes
    // from happily, producing garbage rather than an error.
    if (encoded.size() > static_cast<std::size_t>(INT32_MAX)) {
        return core::makeError(LUAUG_TR("asset.image.err.decode_failed"), {}, "input larger than 2 GiB");
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    // 4 forces RGBA out whatever went in; `channels` still reports the source's
    // own count, which is how a caller tells "opaque by design" from "opaque by
    // accident".
    stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(encoded.data()),
                                            static_cast<int>(encoded.size()), &width, &height, &channels, 4);
    if (pixels == nullptr) {
        const char* reason = stbi_failure_reason();
        return core::makeError(LUAUG_TR("asset.image.err.decode_failed"), {}, reason != nullptr ? reason : "unknown");
    }

    out.width = static_cast<u32>(width);
    out.height = static_cast<u32>(height);
    out.sourceChannels = static_cast<u32>(channels);

    const auto byteCount = static_cast<std::size_t>(out.width) * out.height * 4u;
    const auto* begin = reinterpret_cast<const std::byte*>(pixels);
    out.pixels.assign(begin, begin + byteCount);
    stbi_image_free(pixels);

    return std::nullopt;
}

std::optional<core::EngineError> writePng(const std::filesystem::path& path, std::span<const std::byte> pixels,
                                          u32 width, u32 height)
{
    const auto expected = static_cast<std::size_t>(width) * height * 4u;
    if (width == 0 || height == 0 || pixels.size() < expected) {
        return core::makeError(LUAUG_TR("asset.image.err.bad_pixels"), {},
                               "expected " + std::to_string(expected) + " bytes, got " + std::to_string(pixels.size()));
    }

    EncodedPng encoded;
    const int stride = static_cast<int>(width) * 4;
    if (stbi_write_png_to_func(&appendEncoded, &encoded, static_cast<int>(width), static_cast<int>(height), 4,
                               pixels.data(), stride) == 0) {
        return core::makeError(LUAUG_TR("asset.image.err.encode_failed"));
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
    if (file == nullptr) {
        const std::array<core::I18nArg, 1> args{core::I18nArg{"path", path.string()}};
        return core::makeError(LUAUG_TR("asset.image.err.open_failed"), args);
    }

    const std::size_t written = std::fwrite(encoded.bytes.data(), 1, encoded.bytes.size(), file);
    const bool closed = std::fclose(file) == 0;

    if (written != encoded.bytes.size() || !closed) {
        const std::array<core::I18nArg, 1> args{core::I18nArg{"path", path.string()}};
        return core::makeError(LUAUG_TR("asset.image.err.write_failed"), args);
    }

    return std::nullopt;
}

} // namespace luaug::asset
