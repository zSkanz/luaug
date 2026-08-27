#include "luaug/assetc/compiler.h"

#include "luaug/asset/chunk.h"
#include "luaug/asset/content.h"
#include "luaug/asset/gltf.h"
#include "luaug/asset/image.h"
#include "luaug/asset/mesh_format.h"
#include "luaug/asset/model_split.h"
#include "luaug/assetc/exotic.h"
#include "luaug/core/json.h"
#include "luaug/core/json_writer.h"
#include "luaug/jobs/jobs.h"
#include "luaug/platform/file.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <optional>
#include <system_error>

namespace luaug::assetc {
namespace {

using asset::AssetKind;

[[nodiscard]] std::string lowercase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// Forward slashes always, whatever the host separator is. A URN that differed
// between Windows and Linux would give the same content two names and two
// manifest rows -- and content addressing exists precisely so one thing has one
// name.
[[nodiscard]] std::string urnFor(const std::filesystem::path& relative)
{
    std::string text = relative.generic_string();
    return "asset://" + text;
}

[[nodiscard]] bool endsWith(std::string_view text, std::string_view suffix)
{
    return text.size() > suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

[[nodiscard]] SourceKind classify(const std::filesystem::path& path)
{
    const std::string name = lowercase(path.filename().string());
    const std::string extension = lowercase(path.extension().string());

    // Matched on the compound suffix rather than on `.json`, so an ordinary
    // JSON file a project keeps in its content directory rides through as raw
    // rather than being refused for not being a chunk.
    if (endsWith(name, ".chunk.json")) {
        return SourceKind::Chunk;
    }

    if (extension == ".gltf" || extension == ".glb") {
        return SourceKind::Mesh;
    }
    // The exotic formats, through assimp (`exotic.h`). Classified as meshes
    // because that is what they are; which importer reads one is decided at
    // import time and is not a fact about the file.
    if (isExoticMesh(extension)) {
        return SourceKind::Mesh;
    }
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" ||
        extension == ".bmp") {
        return SourceKind::Texture;
    }
    // Everything else rides through untouched: a font file, a catalog, a shader
    // blob. Copying rather than refusing is what lets a project put anything it
    // wants in its content directory.
    (void)name;
    return SourceKind::Raw;
}

[[nodiscard]] bool readWhole(const std::filesystem::path& path, std::vector<std::byte>& out)
{
    return platform::readFile(path, out);
}

// **Bumped by hand whenever what this tool PRODUCES changes**, so a cache
// written by an older build is a miss rather than a wrong answer. Not derived
// from anything: a version derived from source hashes would invalidate on a
// comment, and one derived from nothing would survive a codec change.
//
// Bump it when: an encoder parameter moves, a format version moves, the
// importer starts producing different geometry, or the naming rules change.
//
// 2: a loose texture a `Material` names as a normal or metallic-roughness map
// is encoded as numbers rather than as colour, so what this tool produces for
// such an image moved.
constexpr core::u32 kCompilerRules = 2;

// What one source compiled to, remembered between runs.
//
// Keyed by everything that decides the answer: the source bytes, its own name,
// the pinned options, the rules version, and -- for a loose texture -- what the
// project's materials say it is for. That is the whole input to a pure
// function, so a hit is not a guess: it is the same answer arrived at without
// doing the work again. **A miss is never wrong, only slow.**
//
// **A COMPANION the source reads is NOT in the key**, and this comment used to
// say it was. A glTF that names an external image beside it keys on its own
// bytes alone, so editing that image leaves the mesh's compiled copy of it
// stale while the loose image itself recompiles. Nothing outside these tests
// sets `cacheRoot` yet, which is the only reason it has not bitten.
struct CachedSource
{
    // The blobs this source produced, in the order it produced them, each with
    // the kind it was added under.
    std::vector<std::pair<AssetKind, std::vector<std::byte>>> blobs;
    // The manifest rows, with their hashes already known.
    std::vector<ManifestEntry> entries;
};

[[nodiscard]] std::filesystem::path cachePathFor(const std::filesystem::path& root, const ContentHash& key)
{
    const std::string hex = key.toHex();
    // Fanned out one level, for the reason the editor's object store is: a
    // project is thousands of entries and one flat directory is where
    // filesystems stop coping.
    return root / hex.substr(0, 2) / (hex + ".cache");
}

// The key for one source. Everything that could change the answer goes in, and
// nothing that could not.
[[nodiscard]] ContentHash cacheKey(std::span<const std::byte> sourceBytes, std::string_view urn,
                                   const CompileOptions& options, SourceKind kind, bool colourData)
{
    core::ContentHasher hasher;
    hasher.update(sourceBytes);

    // **The URN, because the cached VALUE names the source.** An entry carries
    // the manifest rows this file produced and a row IS a name, so two
    // byte-identical files under two names are not the same answer. Keying on
    // content alone made the second of them inherit the first one's row and
    // lose its own -- within a single build, not only across two.
    hasher.update(std::as_bytes(std::span<const char>(urn.data(), urn.size())));

    // The pinned options, byte for byte. An upstream default change is a diff in
    // this tool (Decision 1), so hashing the struct is hashing the decision.
    const asset::MeshCompileOptions& mesh = options.mesh;
    hasher.update(std::as_bytes(std::span<const asset::MeshCompileOptions, 1>{&mesh, 1}));

    const core::u32 rules = kCompilerRules;
    hasher.update(std::as_bytes(std::span<const core::u32, 1>{&rules, 1}));
    const auto kindValue = static_cast<core::u32>(kind);
    hasher.update(std::as_bytes(std::span<const core::u32, 1>{&kindValue, 1}));
    // **What the project's materials say a loose image is for**, because it
    // decides the transfer function and so the bytes. Without it, the sRGB blob
    // written before the material existed comes back under the same name after
    // it does -- a cache that is wrong rather than slow.
    const core::u32 colourValue = colourData ? 1u : 0u;
    hasher.update(std::as_bytes(std::span<const core::u32, 1>{&colourValue, 1}));
    return hasher.finish();
}

// `[1.0, 2.0, 3.0]` and friends. Absent or malformed yields the fallback rather
// than a zero, because a zero here is a part at the world origin and looks like
// a bug in the generator rather than a bug in its file.
[[nodiscard]] core::DVec3 readDVec3(const core::JsonValue& value, core::DVec3 fallback)
{
    if (value.size() != 3) {
        return fallback;
    }
    return core::DVec3{value.at(0).asNumber(fallback.x), value.at(1).asNumber(fallback.y),
                       value.at(2).asNumber(fallback.z)};
}

[[nodiscard]] core::Vec3 readVec3(const core::JsonValue& value, core::Vec3 fallback)
{
    const core::DVec3 wide = readDVec3(value, core::toDVec3(fallback));
    return core::toVec3(wide);
}

// Interned as it goes, so a chunk with four hundred rocks carries one copy of
// the mesh URN rather than four hundred.
[[nodiscard]] u32 internString(asset::Chunk& chunk, std::string_view text)
{
    if (text.empty()) {
        return asset::ChunkInstance::NoString;
    }
    for (usize i = 0; i < chunk.strings.size(); ++i) {
        if (chunk.strings[i] == text) {
            return static_cast<u32>(i);
        }
    }
    chunk.strings.emplace_back(text);
    return static_cast<u32>(chunk.strings.size() - 1);
}

[[nodiscard]] std::optional<core::EngineError> readChunkSource(std::string_view json, asset::Chunk& out, f32& chunkSize)
{
    core::JsonDocument document;
    const core::JsonDocument::ParseResult parsed = document.parse(json, "chunk source");
    if (!parsed.ok) {
        const core::I18nArg args[] = {{"detail", parsed.diagnostic}};
        return core::makeError(LUAUG_TR("assetc.err.chunk_source"), args);
    }

    const core::JsonValue root = document.root();
    if (root["format"].asString() != "luaug-chunk-source") {
        const core::I18nArg args[] = {{"detail", "not a LuauG chunk source"}};
        return core::makeError(LUAUG_TR("assetc.err.chunk_source"), args);
    }

    chunkSize = static_cast<f32>(root["chunkSize"].asNumber(static_cast<core::f64>(asset::DefaultChunkSize)));
    out.id.x = static_cast<core::i32>(root["x"].asInteger());
    out.id.z = static_cast<core::i32>(root["z"].asInteger());
    out.id.layer = static_cast<core::i32>(root["layer"].asInteger());
    out.bounds = asset::chunkBounds(out.id, chunkSize);
    out.bounds.min.y = root["minY"].asNumber(-1.0);
    out.bounds.max.y = root["maxY"].asNumber(1.0);

    const core::JsonValue instances = root["instances"];
    if (instances.size() > asset::MaxChunkInstances) {
        const core::I18nArg args[] = {{"detail", "more instances than the engine will materialise"}};
        return core::makeError(LUAUG_TR("assetc.err.chunk_source"), args);
    }

    out.instances.reserve(instances.size());
    for (usize i = 0; i < instances.size(); ++i) {
        const core::JsonValue row = instances.at(i);
        asset::ChunkInstance instance;

        const std::string_view kind = row["kind"].asString("part");
        instance.kind = kind == "meshpart" ? asset::ChunkInstance::Kind::MeshPart : asset::ChunkInstance::Kind::Part;
        instance.shape = static_cast<core::u8>(row["shape"].asInteger());
        instance.anchored = row["anchored"].asBool(true);
        instance.transparency = static_cast<f32>(row["transparency"].asNumber(0.0));
        instance.cframe.position = readDVec3(row["position"], core::DVec3{});
        instance.size = readVec3(row["size"], core::Vec3{1.0f, 1.0f, 1.0f});
        instance.color = core::Color3{1.0f, 1.0f, 1.0f};
        const core::Vec3 colour = readVec3(row["color"], core::Vec3{1.0f, 1.0f, 1.0f});
        instance.color = core::Color3{colour.x, colour.y, colour.z};
        instance.name = internString(out, row["name"].asString());
        instance.meshContent = internString(out, row["mesh"].asString());

        if (instance.kind == asset::ChunkInstance::Kind::MeshPart &&
            instance.meshContent == asset::ChunkInstance::NoString) {
            // A `MeshPart` with no mesh is an invisible part, which is the
            // shape of a defect that surfaces as "the world is missing things"
            // rather than as an error.
            const core::I18nArg args[] = {{"detail", "a meshpart with no mesh"}};
            return core::makeError(LUAUG_TR("assetc.err.chunk_source"), args);
        }
        out.instances.push_back(instance);
    }
    return std::nullopt;
}

// --- What a loose texture is FOR ---------------------------------------------
//
// A `Material` is an instance like any other, so a project's materials live in
// its scenes and its stamps, wherever their author put them -- and those files
// are already sources. Reading them says what each image they name is, which is
// the one thing a standalone image cannot say about itself.

// The four `Material` properties that name an image, and whether what they name
// is colour or numbers (`api/defs/instances.api.luau`): `ColorMap` is sampled
// and multiplied by `Color`, `EmissiveMap` is what the surface glows with, and
// the other two are values a shader reads rather than a picture anybody looks
// at.
struct MaterialMap
{
    std::string_view property;
    bool colour;
};

constexpr MaterialMap kMaterialMaps[] = {
    {"ColorMap", true},
    {"EmissiveMap", true},
    {"NormalMap", false},
    {"MetallicRoughnessMap", false},
};

// What the project's materials say each image is for, by URN. Ordered rather
// than hashed for the reason every container in this file is: an answer that
// depended on a hash order would be an answer that depended on the machine.
using TextureUses = std::map<std::string, bool>;

// One property map -- a `Material`'s `properties`, or one entry of an
// `overrides` block, which is the same shape at a path inside a placed stamp.
void readMaterialMaps(const core::JsonValue& properties, TextureUses& out)
{
    if (properties.type() != core::JsonType::Object) {
        return;
    }
    for (const MaterialMap& map : kMaterialMaps) {
        const std::string_view urn = properties[map.property].asString();
        if (urn.empty()) {
            continue;
        }
        // **Colour wins**, for the reason the glTF branch gives: an image used
        // as both is one blob, and splitting it would put the same pixels in
        // the pack twice under two names. Merging with an OR is also what makes
        // the answer independent of the order the sources were read in.
        const auto [entry, inserted] = out.emplace(std::string(urn), map.colour);
        if (!inserted) {
            entry->second = entry->second || map.colour;
        }
    }
}

void collectMaterialMaps(const core::JsonValue& value, TextureUses& out)
{
    switch (value.type()) {
    case core::JsonType::Array:
        for (usize index = 0; index < value.size(); ++index) {
            collectMaterialMaps(value.at(index), out);
        }
        break;

    case core::JsonType::Object: {
        if (value["class"].asString() == "Material") {
            readMaterialMaps(value["properties"], out);
        }
        // **A placed stamp's edits count.** A scene records them under
        // `overrides`, keyed by the path inside the stamp, and a map set there
        // is as real as one set in `properties` (ADR 0051). What the path names
        // is somewhere in the stamp FILE, so its class is not written down here
        // -- which is why this reads the property names, and it is sound
        // because `Material` is the only class that declares them.
        if (const core::JsonValue overrides = value["overrides"]; overrides.type() == core::JsonType::Object) {
            for (usize index = 0; index < overrides.size(); ++index) {
                readMaterialMaps(overrides[overrides.keyAt(index)], out);
            }
        }
        for (usize index = 0; index < value.size(); ++index) {
            collectMaterialMaps(value[value.keyAt(index)], out);
        }
        break;
    }

    default:
        break;
    }
    // Recursion is bounded by the parser: a document nested deeper than
    // `core::JsonDocument` allows never parses in the first place.
}

// **Run after the sort and before anything is encoded.** The sort is the first
// of the four determinism rules and this must not disturb it; reading a second
// time here rather than remembering during the walk is what keeps it untouched.
[[nodiscard]] TextureUses collectTextureUses(std::span<const SourceFile> sources)
{
    TextureUses uses;
    for (const SourceFile& source : sources) {
        const std::string name = lowercase(source.path.filename().string());
        if (!endsWith(name, ".scene.json") && !endsWith(name, ".stamp.json") && !endsWith(name, ".chunk.json")) {
            continue;
        }

        std::vector<std::byte> bytes;
        if (!readWhole(source.path, bytes)) {
            continue;
        }
        core::JsonDocument document;
        // **A file that will not parse is left alone here.** The branch that
        // compiles it is the one that owns saying so -- a chunk source with a
        // diagnostic, a scene riding through as raw -- and refusing the build
        // twice for one file would report it twice.
        const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if (!document.parse(text, source.relative.generic_string())) {
            continue;
        }
        collectMaterialMaps(document.root(), uses);
    }
    return uses;
}

// Unclaimed is colour. That is still the honest default for a standalone image:
// nothing in the project says what it is for, it is what most loose textures
// are, and it is what this has always done.
[[nodiscard]] bool looseTextureIsColour(const TextureUses& uses, const std::string& urn)
{
    const auto found = uses.find(urn);
    return found == uses.end() || found->second;
}

} // namespace

const char* sourceKindName(SourceKind kind) noexcept
{
    switch (kind) {
    case SourceKind::Mesh:
        return "mesh";
    case SourceKind::Texture:
        return "texture";
    case SourceKind::Chunk:
        return "chunk";
    case SourceKind::Raw:
        return "raw";
    }
    return "unknown";
}

std::vector<SourceFile> collectSources(const std::filesystem::path& root, std::string& diagnostic)
{
    return collectSources(root, {}, diagnostic);
}

std::vector<SourceFile> collectSources(const std::filesystem::path& root, const std::filesystem::path& exclude,
                                       std::string& diagnostic)
{
    std::vector<SourceFile> sources;

    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) {
        diagnostic = "not a directory: " + root.string();
        return sources;
    }

