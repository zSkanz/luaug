#include "luaug/platform/platform.h"
#include "luaug/rhi/backends.h"

#include <array>
#include <cstddef>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace luaug::rhi;

namespace {

// A GPU device is not guaranteed on every machine this runs on -- a Linux CI
// runner has no Vulkan driver unless lavapipe is installed, and the roadmap
// already allows the render gate to fall back to the dev machine. So a missing
// device is reported and skipped, never failed: a red build that means "this
// runner has no GPU" trains people to ignore red builds.
//
// What is NOT skipped is a device that exists and then misbehaves.
struct GpuFixture
{
    GpuFixture()
    {
        const auto initError = luaug::platform::init({.headless = true});
        REQUIRE_MESSAGE(!initError.has_value(), (initError ? initError->detail : std::string{}));

        luaug::core::EngineError error;
        device = createSdlGpuDevice({.backend = BackendId::SdlGpu, .debug = true}, &error);
        if (device == nullptr)
            MESSAGE("no GPU device on this machine, skipping: " << error.detail);
    }

    ~GpuFixture()
    {
        device.reset();
        luaug::platform::shutdown();
    }

    GpuFixture(const GpuFixture&) = delete;
    GpuFixture& operator=(const GpuFixture&) = delete;

    DeviceResult device;
};

} // namespace

TEST_CASE("a real device reports itself and a usable shader format")
{
    GpuFixture gpu;
    if (gpu.device == nullptr)
        return;

    CHECK(gpu.device->backend() == BackendId::SdlGpu);

    // The difference that matters to a caller deciding whether a readback is
    // worth doing.
    CHECK(gpu.device->caps().rendersPixels);
    CHECK(gpu.device->caps().shaderFormat != ShaderFormat::Unknown);
}

TEST_CASE("first light: a cleared target reads back as the colour it was cleared to")
{
    GpuFixture gpu;
    if (gpu.device == nullptr)
        return;

    IDevice& device = *gpu.device;

    // No shaders, no draws: a clear is a complete frame, and it exercises
    // exactly the path the screenshot harness depends on -- create a target,
    // open a render pass, submit, read the pixels back. If this works, the
    // agent has eyes.
    constexpr u32 kSize = 8;
    const TextureHandle target = device.createTexture({
        .format = TextureFormat::Rgba8Unorm,
        .usage = TextureUsage::ColorTarget,
        .width = kSize,
        .height = kSize,
        .debugName = "first-light",
    });
    REQUIRE(target.valid());

    // 0.5 is deliberate: it is not 0 or 255, so a channel that is dropped,
    // swizzled or written as a constant shows up rather than matching by luck.
    const std::array<ColorAttachment, 1> colors{ColorAttachment{
        .texture = target,
        .loadOp = LoadOp::Clear,
        .storeOp = StoreOp::Store,
        .clearColor = {1.0f, 0.5f, 0.0f, 1.0f},
    }};

    ICmdList* cmd = device.beginFrame();
    REQUIRE(cmd != nullptr);
    cmd->pushDebugGroup("first-light");
    cmd->beginRenderPass({.colorAttachments = colors, .debugName = "clear"});
    cmd->endRenderPass();
    cmd->popDebugGroup();
    device.submitAndPresent();

    std::vector<std::byte> pixels(static_cast<std::size_t>(kSize) * kSize * 4);
    REQUIRE(device.readTexture(target, pixels));

    const auto channel = [&pixels](std::size_t index) { return static_cast<int>(pixels[index]); };

    // Unorm8 rounding of 0.5 lands on 127 or 128 depending on the backend's
    // convention, so the green channel is checked as a range rather than a
    // value. Red, blue and alpha are exact.
    for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(kSize) * kSize; ++pixel) {
        const std::size_t base = pixel * 4;
        REQUIRE(channel(base + 0) == 255);
        REQUIRE(channel(base + 1) >= 127);
        REQUIRE(channel(base + 1) <= 128);
        REQUIRE(channel(base + 2) == 0);
        REQUIRE(channel(base + 3) == 255);
    }

    device.destroy(target);
}

TEST_CASE("resources round-trip through the device")
{
    GpuFixture gpu;
    if (gpu.device == nullptr)
        return;

    IDevice& device = *gpu.device;

    const BufferHandle vertices =
        device.createBuffer({.usage = BufferUsage::Vertex, .sizeBytes = 64, .debugName = "verts"});
    const SamplerHandle sampler = device.createSampler({.debugName = "linear"});

    CHECK(vertices.valid());
    CHECK(sampler.valid());

    SUBCASE("an upload outside a render pass is accepted")
    {
        const std::array<std::byte, 16> data{};
        ICmdList* cmd = device.beginFrame();
        cmd->upload(vertices, data, 0);
        device.submitAndPresent();
        device.waitIdle();
    }

    SUBCASE("a destroyed handle does not resolve to something else")
    {
        device.destroy(vertices);

        // Ids are never recycled, so the stale handle stays dead rather than
        // silently addressing whatever was created next.
        const BufferHandle next = device.createBuffer({.usage = BufferUsage::Vertex, .sizeBytes = 32});
        CHECK_FALSE(next == vertices);
    }

    device.destroy(sampler);
}
