#include "luaug/imgcmp/image.h"

#include <fstream>
#include <ios>
#include <limits>
#include <memory>
#include <stb_image.h>
#include <stb_image_write.h>

namespace luaug::imgcmp {
namespace {

struct StbiFree
{
    void operator()(stbi_uc* pixels) const noexcept { stbi_image_free(pixels); }
};

// stb hands back a raw allocation; owning it means a bad_alloc while copying
// into the vector cannot leak the decode buffer.
using StbiPixels = std::unique_ptr<stbi_uc, StbiFree>;

// stb speaks `int` sizes throughout, so every byte count crossing the boundary
// is checked once here rather than trusted at each call site.
[[nodiscard]] bool fitsInInt(std::size_t value) noexcept
{
    return value <= static_cast<std::size_t>(std::numeric_limits<int>::max());
}

void appendEncodedBytes(void* context, void* data, int size)
{
    if (context == nullptr || data == nullptr || size <= 0)
        return;

    auto* out = static_cast<std::vector<std::uint8_t>*>(context);
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    out->insert(out->end(), bytes, bytes + size);
}

} // namespace

Image makeImage(int width, int height, std::uint8_t fill)
{
    Image image;
    if (width <= 0 || height <= 0)
        return image;

    image.width = width;
    image.height = height;
    image.rgba.assign(image.pixelCount() * 4u, fill);
    return image;
}

LoadResult decodePng(std::span<const std::uint8_t> bytes)
{
    LoadResult result;

    if (bytes.empty()) {
        result.error = "empty image data";
        return result;
    }
    if (!fitsInInt(bytes.size())) {
        result.error = "image data is larger than the decoder can address";
        return result;
    }

    int width = 0;
    int height = 0;
    int channelsInFile = 0;
    const StbiPixels pixels{
        stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channelsInFile, 4)};

    if (!pixels) {
        const char* reason = stbi_failure_reason();
        result.error = reason != nullptr ? reason : "not a supported PNG";
        return result;
    }
    if (width <= 0 || height <= 0) {
        result.error = "decoded image has no pixels";
        return result;
    }

    result.image.width = width;
    result.image.height = height;
    const std::size_t byteCount = result.image.pixelCount() * 4u;
    result.image.rgba.assign(pixels.get(), pixels.get() + byteCount);
    result.ok = true;
    return result;
}

LoadResult loadPngFile(const std::filesystem::path& path)
{
    LoadResult result;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        result.error = "cannot open " + path.string();
        return result;
    }

    const std::streamoff size = file.tellg();
    if (size <= 0) {
        result.error = path.string() + " is empty or not readable";
        return result;
    }

    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) {
        result.error = "cannot read " + path.string();
        return result;
    }

    result = decodePng(bytes);
    if (!result.ok)
        result.error = path.string() + ": " + result.error;
    return result;
}

WriteResult encodePng(const Image& image, std::vector<std::uint8_t>& out)
{
    out.clear();

    if (!image.wellFormed())
        return WriteResult{false, "refusing to encode a malformed image"};

    const std::size_t strideBytes = static_cast<std::size_t>(image.width) * 4u;
    if (!fitsInInt(strideBytes))
        return WriteResult{false, "image row is larger than the encoder can address"};

    const int written = stbi_write_png_to_func(&appendEncodedBytes, &out, image.width, image.height, 4,
                                               image.rgba.data(), static_cast<int>(strideBytes));
    if (written == 0) {
        out.clear();
        return WriteResult{false, "PNG encoding failed"};
    }

    return WriteResult{true, {}};
}

WriteResult writePngFile(const std::filesystem::path& path, const Image& image)
{
    std::vector<std::uint8_t> encoded;
    const WriteResult encodeResult = encodePng(image, encoded);
    if (!encodeResult.ok)
        return encodeResult;

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
        return WriteResult{false, "cannot open " + path.string() + " for writing"};

    file.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    file.close();
    if (!file)
        return WriteResult{false, "cannot write " + path.string()};

    return WriteResult{true, {}};
}

} // namespace luaug::imgcmp
