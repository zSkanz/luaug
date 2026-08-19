#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <string>

#include "luaug/rhi/backends.h"
#include "luaug/rhi/capture.h"

using namespace luaug::rhi;

namespace
{

// One small frame, recorded twice, to prove the stream depends on the calls and
// on nothing else.
std::string recordFrame()
{
    const auto device = createCaptureDevice({.backend = BackendId::Capture});
    REQUIRE(device != nullptr);

    const TextureHandle target = device->createTexture({
        .format = TextureFormat::Rgba8Unorm,
        .usage = TextureUsage::ColorTarget,
        .width = 64,
        .height = 64,
        .debugName = "scene-color",
    });
    const BufferHandle vertices
        = device->createBuffer({.usage = BufferUsage::Vertex, .sizeBytes = 48, .debugName = "cube"});

    const std::array<ColorAttachment, 1> colors{ColorAttachment{
        .texture = target,
        .clearColor = {0.25f, 0.5f, 0.75f, 1.0f},
    }};
    const std::array<BufferHandle, 1> vertexBuffers{vertices};

    ICmdList* cmd = device->beginFrame();
    cmd->pushDebugGroup("debug-draw");
    cmd->beginRenderPass({.colorAttachments = colors, .debugName = "main"});
    cmd->setViewport({.width = 64.0f, .height = 64.0f});
    cmd->bindVertexBuffers(0, vertexBuffers);
    cmd->draw(36, 1, 0, 0);
    cmd->endRenderPass();
    cmd->popDebugGroup();
    device->submitAndPresent();

    return captureStream(*device);
}

} // namespace

TEST_CASE("the same frame records byte-identically")
{
    // The whole gate rests on this. If two runs of one frame can differ, a
    // golden comparison reports noise and gets switched off.
    CHECK(recordFrame() == recordFrame());
}

TEST_CASE("the stream records the calls that were made")
{
    const std::string stream = recordFrame();

    CHECK(stream.find(R"({"op":"createTexture","texture":1,"format":"Rgba8Unorm")") != std::string::npos);
    CHECK(stream.find(R"("name":"scene-color")") != std::string::npos);
    CHECK(stream.find(R"({"op":"draw","vertices":36,"instances":1)") != std::string::npos);
    CHECK(stream.find(R"({"op":"pushDebugGroup","name":"debug-draw"})") != std::string::npos);
    CHECK(stream.find(R"({"op":"endRenderPass"})") != std::string::npos);

    // One JSON object per line, so a failing gate can point at a call.
    CHECK(stream.back() == '\n');
}

TEST_CASE("floats are quantized rather than formatted")
{
    const std::string stream = recordFrame();

    // Four decimals, written from an integer: no libc's float formatting is
    // involved, so an identical frame cannot disagree across platforms.
    CHECK(stream.find(R"("r":0.2500,"g":0.5000,"b":0.7500,"a":1.0000)") != std::string::npos);
}

TEST_CASE("a difference in the frame shows up as a difference in the stream")
{
    const auto device = createCaptureDevice({.backend = BackendId::Capture});
    ICmdList* cmd = device->beginFrame();
    cmd->draw(36, 1, 0, 0);
    const std::string before = captureStream(*device);

    resetCapture(*device);
    cmd = device->beginFrame();
    cmd->draw(37, 1, 0, 0);

    CHECK(captureStream(*device) != before);
}

TEST_CASE("reading a capture from a device that is not one is empty, not a crash")
{
    const auto device = createNullDevice({.backend = BackendId::Null});
    CHECK(captureStream(*device).empty());

    // Also must not corrupt anything.
    resetCapture(*device);
    CHECK(captureStream(*device).empty());
}
