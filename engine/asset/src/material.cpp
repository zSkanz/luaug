#include "luaug/asset/material.h"

#include "luaug/core/i18n.h"
#include "luaug/core/json.h"
#include "luaug/core/json_writer.h"
#include "luaug/core/log.h"
#include "luaug/platform/file.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>

namespace luaug::asset {

namespace {

constexpr std::array<std::string_view, MaterialFieldCount> FieldNames{
    "Color",     "Transparency", "ColorMap",    "NormalMap", "MetallicRoughnessMap", "Emissive",    "EmissiveMap",
    "Metalness", "Roughness",    "NormalScale", "AlphaMode", "AlphaCutoff",          "DoubleSided",
};

constexpr std::array<std::string_view, 3> AlphaModeNames{"Opaque", "Mask", "Blend"};

enum class FieldShape : core::u8
{
    Color,
    Number,
    Map,
    AlphaMode,
    Flag,
};

[[nodiscard]] FieldShape shapeOf(MaterialField field) noexcept
{
    switch (field) {
    case MaterialField::Color:
    case MaterialField::Emissive:
        return FieldShape::Color;
    case MaterialField::ColorMap:
    case MaterialField::NormalMap:
    case MaterialField::MetallicRoughnessMap:
    case MaterialField::EmissiveMap:
        return FieldShape::Map;
    case MaterialField::AlphaMode:
        return FieldShape::AlphaMode;
    case MaterialField::DoubleSided:
        return FieldShape::Flag;
    default:
        return FieldShape::Number;
    }
}

// Where each field lives, for a mutable or a const description alike: one
// table of which field is where, rather than two that could disagree.
template <class Properties>
[[nodiscard]] auto* colorField(MaterialField field, Properties& p) noexcept
{
    return field == MaterialField::Color ? &p.color : field == MaterialField::Emissive ? &p.emissive : nullptr;
}

template <class Properties>
[[nodiscard]] auto* numberField(MaterialField field, Properties& p) noexcept
{
    switch (field) {
    case MaterialField::Transparency:
        return &p.transparency;
    case MaterialField::Metalness:
        return &p.metalness;
    case MaterialField::Roughness:
        return &p.roughness;
    case MaterialField::NormalScale:
        return &p.normalScale;
    case MaterialField::AlphaCutoff:
        return &p.alphaCutoff;
    default:
        return static_cast<decltype(&p.transparency)>(nullptr);
    }
}

template <class Properties>
[[nodiscard]] auto* mapField(MaterialField field, Properties& p) noexcept
{
    switch (field) {
    case MaterialField::ColorMap:
        return &p.colorMap;
    case MaterialField::NormalMap:
        return &p.normalMap;
    case MaterialField::MetallicRoughnessMap:
        return &p.metallicRoughnessMap;
    case MaterialField::EmissiveMap:
        return &p.emissiveMap;
    default:
        return static_cast<decltype(&p.colorMap)>(nullptr);
    }
}

[[nodiscard]] bool readColor(const core::JsonValue& json, core::Color3& out)
{
    if (json.type() != core::JsonType::Array || json.size() != 3)
        return false;
    for (core::usize index = 0; index < 3; ++index) {
        if (json.at(index).type() != core::JsonType::Number)
            return false;
    }
    out = core::Color3{static_cast<core::f32>(json.at(0).asNumber()), static_cast<core::f32>(json.at(1).asNumber()),
                       static_cast<core::f32>(json.at(2).asNumber())};
    return std::isfinite(out.r) && std::isfinite(out.g) && std::isfinite(out.b);
}

// One field's value from the file into `into`, or false for a value of the
// wrong shape.
[[nodiscard]] bool readField(MaterialField field, const core::JsonValue& json, MaterialProperties& into)
{
    switch (shapeOf(field)) {
    case FieldShape::Color:
        return readColor(json, *colorField(field, into));
    case FieldShape::Number: {
        if (json.type() != core::JsonType::Number)
            return false;
        const auto number = static_cast<core::f32>(json.asNumber());
        if (!std::isfinite(number))
            return false;
        *numberField(field, into) = number;
        return true;
    }
    case FieldShape::Map:
        if (json.type() != core::JsonType::String)
            return false;
        *mapField(field, into) = std::string(json.asString());
        return true;
    case FieldShape::AlphaMode:
        if (json.type() != core::JsonType::String)
            return false;
        for (core::usize index = 0; index < AlphaModeNames.size(); ++index) {
            if (json.asString() == AlphaModeNames[index]) {
                into.alphaMode = static_cast<core::i32>(index);
                return true;
            }
        }
        return false;
    case FieldShape::Flag:
        if (json.type() != core::JsonType::Boolean)
            return false;
        into.doubleSided = json.asBool();
        return true;
    }
    return false;
}

void writeField(core::JsonWriter& out, MaterialField field, const MaterialProperties& from)
{
    out.key(materialFieldName(field));
    switch (shapeOf(field)) {
    case FieldShape::Color: {
        const core::Color3& c = *colorField(field, from);
        out.beginInlineArray();
        out.valueFloat(c.r);
        out.valueFloat(c.g);
        out.valueFloat(c.b);
        out.endArray();
        return;
    }
    case FieldShape::Number:
        out.valueFloat(*numberField(field, from));
        return;
    case FieldShape::Map:
        out.value(*mapField(field, from));
        return;
    case FieldShape::AlphaMode: {
        const auto mode = static_cast<core::usize>(from.alphaMode);
        out.value(mode < AlphaModeNames.size() ? AlphaModeNames[mode] : AlphaModeNames[0]);
        return;
    }
    case FieldShape::Flag:
        out.value(from.doubleSided);
        return;
    }
}

} // namespace

const ShaderParameter* MaterialProperties::shaderParameter(std::string_view name) const noexcept
{
    for (const ShaderParameter& parameter : shaderParameters) {
        if (parameter.name == name)
            return &parameter;
    }
    return nullptr;
}

void MaterialProperties::setShaderParameter(ShaderParameter parameter)
{
    const auto at =
        std::lower_bound(shaderParameters.begin(), shaderParameters.end(), parameter.name,
                         [](const ShaderParameter& entry, const std::string& name) { return entry.name < name; });
    if (at != shaderParameters.end() && at->name == parameter.name)
        *at = std::move(parameter);
    else
        shaderParameters.insert(at, std::move(parameter));
}

namespace {

// A shader parameter as a material file writes it: a number, a boolean, an
// array of two to four numbers, a texture URN, or `{"texture": urn,
// "linear": true}` for a texture that is data.
[[nodiscard]] std::optional<ShaderParameter> readShaderParameter(std::string_view name, const core::JsonValue& json)
{
    ShaderParameter out;
    out.name = std::string(name);
    switch (json.type()) {
    case core::JsonType::Number:
        out.value[0] = static_cast<core::f32>(json.asNumber());
        return std::isfinite(out.value[0]) ? std::optional<ShaderParameter>(out) : std::nullopt;
    case core::JsonType::Boolean:
        out.value[0] = json.asBool() ? 1.0f : 0.0f;
        return out;
    case core::JsonType::String:
        out.texture = std::string(json.asString());
        return out.texture.empty() ? std::nullopt : std::optional<ShaderParameter>(out);
    case core::JsonType::Array:
        if (json.size() < 2 || json.size() > 4)
            return std::nullopt;
        out.components = static_cast<core::u8>(json.size());
        for (core::usize index = 0; index < json.size(); ++index) {
            if (json.at(index).type() != core::JsonType::Number)
                return std::nullopt;
            out.value[index] = static_cast<core::f32>(json.at(index).asNumber());
            if (!std::isfinite(out.value[index]))
                return std::nullopt;
        }
        return out;
    case core::JsonType::Object:
        if (json["texture"].type() != core::JsonType::String || json["texture"].asString().empty())
            return std::nullopt;
        out.texture = std::string(json["texture"].asString());
        out.linear = json["linear"].type() == core::JsonType::Boolean && json["linear"].asBool();
        return out;
    default:
        return std::nullopt;
    }
}

void writeShaderParameter(core::JsonWriter& out, const ShaderParameter& parameter)
{
    out.key(parameter.name);
    if (parameter.isTexture()) {
        if (!parameter.linear) {
            out.value(parameter.texture);
            return;
        }
        out.beginObject();
        out.field("texture", parameter.texture);
        out.field("linear", true);
        out.endObject();
        return;
    }
    if (parameter.components <= 1) {
        out.valueFloat(parameter.value[0]);
        return;
    }
    out.beginInlineArray();
    for (core::u8 index = 0; index < parameter.components && index < 4; ++index)
        out.valueFloat(parameter.value[index]);
    out.endArray();
}

} // namespace

std::string_view materialFieldName(MaterialField field) noexcept
{
    const auto index = static_cast<core::usize>(field);
    return index < FieldNames.size() ? FieldNames[index] : std::string_view{};
}

std::optional<MaterialField> materialFieldNamed(std::string_view name) noexcept
{
    for (core::usize index = 0; index < FieldNames.size(); ++index) {
        if (FieldNames[index] == name)
            return static_cast<MaterialField>(index);
    }
    return std::nullopt;
}

bool isMaterialPath(std::string_view path) noexcept
{
    return path.size() > MaterialSuffix.size() && path.ends_with(MaterialSuffix);
}

void copyMaterialField(MaterialField field, const MaterialProperties& from, MaterialProperties& into)
{
    switch (shapeOf(field)) {
    case FieldShape::Color:
        *colorField(field, into) = *colorField(field, from);
        return;
    case FieldShape::Number:
        *numberField(field, into) = *numberField(field, from);
        return;
    case FieldShape::Map:
        *mapField(field, into) = *mapField(field, from);
        return;
    case FieldShape::AlphaMode:
        into.alphaMode = from.alphaMode;
        return;
    case FieldShape::Flag:
        into.doubleSided = from.doubleSided;
        return;
    }
}

bool sameMaterialField(MaterialField field, const MaterialProperties& a, const MaterialProperties& b)
{
    switch (shapeOf(field)) {
    case FieldShape::Color:
        return *colorField(field, a) == *colorField(field, b);
    case FieldShape::Number:
        return *numberField(field, a) == *numberField(field, b);
    case FieldShape::Map:
        return *mapField(field, a) == *mapField(field, b);
    case FieldShape::AlphaMode:
        return a.alphaMode == b.alphaMode;
    case FieldShape::Flag:
        return a.doubleSided == b.doubleSided;
    }
    return false;
}

std::optional<MaterialAsset> readMaterialAsset(std::string_view json, MaterialReadNotes* notes, std::string* error)
{
    MaterialReadNotes scratch;
    MaterialReadNotes& noted = notes != nullptr ? *notes : scratch;

    core::JsonDocument document;
    if (const core::JsonDocument::ParseResult parsed = document.parse(json, "material"); !parsed) {
        if (error != nullptr)
            *error = parsed.diagnostic;
        return std::nullopt;
    }
    const core::JsonValue root = document.root();
    if (root.type() != core::JsonType::Object || root["format"].asString() != MaterialFormat) {
        if (error != nullptr)
            *error = "not a luaug-material file";
        return std::nullopt;
    }
    if (root["version"].asInteger() != MaterialFormatVersion) {
        if (error != nullptr)
            *error =
                "material version " + std::to_string(root["version"].asInteger()) + " is not one this engine reads";
        return std::nullopt;
    }

    MaterialAsset out;
    for (core::usize index = 0; index < root.size(); ++index) {
        const std::string_view key = root.keyAt(index);
        if (key == "format" || key == "version")
            continue;
        const core::JsonValue value = root[key];
        if (key == "parent") {
            if (value.type() == core::JsonType::String)
                out.parent = std::string(value.asString());
            else
                noted.malformedFields.emplace_back(key);
            continue;
        }
        if (key == "instanceParameters") {
            if (value.type() != core::JsonType::Array) {
                noted.malformedFields.emplace_back(key);
                continue;
            }
            for (core::usize item = 0; item < value.size(); ++item) {
                const std::string_view name = value.at(item).asString();
                const std::optional<MaterialField> field = materialFieldNamed(name);
                // A name no built-in field has is a surface shader parameter's
                // (ADR 0091); a built-in field a part may not change is not.
                if (!field.has_value() && isShaderParameterName(name)) {
                    addShaderParameterName(out.instanceShaderParameters, name);
                    continue;
                }
                if (!field.has_value() || (fieldBit(*field) & DeclarableParameters) == 0) {
                    noted.unknownFields.push_back("instanceParameters." + std::string(name));
                    continue;
                }
                out.instanceParameters |= fieldBit(*field);
            }
            continue;
        }
        if (key == "shader") {
            if (value.type() == core::JsonType::String) {
                out.properties.shader = std::string(value.asString());
                out.shaderWritten = true;
            }
            else {
                noted.malformedFields.emplace_back(key);
            }
            continue;
        }
        if (key == "readsSceneColor") {
            if (value.type() == core::JsonType::Boolean) {
                out.properties.readsSceneColor = value.asBool();
                out.shaderWritten = true;
            }
            else {
                noted.malformedFields.emplace_back(key);
            }
            continue;
        }
        if (key == "properties") {
            if (value.type() != core::JsonType::Object) {
                noted.malformedFields.emplace_back(key);
                continue;
            }
            for (core::usize item = 0; item < value.size(); ++item) {
                const std::string_view name = value.keyAt(item);
                const std::optional<MaterialField> field = materialFieldNamed(name);
                if (!field.has_value()) {
                    // **Not a built-in field: a parameter of the shader**
                    // (ADR 0091), kept whatever the shader turns out to
                    // declare. Reported only where it cannot be one -- a base
                    // that names no shader.
                    std::optional<ShaderParameter> parameter = readShaderParameter(name, value[name]);
                    if (!parameter.has_value())
                        noted.malformedFields.push_back("properties." + std::string(name));
                    else
                        out.properties.setShaderParameter(std::move(*parameter));
                    continue;
                }
                if (!readField(*field, value[name], out.properties)) {
                    noted.malformedFields.push_back("properties." + std::string(name));
                    continue;
                }
                out.written |= fieldBit(*field);
            }
            continue;
        }
        noted.unknownFields.emplace_back(key);
    }
    if (out.parent.empty() && out.properties.shader.empty()) {
        for (const ShaderParameter& parameter : out.properties.shaderParameters)
            noted.unknownFields.push_back("properties." + parameter.name);
    }
    return out;
}

std::string writeMaterialAsset(const MaterialAsset& material)
{
    core::JsonWriter out(core::JsonLayout::Indented);
    out.beginObject();
    out.field("format", MaterialFormat);
    out.field("version", MaterialFormatVersion);
    out.field("parent", material.parent);
    if (material.shaderWritten || (material.parent.empty() && !material.properties.shader.empty())) {
        out.field("shader", material.properties.shader);
        out.field("readsSceneColor", material.properties.readsSceneColor);
    }

    out.key("instanceParameters");
    out.beginInlineArray();
    for (core::usize index = 0; index < MaterialFieldCount; ++index) {
        const auto field = static_cast<MaterialField>(index);
        if ((material.instanceParameters & DeclarableParameters & fieldBit(field)) != 0)
            out.value(materialFieldName(field));
    }
    for (const std::string& name : material.instanceShaderParameters)
        out.value(name);
    out.endArray();

    // A base says everything, so reading it never depends on this engine's
    // defaults staying where they are; a variant says only what differs, so
    // editing its parent reaches it.
    const MaterialFieldMask written = material.parent.empty() ? AllMaterialFields : material.written;
    out.key("properties");
    out.beginObject();
    for (core::usize index = 0; index < MaterialFieldCount; ++index) {
        const auto field = static_cast<MaterialField>(index);
        if ((written & fieldBit(field)) != 0)
            writeField(out, field, material.properties);
    }
    for (const ShaderParameter& parameter : material.properties.shaderParameters)
        writeShaderParameter(out, parameter);
    out.endObject();
    out.endObject();

    std::string text = out.text();
    text.push_back('\n');
    return text;
}

const ResolvedMaterial& defaultMaterial() noexcept
{
    static const ResolvedMaterial Default{};
    return Default;
}

ResolvedMaterial resolveMaterial(std::string_view urn, const MaterialLookup& lookup, MaterialResolveNotes* notes)
{
    if (urn.empty() || !lookup)
        return defaultMaterial();

    // Child first, as the parents are walked; applied in reverse.
    std::vector<const MaterialAsset*> chain;
    std::vector<std::string> visited;
    std::string current(urn);
    while (!current.empty()) {
        for (const std::string& seen : visited) {
            if (seen == current) {
                // **Refused, and the engine default instead**: there is no order
                // to apply a loop in, and picking one would make the answer
                // depend on which file the walk happened to start from.
                if (notes != nullptr)
                    notes->cycleClosedBy = visited.back();
                return defaultMaterial();
            }
        }
        const MaterialAsset* found = lookup(current);
        if (found == nullptr) {
            if (!chain.empty() && notes != nullptr) {
                notes->missingParentOf = visited.back();
                notes->missingParent = current;
            }
            break;
        }
        visited.push_back(current);
        chain.push_back(found);
        current = found->parent;
    }
    if (chain.empty())
        return defaultMaterial();

    // **A material with a file declares only what its files declare** -- the
    // engine default's `Color` and `Transparency` are the default's, and an
    // authored material is exactly what its author wrote (ADR 0090).
    ResolvedMaterial out;
    out.instanceParameters = 0;
    for (auto link = chain.rbegin(); link != chain.rend(); ++link) {
        const MaterialAsset& asset = **link;
        // The root of the chain is a base, and a base missing a field means the
        // default for it; `written` covers exactly what a file said.
        for (core::usize index = 0; index < MaterialFieldCount; ++index) {
            const auto field = static_cast<MaterialField>(index);
            if ((asset.written & fieldBit(field)) != 0)
                copyMaterialField(field, asset.properties, out.properties);
        }
        if (asset.shaderWritten || link == chain.rbegin()) {
            out.properties.shader = asset.properties.shader;
            out.properties.readsSceneColor = asset.properties.readsSceneColor;
        }
        // A variant's parameter replaces its parent's of the same name.
        for (const ShaderParameter& parameter : asset.properties.shaderParameters)
            out.properties.setShaderParameter(parameter);
        out.instanceParameters |= static_cast<MaterialFieldMask>(asset.instanceParameters & DeclarableParameters);
        for (const std::string& name : asset.instanceShaderParameters)
            addShaderParameterName(out.instanceShaderParameters, name);
    }
    return out;
}

bool setOverride(MaterialOverrides& overrides, MaterialField field, const MaterialProperties& from)
{
    if ((fieldBit(field) & DeclarableParameters) == 0)
        return false;
    switch (field) {
    case MaterialField::Color:
        overrides.color = from.color;
        break;
    case MaterialField::Transparency:
        overrides.transparency = from.transparency;
        break;
    case MaterialField::Emissive:
        overrides.emissive = from.emissive;
        break;
    case MaterialField::Metalness:
        overrides.metalness = from.metalness;
        break;
    case MaterialField::Roughness:
        overrides.roughness = from.roughness;
        break;
    case MaterialField::NormalScale:
        overrides.normalScale = from.normalScale;
        break;
    case MaterialField::AlphaCutoff:
        overrides.alphaCutoff = from.alphaCutoff;
        break;
    default:
        return false;
    }
    overrides.set |= fieldBit(field);
    return true;
}

void clearOverride(MaterialOverrides& overrides, MaterialField field)
{
    if ((fieldBit(field) & DeclarableParameters) == 0)
        return;
    // The storage goes back to the blank value too, so two override sets that
    // hold the same overrides compare equal whatever they held before.
    const MaterialFieldMask keep = static_cast<MaterialFieldMask>(overrides.set & ~fieldBit(field));
    (void)setOverride(overrides, field, overrideValues(MaterialOverrides{}));
    overrides.set = keep;
}

MaterialProperties overrideValues(const MaterialOverrides& overrides)
{
    MaterialProperties out;
    out.color = overrides.color;
    out.transparency = overrides.transparency;
    out.emissive = overrides.emissive;
    out.metalness = overrides.metalness;
    out.roughness = overrides.roughness;
    out.normalScale = overrides.normalScale;
    out.alphaCutoff = overrides.alphaCutoff;
    return out;
}

void applyOverrides(const MaterialOverrides& overrides, MaterialFieldMask declared, MaterialProperties& into)
{
    const MaterialFieldMask applied = static_cast<MaterialFieldMask>(overrides.set & declared & DeclarableParameters);
    if (applied == 0)
        return;
    const MaterialProperties values = overrideValues(overrides);
    for (core::usize index = 0; index < MaterialFieldCount; ++index) {
        const auto field = static_cast<MaterialField>(index);
        if ((applied & fieldBit(field)) != 0)
            copyMaterialField(field, values, into);
    }
}

// --- the library -------------------------------------------------------------

void MaterialLibrary::setSource(Source source)
{
    m_source = std::move(source);
    forgetAll();
}

const MaterialAsset* MaterialLibrary::asset(std::string_view urn)
{
    if (urn.empty())
        return nullptr;
    const std::string key(urn);
    if (const auto found = m_assets.find(key); found != m_assets.end())
        return found->second.asset.has_value() ? &*found->second.asset : nullptr;

    Loaded loaded;
    if (m_source) {
        MaterialReadNotes notes;
        loaded.asset = m_source(urn, notes);
        // Said once per load -- the entry below is what stops it being said
        // every frame -- and not fatal: what this engine understands still
        // applies.
        for (const std::string& field : notes.unknownFields) {
            const std::array<core::I18nArg, 2> args{core::I18nArg{"path", key}, core::I18nArg{"field", field}};
            core::log(core::LogLevel::Warn, LUAUG_TR("asset.material.warn.unknown_field"), args);
        }
        for (const std::string& field : notes.malformedFields) {
            const std::array<core::I18nArg, 2> args{core::I18nArg{"path", key}, core::I18nArg{"field", field}};
            core::log(core::LogLevel::Warn, LUAUG_TR("asset.material.warn.malformed_field"), args);
        }
    }
    const auto [at, inserted] = m_assets.emplace(key, std::move(loaded));
    (void)inserted;
    return at->second.asset.has_value() ? &*at->second.asset : nullptr;
}

const ResolvedMaterial& MaterialLibrary::resolve(std::string_view urn)
{
    if (urn.empty())
        return defaultMaterial();
    const std::string key(urn);
    if (const auto found = m_resolved.find(key); found != m_resolved.end())
        return found->second;

    MaterialResolveNotes notes;
    ResolvedMaterial resolved = resolveMaterial(urn, [this](std::string_view link) { return asset(link); }, &notes);
    if (asset(urn) == nullptr) {
        const std::array<core::I18nArg, 1> args{core::I18nArg{"path", key}};
        core::log(core::LogLevel::Warn, LUAUG_TR("asset.material.warn.missing"), args);
    }
    if (!notes.cycleClosedBy.empty()) {
        const std::array<core::I18nArg, 2> args{core::I18nArg{"path", key},
                                                core::I18nArg{"closedBy", notes.cycleClosedBy}};
        core::log(core::LogLevel::Warn, LUAUG_TR("asset.material.warn.cycle"), args);
    }
    if (!notes.missingParent.empty()) {
        const std::array<core::I18nArg, 2> args{core::I18nArg{"path", notes.missingParentOf},
                                                core::I18nArg{"parent", notes.missingParent}};
        core::log(core::LogLevel::Warn, LUAUG_TR("asset.material.warn.missing_parent"), args);
    }
    return m_resolved.emplace(key, std::move(resolved)).first->second;
}

void MaterialLibrary::forget(std::string_view urn)
{
    m_assets.erase(std::string(urn));
    m_resolved.clear();
    ++m_revision;
}

void MaterialLibrary::forgetAll()
{
    m_assets.clear();
    m_resolved.clear();
    ++m_revision;
}

void MaterialLibrary::put(std::string_view urn, MaterialAsset material)
{
    m_assets[std::string(urn)] = Loaded{std::move(material)};
    m_resolved.clear();
    ++m_revision;
}

// --- The compiled form ---------------------------------------------------------

namespace {

constexpr std::array<char, 4> CompiledMagic{'L', 'M', 'A', 'T'};
// 2: the surface shader and its parameters (ADR 0091).
// 3: the shader parameters a part may override, by name. 2 is still read.
constexpr core::u32 CompiledVersion = 3;

class ByteWriter
{
public:
    void raw(const void* data, usize size)
    {
        const auto* bytes = static_cast<const std::byte*>(data);
        m_out.insert(m_out.end(), bytes, bytes + size);
    }
    // Little-endian on every host by construction: shifted out byte by byte.
    void word(core::u32 value)
    {
        for (int shift = 0; shift < 32; shift += 8)
            m_out.push_back(static_cast<std::byte>((value >> shift) & 0xFFu));
    }
    void real(core::f32 value)
    {
        core::u32 bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        word(bits);
    }
    void text(std::string_view value)
    {
        word(static_cast<core::u32>(value.size()));
        raw(value.data(), value.size());
    }
    [[nodiscard]] std::vector<std::byte> take() { return std::move(m_out); }

private:
    std::vector<std::byte> m_out;
};

class ByteReader
{
public:
    explicit ByteReader(std::span<const std::byte> bytes) : m_bytes(bytes) {}
    [[nodiscard]] bool word(core::u32& out)
    {
        if (m_at + 4 > m_bytes.size())
            return false;
        out = 0;
        for (usize index = 0; index < 4; ++index)
            out |= static_cast<core::u32>(m_bytes[m_at + index]) << (8 * index);
        m_at += 4;
        return true;
    }
    [[nodiscard]] bool real(core::f32& out)
    {
        core::u32 bits = 0;
        if (!word(bits))
            return false;
        std::memcpy(&out, &bits, sizeof(out));
        return true;
    }
    [[nodiscard]] bool text(std::string& out)
    {
        core::u32 size = 0;
        if (!word(size) || m_at + size > m_bytes.size())
            return false;
        out.assign(reinterpret_cast<const char*>(m_bytes.data() + m_at), size);
        m_at += size;
        return true;
    }
    [[nodiscard]] bool bytes(std::span<std::byte> out)
    {
        if (m_at + out.size() > m_bytes.size())
            return false;
        std::memcpy(out.data(), m_bytes.data() + m_at, out.size());
        m_at += out.size();
        return true;
    }
    [[nodiscard]] bool done() const noexcept { return m_at == m_bytes.size(); }

private:
    std::span<const std::byte> m_bytes;
    usize m_at = 0;
};

} // namespace

bool isShaderParameterName(std::string_view name) noexcept
{
    if (name.empty() || name.size() > 64)
        return false;
    const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    if (!letter(name.front()))
        return false;
    return std::all_of(name.begin(), name.end(), [&](char c) { return letter(c) || (c >= '0' && c <= '9'); });
}

void addShaderParameterName(std::vector<std::string>& names, std::string_view name)
{
    const auto at = std::lower_bound(names.begin(), names.end(), name);
    if (at == names.end() || *at != name)
        names.insert(at, std::string(name));
}

bool ResolvedMaterial::declaresShaderParameter(std::string_view name) const noexcept
{
    return std::binary_search(instanceShaderParameters.begin(), instanceShaderParameters.end(), name);
}

std::vector<std::byte> encodeMaterial(const CompiledMaterial& material)
{
    const MaterialAsset& asset = material.asset;
    const MaterialProperties& p = asset.properties;
    ByteWriter out;
    out.raw(CompiledMagic.data(), CompiledMagic.size());
    out.word(CompiledVersion);
    out.word(asset.written);
    out.word(asset.instanceParameters);
    out.text(asset.parent);
    for (const core::f32 value : {p.color.r, p.color.g, p.color.b, p.transparency, p.emissive.r, p.emissive.g,
                                  p.emissive.b, p.metalness, p.roughness, p.normalScale, p.alphaCutoff})
        out.real(value);
    out.word(static_cast<core::u32>(p.alphaMode));
    out.word(p.doubleSided ? 1u : 0u);
    const std::array<const std::string*, 4> maps{&p.colorMap, &p.normalMap, &p.metallicRoughnessMap, &p.emissiveMap};
    for (usize index = 0; index < maps.size(); ++index) {
        out.text(*maps[index]);
        const std::array<std::byte, 16> hash = core::toBytes(material.mapHashes[index]);
        out.raw(hash.data(), hash.size());
    }
    out.word(asset.shaderWritten ? 1u : 0u);
    out.text(p.shader);
    out.word(p.readsSceneColor ? 1u : 0u);
    out.word(static_cast<core::u32>(p.shaderParameters.size()));
    for (const ShaderParameter& parameter : p.shaderParameters) {
        out.text(parameter.name);
        for (const core::f32 component : parameter.value)
            out.real(component);
        out.word(parameter.components);
        out.text(parameter.texture);
        out.word(parameter.linear ? 1u : 0u);
    }
    out.word(static_cast<core::u32>(asset.instanceShaderParameters.size()));
    for (const std::string& name : asset.instanceShaderParameters)
        out.text(name);
    return out.take();
}

std::optional<CompiledMaterial> decodeMaterial(std::span<const std::byte> bytes)
{
    if (bytes.size() < CompiledMagic.size() || std::memcmp(bytes.data(), CompiledMagic.data(), 4) != 0)
        return std::nullopt;
    ByteReader in(bytes.subspan(CompiledMagic.size()));
    CompiledMaterial out;
    MaterialAsset& asset = out.asset;
    MaterialProperties& p = asset.properties;
    core::u32 version = 0;
    core::u32 written = 0;
    core::u32 declared = 0;
    if (!in.word(version) || (version != CompiledVersion && version != 2) || !in.word(written) || !in.word(declared) ||
        !in.text(asset.parent))
        return std::nullopt;
    asset.written = static_cast<MaterialFieldMask>(written & AllMaterialFields);
    asset.instanceParameters = static_cast<MaterialFieldMask>(declared & DeclarableParameters);
    for (core::f32* value : {&p.color.r, &p.color.g, &p.color.b, &p.transparency, &p.emissive.r, &p.emissive.g,
                             &p.emissive.b, &p.metalness, &p.roughness, &p.normalScale, &p.alphaCutoff}) {
        if (!in.real(*value))
            return std::nullopt;
    }
    core::u32 alphaMode = 0;
    core::u32 doubleSided = 0;
    if (!in.word(alphaMode) || !in.word(doubleSided))
        return std::nullopt;
    p.alphaMode = static_cast<core::i32>(alphaMode);
    p.doubleSided = doubleSided != 0;
    const std::array<std::string*, 4> maps{&p.colorMap, &p.normalMap, &p.metallicRoughnessMap, &p.emissiveMap};
    for (usize index = 0; index < maps.size(); ++index) {
        std::array<std::byte, 16> hash{};
        if (!in.text(*maps[index]) || !in.bytes(hash))
            return std::nullopt;
        out.mapHashes[index] = core::fromBytes(std::span<const std::byte, 16>(hash));
    }
    core::u32 shaderWritten = 0;
    core::u32 readsSceneColor = 0;
    core::u32 parameters = 0;
    if (!in.word(shaderWritten) || !in.text(p.shader) || !in.word(readsSceneColor) || !in.word(parameters))
        return std::nullopt;
    asset.shaderWritten = shaderWritten != 0;
    p.readsSceneColor = readsSceneColor != 0;
    for (core::u32 index = 0; index < parameters; ++index) {
        ShaderParameter parameter;
        core::u32 components = 0;
        core::u32 linear = 0;
        if (!in.text(parameter.name))
            return std::nullopt;
        for (core::f32& component : parameter.value) {
            if (!in.real(component))
                return std::nullopt;
        }
        if (!in.word(components) || components > 4 || !in.text(parameter.texture) || !in.word(linear))
            return std::nullopt;
        parameter.components = static_cast<core::u8>(components);
        parameter.linear = linear != 0;
        p.shaderParameters.push_back(std::move(parameter));
    }
    if (version >= 3) {
        core::u32 names = 0;
        if (!in.word(names))
            return std::nullopt;
        for (core::u32 index = 0; index < names; ++index) {
            std::string name;
            if (!in.text(name) || !isShaderParameterName(name))
                return std::nullopt;
            addShaderParameterName(asset.instanceShaderParameters, name);
        }
    }
    if (!in.done())
        return std::nullopt;
    return out;
}

MaterialLibrary::Source mountedMaterials(const ContentMounts& mounts)
{
    return [&mounts](std::string_view urn, MaterialReadNotes& notes) -> std::optional<MaterialAsset> {
        const ResolvedContent resolved = mounts.resolve(urn);
        if (!resolved.found())
            return std::nullopt;
        // **The compiled form first**, which a shipped game has and a dev
        // session may: the pack's kind says what the blob is.
        if (resolved.source == ResolvedContent::Source::Pack && resolved.kind == AssetKind::Material) {
            if (std::optional<CompiledMaterial> compiled = decodeMaterial(resolved.bytes))
                return std::move(compiled->asset);
            const std::array<core::I18nArg, 1> args{core::I18nArg{"path", std::string(urn)}};
            core::log(core::LogLevel::Warn, LUAUG_TR("asset.material.err.compiled_damaged"), args);
            return std::nullopt;
        }
        std::string text;
        if (resolved.source == ResolvedContent::Source::Loose) {
            std::vector<std::byte> bytes;
            if (!platform::readFile(resolved.path, bytes))
                return std::nullopt;
            text.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        }
        else {
            text.assign(reinterpret_cast<const char*>(resolved.bytes.data()), resolved.bytes.size());
        }
        std::string error;
        std::optional<MaterialAsset> material = readMaterialAsset(text, &notes, &error);
        if (!material.has_value()) {
            const std::array<core::I18nArg, 2> args{core::I18nArg{"path", std::string(urn)},
                                                    core::I18nArg{"reason", error}};
            core::log(core::LogLevel::Warn, LUAUG_TR("asset.material.err.unreadable"), args);
        }
        return material;
    };
}

} // namespace luaug::asset