    // The CONSTRUCTOR's error is checked as well as the increment's, and it has
    // to be: a failed construction leaves the iterator equal to `end`, so the
    // loop body never runs and a build that could not read its own content
    // directory would report success with nothing in it. Added while chasing an
    // empty pack whose cause turned out to be elsewhere -- which is exactly
    // when a silent path is worth closing, because it was indistinguishable
    // from the real bug for twenty minutes.
    std::filesystem::recursive_directory_iterator it(root, ec);
    if (ec) {
        diagnostic = "could not walk " + root.string() + ": " + ec.message();
        return {};
    }

    for (const std::filesystem::recursive_directory_iterator end; it != end; it.increment(ec)) {
        if (ec) {
            diagnostic = "could not walk " + root.string() + ": " + ec.message();
            return {};
        }
        if (!it->is_regular_file()) {
            continue;
        }
        // **Never its own cache.** A project is free to put one under its
        // content directory, and a build that compiled its own cache files
        // would produce a pack that grew every run -- which is what happened
        // the first time this was tested.
        if (!exclude.empty()) {
            // A prefix compare on the generic form, so the answer does not
            // depend on which separator the platform writes.
            const std::string candidate = it->path().generic_string();
            const std::string barrier = exclude.generic_string();
            if (candidate.size() > barrier.size() && candidate.compare(0, barrier.size(), barrier) == 0)
                continue;
        }

        SourceFile source;
        source.path = it->path();
        source.relative = std::filesystem::relative(it->path(), root, ec);
        source.kind = classify(it->path());
        sources.push_back(std::move(source));
    }

