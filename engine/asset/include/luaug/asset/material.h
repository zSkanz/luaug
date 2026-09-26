// A material asset: a `.material.json` under a project's `content/` (ADR 0090).
//
// **A material is a file and never an instance.** It has no transform, it is not
// simulated, and it has no parent that means anything in a world -- and putting
// it in the tree made every boundary of the tree a place it could be lost (D115,
// D130, D133, D142). A part names one by URN, and the library below answers what
// the URN means.
//
// Three things live here, and all three are data:
//
//   - `MaterialProperties`, the flat description a renderer draws: the same
//     thirteen fields the old `Material` class had, with the same defaults.
//   - `MaterialAsset`, what one file holds: a `parent` (a variant names one),
//     the fields it writes, and the `instanceParameters` it lets a part
//     override.
//   - `MaterialOverrides`, what one PART holds: the values it overrides, from
//     the closed set a material may declare. Trivially copyable, because it
//     lives in a component and a snapshot is a per-pool copy (ADR 0016).
//
// L2, and nothing here knows a world exists. `scene` holds a URN and a set of
// overrides; the host owns the `MaterialLibrary` that says what they mean.
#pragma once

#include "luaug/asset/content.h"
#include "luaug/core/content_hash.h"
#include "luaug/core/math.h"
#include "luaug/core/types.h"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace luaug::asset {

// The thirteen fields, in the order the file writes them -- which is ADR 0090's
// order, and the order the old class declared them in.
enum class MaterialField : core::u8
{
    Color,
    Transparency,
    ColorMap,
    NormalMap,
    MetallicRoughnessMap,
    Emissive,
    EmissiveMap,
    Metalness,
    Roughness,
    NormalScale,
    AlphaMode,
    AlphaCutoff,
    DoubleSided,
    Count,
};

inline constexpr core::usize MaterialFieldCount = static_cast<core::usize>(MaterialField::Count);

// One bit per field, bit N for `MaterialField` N.
using MaterialFieldMask = core::u16;

[[nodiscard]] constexpr MaterialFieldMask fieldBit(MaterialField field) noexcept
{
    return static_cast<MaterialFieldMask>(1u << static_cast<unsigned>(field));
}

inline constexpr MaterialFieldMask AllMaterialFields = static_cast<MaterialFieldMask>((1u << MaterialFieldCount) - 1u);

// **The closed set a material may let a part override** (ADR 0090). Maps are not
// on it: a different texture is a different material, and that is what lets the
// renderer batch. Neither are `AlphaMode` and `DoubleSided`, which change how a
// surface is DRAWN rather than what it looks like, and so decide a pipeline.
inline constexpr MaterialFieldMask DeclarableParameters =
    fieldBit(MaterialField::Color) | fieldBit(MaterialField::Transparency) | fieldBit(MaterialField::Emissive) |
    fieldBit(MaterialField::Metalness) | fieldBit(MaterialField::Roughness) | fieldBit(MaterialField::NormalScale) |
    fieldBit(MaterialField::AlphaCutoff);

// **What the engine default material declares**: a grey-box part can still be
// tinted and faded without anybody authoring a material, and every scene
// written before ADR 0090 opens looking the same.
inline constexpr MaterialFieldMask DefaultMaterialParameters =
    fieldBit(MaterialField::Color) | fieldBit(MaterialField::Transparency);

// The field's name as the file, the script and the Properties panel spell it.
[[nodiscard]] std::string_view materialFieldName(MaterialField field) noexcept;
[[nodiscard]] std::optional<MaterialField> materialFieldNamed(std::string_view name) noexcept;

// `Enum.AlphaMode`, glTF's three, by the value the enum declares.
enum class MaterialAlphaMode : core::i32
{
    Opaque = 0,
    Mask = 1,
    Blend = 2,
};

