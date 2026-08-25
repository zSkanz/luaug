#include "luaug/app/thumbnails.h"
#include "luaug/asset/image.h"
#include "luaug/rhi/backends.h"

#include <array>
#include <doctest/doctest.h>
#include <filesystem>
#include <initializer_list>
#include <string>

using luaug::app::makeThumbnail;
using luaug::app::ThumbnailCache;
using luaug::asset::Image;
using luaug::core::u32;
using luaug::core::u8;
using luaug::core::usize;

namespace {

// A source built texel by texel, RGBA8, which is what `decodeImage` hands back
// and therefore what the resample has to take.
[[nodiscard]] Image imageOf(u32 width, u32 height, std::initializer_list<std::array<u8, 4>> texels)
{
    Image image;
    image.width = width;
    image.height = height;
    image.sourceChannels = 4;
    image.pixels.reserve(static_cast<usize>(width) * height * 4u);
    for (const std::array<u8, 4>& texel : texels) {
        for (const u8 channel : texel)
            image.pixels.push_back(static_cast<std::byte>(channel));
    }
    REQUIRE(image.valid());
    return image;
}

[[nodiscard]] u8 channelAt(const Image& image, u32 x, u32 y, u32 channel)
{
    const usize index = (static_cast<usize>(y) * image.width + x) * 4u + channel;
    REQUIRE(index < image.pixels.size());
    return static_cast<u8>(image.pixels[index]);
}

struct DeviceFixture
{
    luaug::rhi::DeviceResult device = luaug::rhi::createNullDevice({.backend = luaug::rhi::BackendId::Null});
    luaug::rhi::ICmdList* cmd = nullptr;

    DeviceFixture()
    {
        REQUIRE(device != nullptr);
        cmd = device->beginFrame();
        REQUIRE(cmd != nullptr);
    }
};

} // namespace

TEST_CASE("a thumbnail keeps the aspect of what it is a thumbnail of")
{
    Image out;

    // Wider than tall: the long side lands on the edge and the short one
    // follows. A square result here would be a picture of something else.
    REQUIRE(makeThumbnail(imageOf(4, 1, {{255, 0, 0, 255}, {255, 0, 0, 255}, {255, 0, 0, 255}, {255, 0, 0, 255}}), 128,
                          out));
    CHECK(out.width == 128);
    CHECK(out.height == 32);

    // And taller than wide, which is the same rule read the other way.
    REQUIRE(makeThumbnail(imageOf(1, 4, {{255, 0, 0, 255}, {255, 0, 0, 255}, {255, 0, 0, 255}, {255, 0, 0, 255}}), 64,
                          out));
    CHECK(out.width == 16);
    CHECK(out.height == 64);
}

TEST_CASE("a thumbnail of a very long strip is still a texture")
{
    // 512:1 at an edge of 128 rounds the short side to zero, and a zero-height
    // texture is one the device refuses to make. One texel is the floor.
    Image source;
    source.width = 512;
    source.height = 1;
    source.sourceChannels = 4;
    source.pixels.assign(static_cast<usize>(512) * 4u, static_cast<std::byte>(255));
    REQUIRE(source.valid());

    Image out;
    REQUIRE(makeThumbnail(source, 128, out));
    CHECK(out.width == 128);
    CHECK(out.height == 1);
    CHECK(out.valid());
}

TEST_CASE("halving black and white averages in light, not in bytes")
{
    // **The case that says the resample is correct.** Half black and half white
    // is half the light, and half the light encodes as 188 in sRGB -- not 128.
    // Averaging the stored bytes gives 128, which is the mistake every naive
    // downscale makes and the reason resized images come out muddy.
    Image out;
    REQUIRE(makeThumbnail(imageOf(2, 1, {{0, 0, 0, 255}, {255, 255, 255, 255}}), 1, out));
    REQUIRE(out.width == 1);
    REQUIRE(out.height == 1);

    CHECK(channelAt(out, 0, 0, 0) == 188);
    CHECK(channelAt(out, 0, 0, 1) == 188);
    CHECK(channelAt(out, 0, 0, 2) == 188);
    CHECK(channelAt(out, 0, 0, 3) == 255);
}

TEST_CASE("a transparent texel contributes no colour, only transparency")
{
    // A red texel beside a fully transparent black one. Weighted by alpha, the
    // colour that survives is the red -- because nothing was drawn in the other
    // half and "nothing" has no colour to average in. Unweighted, the result
    // would be a dark red, which is how a cut-out picks up a black fringe.
    Image out;
    REQUIRE(makeThumbnail(imageOf(2, 1, {{255, 0, 0, 255}, {0, 0, 0, 0}}), 1, out));

    CHECK(channelAt(out, 0, 0, 0) == 255);
    CHECK(channelAt(out, 0, 0, 1) == 0);
    CHECK(channelAt(out, 0, 0, 2) == 0);
    // The transparency itself still averages: half of it was see-through.
    CHECK(channelAt(out, 0, 0, 3) == 128);
}

