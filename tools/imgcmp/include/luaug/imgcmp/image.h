// PNG decoding and encoding, and the fixed RGBA8 image the comparator works on.
//
// Diagnostics here are developer-facing English and deliberately do NOT go
// through the engine i18n catalog: rule R3 governs engine and game-facing
// output, and imgcmp is a repo tool run by CI and by the agent. The same
// reasoning keeps it from linking anything under engine/ -- this is how a
// rendered frame gets verified, so a broken engine build must not also take
// away the means of checking it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace luaug::imgcmp
{

// A decoded image, always 4 channels. The comparator is a fixed-format tool, so
// every loader forces RGBA8 and no caller ever branches on channel count.
// Rows run top-to-bottom (stb_image's order, and the engine's readback order).
struct Image
{
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;

    [[nodiscard]] bool empty() const noexcept { return width <= 0 || height <= 0; }

    // Number of pixels the dimensions claim, which is not necessarily what
    // `rgba` holds -- see wellFormed().
    [[nodiscard]] std::size_t pixelCount() const noexcept
    {
        if (empty())
            return 0;
        return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    }

    // Contract every consumer relies on: non-degenerate dimensions and a buffer
    // that matches them exactly. Loaders guarantee it; hand-built images (tests,
    // future callers) must be checked before indexing.
    [[nodiscard]] bool wellFormed() const noexcept { return !empty() && rgba.size() == pixelCount() * 4u; }
};

// Builds an image whose buffer matches its dimensions, filled with `fill`.
[[nodiscard]] Image makeImage(int width, int height, std::uint8_t fill = 0);

struct LoadResult
{
    bool ok = false;
    Image image;
    std::string error;
};

struct WriteResult
{
    bool ok = false;
    std::string error;
};

// Decoding is restricted to PNG (see the STBI_ONLY_PNG rationale in
// CMakeLists.txt): anything else is reported as an unsupported format rather
// than silently accepted.
[[nodiscard]] LoadResult decodePng(std::span<const std::uint8_t> bytes);
[[nodiscard]] LoadResult loadPngFile(const std::filesystem::path& path);

[[nodiscard]] WriteResult encodePng(const Image& image, std::vector<std::uint8_t>& out);
[[nodiscard]] WriteResult writePngFile(const std::filesystem::path& path, const Image& image);

} // namespace luaug::imgcmp
