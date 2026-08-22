#include "luaug/rhi/backends.h"
#include "luaug/rhi/capture.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <doctest/doctest.h>
#include <span>
#include <string>
#include <vector>

using namespace luaug::rhi;

namespace {

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
    const BufferHandle vertices =
        device->createBuffer({.usage = BufferUsage::Vertex, .sizeBytes = 48, .debugName = "cube"});

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

// One upload, recorded, so a test can read what the summary made of it. D026:
// this call recorded a byte count through M7.5, so a frame that drew the same
// number of different vertices recorded identically.
std::string recordUpload(BufferUsage usage, std::span<const std::byte> data)
{
    const auto device = createCaptureDevice({.backend = BackendId::Capture});
    REQUIRE(device != nullptr);
    const BufferHandle buffer =
        device->createBuffer({.usage = usage, .sizeBytes = static_cast<luaug::core::u32>(data.size())});

    ICmdList* cmd = device->beginFrame();
    cmd->upload(buffer, data, 0);
    device->submitAndPresent();

    const std::string stream = captureStream(*device);
    const std::size_t line = stream.find(R"({"op":"upload")");
    REQUIRE(line != std::string::npos);
    return stream.substr(line, stream.find('\n', line) - line);
}

// The bytes of `count` floats, so a test can say what it means in floats and
// hand the device the bytes it actually takes.
std::vector<std::byte> bytesOf(std::span<const float> values)
{
    std::vector<std::byte> out(values.size() * sizeof(float));
    std::memcpy(out.data(), values.data(), out.size());
    return out;
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

TEST_CASE("an upload is recorded by what it carried, not by how much")
{
    // The defect itself: two uploads of the same SIZE and different contents.
    // Through M7.5 these recorded identically, so the two UI goldens would have
    // been byte-identical if only the quads had moved -- which is how D026 was
    // found.
    const std::array<float, 8> before{0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    const std::array<float, 8> after{0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.5f, 0.0f, 1.0f};

    const std::vector<std::byte> beforeBytes = bytesOf(before);
    const std::vector<std::byte> afterBytes = bytesOf(after);
    REQUIRE(beforeBytes.size() == afterBytes.size());

    CHECK(recordUpload(BufferUsage::Vertex, beforeBytes) != recordUpload(BufferUsage::Vertex, afterBytes));
}

TEST_CASE("reordering an upload is visible even when the mean is not")
{
    // A permutation leaves the mean exactly where it was, which is why `flow`
    // exists beside it.
    const std::array<float, 4> ordered{0.0f, 1.0f, 2.0f, 3.0f};
    const std::array<float, 4> shuffled{0.0f, 2.0f, 1.0f, 3.0f};

    CHECK(recordUpload(BufferUsage::Vertex, bytesOf(ordered)) != recordUpload(BufferUsage::Vertex, bytesOf(shuffled)));
}

TEST_CASE("an index buffer is hashed as integers rather than read as floats")
{
    // Index 1,000 read as an f32 is a denormal around 1.4e-42, so a summary that
    // guessed at the type would give every index buffer in the engine the same
    // answer. That is D042's mistake -- reinterpreting a word instead of
    // converting it -- and the kind comes from `BufferDesc` so it cannot recur.
    const std::array<luaug::core::u32, 6> indices{0u, 1u, 2u, 2u, 1u, 3u};
    const std::array<luaug::core::u32, 6> different{0u, 1u, 2u, 2u, 3u, 1u};

    std::vector<std::byte> a(indices.size() * sizeof(luaug::core::u32));
    std::vector<std::byte> b(a.size());
    std::memcpy(a.data(), indices.data(), a.size());
    std::memcpy(b.data(), different.data(), b.size());

    const std::string recorded = recordUpload(BufferUsage::Index, a);
    CHECK(recorded.find(R"("packedWords":6)") != std::string::npos);
    CHECK(recorded != recordUpload(BufferUsage::Index, b));
}

TEST_CASE("a packed colour is not summed as the enormous float its bits spell")
{
    // A `render::UiVertex` colour with alpha 127 reads as a NORMAL float of
    // about 1.7e38 -- nothing about the bits says otherwise. Summing it
    // overflowed the printed quantity, and the two tiers disagreed about what an
    // out-of-range conversion produces. The magnitude band is what stops it.
    const std::array<luaug::core::u32, 4> vertex{0x00000000u, 0x3F800000u, 0x7F204060u, 0x00000000u};
    std::vector<std::byte> data(vertex.size() * sizeof(luaug::core::u32));
    std::memcpy(data.data(), vertex.data(), data.size());

    const std::string recorded = recordUpload(BufferUsage::Vertex, data);
    CHECK(recorded.find(R"("packedWords":1)") != std::string::npos);
    // Three ordinary floats averaging a third, rather than a number no i64 can
    // hold.
    CHECK(recorded.find(R"("mean":0.3333)") != std::string::npos);
}
