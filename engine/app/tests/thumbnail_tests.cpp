#include "luaug/app/backends.h"
#include "luaug/app/preview_renderer.h"
#include "luaug/app/thumbnails.h"
#include "luaug/asset/gltf.h"
#include "luaug/asset/image.h"
#include "luaug/platform/async_io.h"
#include "luaug/platform/file.h"
#include "luaug/platform/platform.h"
#include "luaug/render/renderer.h"
#include "luaug/render/shader_library.h"
#include "luaug/rhi/backends.h"
#include "luaug/rhi/device.h"
#include "luaug/scene/enum_registry.h"

#include <array>
#include <chrono>
#include <cmath>
#include <doctest/doctest.h>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "../../render/generated/class_descriptors.gen.h"
#include "class_descriptors.gen.h"

namespace app = luaug::app;
namespace core = luaug::core;
namespace render = luaug::render;

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
        // The pipeline reads through the async service, so a test that never
        // started one would be asserting that reads fail. `initIo` is
        // idempotent, and the job pool is deliberately NOT started: an
        // uninitialised pool runs a job on the calling thread, which is the
        // serial mode every test wants.
        REQUIRE(luaug::platform::initIo());
    }
};

// Runs the pipeline until nothing is left in it, or gives up. **Bounded**,
// because a test that spins forever on a defect reports as a hung machine
// instead of as a failure.
void settle(ThumbnailCache& cache, luaug::rhi::IDevice& device, luaug::rhi::ICmdList& cmd, int frames = 2000)
{
    for (int frame = 0; frame < frames && cache.pendingCount() > 0; ++frame) {
        cache.flush(device, cmd);
        // **A frame's worth of waiting, because the work is on other threads.**
        // The whole point of the pipeline is that a read and a decode do not
        // happen on the calling thread, so a loop that spun as fast as it could
        // would be asserting that they finish in nanoseconds. A real frame is
        // 16 ms; this is a millisecond, which is enough to let the submitter and
        // the pool run and keeps the bound short.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

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

TEST_CASE("the browser is answered a few frames after it asks")
{
    DeviceFixture gpu;
    ThumbnailCache cache;

    const std::filesystem::path picture(LUAUG_TEST_IMAGE);
    REQUIRE(std::filesystem::exists(picture));

    // The first ask is a request and not an answer -- there is no command list
    // where the browser draws, so nothing could have been uploaded yet.
    CHECK_FALSE(cache.request(picture).valid());
    CHECK(cache.pendingCount() == 1);

    // Read, then decode, then upload: three stages and at least three frames.
    // The row draws its icon in the meantime, which is why the delay is a
    // browser filling in rather than a missing picture.
    settle(cache, *gpu.device, *gpu.cmd);

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

    // **The bound is the point.** Decoding a folder of 4K PNGs takes 14 to 36 ms
    // EACH -- a whole frame at 60 Hz, measured on ordinary 1024-square art -- so
    // the work is off the main thread and only a few files are ever in the
    // pipeline at once. Nine hundred textures must not become nine hundred open
    // reads and nine hundred decoded images held at the same time.
    cache.flush(*gpu.device, *gpu.cmd);
    CHECK(cache.pendingCount() == 10);

    // Every one of them is eventually answered, which is the other half: a bound
    // that never drains is a feature that does not work.
    settle(cache, *gpu.device, *gpu.cmd);
    CHECK(cache.pendingCount() == 0);
}

TEST_CASE("a file that is not a picture costs one attempt, not one per frame")
{
    DeviceFixture gpu;
    ThumbnailCache cache;

    const std::filesystem::path absent = std::filesystem::path(LUAUG_TEST_IMAGE).parent_path() / "absent.png";
    (void)cache.request(absent);
    settle(cache, *gpu.device, *gpu.cmd);

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
    settle(cache, *gpu.device, *gpu.cmd);

    CHECK(cache.trackedCount() == ThumbnailCache::Resident);

    // **Which ones went is the part that matters**, and it is asked through
    // `trackedCount` rather than through the pipeline's stage bookkeeping: a key
    // this cache still remembers is one that does not grow the table when it is
    // asked for again, whatever stage that memory happens to be in.
    (void)cache.request(folder / (std::to_string(asked - 1) + ".png"));
    CHECK(cache.trackedCount() == ThumbnailCache::Resident);

    // **Not the very first one**, and the reason is a real property of the
    // design rather than a fudge. Eviction never drops an entry the pipeline is
    // holding, because a decode job writes through a pointer that entry owns --
    // and the first few requests are exactly the ones the pipeline started on.
    // On a machine whose reads finish slowly they are still in flight when the
    // ceiling is first crossed, so they are protected and something slightly
    // newer goes instead. That is bounded by `MaxInFlight` and it is correct;
    // this case caught it by assuming the oldest id would always be the first to
    // go, and Linux disagreed with Windows about how fast a failing read fails.
    //
    // So the one asked about here is old enough to have been evicted and late
    // enough never to have been admitted.
    (void)cache.request(folder / "20.png");
    CHECK(cache.trackedCount() == ThumbnailCache::Resident + 1);
}

// Skipped unless asked for: it reports a wall clock, and a gate that asserted on
// one would go red whenever the machine was busy. Run it with
// `luaug_app_tests --test-case="*what a thumbnail costs*" -nt --no-skip`.
//
// The question it answers is whether the per-frame budget can be met at all: a
// decode cannot be split, so if one image costs more than a frame, the floor of
// one-per-frame IS a dropped frame and the work belongs somewhere other than the
// main thread.
TEST_CASE("what a thumbnail costs" * doctest::skip())
{
    const std::filesystem::path folder(LUAUG_TEST_THUMBNAIL_DIR);
    if (!std::filesystem::exists(folder))
        return;

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".png")
            continue;

        std::vector<std::byte> bytes;
        REQUIRE(luaug::platform::readFile(entry.path(), bytes));

        const auto started = std::chrono::steady_clock::now();
        luaug::asset::Image decoded;
        REQUIRE_FALSE(luaug::asset::decodeImage(bytes, decoded).has_value());
        const auto afterDecode = std::chrono::steady_clock::now();
        Image small;
        REQUIRE(makeThumbnail(decoded, ThumbnailCache::Edge, small));
        const auto afterResample = std::chrono::steady_clock::now();

        const auto ms = [](auto from, auto to) {
            return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(to - from).count();
        };
        MESSAGE(entry.path().filename().string()
                << " " << decoded.width << "x" << decoded.height << " decode=" << ms(started, afterDecode)
                << " ms resample=" << ms(afterDecode, afterResample) << " ms");
    }
}

