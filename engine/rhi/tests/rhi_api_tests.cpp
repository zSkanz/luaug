#include <doctest/doctest.h>

#include "luaug/rhi/descs.h"
#include "luaug/rhi/types.h"

using namespace luaug::rhi;

// Header-only: this file links no backend on purpose. If it ever stops
// compiling without one, the seam has grown an implementation dependency.

TEST_CASE("handles default to null and are not interchangeable")
{
    // The point of separate structs is that a compiler, not a code reviewer,
    // catches a TextureHandle passed where a BufferHandle belongs. That cannot
    // be asserted at runtime, so what is asserted here is the other half of the
    // contract: zero is null, and a backend never hands zero out.
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
    CHECK(isDepthFormat(TextureFormat::D24UnormS8Uint));
    CHECK_FALSE(isDepthFormat(TextureFormat::Rgba8Unorm));
}

TEST_CASE("descriptors default to the common case")
{
    // Call sites name only what they care about, which is what keeps a
    // pipeline description readable and a capture stream diffable.
    constexpr GraphicsPipelineDesc pipeline{};

    CHECK(pipeline.primitive == PrimitiveType::TriangleList);
    CHECK(pipeline.rasterizer.cullMode == CullMode::Back);
    CHECK(pipeline.rasterizer.frontFace == FrontFace::CounterClockwise);
    CHECK_FALSE(pipeline.depthStencil.depthTest);
    CHECK(pipeline.depthStencilFormat == TextureFormat::Undefined);

    constexpr ColorAttachment attachment{};
    CHECK(attachment.loadOp == LoadOp::Clear);
    CHECK(attachment.storeOp == StoreOp::Store);
}
