// `imgstrip` -- lays several screenshots side by side into one image.
//
// It exists because M4.5's deliverable is a claim about *change over time*: the
// sun crossing the sky, shadows lengthening, a pane fading. A single screenshot
// cannot carry that claim and a folder of eight numbered files makes a reviewer
// assemble it themselves. One strip, fixed camera, one variable, is the shape of
// evidence that can be looked at in a second.
//
// It lives beside `imgcmp` and links the same library for the same reason that
// tool gives: this is how a rendered frame gets looked at, so it must not depend
// on anything under `engine/` and must keep working when the engine build is
// broken.
//
// Developer-facing English, not the i18n catalog: R3 governs engine and
// game-facing output, and a repo tool run by the agent is neither.

#include "luaug/imgcmp/image.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view kUsage = "usage: imgstrip <out.png> <in0.png> <in1.png> [...]\n"
                                    "  Lays the inputs side by side, left to right, into one image.\n"
                                    "  Every input must have the same dimensions.\n";

} // namespace

int main(int argc, char** argv)
{
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    if (args.size() < 3) {
        std::fputs(kUsage.data(), stderr);
        return 2;
    }

    std::vector<luaug::imgcmp::Image> frames;
    frames.reserve(args.size() - 1);
    for (std::size_t index = 1; index < args.size(); ++index) {
        luaug::imgcmp::LoadResult loaded = luaug::imgcmp::loadPngFile(std::string(args[index]));
        if (!loaded.ok) {
            std::fprintf(stderr, "imgstrip: %s\n", loaded.error.c_str());
            return 1;
        }
        // Dimensions must agree, and this refuses rather than scaling: a strip
        // whose frames were silently resampled is no longer evidence about the
        // frames.
        if (!frames.empty() &&
            (loaded.image.width != frames.front().width || loaded.image.height != frames.front().height)) {
            std::fprintf(stderr, "imgstrip: %s is %dx%d, expected %dx%d\n", std::string(args[index]).c_str(),
                         loaded.image.width, loaded.image.height, frames.front().width, frames.front().height);
            return 1;
        }
        frames.push_back(std::move(loaded.image));
    }

    const int frameWidth = frames.front().width;
    const int height = frames.front().height;
    luaug::imgcmp::Image strip = luaug::imgcmp::makeImage(frameWidth * static_cast<int>(frames.size()), height, 0xFF);

    for (std::size_t index = 0; index < frames.size(); ++index) {
        const luaug::imgcmp::Image& frame = frames[index];
        const std::size_t columnOffset = index * static_cast<std::size_t>(frameWidth);
        for (std::size_t row = 0; row < static_cast<std::size_t>(height); ++row) {
            const std::uint8_t* source = frame.rgba.data() + row * static_cast<std::size_t>(frameWidth) * 4u;
            std::uint8_t* destination =
                strip.rgba.data() + (row * static_cast<std::size_t>(strip.width) + columnOffset) * 4u;
            std::copy(source, source + static_cast<std::size_t>(frameWidth) * 4u, destination);
        }
    }

    const luaug::imgcmp::WriteResult written = luaug::imgcmp::writePngFile(std::string(args[0]), strip);
    if (!written.ok) {
        std::fprintf(stderr, "imgstrip: %s\n", written.error.c_str());
        return 1;
    }

    std::printf("imgstrip: %zu frames, %dx%d -> %s\n", frames.size(), strip.width, strip.height,
                std::string(args[0]).c_str());
    return 0;
}
