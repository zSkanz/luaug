#include <doctest/doctest.h>

#include <array>
#include <cstddef>

#include "luaug/rhi/backends.h"

using namespace luaug::rhi;

TEST_CASE("handles default to null and are not interchangeable")
{
    // The whole point of separate structs: a compiler, not a code reviewer,
    // catches a TextureHandle passed where a BufferHandle belongs. That cannot
    // be asserted at runtime, so what is asserted here is the other half of the
    // contract -- zero is null, and a backend never hands zero out.
    CHECK_FALSE(BufferHandle{}.valid());
    CHECK_FALSE(TextureHandle{}.valid());
    CHECK_FALSE(SamplerHandle{}.valid());
    CHECK_FALSE(ShaderHandle{}.valid());
    CHECK_FALSE(PipelineHandle{}.valid());

    CHECK(BufferHandle{7} == BufferHandle{7});
    CHECK_FALSE(BufferHandle{7} == BufferHandle{8});
}

TEST_CASE("usage flags compose")
{
    constexpr auto usage = TextureUsage::Sampled | TextureUsage::ColorTarget;

    CHECK(hasUsage(usage, TextureUsage::Sampled));
    CHECK(hasUsage(usage, TextureUsage::ColorTarget));
    CHECK_FALSE(hasUsage(usage, TextureUsage::DepthStencilTarget));

    CHECK(isDepthFormat(TextureFormat::D32Float));
    CHECK_FALSE(isDepthFormat(TextureFormat::Rgba8Unorm));
}

TEST_CASE("the null backend satisfies the interface")
{
    const auto device = createNullDevice({.backend = BackendId::Null});
    REQUIRE(device != nullptr);
    CHECK(device->backend() == BackendId::Null);

    // A caller must be able to tell that this device will never produce a
    // picture, without knowing which backend it got.
    CHECK_FALSE(device->caps().rendersPixels);

    SUBCASE("resources get distinct, non-null handles")
    {
        const BufferHandle vertices = device->createBuffer({.usage = BufferUsage::Vertex, .sizeBytes = 256});
        const BufferHandle indices = device->createBuffer({.usage = BufferUsage::Index, .sizeBytes = 64});
        const TextureHandle target = device->createTexture({
            .format = TextureFormat::Rgba8Unorm,
            .usage = TextureUsage::ColorTarget,
            .width = 64,
            .height = 64,
        });

        CHECK(vertices.valid());
        CHECK(indices.valid());
        CHECK(target.valid());
        CHECK_FALSE(vertices == indices);

        device->destroy(vertices);
        device->destroy(indices);
        device->destroy(target);
    }

    SUBCASE("a frame can be recorded end to end")
    {
        ICmdList* cmd = device->beginFrame();
        REQUIRE(cmd != nullptr);

        const std::array<ColorAttachment, 1> colors{ColorAttachment{
            .texture = device->createTexture({
                .format = TextureFormat::Rgba8Unorm,
                .usage = TextureUsage::ColorTarget,
                .width = 4,
                .height = 4,
            }),
            .clearColor = {0.1f, 0.2f, 0.3f, 1.0f},
        }};

        cmd->pushDebugGroup("test");
        cmd->beginRenderPass({.colorAttachments = colors});
        cmd->setViewport({.width = 4.0f, .height = 4.0f});
        cmd->draw(3, 1, 0, 0);
        cmd->endRenderPass();
        cmd->popDebugGroup();

        device->submitAndPresent();
        device->waitIdle();
    }

    SUBCASE("no swapchain and no readback, reported rather than crashed")
    {
        // The contract every caller has to honour anyway: an invalid swapchain
        // texture is normal -- a minimized window on a real backend looks the
        // same -- so code written against the null device already handles it.
        std::array<std::byte, 16> pixels{};
        CHECK_FALSE(device->readTexture(TextureHandle{1}, pixels));
    }
}