    // **Sorted before anything is processed**, and this is one of the four
    // things that make the build deterministic (M7 brief, Decision 1). A
    // directory iterator's order is the filesystem's, which differs between
    // machines and even between runs -- and processing order decides pack
    // insertion order, dedupe outcomes and every diagnostic's sequence.
    std::sort(sources.begin(), sources.end(), [](const SourceFile& a, const SourceFile& b) {
        return a.relative.generic_string() < b.relative.generic_string();
    });
    return sources;
}

// The whole tree, narrowed to what a caller asked for. Applied AFTER the walk
// and the sort rather than during them, so a one-source import sees exactly the
// source a full build would have seen at that position -- same `relative`, same
// `kind`, same URN.
[[nodiscard]] std::vector<SourceFile> narrowTo(std::vector<SourceFile> sources,
                                               std::span<const std::filesystem::path> only)
{
    if (only.empty())
        return sources;

    std::vector<SourceFile> kept;
    kept.reserve(only.size());
    for (SourceFile& source : sources) {
        std::error_code ec;
        for (const std::filesystem::path& wanted : only) {
            if (std::filesystem::equivalent(source.path, wanted, ec)) {
                kept.push_back(std::move(source));
                break;
            }
        }
    }
    return kept;
}

namespace {

// A cache entry is its own tiny binary format rather than JSON: it holds blobs,
// and base64 of a four-megabyte mesh to store it beside a manifest would cost
// more than recompiling it.
//
//   u32 magic, u32 blobCount, then per blob: u32 kind, u64 size, bytes
//   u32 entryCount, then per entry: the row's fixed fields and its urn
constexpr core::u32 kCacheMagic = 0x4C554143u; // "LUAC"

void putU32(std::vector<std::byte>& out, core::u32 value)
{
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
}

void putU64(std::vector<std::byte>& out, core::u64 value)
{
    for (int shift = 0; shift < 64; shift += 8)
        out.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
}

[[nodiscard]] bool takeU32(std::span<const std::byte> bytes, usize& at, core::u32& out)
{
    if (at + 4 > bytes.size())
        return false;
    out = 0;
    for (int index = 0; index < 4; ++index)
        out |= static_cast<core::u32>(bytes[at + static_cast<usize>(index)]) << (index * 8);
    at += 4;
    return true;
}

[[nodiscard]] bool takeU64(std::span<const std::byte> bytes, usize& at, core::u64& out)
{
    if (at + 8 > bytes.size())
        return false;
    out = 0;
    for (int index = 0; index < 8; ++index)
        out |= static_cast<core::u64>(bytes[at + static_cast<usize>(index)]) << (index * 8);
    at += 8;
    return true;
}

[[nodiscard]] std::vector<std::byte> encodeCache(const CachedSource& cached)
{
    std::vector<std::byte> out;
    putU32(out, kCacheMagic);
    putU32(out, static_cast<core::u32>(cached.blobs.size()));
    for (const auto& [kind, blob] : cached.blobs) {
        putU32(out, static_cast<core::u32>(kind));
        putU64(out, blob.size());
        out.insert(out.end(), blob.begin(), blob.end());
    }
    putU32(out, static_cast<core::u32>(cached.entries.size()));
    for (const ManifestEntry& entry : cached.entries) {
        putU64(out, entry.hash.high);
        putU64(out, entry.hash.low);
        putU32(out, static_cast<core::u32>(entry.kind));
        putU64(out, entry.originalBytes);
        putU64(out, entry.storedBytes);
        putU32(out, entry.lodCount);
        putU32(out, entry.vertexCount);
        putU32(out, entry.meshletCount);
        putU32(out, static_cast<core::u32>(entry.urn.size()));
        const auto* text = reinterpret_cast<const std::byte*>(entry.urn.data());
        out.insert(out.end(), text, text + entry.urn.size());
    }
    return out;
}

// **Every field is checked against the remaining length before it is read.** A
// cache file is written by this machine and is still a file on a disk: a
// truncated one from a killed build must be a miss, not a crash.
[[nodiscard]] bool decodeCache(std::span<const std::byte> bytes, CachedSource& out)
{
    usize at = 0;
    core::u32 magic = 0;
    if (!takeU32(bytes, at, magic) || magic != kCacheMagic)
        return false;

    core::u32 blobCount = 0;
    if (!takeU32(bytes, at, blobCount))
        return false;
    for (core::u32 index = 0; index < blobCount; ++index) {
        core::u32 kind = 0;
        core::u64 size = 0;
        if (!takeU32(bytes, at, kind) || !takeU64(bytes, at, size) || at + size > bytes.size())
            return false;
        out.blobs.emplace_back(static_cast<AssetKind>(kind),
                               std::vector<std::byte>(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                                                      bytes.begin() + static_cast<std::ptrdiff_t>(at + size)));
        at += size;
    }

    core::u32 entryCount = 0;
    if (!takeU32(bytes, at, entryCount))
        return false;
    for (core::u32 index = 0; index < entryCount; ++index) {
        ManifestEntry entry;
        core::u32 kind = 0;
        core::u64 original = 0;
        core::u64 stored = 0;
        core::u32 urnSize = 0;
        if (!takeU64(bytes, at, entry.hash.high) || !takeU64(bytes, at, entry.hash.low) || !takeU32(bytes, at, kind) ||
            !takeU64(bytes, at, original) || !takeU64(bytes, at, stored) || !takeU32(bytes, at, entry.lodCount) ||
            !takeU32(bytes, at, entry.vertexCount) || !takeU32(bytes, at, entry.meshletCount) ||
            !takeU32(bytes, at, urnSize) || at + urnSize > bytes.size()) {
            return false;
        }
        entry.kind = static_cast<AssetKind>(kind);
        entry.originalBytes = static_cast<usize>(original);
        entry.storedBytes = static_cast<usize>(stored);
        entry.urn.assign(reinterpret_cast<const char*>(bytes.data() + at), urnSize);
        at += urnSize;
        out.entries.push_back(std::move(entry));
    }
    return true;
}

} // namespace

