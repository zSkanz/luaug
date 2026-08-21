#include "luaug/assetc/compiler.h"

#include "luaug/asset/chunk.h"
#include "luaug/asset/gltf.h"
#include "luaug/asset/image.h"
#include "luaug/asset/mesh_format.h"
#include "luaug/assetc/exotic.h"
#include "luaug/core/json.h"
#include "luaug/core/json_writer.h"
#include "luaug/platform/file.h"

#include <algorithm>
#include <cctype>
#include <fstream>
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

[[nodiscard]] SourceKind classify(const std::filesystem::path& path)
{
    const std::string name = lowercase(path.filename().string());
    const std::string extension = lowercase(path.extension().string());

    // Matched on the compound suffix rather than on `.json`, so an ordinary
    // JSON file a project keeps in its content directory rides through as raw
    // rather than being refused for not being a chunk.
    if (name.size() > 11 && name.compare(name.size() - 11, 11, ".chunk.json") == 0) {
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

CompileResult compile(const CompileOptions& options)
{
    CompileResult result;

    std::string diagnostic;
    const std::vector<SourceFile> sources = collectSources(options.inputRoot, diagnostic);
    if (!diagnostic.empty()) {
        result.diagnostic = diagnostic;
        return result;
    }

    asset::PackWriter pack;
    std::vector<ManifestEntry> manifest;
    asset::ChunkIndex chunkIndex;

    for (const SourceFile& source : sources) {
        std::vector<std::byte> bytes;
        if (!readWhole(source.path, bytes)) {
            result.diagnostic = "could not read " + source.path.string();
            return result;
        }

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
            std::vector<asset::TextureSlot> slots;
            slots.reserve(model.images.size());
            for (const asset::Image& image : model.images) {
                std::vector<std::byte> encoded;
                const auto error = encodeTexture(image, encoded);
                if (error) {
                    result.diagnostic = source.relative.generic_string() + ": " + error->message;
                    return result;
                }
                asset::TextureSlot slot;
                slot.hash = pack.addContent(AssetKind::Texture, encoded);
                // Colour data until something says otherwise. The material
                // decides per SLOT; this is the file's default and the mesh
                // record is what carries the truth.
                slot.srgb = true;
                slots.push_back(slot);
                result.textureCount += 1;
            }

            asset::CompiledMesh compiled;
            if (const auto error = asset::compileMesh(model, slots, options.mesh, compiled)) {
                result.diagnostic = source.relative.generic_string() + ": " + error->message;
                return result;
            }

            const std::vector<std::byte> encoded = asset::encodeMesh(compiled);
            ManifestEntry entry;
            entry.urn = urnFor(source.relative);
            entry.hash = pack.addContent(AssetKind::Mesh, encoded);
            entry.kind = AssetKind::Mesh;
            entry.originalBytes = bytes.size();
            entry.storedBytes = encoded.size();
            entry.lodCount = static_cast<u32>(compiled.lods.size());
            entry.vertexCount = static_cast<u32>(compiled.vertices.size());
            entry.meshletCount = static_cast<u32>(compiled.meshlets.meshlets.size());
            manifest.push_back(std::move(entry));
            result.meshCount += 1;
            break;
        }

        case SourceKind::Texture: {
            asset::Image image;
            if (const auto error = asset::decodeImage(bytes, image)) {
                result.diagnostic = source.relative.generic_string() + ": " + error->message;
                return result;
            }
            std::vector<std::byte> encoded;
            if (const auto error = encodeTexture(image, encoded)) {
                result.diagnostic = source.relative.generic_string() + ": " + error->message;
                return result;
            }

            ManifestEntry entry;
            entry.urn = urnFor(source.relative);
            entry.hash = pack.addContent(AssetKind::Texture, encoded);
            entry.kind = AssetKind::Texture;
            entry.originalBytes = bytes.size();
            entry.storedBytes = encoded.size();
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
            entry.urn = urnFor(source.relative);
            entry.hash = pack.addContent(AssetKind::Raw, bytes);
            entry.kind = AssetKind::Raw;
            entry.originalBytes = bytes.size();
            entry.storedBytes = bytes.size();
            manifest.push_back(std::move(entry));
            result.rawCount += 1;
            break;
        }
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

} // namespace luaug::assetc
