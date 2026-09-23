#include "luaug/asset/image.h"

#include "luaug/core/i18n.h"
#include "luaug/core/text_key.h"

#include <algorithm>
#include <array>
#include <cmath>
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

std::optional<core::EngineError> decodeHeightmap(std::span<const std::byte> encoded, std::string_view fileName,
                                                 HeightImage& out)
{
    out = HeightImage{};
    if (encoded.empty())
        return core::makeError(LUAUG_TR("asset.image.err.decode_failed"), {}, "empty input");
    if (encoded.size() > static_cast<std::size_t>(INT32_MAX))
        return core::makeError(LUAUG_TR("asset.image.err.decode_failed"), {}, "input larger than 2 GiB");

    std::string extension;
    if (const std::size_t dot = fileName.rfind('.'); dot != std::string_view::npos) {
        for (const char c : fileName.substr(dot))
            extension.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
    }

    if (extension == ".r16" || extension == ".raw") {
        const std::size_t samples = encoded.size() / 2u;
        const auto side = static_cast<u32>(std::llround(std::sqrt(static_cast<double>(samples))));
        if (encoded.size() % 2u != 0 || side == 0 || static_cast<std::size_t>(side) * side != samples) {
            return core::makeError(LUAUG_TR("asset.image.err.decode_failed"), {},
                                   "a RAW heightmap is square sixteen-bit samples; this one is " +
                                       std::to_string(encoded.size()) + " bytes");
        }
        out.width = side;
        out.height = side;
        out.samples.resize(samples);
        for (std::size_t at = 0; at < samples; ++at) {
            const auto low = static_cast<unsigned>(encoded[at * 2u]);
            const auto high = static_cast<unsigned>(encoded[at * 2u + 1u]);
            out.samples[at] = static_cast<float>(low | (high << 8u)) / 65535.0f;
        }
        return std::nullopt;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    // One channel out, at sixteen bits: stb widens an eight-bit file to the same
    // range, so both depths arrive as one layout.
    stbi_us* pixels = stbi_load_16_from_memory(reinterpret_cast<const stbi_uc*>(encoded.data()),
                                               static_cast<int>(encoded.size()), &width, &height, &channels, 1);
    if (pixels == nullptr) {
        const char* reason = stbi_failure_reason();
        return core::makeError(LUAUG_TR("asset.image.err.decode_failed"), {}, reason != nullptr ? reason : "unknown");
    }
    out.width = static_cast<u32>(width);
    out.height = static_cast<u32>(height);
    out.samples.resize(static_cast<std::size_t>(out.width) * out.height);
    for (std::size_t at = 0; at < out.samples.size(); ++at)
        out.samples[at] = static_cast<float>(pixels[at]) / 65535.0f;
    stbi_image_free(pixels);
    return std::nullopt;
}

std::vector<float> resampleHeights(const HeightImage& image, u32 columns, u32 rows, float low, float high)
{
    std::vector<float> heights;
    if (!image.valid() || columns == 0 || rows == 0)
        return heights;
    heights.resize(static_cast<std::size_t>(columns) * rows);

    // Corner to corner: the first and last columns land on the first and last
    // pixels, so a map imported at its own size is copied exactly, and one
    // imported larger stretches without losing its edges.
    const auto scale = [](u32 target, u32 source) {
        return target > 1 ? static_cast<double>(source - 1u) / static_cast<double>(target - 1u) : 0.0;
    };
    const double stepX = scale(columns, image.width);
    const double stepY = scale(rows, image.height);
    const auto pixel = [&](u32 x, u32 y) { return image.samples[static_cast<std::size_t>(y) * image.width + x]; };

    for (u32 row = 0; row < rows; ++row) {
        const double v = static_cast<double>(row) * stepY;
        const auto y0 = std::min(static_cast<u32>(v), image.height - 1u);
        const u32 y1 = std::min(y0 + 1u, image.height - 1u);
        const auto fy = static_cast<float>(v - static_cast<double>(y0));
        for (u32 column = 0; column < columns; ++column) {
            const double u = static_cast<double>(column) * stepX;
            const auto x0 = std::min(static_cast<u32>(u), image.width - 1u);
            const u32 x1 = std::min(x0 + 1u, image.width - 1u);
            const auto fx = static_cast<float>(u - static_cast<double>(x0));
            const float top = pixel(x0, y0) + (pixel(x1, y0) - pixel(x0, y0)) * fx;
            const float bottom = pixel(x0, y1) + (pixel(x1, y1) - pixel(x0, y1)) * fx;
            const float t = top + (bottom - top) * fy;
            heights[static_cast<std::size_t>(row) * columns + column] = low + (high - low) * t;
        }
    }
    return heights;
}

} // namespace luaug::asset