CompileResult compile(const CompileOptions& options)
{
    CompileResult result;

    std::string diagnostic;
    // **Two lists, and the difference is the whole of `importOne`'s correctness.**
    // `allSources` is the tree, and the material sweep below reads it: a
    // texture's transfer function comes from the materials that NAME it, and
    // those live in scenes and stamps a one-source import is not compiling. An
    // import that swept only its own file would encode every normal map as
    // colour, which is exactly the defect the sRGB work closed.
    //
    // `sources` is what actually gets compiled. Narrowing after the walk and the
    // sort is also what keeps a one-source import seeing the same `relative`,
    // the same `kind` and therefore the same URN a full build would have given
    // it at that position.
    const std::vector<SourceFile> allSources = collectSources(options.inputRoot, options.cacheRoot, diagnostic);
    const std::vector<SourceFile> sources = narrowTo(allSources, options.only);
    if (!diagnostic.empty()) {
        result.diagnostic = diagnostic;
        return result;
    }

    // **What the project's materials say each loose image is for**, decided
    // before anything is encoded because the answer lives in another file: a
    // texture's transfer function is a property of what REFERENCES it.
    //
    // Skipped when there is no loose texture to decide about, which is every
    // content directory that is only meshes and chunks -- and the streamed
    // world the determinism gate builds is one of them.
    // Asked of the WHOLE tree, like the sweep it guards: a one-source import of
    // a texture must still find the materials that claim it.
    const bool anyLooseTexture = std::any_of(allSources.begin(), allSources.end(),
                                             [](const SourceFile& s) { return s.kind == SourceKind::Texture; });
    const TextureUses textureUses = anyLooseTexture ? collectTextureUses(allSources) : TextureUses{};

    asset::PackWriter pack;
    std::vector<ManifestEntry> manifest;
    asset::ChunkIndex chunkIndex;

    // --- Textures encoded in parallel, merged in source order (E9 step 11) ---
    //
    // **What is parallel here is one texture per worker, and what stays serial
    // is basis's own threading.** `texture.cpp` sets `m_multithreading = false`
    // and explains why: the encoder resolves ties across its internal threads by
    // completion order, so the BYTES would depend on how busy the machine was
    // and a content hash would stop being a name. That argument is about one
    // encode. Two encodes of two different images share nothing, so running
    // them side by side changes neither one's output.
    //
    // The determinism discipline is the one `jobs` documents for exactly this:
    // per-job buffers, a barrier, and a merge in a stable order -- here the
    // source order the sort above established, which decides pack insertion,
    // dedupe and every diagnostic's sequence. Nothing downstream can tell how
    // many workers ran.
    //
    // A texture is encoded here even when the cache is about to answer for it.
    // That is a real cost and it is the smaller one: deciding otherwise means
    // reading every source and computing every cache key before any encode
    // starts, which serialises the disk in front of the work this exists to
    // parallelise. A cold build is the case that hurts and it is the case this
    // helps.
    std::vector<std::optional<std::vector<std::byte>>> preEncoded(sources.size());
    {
        std::vector<usize> textureIndices;
        for (usize index = 0; index < sources.size(); ++index) {
            if (sources[index].kind == SourceKind::Texture)
                textureIndices.push_back(index);
        }

        if (textureIndices.size() > 1) {
            jobs::parallelFor("assetc.texture.encode", jobs::Domain::Tooling, 0, textureIndices.size(), 1,
                              [&](usize begin, usize end, core::u32 bucket) noexcept {
                                  // The bucket index is what a stable commit would merge by;
                                  // here every job writes into its own SOURCE slot, which is a
                                  // stronger ordering than the bucket and makes it unused.
                                  (void)bucket;
                                  // **Nothing may leave this body.** `jobs` requires a
                                  // `noexcept` callable, and on MSVC an exception escaping one
                                  // is `__fastfail` -- the process died with 0xC0000409 and no
                                  // output at all the first time this ran. A decode or an
                                  // encode that throws leaves its slot empty, and the serial
                                  // loop below then does the work and produces the diagnostic,
                                  // which is where a diagnostic belongs anyway.
                                  try {
                                      for (usize at = begin; at < end; ++at) {
                                          const usize index = textureIndices[at];
                                          std::vector<std::byte> raw;
                                          if (!readWhole(sources[index].path, raw))
                                              continue;
                                          asset::Image image;
                                          if (asset::decodeImage(raw, image))
                                              continue;
                                          const std::string urn = urnFor(sources[index].relative);
                                          std::vector<std::byte> encoded;
                                          if (encodeTexture(image, looseTextureIsColour(textureUses, urn), encoded))
                                              continue;
                                          // Written into this source's OWN slot and read after
                                          // the barrier, which is what makes the merge stable:
                                          // no two workers touch one element and nothing is
                                          // appended.
                                          preEncoded[index] = std::move(encoded);
                                      }
                                  } catch (...) {
                                      // Left for the serial loop, which will say what went
                                      // wrong with the source it went wrong on.
                                  }
                              });
        }
    }

    for (const SourceFile& source : sources) {
        std::vector<std::byte> bytes;
        if (!readWhole(source.path, bytes)) {
            result.diagnostic = "could not read " + source.path.string();
            return result;
        }

        const std::string urn = urnFor(source.relative);
        // Only a loose texture has a transfer function to decide; for everything
        // else this is a constant, so a mesh's key does not move.
        const bool textureIsColour = source.kind == SourceKind::Texture ? looseTextureIsColour(textureUses, urn) : true;

        // **Answered from the cache when the inputs are the ones it was written
        // for.** A chunk is not cached: it is cheap to build and its blob is
        // written beside the pack rather than into it, so there is nothing here
        // to hand back.
        const bool cacheable = options.cacheRoot.empty() ? false : source.kind != SourceKind::Chunk;
        const ContentHash key = cacheable ? cacheKey(bytes, urn, options, source.kind, textureIsColour) : ContentHash{};
        if (cacheable) {
            std::vector<std::byte> cachedBytes;
            CachedSource cached;
            if (platform::readFile(cachePathFor(options.cacheRoot, key), cachedBytes) &&
                decodeCache(cachedBytes, cached)) {
                for (const auto& [blobKind, blob] : cached.blobs)
                    (void)pack.addContent(blobKind, blob);
                for (const ManifestEntry& entry : cached.entries) {
                    switch (entry.kind) {
                    case AssetKind::Mesh:
                        result.meshCount += 1;
                        break;
                    case AssetKind::Texture:
                        result.textureCount += 1;
                        break;
                    default:
                        result.rawCount += 1;
                        break;
                    }
                    manifest.push_back(entry);
                }
                // A texture blob a mesh named is in `blobs` but not in
                // `entries`, and it is counted where it was produced -- so the
                // reported totals are the same on a hit as on a miss.
                result.textureCount += static_cast<u32>(cached.blobs.size() - cached.entries.size());
                result.stats.cacheHits += 1;
                continue;
            }
            result.stats.cacheMisses += 1;
        }

        // Everything this source adds to the pack, so a miss can be remembered.
        // Recorded as it goes rather than diffed afterwards: the pack
        // deduplicates, and a blob two sources share would otherwise be
        // attributed to neither.
        CachedSource produced;
        const auto remember = [&produced](AssetKind kind, std::span<const std::byte> blob) {
            produced.blobs.emplace_back(kind, std::vector<std::byte>(blob.begin(), blob.end()));
        };
        const auto rememberEntry = [&produced](const ManifestEntry& entry) { produced.entries.push_back(entry); };

        switch (source.kind) {
        case SourceKind::Mesh: {
            asset::Model model;
            asset::GltfImportOptions importOptions;
            const std::string extension = lowercase(source.path.extension().string());
            const auto imported = isExoticMesh(extension)
                                      ? importExotic(bytes, source.path.parent_path(), extension, model)
                                      : asset::importGltf(bytes, source.path.parent_path(), importOptions, model);
            if (imported) {
                result.diagnostic = source.relative.generic_string() + ": " + imported->message;
                return result;
            }

            // Every image the file carries becomes its own blob, named by what
            // it contains -- so a texture shared by forty meshes is one blob
            // rather than forty, and that is the whole point of addressing
            // content by hash.
            // **Which images are colour and which are numbers**, decided by
            // what REFERENCES them rather than by what they look like. Base
            // colour and emissive are colour; normal and metallic-roughness are
            // data, and running them through an sRGB curve bends every value by
            // a smooth amount that reads as bad lighting rather than as a broken
            // texture.
            //
            // Every image this compiler has ever produced was marked sRGB,
            // because the field was written `true` with a comment saying the
            // material would decide and nothing ever did.
            //
            // An image used as BOTH -- which an exporter packing roughness into
            // a colour map produces -- is encoded as colour. That is the wrong
            // answer for one of its two uses and the right one for the other,
            // and it is the choice that keeps a shared blob a shared blob;
            // splitting it would mean the same pixels twice in the pack under
            // two names.
            std::vector<bool> colourData(model.images.size(), false);
            for (const asset::MaterialDef& material : model.materials) {
                if (material.baseColor.present() && material.baseColor.image < colourData.size())
                    colourData[material.baseColor.image] = true;
                if (material.emissive.present() && material.emissive.image < colourData.size())
                    colourData[material.emissive.image] = true;
            }

            std::vector<asset::TextureSlot> slots;
            slots.reserve(model.images.size());
            for (usize imageIndex = 0; imageIndex < model.images.size(); ++imageIndex) {
                const bool srgb = colourData[imageIndex];
                std::vector<std::byte> encoded;
                const auto error = encodeTexture(model.images[imageIndex], srgb, encoded);
                if (error) {
                    result.diagnostic = source.relative.generic_string() + ": " + error->message;
                    return result;
                }
                asset::TextureSlot slot;
                slot.hash = pack.addContent(AssetKind::Texture, encoded);
                slot.srgb = srgb;
                slots.push_back(slot);
                result.textureCount += 1;
                result.stats.texturesEncoded += 1;
                remember(AssetKind::Texture, encoded);
            }

            asset::CompiledMesh compiled;
            if (const auto error = asset::compileMesh(model, slots, options.mesh, compiled)) {
                result.diagnostic = source.relative.generic_string() + ": " + error->message;
                return result;
            }

            const std::vector<std::byte> encoded = asset::encodeMesh(compiled);
            ManifestEntry entry;
            entry.urn = urn;
            entry.hash = pack.addContent(AssetKind::Mesh, encoded);
            entry.kind = AssetKind::Mesh;
            entry.originalBytes = bytes.size();
            entry.storedBytes = encoded.size();
            entry.lodCount = static_cast<u32>(compiled.lods.size());
            entry.vertexCount = static_cast<u32>(compiled.vertices.size());
            entry.meshletCount = static_cast<u32>(compiled.meshlets.meshlets.size());
            result.stats.meshesCompiled += 1;
            remember(AssetKind::Mesh, encoded);
            rememberEntry(entry);
            manifest.push_back(std::move(entry));
            result.meshCount += 1;

            // --- And one blob per primitive, under a fragment (E9 step 12) ---
            //
            // **A model arrives as one opaque `MeshPart` and that is the wrong
            // shape**: five materials become five submeshes of one part, so
            // there is nothing to select, nothing to give a material to and
            // nothing for a `Model.Scale` to scale. `splitByPrimitive` has cut
            // an `asset::Model` into named pieces since E9 opened and had no
            // caller outside its own tests.
            //
            // **Emitted BESIDE the whole model rather than instead of it**,
            // and it stays that way (ADR 0065). Every scene naming
            // `asset://models/horse.gltf` resolves to exactly the blob it
            // resolved to before, and one naming
            // `asset://models/horse.gltf#Body` resolves to the piece.
            //
            // This note used to say the cut-over would remove the whole-model
            // row. It does not, and the reason is below: one piece IS the whole
            // model, so a single-primitive file and every skinned file have the
            // whole-model row as their only name. What made the row look
            // redundant was the import placing a `Model` of named parts -- true
            // of new content, and silent about the scenes that already name the
            // model.
            //
            // The fragment IS the piece's name (decision 7 in the finish-line
            // ledger), which is also the instance name the editor gives it --
            // so a person reading a scene sees the same word the Explorer shows
            // them.
            const std::vector<asset::ModelPiece> pieces = asset::splitByPrimitive(model);
            // One piece is the whole model, which every static single-primitive
            // file and every skinned file produces. Emitting a fragment for it
            // would be a second name for one blob.
            if (pieces.size() > 1) {
                for (const asset::ModelPiece& piece : pieces) {
                    asset::CompiledMesh compiledPiece;
                    if (const auto error = asset::compileMesh(piece.model, slots, options.mesh, compiledPiece)) {
                        result.diagnostic = source.relative.generic_string() + "#" + piece.name + ": " + error->message;
                        return result;
                    }

                    const std::vector<std::byte> pieceBytes = asset::encodeMesh(compiledPiece);
                    ManifestEntry pieceEntry;
                    pieceEntry.urn = urn + "#" + piece.name;
                    pieceEntry.hash = pack.addContent(AssetKind::Mesh, pieceBytes);
                    pieceEntry.kind = AssetKind::Mesh;
                    // The SOURCE file's size, because that is what this piece
                    // came out of and there is no smaller original to name.
                    pieceEntry.originalBytes = bytes.size();
                    pieceEntry.storedBytes = pieceBytes.size();
                    pieceEntry.lodCount = static_cast<u32>(compiledPiece.lods.size());
                    pieceEntry.vertexCount = static_cast<u32>(compiledPiece.vertices.size());
                    pieceEntry.meshletCount = static_cast<u32>(compiledPiece.meshlets.meshlets.size());
                    result.stats.meshesCompiled += 1;
                    remember(AssetKind::Mesh, pieceBytes);
                    rememberEntry(pieceEntry);
                    manifest.push_back(std::move(pieceEntry));
                    result.meshCount += 1;
                }
            }
            break;
        }

        case SourceKind::Texture: {
            // **Taken from the parallel pass when it produced one.** The bytes
            // are identical either way -- the same image, the same transfer
            // function, the same single-threaded encoder -- so this is a lookup
            // and not a second answer. When the pass declined (one texture in
            // the build, a read that failed, an image that would not decode) the
            // serial path below produces the same result and the diagnostic.
            const usize sourceIndex = static_cast<usize>(&source - sources.data());
            std::vector<std::byte> encoded;
            if (preEncoded[sourceIndex].has_value()) {
                encoded = std::move(*preEncoded[sourceIndex]);
                preEncoded[sourceIndex].reset();
            }

            asset::Image image;
            if (encoded.empty()) {
                if (const auto error = asset::decodeImage(bytes, image)) {
                    result.diagnostic = source.relative.generic_string() + ": " + error->message;
                    return result;
                }
            }
            // **What a `Material` in this project says this image is for.**
            // `ColorMap` and `EmissiveMap` are colour; `NormalMap` and
            // `MetallicRoughnessMap` are numbers, and bending those through the
            // sRGB curve makes every value wrong by a smooth amount that reads
            // as bad lighting rather than as a broken texture -- the same defect
            // the glTF branch above was fixed for, arriving by the other door.
            //
            // An image NO material claims stays colour, which is the honest
            // default for a standalone image and what this has always done.
            if (encoded.empty()) {
                if (const auto error = encodeTexture(image, textureIsColour, encoded)) {
                    result.diagnostic = source.relative.generic_string() + ": " + error->message;
                    return result;
                }
            }

            ManifestEntry entry;
            entry.urn = urn;
            entry.hash = pack.addContent(AssetKind::Texture, encoded);
            entry.kind = AssetKind::Texture;
            entry.originalBytes = bytes.size();
            entry.storedBytes = encoded.size();
            result.stats.texturesEncoded += 1;
            remember(AssetKind::Texture, encoded);
            rememberEntry(entry);
            manifest.push_back(std::move(entry));
            result.textureCount += 1;
            break;
        }

        case SourceKind::Chunk: {
            asset::Chunk chunk;
            f32 chunkSize = asset::DefaultChunkSize;
            const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            if (const auto error = readChunkSource(text, chunk, chunkSize)) {
                result.diagnostic = source.relative.generic_string() + ": " + error->message;
                return result;
            }
            // Every cell in one world agrees about the grid, or two chunks
            // describe overlapping regions and the manager scores both.
            if (result.chunkCount > 0 && chunkIndex.chunkSize != chunkSize) {
                result.diagnostic = source.relative.generic_string() + ": disagrees about the chunk size";
                return result;
            }
            chunkIndex.chunkSize = chunkSize;

            std::filesystem::path relative = source.relative;
            // `world/cell_0_0.chunk.json` becomes `world/cell_0_0.lchunk`: the
            // output keeps the author's own directory layout, so a person
            // looking for a chunk finds it where they put it.
            relative.replace_extension();
            relative.replace_extension(".lchunk");

            ChunkOutput output;
            output.relativePath = relative.generic_string();
            output.bytes = asset::encodeChunk(chunk);

            asset::ChunkIndexEntry entry;
            entry.id = chunk.id;
            entry.bounds = chunk.bounds;
            entry.urn = urnFor(relative);
            entry.instanceCount = static_cast<u32>(chunk.instances.size());
            entry.bytes = static_cast<u32>(output.bytes.size());
            chunkIndex.chunks.push_back(std::move(entry));

            result.chunks.push_back(std::move(output));
            result.chunkCount += 1;
            break;
        }

        case SourceKind::Raw: {
            ManifestEntry entry;
            entry.urn = urn;
            entry.hash = pack.addContent(AssetKind::Raw, bytes);
            entry.kind = AssetKind::Raw;
            entry.originalBytes = bytes.size();
            entry.storedBytes = bytes.size();
            remember(AssetKind::Raw, bytes);
            rememberEntry(entry);
            manifest.push_back(std::move(entry));
            result.rawCount += 1;
            break;
        }
        }

        // **The cache is written LAST**, after the source has compiled without
        // a diagnostic -- so a build that failed halfway leaves nothing behind
        // that a later run would trust. A write that fails is a warning and the
        // build proceeds: a cache that cannot be written is slow, not wrong.
        if (cacheable && !produced.blobs.empty()) {
            const std::filesystem::path path = cachePathFor(options.cacheRoot, key);
            std::string ignored;
            if (platform::createDirectories(path.parent_path()))
                (void)writeFile(path, encodeCache(produced), ignored);
        }
    }

    // The manifest is sorted by URN, which is the second determinism rule: the
    // file is a function of what is in it and not of the order it was built.
    std::sort(manifest.begin(), manifest.end(),
              [](const ManifestEntry& a, const ManifestEntry& b) { return a.urn < b.urn; });

    // Sorted by id, which is what makes a lookup a binary search and the
    // materialisation order a property of the world rather than of the
    // filesystem.
    std::sort(chunkIndex.chunks.begin(), chunkIndex.chunks.end(),
              [](const asset::ChunkIndexEntry& a, const asset::ChunkIndexEntry& b) { return a.id < b.id; });
    std::sort(result.chunks.begin(), result.chunks.end(),
              [](const ChunkOutput& a, const ChunkOutput& b) { return a.relativePath < b.relativePath; });

    // Handed back beside the pack, for a caller that writes an object store
    // instead of one: the store is one file per blob and cannot be produced from
    // a built pack without unpacking what was just packed.
    for (const asset::PackWriter::BlobView& blob : pack.blobs())
        result.blobs.emplace_back(blob.hash, std::vector<std::byte>(blob.bytes.begin(), blob.bytes.end()));

    result.pack = pack.build();
    result.manifest = writeManifest(manifest);
    result.chunkIndex = result.chunkCount > 0 ? asset::writeChunkIndex(chunkIndex) : std::string{};
    result.entries = std::move(manifest);
    result.ok = true;
    return result;
}

