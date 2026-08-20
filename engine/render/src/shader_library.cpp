#include "luaug/render/shader_library.h"

#include <array>

#include "luaug/core/json.h"
#include "luaug/core/text_key.h"
#include "luaug/platform/file.h"

namespace luaug::render
{
namespace
{

using core::I18nArg;

// The manifest's key for each format, written by cmake/luaug_shaders.cmake.
[[nodiscard]] std::string_view formatKey(rhi::ShaderFormat format) noexcept
{
    switch (format)
    {
    case rhi::ShaderFormat::SpirV: return "spirv";
    case rhi::ShaderFormat::Dxil: return "dxil";
    case rhi::ShaderFormat::Msl: return "msl";
    case rhi::ShaderFormat::Unknown: return "";
    }
    return "";
}

[[nodiscard]] std::optional<rhi::ShaderStage> parseStage(std::string_view text) noexcept
{
    if (text == "vertex")
        return rhi::ShaderStage::Vertex;
    if (text == "fragment")
        return rhi::ShaderStage::Fragment;
    return std::nullopt;
}

} // namespace

std::optional<core::EngineError> ShaderLibrary::load(
    const std::filesystem::path& contentDir, rhi::ShaderFormat format)
{
    entries_.clear();
    format_ = format;

    const std::string_view key = formatKey(format);
    if (key.empty())
    {
        // A device that reports no shader format cannot be given shaders. Saying
        // so here beats every create() failing individually for a reason that
        // has nothing to do with the shader being asked for.
        return core::makeError(LUAUG_TR("render.err.shader_format_unknown"));
    }

    const std::filesystem::path manifestPath = contentDir / "shaders" / "manifest.json";
    std::string text;
    if (!platform::readTextFile(manifestPath, text))
    {
        const std::array<I18nArg, 1> args{I18nArg{"path", manifestPath.string()}};
        return core::makeError(LUAUG_TR("render.err.shader_manifest_missing"), args);
    }

    core::JsonDocument document;
    if (const auto parsed = document.parse(text, manifestPath.string()); !parsed)
        return core::makeError(LUAUG_TR("render.err.shader_manifest_invalid"), {}, parsed.diagnostic);

    const std::filesystem::path root = manifestPath.parent_path();
    const core::JsonValue shaders = document.root()["shaders"];

    for (core::usize i = 0; i < shaders.size(); ++i)
    {
        const core::JsonValue shader = shaders.at(i);

        const std::optional<rhi::ShaderStage> stage = parseStage(shader["stage"].asString());
        const std::string_view name = shader["name"].asString();
        const std::string_view blob = shader["formats"][key].asString();

        // A shader compiled for other formats but not this one is not an error
        // in itself -- it is only an error when something asks for it, and
        // create() reports that with the name the caller used.
        if (!stage.has_value() || name.empty() || blob.empty())
            continue;

        Entry entry;
        entry.name = name;
        entry.stage = *stage;
        entry.entryPoint = shader["entrypoint"].asString("main");
        entry.blob = root / blob;

        // The counts are the reflection's, never a guess: SDL_GPU rejects a
        // shader whose declared counts disagree with its bindings, and only the
        // shader source knows them. A missing sidecar therefore fails loudly
        // rather than defaulting to zero, which would link and then bind
        // nothing.
        const std::filesystem::path reflectPath = root / shader["reflect"].asString();
        std::string reflectText;
        if (!platform::readTextFile(reflectPath, reflectText))
        {
            const std::array<I18nArg, 1> args{I18nArg{"path", reflectPath.string()}};
            return core::makeError(LUAUG_TR("render.err.shader_reflect_missing"), args);
        }

        core::JsonDocument reflect;
        if (const auto parsed = reflect.parse(reflectText, reflectPath.string()); !parsed)
            return core::makeError(LUAUG_TR("render.err.shader_manifest_invalid"), {}, parsed.diagnostic);

        entry.samplerCount = static_cast<u32>(reflect.root()["samplers"].asInteger(0));
        entry.uniformBufferCount = static_cast<u32>(reflect.root()["uniform_buffers"].asInteger(0));

        entries_.push_back(std::move(entry));
    }

    if (entries_.empty())
    {
        const std::array<I18nArg, 1> args{I18nArg{"format", formatKey(format)}};
        return core::makeError(LUAUG_TR("render.err.shader_none_for_format"), args);
    }

    return std::nullopt;
}

const ShaderLibrary::Entry* ShaderLibrary::find(
    std::string_view name, rhi::ShaderStage stage) const noexcept
{
    for (const Entry& entry : entries_)
    {
        if (entry.name == name && entry.stage == stage)
            return &entry;
    }
    return nullptr;
}

rhi::ShaderHandle ShaderLibrary::create(
    rhi::IDevice& device, std::string_view name, rhi::ShaderStage stage, core::EngineError* outError) const
{
    const Entry* entry = find(name, stage);
    if (entry == nullptr)
    {
        if (outError != nullptr)
        {
            const std::array<I18nArg, 1> args{I18nArg{"name", name}};
            *outError = core::makeError(LUAUG_TR("render.err.shader_not_found"), args);
        }
        return {};
    }

    std::vector<std::byte> code;
    if (!platform::readFile(entry->blob, code))
    {
        if (outError != nullptr)
        {
            const std::array<I18nArg, 1> args{I18nArg{"path", entry->blob.string()}};
            *outError = core::makeError(LUAUG_TR("render.err.shader_blob_missing"), args);
        }
        return {};
    }

    return device.createShader({
        .stage = entry->stage,
        .format = format_,
        .code = code,
        .entryPoint = entry->entryPoint,
        .samplerCount = entry->samplerCount,
        .uniformBufferCount = entry->uniformBufferCount,
        .debugName = entry->name,
    });
}

} // namespace luaug::render
