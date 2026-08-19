#include <doctest/doctest.h>

#include <array>
#include <cstddef>

#include "luaug/rhi/backends.h"

using namespace luaug::rhi;

TEST_CASE("the null backend satisfies the interface")
{
    const auto device = createNullDevice({.backend = BackendId::Null});
    REQUIRE(device != nullptr);
    CHECK(device->backend() == BackendId::Null);

    // A caller must be able to tell that this device will never produce a
    // picture without knowing which backend it got.
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
        const TextureHandle target = device->createTexture({
            .format = TextureFormat::Rgba8Unorm,
            .usage = TextureUsage::ColorTarget,
            .width = 4,
            .height = 4,
        });
        const std::array<ColorAttachment, 1> colors{ColorAttachment{
            .texture = target,
            .clearColor = {0.1f, 0.2f, 0.3f, 1.0f},
        }};

        ICmdList* cmd = device->beginFrame();
        REQUIRE(cmd != nullptr);

        cmd->pushDebugGroup("test");
        cmd->beginRenderPass({.colorAttachments = colors});
        cmd->setViewport({.width = 4.0f, .height = 4.0f});
        cmd->draw(3, 1, 0, 0);
        cmd->endRenderPass();
        cmd->popDebugGroup();

        device->submitAndPresent();
        device->waitIdle();
    }

    SUBCASE("no readback, reported rather than crashed")
    {
        std::array<std::byte, 16> pixels{};
        CHECK_FALSE(device->readTexture(TextureHandle{1}, pixels));
    }
}