std::string writeManifest(std::span<const ManifestEntry> entries)
{
    core::JsonWriter json;
    json.beginObject();
    json.field("format", "luaug-content-manifest");
    json.field("version", static_cast<core::u64>(1));

    json.key("assets");
    json.beginArray();
    for (const ManifestEntry& entry : entries) {
        json.beginObject();
        json.field("urn", entry.urn);
        json.field("hash", entry.hash.toHex());
        json.field("kind", asset::assetKindName(entry.kind));
        json.field("bytes", static_cast<core::u64>(entry.storedBytes));
        if (entry.kind == AssetKind::Mesh) {
            json.field("lods", static_cast<core::u64>(entry.lodCount));
            json.field("vertices", static_cast<core::u64>(entry.vertexCount));
            json.field("meshlets", static_cast<core::u64>(entry.meshletCount));
        }
        json.endObject();
    }
    json.endArray();
    json.endObject();

    std::string text = json.text();
    text.push_back('\n');
    return text;
}

bool writeFile(const std::filesystem::path& path, std::span<const std::byte> bytes, std::string& diagnostic)
{
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        diagnostic = "could not open for writing: " + path.string();
        return false;
    }
    if (!bytes.empty()) {
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    out.close();
    if (!out) {
        diagnostic = "could not write: " + path.string();
        return false;
    }
    return true;
}