TEST_CASE("a fully transparent source resamples without dividing by nothing")
{
    Image out;
    REQUIRE(makeThumbnail(imageOf(2, 1, {{40, 90, 200, 0}, {10, 20, 30, 0}}), 1, out));
    CHECK(channelAt(out, 0, 0, 3) == 0);
}

TEST_CASE("a thumbnail of nothing is refused rather than made")
{
    Image out;
    CHECK_FALSE(makeThumbnail(Image{}, 128, out));

    Image real = imageOf(1, 1, {{255, 255, 255, 255}});
    CHECK_FALSE(makeThumbnail(real, 0, out));
}

TEST_CASE("the browser is answered on the frame after it asks")
{
    DeviceFixture gpu;
    ThumbnailCache cache;

    const std::filesystem::path picture(LUAUG_TEST_IMAGE);
    REQUIRE(std::filesystem::exists(picture));

    // The first ask is a request and not an answer -- there is no command list
    // where the browser draws, so nothing could have been uploaded yet.
    CHECK_FALSE(cache.request(picture).valid());
    CHECK(cache.pendingCount() == 1);

    cache.flush(*gpu.device, *gpu.cmd);

    const ThumbnailCache::Thumbnail ready = cache.request(picture);
    CHECK(ready.valid());
    CHECK(cache.pendingCount() == 0);
    CHECK(cache.residentCount() == 1);
    // Downscaled on the way in. A browser row is 96 px at its largest and the
    // source is whatever an artist saved.
    CHECK(ready.width <= ThumbnailCache::Edge);
    CHECK(ready.height <= ThumbnailCache::Edge);
    CHECK((ready.width == ThumbnailCache::Edge || ready.height == ThumbnailCache::Edge));
}

TEST_CASE("a folder of pictures fills in over frames rather than in one")
{
    DeviceFixture gpu;
    ThumbnailCache cache;

    // Ten at once, which is what clicking into a folder looks like.
    for (int index = 0; index < 10; ++index)
        (void)cache.request(std::filesystem::path(LUAUG_TEST_IMAGE).parent_path() / (std::to_string(index) + ".png"));
    CHECK(cache.pendingCount() == 10);

    // **The budget is the point.** Decoding a folder of 4K PNGs in one frame is
    // a freeze on exactly the frame somebody just clicked, so a fixed few land
    // per frame and the rest wait.
    cache.flush(*gpu.device, *gpu.cmd);
    CHECK(cache.pendingCount() == 10 - ThumbnailCache::PerFrame);

    cache.flush(*gpu.device, *gpu.cmd);
    CHECK(cache.pendingCount() == 10 - 2 * ThumbnailCache::PerFrame);
}

TEST_CASE("a file that is not a picture costs one attempt, not one per frame")
{
    DeviceFixture gpu;
    ThumbnailCache cache;

    const std::filesystem::path absent = std::filesystem::path(LUAUG_TEST_IMAGE).parent_path() / "absent.png";
    (void)cache.request(absent);
    cache.flush(*gpu.device, *gpu.cmd);

    CHECK(cache.pendingCount() == 0);
    CHECK(cache.residentCount() == 0);
    // Still refused, and still not queued again: a folder holding one bad file
    // must not spend a decode slot on it every frame it is on screen.
    CHECK_FALSE(cache.request(absent).valid());
    CHECK(cache.pendingCount() == 0);
}

TEST_CASE("scrolling forever does not remember forever")
{
    DeviceFixture gpu;
    ThumbnailCache cache;

    // More names than the ceiling, asked for in order, so the earliest ones are
    // the least recently wanted when the ceiling is crossed.
    const std::filesystem::path folder = std::filesystem::path(LUAUG_TEST_IMAGE).parent_path();
    const usize asked = ThumbnailCache::Resident + 40;
    for (usize index = 0; index < asked; ++index) {
        (void)cache.request(folder / (std::to_string(index) + ".png"));
        // A frame each, which is what gives them an order to be evicted in.
        cache.flush(*gpu.device, *gpu.cmd);
    }

    CHECK(cache.trackedCount() == ThumbnailCache::Resident);

    // **Which ones went is the part that matters.** Every one of these names is
    // a file that is not there, so each is remembered as refused -- and asking
    // for a remembered one queues nothing. The newest is still remembered; the
    // oldest is not, so asking again starts it over.
    (void)cache.request(folder / (std::to_string(asked - 1) + ".png"));
    CHECK(cache.pendingCount() == 0);

    (void)cache.request(folder / "0.png");
    CHECK(cache.pendingCount() == 1);
}
