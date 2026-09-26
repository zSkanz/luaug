#include "luaug/asset/surface_shader.h"

#include "luaug/core/number_parse.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>
#include <optional>

namespace luaug::asset {

using core::f32;
using core::u32;

namespace {

// The source with every comment replaced by spaces, newlines kept, so a match's
// position is still its line.
[[nodiscard]] std::string withoutComments(std::string_view source)
{
    std::string out(source);
    for (std::size_t at = 0; at < out.size(); ++at) {
        if (out[at] == '"') {
            for (++at; at < out.size() && out[at] != '"' && out[at] != '\n'; ++at) {
            }
            continue;
        }
        if (out[at] == '/' && at + 1 < out.size() && out[at + 1] == '/') {
            for (; at < out.size() && out[at] != '\n'; ++at)
                out[at] = ' ';
            continue;
        }
        if (out[at] == '/' && at + 1 < out.size() && out[at + 1] == '*') {
            out[at] = ' ';
            out[at + 1] = ' ';
            for (at += 2; at < out.size(); ++at) {
                if (out[at] == '*' && at + 1 < out.size() && out[at + 1] == '/') {
                    out[at] = ' ';
                    out[at + 1] = ' ';
                    ++at;
                    break;
                }
                if (out[at] != '\n')
                    out[at] = ' ';
            }
        }
    }
    return out;
}

[[nodiscard]] bool identifierChar(char c) noexcept
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

[[nodiscard]] bool isIdentifier(std::string_view text) noexcept
{
    if (text.empty() || std::isdigit(static_cast<unsigned char>(text.front())) != 0)
        return false;
    return std::all_of(text.begin(), text.end(), identifierChar);
}

[[nodiscard]] std::string_view trimmed(std::string_view text) noexcept
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
        text.remove_suffix(1);
    return text;
}

[[nodiscard]] u32 lineAt(std::string_view text, std::size_t at) noexcept
{
    return 1u + static_cast<u32>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(at), '\n'));
}

// Every whole-word occurrence of `word`, as offsets.
[[nodiscard]] std::vector<std::size_t> occurrences(std::string_view text, std::string_view word)
{
    std::vector<std::size_t> found;
    for (std::size_t at = text.find(word); at != std::string_view::npos; at = text.find(word, at + 1)) {
        const bool before = at == 0 || !identifierChar(text[at - 1]);
        const bool after = at + word.size() >= text.size() || !identifierChar(text[at + word.size()]);
        if (before && after)
            found.push_back(at);
    }
    return found;
}