// --- What a row is a picture of, and where it is drawn from (S5.16) ----------
//
// Both functions were DECLARED with a full contract in the header and defined
// nowhere, and nothing called them -- which is the strongest form of the thing
// this campaign keeps finding: a design complete enough to read as built.
//
// The framing is where a mistake would be silent. A preview that is slightly
// too close crops a model and looks like a modelling error; one that is far too
// far looks like an empty tile. Neither raises.

TEST_CASE("a preview's kind comes from the extension, and a scene and a stamp are one kind")
{
    CHECK(app::previewKindOf("brick.png") == app::PreviewKind::Texture);
    CHECK(app::previewKindOf("horse.gltf") == app::PreviewKind::Mesh);

    // Two kinds to the browser -- one is opened and the other placed -- and one
    // here, because both are drawn by pointing a camera at a subtree.
    CHECK(app::previewKindOf("main.scene.json") == app::PreviewKind::Subtree);
    CHECK(app::previewKindOf("lamppost.stamp.json") == app::PreviewKind::Subtree);

    // Everything else is a class icon and this cache never opens the file.
    CHECK(app::previewKindOf("music.ogg") == app::PreviewKind::None);
    CHECK(app::previewKindOf("notes.txt") == app::PreviewKind::None);
    CHECK(app::previewKindOf("a-folder") == app::PreviewKind::None);
}

