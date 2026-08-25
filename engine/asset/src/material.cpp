#include "luaug/asset/material.h"

#include "luaug/core/i18n.h"
#include "luaug/core/json.h"
#include "luaug/core/json_writer.h"
#include "luaug/core/text_key.h"

namespace luaug::asset {
namespace {

using core::I18nArg;
using core::JsonValue;
using core::JsonWriter;

constexpr std::string_view MaterialFormat = "luaug-material";

[[nodiscard]] const char* alphaModeName(AlphaMode mode) noexcept
{
    switch (mode) {
    case AlphaMode::Mask:
        return "mask";
    case AlphaMode::Blend:
        return "blend";
    case AlphaMode::Opaque:
        break;
    }
    return "opaque";
}

[[nodiscard]] AlphaMode alphaModeFromName(std::string_view name, AlphaMode fallback) noexcept
{
    if (name == "opaque") {
        return AlphaMode::Opaque;
    }
    if (name == "mask") {
        return AlphaMode::Mask;
    }
    if (name == "blend") {
        return AlphaMode::Blend;
    }
    // An unknown mode is the default rather than an error: a file written by a
    // newer build should lose one property, not fail to open.
    return fallback;
}

void writeColor(JsonWriter& out, std::string_view key, const Color3& color)
{
    out.key(key);
    out.beginArray();
    out.value(static_cast<core::f64>(color.r));
    out.value(static_cast<core::f64>(color.g));
    out.value(static_cast<core::f64>(color.b));
    out.endArray();
}

void writeTexture(JsonWriter& out, std::string_view key, const MaterialTexture& texture)
{
    // An unused slot is written as `null` rather than omitted, so that the key
    // set is the same in every material and a diff between two of them lines up.
    if (!texture.present()) {
        out.key(key);
        out.nullValue();
        return;
    }
    out.key(key);
    out.beginObject();
    out.field("content", texture.content);
    if (texture.uvSet != 0) {
        // Omitted at zero, which is what all but a handful of files use. The
        // key is optional on read, so this costs nothing and keeps the common
        // material short enough to read at a glance.
        out.field("uvSet", static_cast<core::u64>(texture.uvSet));
    }
    out.endObject();
}

[[nodiscard]] Color3 readColor(const JsonValue& value, const Color3& fallback)
{
    if (value.size() != 3) {
        return fallback;
    }
    return Color3{static_cast<f32>(value.at(0).asNumber(static_cast<core::f64>(fallback.r))),
                  static_cast<f32>(value.at(1).asNumber(static_cast<core::f64>(fallback.g))),
                  static_cast<f32>(value.at(2).asNumber(static_cast<core::f64>(fallback.b)))};
}

[[nodiscard]] f32 readScalar(const JsonValue& value, f32 fallback)
{
    return static_cast<f32>(value.asNumber(static_cast<core::f64>(fallback)));
}

void readTexture(const JsonValue& value, MaterialTexture& out)
{
    if (!value.has("content")) {
        return;
    }
    out.content = std::string(value["content"].asString());
    out.uvSet = static_cast<u32>(value["uvSet"].asInteger(0));
}

} // namespace

std::string writeMaterial(const MaterialAsset& material)
{
    JsonWriter out;
    out.beginObject();
    out.field("format", MaterialFormat);
    out.field("version", static_cast<core::u64>(1));
    out.field("name", material.name);
    out.field("shader", material.shader);
    writeColor(out, "baseColorFactor", material.baseColorFactor);
    out.field("baseColorAlpha", static_cast<core::f64>(material.baseColorAlpha));
    out.field("metallicFactor", static_cast<core::f64>(material.metallicFactor));
    out.field("roughnessFactor", static_cast<core::f64>(material.roughnessFactor));
    writeColor(out, "emissiveFactor", material.emissiveFactor);
    out.field("normalScale", static_cast<core::f64>(material.normalScale));
    out.field("alphaMode", alphaModeName(material.alphaMode));
    out.field("alphaCutoff", static_cast<core::f64>(material.alphaCutoff));
    out.field("doubleSided", material.doubleSided);
    writeTexture(out, "baseColor", material.baseColor);
    writeTexture(out, "normal", material.normal);
    writeTexture(out, "metallicRoughness", material.metallicRoughness);
    writeTexture(out, "emissive", material.emissive);
    out.endObject();

    std::string text = out.text();
    text += '\n';
    return text;
}

std::optional<core::EngineError> readMaterial(std::string_view text, std::string_view sourceName, MaterialAsset& out)
{
    core::JsonDocument document;
    const core::JsonDocument::ParseResult parsed = document.parse(text, std::string(sourceName));
    if (!parsed.ok) {
        const I18nArg args[] = {{"detail", parsed.diagnostic}};
        return core::makeError(LUAUG_TR("asset.material.err.malformed"), args);
    }

    const JsonValue root = document.root();
    if (root["format"].asString() != MaterialFormat) {
        // Refused by name. A `.json` that is really a scene should say so rather
        // than load as a white material with every default.
        const I18nArg args[] = {{"detail", std::string(sourceName)}};
        return core::makeError(LUAUG_TR("asset.material.err.not_a_material"), args);
    }

    // Every field falls back to what `out` already holds, which is the struct's
    // own default -- so a trimmed file loads and an older build's file loads.
    MaterialAsset material;
    material.name = std::string(root["name"].asString(material.name));
    material.shader = std::string(root["shader"].asString(material.shader));
    material.baseColorFactor = readColor(root["baseColorFactor"], material.baseColorFactor);
    material.baseColorAlpha = readScalar(root["baseColorAlpha"], material.baseColorAlpha);
    material.metallicFactor = readScalar(root["metallicFactor"], material.metallicFactor);
    material.roughnessFactor = readScalar(root["roughnessFactor"], material.roughnessFactor);
    material.emissiveFactor = readColor(root["emissiveFactor"], material.emissiveFactor);
    material.normalScale = readScalar(root["normalScale"], material.normalScale);
    material.alphaMode = alphaModeFromName(root["alphaMode"].asString(), material.alphaMode);
    material.alphaCutoff = readScalar(root["alphaCutoff"], material.alphaCutoff);
    material.doubleSided = root["doubleSided"].asBool(material.doubleSided);
    readTexture(root["baseColor"], material.baseColor);
    readTexture(root["normal"], material.normal);
    readTexture(root["metallicRoughness"], material.metallicRoughness);
    readTexture(root["emissive"], material.emissive);

    out = std::move(material);
    return std::nullopt;
}

} // namespace luaug::asset