// The arguments of a call whose name ends at `at`, split at top-level commas;
// empty when there is no well-formed parenthesised list.
[[nodiscard]] std::optional<std::vector<std::string_view>> argumentsAt(std::string_view text, std::size_t at)
{
    while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at])) != 0)
        ++at;
    if (at >= text.size() || text[at] != '(')
        return std::nullopt;
    std::vector<std::string_view> arguments;
    int depth = 0;
    std::size_t start = at + 1;
    for (std::size_t i = at; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '(') {
            ++depth;
        }
        else if (c == ')') {
            if (--depth == 0) {
                arguments.push_back(trimmed(text.substr(start, i - start)));
                return arguments;
            }
        }
        else if (c == ',' && depth == 1) {
            arguments.push_back(trimmed(text.substr(start, i - start)));
            start = i + 1;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<f32> number(std::string_view text) noexcept
{
    text = trimmed(text);
    if (!text.empty() && (text.back() == 'f' || text.back() == 'F'))
        text.remove_suffix(1);
    if (text.empty())
        return std::nullopt;
    if (text.front() == '+')
        text.remove_prefix(1);
    // Not `std::from_chars`: its floating-point overloads are missing from the
    // Android NDK's standard library (see `core/number_parse.h`).
    core::f64 value = 0.0;
    if (text.empty() || !core::decimalToDouble(text, value))
        return std::nullopt;
    return static_cast<f32>(value);
}

[[nodiscard]] u32 componentsOf(SurfaceParamType type) noexcept
{
    switch (type) {
    case SurfaceParamType::Float2:
        return 2;
    case SurfaceParamType::Float3:
        return 3;
    case SurfaceParamType::Float4:
        return 4;
    default:
        return 1;
    }
}

[[nodiscard]] std::string_view hlslType(SurfaceParamType type) noexcept
{
    switch (type) {
    case SurfaceParamType::Float2:
        return "float2";
    case SurfaceParamType::Float3:
        return "float3";
    case SurfaceParamType::Float4:
        return "float4";
    case SurfaceParamType::Int:
        return "int";
    case SurfaceParamType::Bool:
        return "bool";
    default:
        return "float";
    }
}

[[nodiscard]] std::optional<SurfaceParamType> typeNamed(std::string_view name) noexcept
{
    if (name == "float")
        return SurfaceParamType::Float;
    if (name == "float2")
        return SurfaceParamType::Float2;
    if (name == "float3")
        return SurfaceParamType::Float3;
    if (name == "float4")
        return SurfaceParamType::Float4;
    if (name == "int")
        return SurfaceParamType::Int;
    if (name == "bool")
        return SurfaceParamType::Bool;
    return std::nullopt;
}

// A default: `0.5`, `true`, `3`, or `float3(0, 0.1, 0.2)` for its own type.
[[nodiscard]] bool parseDefault(std::string_view text, SurfaceParam& param)
{
    text = trimmed(text);
    if (param.type == SurfaceParamType::Bool) {
        if (text == "true" || text == "1") {
            param.value[0] = 1.0f;
            return true;
        }
        if (text == "false" || text == "0") {
            param.value[0] = 0.0f;
            return true;
        }
        return false;
    }
    const u32 components = componentsOf(param.type);
    if (components == 1) {
        const std::optional<f32> value = number(text);
        if (!value.has_value())
            return false;
        if (param.type == SurfaceParamType::Int && *value != static_cast<f32>(static_cast<int>(*value)))
            return false;
        param.value[0] = *value;
        return true;
    }
    const std::string_view prefix = hlslType(param.type);
    if (text.substr(0, prefix.size()) != prefix)
        return false;
    const std::optional<std::vector<std::string_view>> parts = argumentsAt(text, prefix.size());
    if (!parts.has_value() || parts->size() != components)
        return false;
    for (u32 index = 0; index < components; ++index) {
        const std::optional<f32> value = number((*parts)[index]);
        if (!value.has_value())
            return false;
        param.value[index] = *value;
    }
    return true;
}

[[nodiscard]] bool parseAnnotation(std::string_view text, SurfaceParam& param)
{
    text = trimmed(text);
    if (text == "colour" || text == "color") {
        param.annotation = SurfaceAnnotation::Colour;
        return param.type == SurfaceParamType::Float3 || param.type == SurfaceParamType::Float4;
    }
    if (text == "toggle") {
        param.annotation = SurfaceAnnotation::Toggle;
        return param.type == SurfaceParamType::Bool;
    }
    if (text.substr(0, 5) == "range") {
        const std::optional<std::vector<std::string_view>> bounds = argumentsAt(text, 5);
        if (!bounds.has_value() || bounds->size() != 2)
            return false;
        const std::optional<f32> low = number((*bounds)[0]);
        const std::optional<f32> high = number((*bounds)[1]);
        if (!low.has_value() || !high.has_value() || !(*low < *high))
            return false;
        param.annotation = SurfaceAnnotation::Range;
        param.minimum = *low;
        param.maximum = *high;
        return param.type == SurfaceParamType::Float || param.type == SurfaceParamType::Int;
    }
    return false;
}

// Tokens a surface must not write: the layout is the engine's.
constexpr std::string_view Forbidden[] = {
    "register",   "cbuffer",      "tbuffer",   "SamplerState", "SamplerComparisonState",
    "Texture2D",  "TextureCube",  "Texture3D", "RWTexture2D",  "StructuredBuffer",
    "VertexMain", "FragmentMain",
};

} // namespace