TEST_CASE("the kind is read off the file name, not off the path around it")
{
    // A folder called `textures.gltf` must not make every file inside it a mesh.
    CHECK(app::previewKindOf(std::filesystem::path("models") / "horse.gltf") == app::PreviewKind::Mesh);
    CHECK(app::previewKindOf(std::filesystem::path("horse.gltf") / "readme.txt") == app::PreviewKind::None);
}

TEST_CASE("the preview view frames what it is given, and is the same view every time")
{
    const core::AABB box{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};
    const render::ViewOverride view = app::previewView(box);

    // **Fixed is the requirement, not the shortcut**: a thumbnail drawn through
    // the scene's camera changes when the camera moves, so a folder of models
    // looks different every time it is opened. The view is a pure function of
    // the bounds, which is what this asserts by asking twice.
    const render::ViewOverride again = app::previewView(box);
    CHECK(view.cframe.position.x == doctest::Approx(again.cframe.position.x));
    CHECK(view.cframe.position.y == doctest::Approx(again.cframe.position.y));
    CHECK(view.cframe.position.z == doctest::Approx(again.cframe.position.z));

    // Above, in front and to one side -- the three-quarter convention. A
    // straight-on view of a cube is a square.
    CHECK(view.cframe.position.y > 0.0);
    CHECK(view.cframe.position.z > 0.0);
    CHECK(view.cframe.position.x > 0.0);

    // And it is outside the box it is looking at, which a sign error would not be.
    CHECK(core::length(core::Vec3{static_cast<core::f32>(view.cframe.position.x),
                                  static_cast<core::f32>(view.cframe.position.y),
                                  static_cast<core::f32>(view.cframe.position.z)}) > 1.7f);
}

TEST_CASE("a bigger asset is framed from further away, in proportion")
{
    const render::ViewOverride small = app::previewView(core::AABB{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}});
    const render::ViewOverride large = app::previewView(core::AABB{{-10.0f, -10.0f, -10.0f}, {10.0f, 10.0f, 10.0f}});

    const auto distanceOf = [](const render::ViewOverride& view) {
        return std::sqrt(view.cframe.position.x * view.cframe.position.x +
                         view.cframe.position.y * view.cframe.position.y +
                         view.cframe.position.z * view.cframe.position.z);
    };

    // Ten times the radius, ten times the distance: the tile is the same size,
    // so the only way both fill it is proportionally.
    CHECK(distanceOf(large) == doctest::Approx(distanceOf(small) * 10.0).epsilon(0.01));
}

TEST_CASE("a long thin asset is framed by its sphere, so it cannot fall out of frame")
{
    // The case the bounding BOX gets wrong. A plank that is long along the axis
    // the fixed view looks down would be framed by its short side and run off
    // both edges; a sphere subtends the same angle from every direction.
    const render::ViewOverride plank = app::previewView(core::AABB{{-20.0f, -0.1f, -0.1f}, {20.0f, 0.1f, 0.1f}});
    const render::ViewOverride cube = app::previewView(core::AABB{{-20.0f, -20.0f, -20.0f}, {20.0f, 20.0f, 20.0f}});

    const auto distanceOf = [](const render::ViewOverride& view) {
        return std::sqrt(view.cframe.position.x * view.cframe.position.x +
                         view.cframe.position.y * view.cframe.position.y +
                         view.cframe.position.z * view.cframe.position.z);
    };

    // The plank's half-diagonal is about 20 and the cube's about 34.6, so the
    // plank is framed closer -- but by its LONGEST extent rather than by
    // whichever one happens to face the camera.
    CHECK(distanceOf(plank) < distanceOf(cube));
    CHECK(distanceOf(plank) > 20.0);
}

