// Loading compiled shader blobs by name, for the backend the device actually
// got (ADR 0006, architecture.md §8).
//
// The build compiles every `shaders/src/*.hlsl` once per format and writes a
// manifest beside the executable. This reads that manifest, picks the format
// the device reports through `rhi::Capabilities::shaderFormat`, and hands back
// a shader ready to go into a pipeline.
//
// The per-stage resource counts come from the reflection sidecar the build
// emits, not from a number typed here. `SDL_CreateGPUShader` rejects a shader
// whose declared counts disagree with its bindings, and only the shader source
// knows the truth -- so the one place that knows tells the one place that asks.
#pragma once

#include "luaug/core/error.h"
#include "luaug/core/types.h"
#include "luaug/rhi/device.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace luaug::render {

using core::u32;

class ShaderLibrary
{
public:
    // Reads `<contentDir>/shaders/manifest.json` and the reflection sidecars it
    // names. Blobs are not read here -- a manifest with fifty shaders should not
    // cost fifty file reads to answer a question about one.
    [[nodiscard]] std::optional<core::EngineError> load(const std::filesystem::path& contentDir,
                                                        rhi::ShaderFormat format);

    // Creates the shader on the device, reading its blob now. Returns an
    // invalid handle and fills `outError` when the name, the stage or the blob
    // is missing -- all of which mean the content directory disagrees with the
    // binary, which is a deployment problem worth naming precisely.
    [[nodiscard]] rhi::ShaderHandle create(rhi::IDevice& device, std::string_view name, rhi::ShaderStage stage,
                                           core::EngineError* outError = nullptr) const;

    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    [[nodiscard]] rhi::ShaderFormat format() const noexcept { return format_; }

private:
    struct Entry
    {
        std::string name;
        rhi::ShaderStage stage = rhi::ShaderStage::Vertex;
        std::string entryPoint;
        // Absolute, resolved at load time: the manifest stores paths relative
        // to itself, and resolving once beats resolving at every create().
        std::filesystem::path blob;
        u32 samplerCount = 0;
        u32 uniformBufferCount = 0;
    };

    [[nodiscard]] const Entry* find(std::string_view name, rhi::ShaderStage stage) const noexcept;

    std::vector<Entry> entries_;
    rhi::ShaderFormat format_ = rhi::ShaderFormat::Unknown;
};

} // namespace luaug::render