const SurfaceParam* SurfaceReflection::param(std::string_view name) const noexcept
{
    for (const SurfaceParam& entry : params) {
        if (entry.name == name)
            return &entry;
    }
    return nullptr;
}

SurfaceReflection reflectSurface(std::string_view source)
{
    SurfaceReflection reflection;
    const std::string text = withoutComments(source);
    const std::string_view view = text;
    const auto error = [&](std::size_t at, std::string_view key, std::string_view subject) {
        reflection.errors.push_back(SurfaceDiagnostic{lineAt(view, at), std::string(key), std::string(subject)});
    };

    for (const std::string_view word : Forbidden) {
        for (const std::size_t at : occurrences(view, word))
            error(at, "asset.err.surface_forbidden", word);
    }

    // **Parameters, in the order written**, packed as a cbuffer packs them: no
    // value straddles a 16-byte row.
    u32 offset = SurfaceBlockHeaderBytes;
    for (const std::size_t at : occurrences(view, "LUAUG_PARAM")) {
        // The one in the contract header's own `#define` is not a declaration.
        const std::size_t lineStart = view.rfind('\n', at);
        const std::string_view before =
            trimmed(view.substr(lineStart == std::string_view::npos ? 0 : lineStart + 1,
                                at - (lineStart == std::string_view::npos ? 0 : lineStart + 1)));
        if (before == "#define")
            continue;
        const std::optional<std::vector<std::string_view>> arguments = argumentsAt(view, at + 11);
        if (!arguments.has_value() || arguments->size() < 3 || arguments->size() > 4) {
            error(at, "asset.err.surface_param_shape", "LUAUG_PARAM");
            continue;
        }
        SurfaceParam param;
        param.line = lineAt(view, at);
        const std::optional<SurfaceParamType> type = typeNamed((*arguments)[0]);
        if (!type.has_value()) {
            error(at, "asset.err.surface_param_type", (*arguments)[0]);
            continue;
        }
        param.type = *type;
        param.name = std::string((*arguments)[1]);
        if (!isIdentifier(param.name) || param.name.rfind("Luaug", 0) == 0 || reflection.param(param.name) != nullptr) {
            error(at, "asset.err.surface_param_name", param.name);
            continue;
        }
        if (!parseDefault((*arguments)[2], param)) {
            error(at, "asset.err.surface_param_default", param.name);
            continue;
        }
        if (arguments->size() == 4 && !parseAnnotation((*arguments)[3], param)) {
            error(at, "asset.err.surface_param_annotation", param.name);
            continue;
        }
        const u32 bytes = 4u * componentsOf(param.type);
        if ((offset % 16u) + bytes > 16u)
            offset = (offset + 15u) & ~15u;
        param.offset = offset;
        offset += bytes;
        reflection.params.push_back(std::move(param));
    }
    reflection.blockBytes = (offset + 15u) & ~15u;
    if (reflection.blockBytes > MaxSurfaceBlockBytes)
        error(0, "asset.err.surface_block_size", std::to_string(reflection.blockBytes));

    for (const std::size_t at : occurrences(view, "LUAUG_TEXTURE")) {
        const std::size_t lineStart = view.rfind('\n', at);
        const std::size_t from = lineStart == std::string_view::npos ? 0 : lineStart + 1;
        if (trimmed(view.substr(from, at - from)) == "#define")
            continue;
        const std::optional<std::vector<std::string_view>> arguments = argumentsAt(view, at + 13);
        if (!arguments.has_value() || arguments->empty() || arguments->size() > 2) {
            error(at, "asset.err.surface_param_shape", "LUAUG_TEXTURE");
            continue;
        }
        SurfaceTexture texture;
        texture.line = lineAt(view, at);
        texture.name = std::string((*arguments)[0]);
        const bool taken = reflection.param(texture.name) != nullptr ||
                           std::any_of(reflection.textures.begin(), reflection.textures.end(),
                                       [&](const SurfaceTexture& other) { return other.name == texture.name; });
        if (!isIdentifier(texture.name) || texture.name.rfind("Luaug", 0) == 0 || taken) {
            error(at, "asset.err.surface_param_name", texture.name);
            continue;
        }
        if (arguments->size() == 2) {
            const std::string_view fallback = (*arguments)[1];
            if (fallback == "black")
                texture.fallback = SurfaceTextureDefault::Black;
            else if (fallback == "normal")
                texture.fallback = SurfaceTextureDefault::Normal;
            else if (fallback != "white") {
                error(at, "asset.err.surface_param_annotation", texture.name);
                continue;
            }
        }
        if (reflection.textures.size() == MaxSurfaceTextures) {
            error(at, "asset.err.surface_texture_count", texture.name);
            continue;
        }
        reflection.textures.push_back(std::move(texture));
    }

    const auto defines = [&](std::string_view function) {
        for (const std::size_t at : occurrences(view, function)) {
            const std::size_t lineStart = view.rfind('\n', at);
            const std::size_t from = lineStart == std::string_view::npos ? 0 : lineStart + 1;
            const std::string_view head = trimmed(view.substr(from, at - from));
            if (head == "void" && argumentsAt(view, at + function.size()).has_value())
                return true;
        }
        return false;
    };
    reflection.hasVertex = defines("surfaceVertex");
    reflection.hasFragment = defines("surfaceFragment");
    return reflection;
}