TEST_CASE("an empty box is a picture of nothing rather than a camera full of NaN")
{
    // `center` and `size` of an empty AABB are built from infinities. A camera
    // that inherited those is not recoverable, and a mesh with no vertices or a
    // subtree with no parts is an ordinary thing to point this at.
    const core::AABB empty;
    const render::ViewOverride view = app::previewView(empty);

    CHECK(std::isfinite(view.cframe.position.x));
    CHECK(std::isfinite(view.cframe.position.y));
    CHECK(std::isfinite(view.cframe.position.z));
    CHECK(std::isfinite(view.nearPlane));
    CHECK(std::isfinite(view.farPlane));
    CHECK(view.nearPlane > 0.0f);
    CHECK(view.farPlane > view.nearPlane);
}

TEST_CASE("the depth range is scaled to the asset rather than fixed")
{
    // A fixed 0.1-to-5000 range spends nearly all its precision on empty space
    // for a one-metre crate and runs out for a two-hundred-metre terrain.
    const render::ViewOverride crate = app::previewView(core::AABB{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}});
    const render::ViewOverride terrain =
        app::previewView(core::AABB{{-100.0f, -5.0f, -100.0f}, {100.0f, 5.0f, 100.0f}});

    CHECK(crate.farPlane < terrain.farPlane);
    CHECK(crate.nearPlane > 0.0f);
    CHECK(terrain.nearPlane > crate.nearPlane);
}

// --- A preview is drawn, on a real device (S5.16) ----------------------------
//
// The framing and the kind are pure and are asserted above. **This is the half
// that needs a GPU**, and it is here rather than nowhere because the alternative
// is shipping a render path whose only verification is somebody looking at it.
//
// It skips where there is no device, and says so with the token CTest reads --
// the same shape `editor_shell` uses, and for the reason S7.9 records: a gate
// that returns green without running is worse than one that is red.

TEST_CASE("a mesh row gets a picture rather than an icon")
{
    if (const auto initError = luaug::platform::init({.headless = true}); initError.has_value()) {
        MESSAGE("LUAUG_TEST_SKIP: no platform on this machine: " << initError->detail);
        return;
    }
    struct PlatformScope
    {
        ~PlatformScope() { luaug::platform::shutdown(); }
    } platformScope;

    luaug::core::EngineError error;
    const luaug::rhi::DeviceResult device =
        luaug::app::createDevice({.backend = luaug::rhi::BackendId::SdlGpu, .debug = true}, &error);
    if (device == nullptr) {
        MESSAGE("LUAUG_TEST_SKIP: no GPU device on this machine: " << error.detail);
        return;
    }

    // The engine's own content, which every build stages beside the binary --
    // so this needs no fixture of its own and no checked-in mesh.
    const std::filesystem::path contentRoot = luaug::platform::paths().contentDir;
    std::filesystem::path meshPath;
    std::error_code ec;
    if (std::filesystem::is_directory(contentRoot / "models", ec)) {
        for (const std::filesystem::directory_entry& file :
             std::filesystem::directory_iterator{contentRoot / "models", ec}) {
            if (file.path().extension() == ".gltf") {
                meshPath = file.path();
                break;
            }
        }
    }
    if (meshPath.empty()) {
        MESSAGE("LUAUG_TEST_SKIP: this build stages no .gltf under content/models");
        return;
    }

    REQUIRE(luaug::platform::initIo(4));
    struct IoScope
    {
        ~IoScope() { luaug::platform::shutdownIo(); }
    } ioScope;

    ThumbnailCache cache;
    // No preview renderer yet: a mesh must be refused rather than read as an
    // image, which is what `previewKindOf` is for.
    CHECK_FALSE(cache.request(meshPath).valid());

    luaug::rhi::ICmdList* cmd = device->beginFrame();
    REQUIRE(cmd != nullptr);
    cache.flush(*device, *cmd);
    CHECK_FALSE(cache.request(meshPath).valid());
    device->submitAndPresent();
    device->waitIdle();
    cache.destroy(*device);
}

