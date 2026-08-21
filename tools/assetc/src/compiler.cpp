#include "luaug/assetc/compiler.h"

#include "luaug/asset/gltf.h"
#include "luaug/asset/image.h"
#include "luaug/asset/mesh_format.h"
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

    if (extension == ".gltf" || extension == ".glb") {
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

} // namespace

const char* sourceKindName(SourceKind kind) noexcept
{
    switch (kind) {
    case SourceKind::Mesh:
        return "mesh";
    case SourceKind::Texture:
        return "texture";
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

    for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec)) {
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
            if (const auto error = asset::importGltf(bytes, source.path.parent_path(), importOptions, model)) {
                result.diagnostic = source.relative.generic_string() + ": " + error->message;
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

    result.pack = pack.build();
    result.manifest = writeManifest(manifest);
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