// The flat description: every field with a value. **The defaults are the engine
// default material**, which looks exactly as a plain part always has: white,
// dielectric, roughness 0.7.
// One value of the surface shader a material names (ADR 0091), by the name the
// shader declares it under: a number or vector in `value` (as many components
// as `components` says), or a texture by URN. Kept apart from the built-in
// fields on purpose: their set is closed and every system that walks it -- the
// panel, the wire, the overrides -- stays as it was.
struct ShaderParameter
{
    std::string name;
    std::array<core::f32, 4> value{};
    core::u8 components = 1;
    std::string texture;
    // A texture that is data -- a normal or a height -- rather than a colour.
    bool linear = false;

    [[nodiscard]] bool isTexture() const noexcept { return !texture.empty(); }
    [[nodiscard]] bool operator==(const ShaderParameter&) const = default;
};

struct MaterialProperties
{
    core::Color3 color{1.0f, 1.0f, 1.0f};
    core::f32 transparency = 0.0f;
    // Content URNs, empty for none.
    std::string colorMap;
    std::string normalMap;
    // Occlusion, roughness and metalness in one image's R, G and B -- glTF's
    // packing, and one field because they are one file.
    std::string metallicRoughnessMap;
    core::Color3 emissive{0.0f, 0.0f, 0.0f};
    std::string emissiveMap;
    core::f32 metalness = 0.0f;
    // Fairly rough, which is what an untextured building block looks like;
    // glTF's own default is 1, which is right for a file that forgot to say and
    // wrong for a material somebody is about to author.
    core::f32 roughness = 0.7f;
    core::f32 normalScale = 1.0f;
    core::i32 alphaMode = 0;
    core::f32 alphaCutoff = 0.5f;
    bool doubleSided = false;

    // **The surface shader** (ADR 0091): a URN, or empty for the built-in
    // surface. `readsSceneColor` asks the renderer for what is behind.
    std::string shader;
    bool readsSceneColor = false;
    // The shader's values, sorted by name; a name no parameter of the shader
    // declares is kept and ignored, so switching shaders loses nothing.
    std::vector<ShaderParameter> shaderParameters;

    [[nodiscard]] const ShaderParameter* shaderParameter(std::string_view name) const noexcept;
    // Sets one, keeping the list sorted.
    void setShaderParameter(ShaderParameter parameter);
    [[nodiscard]] bool operator==(const MaterialProperties&) const = default;
};

// Copies one field from `from` into `into`.
void copyMaterialField(MaterialField field, const MaterialProperties& from, MaterialProperties& into);
// Whether one field holds the same value in both.
[[nodiscard]] bool sameMaterialField(MaterialField field, const MaterialProperties& a, const MaterialProperties& b);

// What one `.material.json` holds.
struct MaterialAsset
{
    // Another material asset's URN, or empty for a base.
    std::string parent;
    // Which parameters a part wearing this may override. A variant inherits its
    // parent's and may add more; only `DeclarableParameters` bits are kept.
    MaterialFieldMask instanceParameters = 0;
    // **And which of its surface shader's parameters** (ADR 0091), by name --
    // any `instanceParameters` entry that is not a built-in field. Sorted,
    // each once. A shader's parameters are whatever its file declares, so they
    // cannot be bits of the built-in mask.
    std::vector<std::string> instanceShaderParameters;
    // Which fields this file writes. A base written by `writeMaterialAsset`
    // writes every one; a variant writes only what it overrides, and every
    // other field comes from its parent.
    MaterialFieldMask written = 0;
    // Whether this file says `shader` and `readsSceneColor` -- a variant that
    // does not keeps its parent's.
    bool shaderWritten = false;
    // The values; meaningful for the `written` fields.
    MaterialProperties properties;

    [[nodiscard]] bool operator==(const MaterialAsset&) const = default;
};

inline constexpr std::string_view MaterialFormat = "luaug-material";
inline constexpr core::i64 MaterialFormatVersion = 1;
// The compound suffix that makes a file a material, wherever it sits (ADR 0049's
// rule for stamps, applied here).
inline constexpr std::string_view MaterialSuffix = ".material.json";