namespace {

void appendBlock(std::string& out, const SurfaceReflection& reflection, std::string_view registerName)
{
    out += "cbuffer LuaugSurfaceBlock : register(";
    out += registerName;
    out += ")\n{\n    float4 LuaugSurfaceClock;\n    uint4 LuaugSurfaceTextures;\n";
    for (const SurfaceParam& param : reflection.params) {
        out += "    ";
        out += hlslType(param.type);
        out += ' ';
        out += param.name;
        out += ";\n";
    }
    out += "};\n\n";
}

void appendTextures(std::string& out, const SurfaceReflection& reflection, bool fragment, std::string_view space)
{
    for (u32 index = 0; index < reflection.textures.size(); ++index) {
        const std::string slot = std::to_string(fragment ? surfaceFragmentSlot(index) : index);
        const std::string& name = reflection.textures[index].name;
        out += "static const uint " + name + "Bit = " + std::to_string(1u << index) + "u;\n";
        out += "Texture2D " + name + " : register(t" + slot + ", " + std::string(space) + ");\n";
        out += "SamplerState " + name + "Sampler : register(s" + slot + ", " + std::string(space) + ");\n";
    }
    out += '\n';
}

[[nodiscard]] bool instanced(SurfaceVariant variant) noexcept
{
    return variant == SurfaceVariant::ForwardInstanced || variant == SurfaceVariant::DepthInstanced;
}

[[nodiscard]] bool depthOnly(SurfaceVariant variant) noexcept
{
    return variant == SurfaceVariant::Depth || variant == SurfaceVariant::DepthInstanced;
}

constexpr std::string_view VertexInput = R"(struct VertexInput
{
    float3 Position : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float4 Tangent : TEXCOORD2;
    float2 Uv : TEXCOORD3;
#if defined(LUAUG_SURFACE_INSTANCED)
    float4 ModelColumn0 : TEXCOORD4;
    float4 ModelColumn1 : TEXCOORD5;
    float4 ModelColumn2 : TEXCOORD6;
    float4 ModelColumn3 : TEXCOORD7;
    float4 InstanceAlphaTint : TEXCOORD8;
#endif
};

// The vertex as the mesh gave it, and what a surface reads about it before it
// moves.
SurfaceVertex luaugSurfaceVertex(VertexInput input)
{
    SurfaceVertex vertex;
    vertex.Position = input.Position;
    vertex.Normal = input.Normal;
    vertex.Tangent = input.Tangent;
    vertex.Uv0 = input.Uv;
    vertex.Uv1 = input.Uv;
    vertex.Color = float4(1.0f, 1.0f, 1.0f, 1.0f);
    return vertex;
}

SurfaceInputs luaugVertexInputs(SurfaceVertex vertex, float4x4 model)
{
    SurfaceInputs inputs;
    inputs.Time = LuaugSurfaceClock.x;
    inputs.CameraPosition = LuaugSurfaceClock.yzw;
    inputs.ObjectToWorld = model;
    // The draw is relative to the camera (the renderer's floating origin); the
    // world is not.
    inputs.WorldPosition = mul(model, float4(vertex.Position, 1.0f)).xyz + LuaugSurfaceClock.yzw;
    inputs.WorldNormal = normalize(mul((float3x3)model, vertex.Normal));
    inputs.WorldTangent = float4(mul((float3x3)model, vertex.Tangent.xyz), vertex.Tangent.w);
    inputs.Uv0 = vertex.Uv0;
    inputs.Uv1 = vertex.Uv1;
    inputs.VertexColor = vertex.Color;
    inputs.ScreenUv = float2(0.0f, 0.0f);
    inputs.ScreenPosition = float4(0.0f, 0.0f, 0.0f, 0.0f);
    inputs.SceneDepth = 3.0e38f;
    inputs.SceneColor = float3(0.0f, 0.0f, 0.0f);
    return inputs;
}

float4x4 luaugModel(VertexInput input)
{
#if defined(LUAUG_SURFACE_INSTANCED)
    return transpose(float4x4(input.ModelColumn0, input.ModelColumn1, input.ModelColumn2, input.ModelColumn3));
#elif defined(LUAUG_SURFACE_DEPTH)
    return ShadowModel;
#else
    return Model;
#endif
}

)";