CompileResult importOne(const CompileOptions& options, const std::filesystem::path& sourcePath)
{
    CompileOptions one = options;
    one.only.clear();
    one.only.push_back(sourcePath);
    return compile(one);
}

// The manifest's own spelling, read back. `content.cpp` has the same table and
// keeps it file-local, which is right for it and leaves this side needing one:
// two callers of one JSON shape is exactly where a second spelling drifts.
[[nodiscard]] asset::AssetKind assetKindFromNameLocal(std::string_view name) noexcept
{
    for (const asset::AssetKind kind : {asset::AssetKind::Mesh, asset::AssetKind::Texture, asset::AssetKind::Material,
                                        asset::AssetKind::Prefab, asset::AssetKind::Chunk, asset::AssetKind::Raw}) {
        if (name == asset::assetKindName(kind))
            return kind;
    }
    return asset::AssetKind::Unknown;
}

std::optional<core::EngineError> writeObjectStore(const CompileResult& result, const std::filesystem::path& objects,
                                                  const std::filesystem::path& index)
{
    if (!platform::createDirectories(objects)) {
        const core::I18nArg args[] = {{"path", objects.string()}};
        return core::makeError(LUAUG_TR("asset.store.err.write_failed"), args);
    }

    // **Blobs first, index last.** A store whose index names a blob that is not
    // there fails at first use; one whose blobs are there and unnamed merely
    // wastes disk until the next import overwrites the index anyway.
    for (const auto& [hash, bytes] : result.blobs) {
        const std::filesystem::path path = asset::ContentMounts::objectPath(objects, hash);
        // **Skipped when it is already there, and that is content addressing
        // doing its job** rather than an optimisation: the name IS the hash, so
        // a file that exists under it holds these bytes and rewriting it would
        // write the same ones.
        if (platform::fileExists(path))
            continue;
        if (!platform::createDirectories(path.parent_path())) {
            const core::I18nArg args[] = {{"path", path.parent_path().string()}};
            return core::makeError(LUAUG_TR("asset.store.err.write_failed"), args);
        }
        std::string diagnostic;
        if (!writeFile(path, bytes, diagnostic)) {
            const core::I18nArg args[] = {{"path", path.string()}};
            return core::makeError(LUAUG_TR("asset.store.err.write_failed"), args);
        }
    }

    // **Merged, not replaced.** An import adds to a store that already has forty
    // other models in it, and a re-import of one source must replace its own
    // rows rather than append a second set -- a URN names one blob, and two rows
    // for it would make which one wins depend on read order.
    //
    // Keyed by URN in a sorted map, so the file this writes is a pure function
    // of what is in the store and not of the order imports happened in.
    std::map<std::string, std::pair<core::ContentHash, AssetKind>> rows;

    std::string existing;
    if (platform::readTextFile(index, existing)) {
        core::JsonDocument document;
        if (document.parse(existing, index.string()).ok) {
            // **`assets`, because that is what `readManifest` reads.** The writer
            // spoke its own dialect first and the round-trip case caught it:
            // the store mounted without complaint and resolved nothing.
            const core::JsonValue entries = document.root()["assets"];
            for (usize at = 0; at < entries.size(); ++at) {
                const core::JsonValue entry = entries.at(at);
                const std::string_view urn = entry["urn"].asString();
                core::ContentHash hash;
                if (urn.empty() || !core::parseHex(entry["hash"].asString(), hash))
                    continue;
                rows[std::string(urn)] = {hash, assetKindFromNameLocal(entry["kind"].asString())};
            }
        }
        // A manifest that will not parse is REPLACED rather than refused. It is
        // a cache of what is already on disk under content-addressed names, so
        // the worst a lost one costs is a re-import; refusing would leave a
        // project unable to import anything until somebody deleted a file by
        // hand.
    }

    for (const ManifestEntry& entry : result.entries)
        rows[entry.urn] = {entry.hash, entry.kind};

    core::JsonWriter writer;
    writer.beginObject();
    writer.field("format", "luaug-content-manifest");
    writer.field("version", static_cast<core::i64>(1));
    writer.key("assets");
    writer.beginArray();
    for (const auto& [urn, row] : rows) {
        writer.beginObject();
        writer.field("urn", urn);
        writer.field("hash", row.first.toHex());
        writer.field("kind", asset::assetKindName(row.second));
        writer.endObject();
    }
    writer.endArray();
    writer.endObject();

    if (!platform::createDirectories(index.parent_path())) {
        const core::I18nArg args[] = {{"path", index.parent_path().string()}};
        return core::makeError(LUAUG_TR("asset.store.err.write_failed"), args);
    }
    const std::string& text = writer.text();
    std::string diagnostic;
    if (!writeFile(index, std::as_bytes(std::span<const char>(text.data(), text.size())), diagnostic)) {
        const core::I18nArg args[] = {{"path", index.string()}};
        return core::makeError(LUAUG_TR("asset.store.err.write_failed"), args);
    }
    return std::nullopt;
}

} // namespace luaug::assetc