[[nodiscard]] bool isMaterialPath(std::string_view path) noexcept;

// What reading a file noticed that did not stop it being read.
struct MaterialReadNotes
{
    // Keys the reader does not know, top-level or under `properties`, and
    // `instanceParameters` entries outside the declarable set. **Reported and
    // not fatal**: a file written by a newer engine opens in this one with
    // everything this one understands.
    std::vector<std::string> unknownFields;
    // Fields present with a value of the wrong shape, which read as absent.
    std::vector<std::string> malformedFields;
};

// Parses a material file. Fails only for text that is not JSON, or that is not
// a material of a version this engine reads; anything narrower is a note.
[[nodiscard]] std::optional<MaterialAsset> readMaterialAsset(std::string_view json, MaterialReadNotes* notes = nullptr,
                                                             std::string* error = nullptr);

// **The file's text, a pure function of what it holds** (ADR 0090): fixed key
// order, indented one field per line, floats as their shortest decimal. A base
// (no `parent`) writes every field; a variant writes only its `written` ones.
[[nodiscard]] std::string writeMaterialAsset(const MaterialAsset& material);

// A material chain folded into one description.
struct ResolvedMaterial
{
    MaterialProperties properties;
    MaterialFieldMask instanceParameters = DefaultMaterialParameters;
    // The shader parameters a part may override: the union down the chain.
    std::vector<std::string> instanceShaderParameters;

    [[nodiscard]] bool declaresShaderParameter(std::string_view name) const noexcept;
};

// Whether `name` can name a shader parameter: an HLSL identifier.
[[nodiscard]] bool isShaderParameterName(std::string_view name) noexcept;
// Adds `name` to a sorted list of names, once.
void addShaderParameterName(std::vector<std::string>& names, std::string_view name);

// The engine default material: built in, never a file.
[[nodiscard]] const ResolvedMaterial& defaultMaterial() noexcept;

// What resolving a chain could not do.
struct MaterialResolveNotes
{
    // The file whose `parent` closed a loop. The material resolves to the
    // engine default.
    std::string cycleClosedBy;
    // A file in the chain that names a parent that cannot be found, and that
    // parent. What was found still applies, over the engine default.
    std::string missingParentOf;
    std::string missingParent;
};

using MaterialLookup = std::function<const MaterialAsset*(std::string_view urn)>;

// **Parent-first into one flat description.** A cycle resolves to the engine
// default; a missing parent resolves what was found over the default. `found`
// answers for the chain's first link; when it is missing, the answer is the
// default and no note is made -- that is the caller's to say, because only the
// caller knows whether the URN was ever meant to exist.
[[nodiscard]] ResolvedMaterial resolveMaterial(std::string_view urn, const MaterialLookup& lookup,
                                               MaterialResolveNotes* notes = nullptr);

// What one part overrides, from the declarable set.
//
// **Trivially copyable on purpose**: it lives in `PartComponent`, and a world
// snapshot is a per-pool copy. A value for a field whose bit is not in `set`
// means nothing and is kept at its default, so two equal override sets compare
// equal byte for byte.
struct MaterialOverrides
{
    MaterialFieldMask set = 0;
    core::Color3 color{1.0f, 1.0f, 1.0f};
    core::f32 transparency = 0.0f;
    core::Color3 emissive{0.0f, 0.0f, 0.0f};
    core::f32 metalness = 0.0f;
    core::f32 roughness = 0.7f;
    core::f32 normalScale = 1.0f;
    core::f32 alphaCutoff = 0.5f;

    [[nodiscard]] bool operator==(const MaterialOverrides&) const = default;
    [[nodiscard]] bool has(MaterialField field) const noexcept { return (set & fieldBit(field)) != 0; }
    [[nodiscard]] bool empty() const noexcept { return set == 0; }
};