constexpr std::string_view ForwardVertex = R"(Interpolants VertexMain(VertexInput input)
{
    const float4x4 model = luaugModel(input);
    SurfaceVertex vertex = luaugSurfaceVertex(input);
    surfaceVertex(vertex, luaugVertexInputs(vertex, model));

    Interpolants output;
    const float4 shadingPosition = mul(model, float4(vertex.Position, 1.0f));
    output.ShadingPosition = shadingPosition.xyz;
    output.Position = mul(ViewProjection, shadingPosition);
    output.ViewDepth = output.Position.w;
#if defined(LUAUG_SURFACE_INSTANCED)
    const float3 a = input.ModelColumn0.xyz;
    const float3 b = input.ModelColumn1.xyz;
    const float3 c = input.ModelColumn2.xyz;
    output.Normal = mul(float3x3(cross(b, c), cross(c, a), cross(a, b)), vertex.Normal);
    output.InstanceAlpha = input.InstanceAlphaTint.x;
#else
    output.Normal = mul((float3x3)NormalMatrix, vertex.Normal);
    output.InstanceAlpha = InstanceAlphaUnused.x;
#endif
    output.Tangent = float4(mul((float3x3)model, vertex.Tangent.xyz), vertex.Tangent.w);
    output.Uv = vertex.Uv0;
    return output;
}
)";

constexpr std::string_view DepthVertex = R"(struct Interpolants
{
    float4 Position : SV_Position;
};

Interpolants VertexMain(VertexInput input)
{
    const float4x4 model = luaugModel(input);
    SurfaceVertex vertex = luaugSurfaceVertex(input);
    surfaceVertex(vertex, luaugVertexInputs(vertex, model));
    Interpolants output;
    output.Position = mul(LightViewProjection, mul(model, float4(vertex.Position, 1.0f)));
    return output;
}
)";