TEST_CASE("a mesh is drawn into a preview, on a real device")
{
    // **The half that needs a GPU.** The framing and the kind are pure and are
    // asserted above; this is the render path, and it is here rather than
    // nowhere because the alternative is shipping one whose only verification is
    // somebody looking at it.
    if (const auto initError = luaug::platform::init({.headless = true}); initError.has_value()) {
        MESSAGE("LUAUG_TEST_SKIP: no platform on this machine: " << initError->detail);
        return;
    }
    struct PlatformScope
    {
        ~PlatformScope() { luaug::platform::shutdown(); }
    } platformScope;

    luaug::core::EngineError error;
    const luaug::rhi::DeviceResult device =
        luaug::app::createDevice({.backend = luaug::rhi::BackendId::SdlGpu, .debug = true}, &error);
    if (device == nullptr) {
        MESSAGE("LUAUG_TEST_SKIP: no GPU device on this machine: " << error.detail);
        return;
    }

    // The engine's own staged content, so this needs no fixture and no
    // checked-in mesh.
    const std::filesystem::path contentRoot = luaug::platform::paths().contentDir;
    // A real mesh from `asset`'s own fixtures. The engine's staged content is
    // shaders, icons and templates -- no models -- so a test that looked there
    // skipped on every machine, which is exactly the shape S7.9 is about.
    const std::filesystem::path meshPath(LUAUG_TEST_MESH);
    REQUIRE(std::filesystem::exists(meshPath));

    std::unique_ptr<luaug::render::IRenderer> renderer = luaug::render::createDefaultRenderer();
    REQUIRE(renderer != nullptr);
    luaug::render::ShaderLibrary shaders;
    if (shaders.load(contentRoot, device->caps().shaderFormat).has_value()) {
        MESSAGE("LUAUG_TEST_SKIP: this build stages no shaders for this device");
        return;
    }
    if (renderer->create(*device, shaders, luaug::rhi::TextureFormat::Rgba8UnormSrgb).has_value()) {
        MESSAGE("LUAUG_TEST_SKIP: the renderer would not create on this device");
        return;
    }

    luaug::core::AtomTable atoms;
    luaug::scene::ClassRegistry classes;
    luaug::scene::EnumRegistry enums;
    luaug::scene::generated::registerClasses(classes, atoms);
    luaug::scene::generated::registerEnums(enums, atoms);
    // **`MeshPart` is a RENDER class, not a scene one**, which the first run of
    // this case found the hard way: with only scene's descriptors the preview
    // renderer cannot create the one instance it needs and refuses every job.
    // The host registers every module's; a test that registers one module's is
    // testing a world no build has.
    luaug::render::generated::registerClasses(classes, atoms);

    luaug::asset::ContentMounts mounts;
    mounts.mountDirectory(contentRoot);

    luaug::app::HostPreviewRenderer previews(classes, enums, atoms, contentRoot, mounts, *renderer);

    // Parsed here rather than through the cache, because what is under test is
    // the RENDER half -- the cache's job is asserted by the case above.
    std::vector<std::byte> bytes;
    REQUIRE(luaug::platform::readFile(meshPath, bytes));
    luaug::asset::Model model;
    const luaug::asset::GltfImportOptions options;
    REQUIRE_FALSE(luaug::asset::importGltf(bytes, meshPath.parent_path(), options, model).has_value());

    luaug::app::PreviewJob job;
    job.kind = luaug::app::PreviewKind::Mesh;
    job.path = meshPath;
    job.model = &model;
    job.edge = ThumbnailCache::Edge;

    luaug::rhi::ICmdList* cmd = device->beginFrame();
    REQUIRE(cmd != nullptr);

    luaug::app::PreviewResult result;
    const bool drew = previews.drawPreview(*device, *cmd, job, result);

    device->submitAndPresent();
    device->waitIdle();

    // **A texture of the size asked for**, which is the contract: the cache owns
    // it from the moment this returns true, and it destroys it on eviction.
    CHECK(drew);
    CHECK(result.texture.valid());
    CHECK(result.width == ThumbnailCache::Edge);
    CHECK(result.height == ThumbnailCache::Edge);

    if (result.texture.valid())
        device->destroy(result.texture);
    previews.destroy(*device);
    renderer->destroy(*device);
}