// Sets one override, taking its value from `from`. Refused (false) for a field
// outside the declarable set.
bool setOverride(MaterialOverrides& overrides, MaterialField field, const MaterialProperties& from);
// Clears one, putting its storage back to the default so equality stays exact.
void clearOverride(MaterialOverrides& overrides, MaterialField field);
// The overrides `declared` allows, written over `into`. An override the
// material does not declare is KEPT on the part and ignored here (ADR 0090).
void applyOverrides(const MaterialOverrides& overrides, MaterialFieldMask declared, MaterialProperties& into);
// The override set as a `MaterialProperties`: its fields carry the override
// values where set and the defaults elsewhere.
[[nodiscard]] MaterialProperties overrideValues(const MaterialOverrides& overrides);

// **The host's materials, keyed by URN** (ADR 0090). Loads through a source --
// normally the content mounts, compiled first and loose second -- and keeps
// each file and each resolved chain until told the file changed.
//
// One per host, shared by every world the host draws: a stamp's stage and the
// game resolve the same URN to the same material, which is the property D115
// was missing.
class MaterialLibrary
{
public:
    // Reads one material by URN, or nothing.
    using Source = std::function<std::optional<MaterialAsset>(std::string_view urn, MaterialReadNotes& notes)>;

    MaterialLibrary() = default;
    explicit MaterialLibrary(Source source) : m_source(std::move(source)) {}

    MaterialLibrary(const MaterialLibrary&) = delete;
    MaterialLibrary& operator=(const MaterialLibrary&) = delete;

    void setSource(Source source);

    // The file behind a URN, loaded on first ask; nothing when it cannot be
    // read. Reported once per load, not once per frame.
    [[nodiscard]] const MaterialAsset* asset(std::string_view urn);
    // Whether a URN names a material this library can read.
    [[nodiscard]] bool exists(std::string_view urn) { return asset(urn) != nullptr; }

    // The URN's chain folded flat. The empty URN, a missing file and a cycle
    // all answer the engine default -- a surface that drew nothing because its
    // material failed would be harder to diagnose than a white one.
    [[nodiscard]] const ResolvedMaterial& resolve(std::string_view urn);

    // **A changed file** (ADR 0062): forgets it and every resolved chain,
    // because any variant may inherit through it.
    void forget(std::string_view urn);
    void forgetAll();

    // Puts a material in without a file -- the editor's unsaved edit, a test.
    // It stays until `forget`.
    void put(std::string_view urn, MaterialAsset material);

    // Bumped by every `forget`, `forgetAll` and `put`, so a cache keyed on a
    // resolved material knows when to stop trusting it.
    [[nodiscard]] core::u64 revision() const noexcept { return m_revision; }

private:
    struct Loaded
    {
        std::optional<MaterialAsset> asset;
    };

    Source m_source;
    std::unordered_map<std::string, Loaded> m_assets;
    std::unordered_map<std::string, ResolvedMaterial> m_resolved;
    core::u64 m_revision = 0;
};

// **The compiled form** (`AssetKind::Material`, ADR 0090): the parameter
// block and, for each of the four maps, its URN and the content hash of the
// compiled texture it names (zero for a map the pack does not hold). Binary,
// little-endian, versioned by its own magic -- the pack's table of contents is
// what says a blob is a material, so a mesh asking for one fails at the index.
struct CompiledMaterial
{
    MaterialAsset asset;
    // ColorMap, NormalMap, MetallicRoughnessMap, EmissiveMap.
    std::array<core::ContentHash, 4> mapHashes{};
};

[[nodiscard]] std::vector<std::byte> encodeMaterial(const CompiledMaterial& material);
[[nodiscard]] std::optional<CompiledMaterial> decodeMaterial(std::span<const std::byte> bytes);

// The source that reads through content mounts: a compiled material from a
// pack first (`AssetKind::Material`), the loose file second.
[[nodiscard]] MaterialLibrary::Source mountedMaterials(const ContentMounts& mounts);

} // namespace luaug::asset