constexpr std::string_view ForwardFragment = R"(float4 FragmentMain(Interpolants input) : SV_Target0
{
    const float3 geometricNormal = normalize(input.Normal);
    SurfaceInputs inputs;
    inputs.Time = LuaugSurfaceClock.x;
    inputs.CameraPosition = LuaugSurfaceClock.yzw;
    inputs.ObjectToWorld = float4x4(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                    0.0f, 0.0f, 1.0f);
    inputs.WorldPosition = input.ShadingPosition + LuaugSurfaceClock.yzw;
    inputs.WorldNormal = geometricNormal;
    inputs.WorldTangent = input.Tangent;
    inputs.Uv0 = input.Uv;
    inputs.Uv1 = input.Uv;
    inputs.VertexColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    inputs.ScreenUv = input.Position.xy * ViewportParams.zw;
    inputs.ScreenPosition = float4(input.Position.xy, 0.0f, input.ViewDepth);
#if defined(LUAUG_SURFACE_BLENDED)
    inputs.SceneDepth = LuaugSceneDepth.SampleLevel(LuaugSceneDepthSampler, inputs.ScreenUv, 0.0f);
    inputs.SceneColor = LuaugSceneColor.SampleLevel(LuaugSceneColorSampler, inputs.ScreenUv, 0.0f).rgb;
#else
    inputs.SceneDepth = 3.0e38f;
    inputs.SceneColor = float3(0.0f, 0.0f, 0.0f);
#endif

    SurfaceOutput surface;
    surface.BaseColor = float3(1.0f, 1.0f, 1.0f);
    surface.Alpha = 1.0f;
    surface.Metallic = 0.0f;
    surface.Roughness = 0.7f;
    surface.Normal = geometricNormal;
    surface.Emissive = float3(0.0f, 0.0f, 0.0f);
    surfaceFragment(inputs, surface);

    // The draw's own alpha -- a part's transparency -- on top of the surface's.
    const float alpha = surface.Alpha * input.InstanceAlpha;
    clip(alpha - MetallicRoughnessNormalCutoff.w);
    // **Every interpolant is read**, whatever the surface reads: D3D12 links a
    // pixel shader's inputs to the vertex shader's outputs by position, and a
    // surface that never touched its uv had them stripped and its pipeline
    // refused. A test no value can pass keeps them without costing a thing --
    // the clip above already makes this a shader that may discard.
    if (input.Uv.x == -3.0e38f && input.Tangent.w == -3.0e38f)
        discard;
    const float3 normal = normalize(surface.Normal);
    Surface lit = makeSurface(input.ShadingPosition, normal, surface.BaseColor, saturate(surface.Metallic),
                              saturate(surface.Roughness));
    lit.Alpha = antiAliasedAlpha(lit.Alpha, geometricNormal);
    float3 color = lightSurface(lit, input.ShadingPosition, normal, input.ViewDepth, input.Position.xy);
    color += surface.Emissive;
    color = applyFog(color, FogColor.rgb, FogRange, length(input.ShadingPosition));
    return float4(color, alpha);
}
)";

} // namespace

std::string surfaceWrapper(const SurfaceReflection& reflection, SurfaceVariant variant, SurfaceStage stage,
                           std::string_view userInclude)
{
    std::string out;
    out += "// GENERATED from a surface shader by the engine (ADR 0091). Do not edit.\n";
    out += "#define LUAUG_SURFACE_WRAPPER\n";
    if (instanced(variant))
        out += "#define LUAUG_SURFACE_INSTANCED\n";
    if (variant == SurfaceVariant::ForwardBlended)
        out += "#define LUAUG_SURFACE_BLENDED\n";
    if (depthOnly(variant)) {
        out += "#define LUAUG_SURFACE_DEPTH\n#define LUAUG_UNIFORMS_SHADOW\n#include \"luaug_pbr.hlsli\"\n";
        // The lighting functions a fragment function may call exist only in the
        // forward headers; a depth pass never runs one.
    }
    else {
        out += "#define LUAUG_UNIFORMS_OBJECT\n#define LUAUG_UNIFORMS_FRAME\n#define LUAUG_UNIFORMS_MATERIAL\n";
        out += "#include \"luaug_forward.hlsli\"\n";
    }
    out += "#include \"luaug/surface.hlsli\"\n\n";

    if (stage == SurfaceStage::Vertex) {
        appendBlock(out, reflection, "b1, space1");
        appendTextures(out, reflection, false, "space0");
    }
    else {
        appendBlock(out, reflection, "b2, space3");
        appendTextures(out, reflection, true, "space2");
    }
    // In both stages' text, since both carry both entry points; the vertex
    // stage never reads them and the compiler drops them there.
    if (variant == SurfaceVariant::ForwardBlended) {
        out += "Texture2D<float> LuaugSceneDepth : register(t14, space2);\n";
        out += "SamplerState LuaugSceneDepthSampler : register(s14, space2);\n";
        out += "Texture2D LuaugSceneColor : register(t15, space2);\n";
        out += "SamplerState LuaugSceneColorSampler : register(s15, space2);\n\n";
        out += "float sceneDepthAt(float2 uv)\n{\n"
               "    return LuaugSceneDepth.SampleLevel(LuaugSceneDepthSampler, uv, 0.0f);\n}\n";
        out += "float3 sceneColorAt(float2 uv)\n{\n"
               "    return LuaugSceneColor.SampleLevel(LuaugSceneColorSampler, uv, 0.0f).rgb;\n}\n\n";
    }
    else {
        // Every variant compiles the same user file, so the functions exist in
        // every variant; outside a blended surface there is no scene to read.
        out += "float sceneDepthAt(float2 uv)\n{\n    return 3.0e38f;\n}\n";
        out += "float3 sceneColorAt(float2 uv)\n{\n    return float3(0.0f, 0.0f, 0.0f);\n}\n\n";
    }

    out += "#include \"";
    out += userInclude;
    out += "\"\n\n";
    if (!reflection.hasVertex)
        out += "void surfaceVertex(inout SurfaceVertex vertex, SurfaceInputs inputs)\n{\n}\n\n";
    if (!reflection.hasFragment)
        out += "void surfaceFragment(SurfaceInputs inputs, inout SurfaceOutput surface)\n{\n}\n\n";

    out += VertexInput;
    if (depthOnly(variant)) {
        out += DepthVertex;
        out += "\nvoid FragmentMain()\n{\n}\n";
    }
    else {
        out += ForwardVertex;
        out += '\n';
        out += ForwardFragment;
    }
    return out;
}

SurfaceResourceCounts surfaceResourceCounts(const SurfaceReflection& reflection, SurfaceVariant variant,
                                            SurfaceStage stage) noexcept
{
    const u32 textures = static_cast<u32>(reflection.textures.size());
    if (stage == SurfaceStage::Vertex)
        return SurfaceResourceCounts{textures, 2};
    if (depthOnly(variant))
        return SurfaceResourceCounts{0, 0};
    if (variant == SurfaceVariant::ForwardBlended)
        return SurfaceResourceCounts{SceneColorSlot + 1, 3};
    return SurfaceResourceCounts{textures > 4 ? surfaceFragmentSlot(textures - 1) + 1 : EngineFragmentSamplers, 3};
}

void writeSurfaceParam(const SurfaceParam& param, std::span<const f32> value, std::span<core::u8> block) noexcept
{
    const u32 components = componentsOf(param.type);
    if (param.offset + 4u * components > block.size())
        return;
    for (u32 index = 0; index < components; ++index) {
        const f32 component = index < value.size() ? value[index] : param.value[index];
        core::u8* at = block.data() + param.offset + 4u * index;
        if (param.type == SurfaceParamType::Int) {
            const auto whole = static_cast<core::i32>(component);
            std::memcpy(at, &whole, 4);
        }
        else if (param.type == SurfaceParamType::Bool) {
            const core::u32 flag = component != 0.0f ? 1u : 0u;
            std::memcpy(at, &flag, 4);
        }
        else {
            std::memcpy(at, &component, 4);
        }
    }
}

} // namespace luaug::asset
