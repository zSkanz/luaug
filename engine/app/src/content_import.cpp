#include "luaug/app/content_import.h"

#include "luaug/app/content_tree.h"
#include "luaug/asset/gltf.h"
#include "luaug/asset/material.h"
#include "luaug/asset/model.h"
#include "luaug/core/json.h"
#include "luaug/core/log.h"
#include "luaug/platform/file.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#if LUAUG_DEBUG_UI
#include "luaug/assetc/compiler.h"
#endif

namespace luaug::app {

std::filesystem::path importObjectsDir(const std::filesystem::path& projectRoot)
{
    return projectRoot / ".luaug" / "import" / "objects";
}

std::filesystem::path importIndexPath(const std::filesystem::path& projectRoot)
{
    return projectRoot / ".luaug" / "import" / "index.json";
}

#if LUAUG_DEBUG_UI

ContentImportReport compileImported(const std::filesystem::path& projectRoot, const std::filesystem::path& contentRoot,
                                    std::span<const std::string> names)
{
    ContentImportReport report;
    if (projectRoot.empty() || contentRoot.empty() || names.empty())
        return report;

    assetc::CompileOptions options;
    options.inputRoot = contentRoot;
    // **The project's own cache**, beside the store rather than inside
    // `content/`: a build that compiled its own cache would produce a store that
    // grew every import, which is the mistake `collectSources`'s exclusion
    // already exists to prevent on the command-line side.
    options.cacheRoot = projectRoot / ".luaug" / "import" / "cache";

    for (const std::string& name : names) {
        // Only what the compiler has something to do with. A script or a scene
        // is content the project reads directly, and skipping it is not a
        // failure -- there is nothing to compile.
        const ContentKind kind = contentKindOf(name);
        if (kind != ContentKind::Mesh && kind != ContentKind::Texture)
            continue;

        const std::filesystem::path source = contentRoot / std::filesystem::path(name);
        const assetc::CompileResult result = assetc::importOne(options, source);
        if (!result.ok) {
            report.failed.push_back(name);
            if (report.diagnostic.empty())
                report.diagnostic = result.diagnostic;
            continue;
        }

        // **Written per source rather than once at the end**, so an import of
        // forty files that fails on the thirty-first leaves thirty compiled
        // rather than nothing. The store merges, which is what makes that safe.
        if (const auto error =
                assetc::writeObjectStore(result, importObjectsDir(projectRoot), importIndexPath(projectRoot))) {
            report.failed.push_back(name);
            if (report.diagnostic.empty())
                report.diagnostic = error->message;
            continue;
        }

        report.compiled.push_back(name);
        report.meshes += result.meshCount;
        report.textures += result.textureCount;
        report.cacheHits += result.stats.cacheHits;
        report.cacheMisses += result.stats.cacheMisses;

        // The fragments this source produced, read off the rows it produced --
        // so the editor names an instance with the same word the store is keyed
        // by, rather than deriving it a second time and hoping.
        std::vector<std::string> fragments;
        const std::string urn = "asset://" + std::filesystem::path(name).generic_string();
        for (const assetc::ManifestEntry& entry : result.entries) {
            const std::size_t hash = entry.urn.find('#');
            if (hash == std::string::npos || entry.urn.compare(0, hash, urn) != 0)
                continue;
            fragments.push_back(entry.urn.substr(hash + 1));
        }
        if (!fragments.empty())
            report.pieces.emplace_back(name, std::move(fragments));
    }

    return report;
}

#else

ContentImportReport compileImported(const std::filesystem::path&, const std::filesystem::path&,
                                    std::span<const std::string>)
{
    // A build with no editor imports nothing, because it has no browser to
    // import from. The symbol exists so the call site needs no `#ifdef` of its
    // own -- the same shape `DebugOverlay` uses to keep the frame loop free of
    // them.
    return {};
}

#endif

ContentImportReport openProjectContent(const std::filesystem::path& projectRoot,
                                       const std::filesystem::path& contentRoot, asset::ContentMounts& mounts)
{
    ContentImportReport report;
    std::error_code ec;

    // **A source tree is optional and the store is not.** They are separate
    // conditions rather than one, because the compiled form is the authoritative
    // one now: a project whose `content/` has been stripped -- shipped compiled,
    // or simply deleted -- still has everything it needs to run, and refusing to
    // mount its store because the sources are gone would be the tail wagging the
    // dog.
    if (!contentRoot.empty() && std::filesystem::is_directory(contentRoot, ec)) {
        mounts.mountDirectory(contentRoot);

        // **The tree is walked here rather than taken from the editor's**, so a
        // headless run needs no browser. A `ContentTree` is a directory scan and
        // nothing else -- no watcher, no window -- and the editor's own copy
        // stays its own, because a tool's view of a folder is a tool's business.
        ContentTree tree;
        (void)tree.open(contentRoot);
        std::vector<std::string> pending = tree.filesOfKind(ContentKind::Mesh);
        for (std::string& texture : tree.filesOfKind(ContentKind::Texture))
            pending.push_back(std::move(texture));
        if (!pending.empty())
            report = compileImported(projectRoot, contentRoot, pending);
    }

    // **Above the source directory**, which is the point: `resolve` walks mounts
    // in reverse, so the compiled form of a texture -- BC7 with mips -- outranks
    // the raw PNG beside it, and the compiled form of a mesh outranks the JSON
    // that no longer has a reader.
    //
    // A store that is not there is not an error. A project whose content the
    // compiler had nothing to do with has none, and so does one in a build with
    // no compiler.
    const std::filesystem::path objects = importObjectsDir(projectRoot);
    const std::filesystem::path index = importIndexPath(projectRoot);
    if (std::filesystem::is_regular_file(index, ec))
        (void)mounts.mountObjects(objects, index);

    return report;
}

// --- The materials of an imported model (ADR 0090) ----------------------------

namespace {

// The JSON a glTF carries: the whole file for a `.gltf`, the first chunk of a
// `.glb`.
[[nodiscard]] std::string gltfJson(std::span<const std::byte> bytes)
{
    const auto word = [&](core::usize at) -> core::u32 {
        core::u32 value = 0;
        for (core::usize index = 0; index < 4; ++index)
            value |= static_cast<core::u32>(bytes[at + index]) << (8 * index);
        return value;
    };
    // "glTF", then a version and a length; the JSON chunk follows.
    if (bytes.size() >= 20 && static_cast<char>(bytes[0]) == 'g' && static_cast<char>(bytes[1]) == 'l' &&
        static_cast<char>(bytes[2]) == 'T' && static_cast<char>(bytes[3]) == 'F') {
        const core::u32 length = word(12);
        if (20 + static_cast<core::usize>(length) > bytes.size())
            return {};
        return std::string(reinterpret_cast<const char*>(bytes.data() + 20), length);
    }
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

// A URI relative to the model, as a content path: `%20` decoded, `./` and
// `a/../` folded away. Empty for anything that is not a plain relative file.
[[nodiscard]] std::string contentPathOf(const std::string& folder, std::string_view uri)
{
    if (uri.empty() || uri.starts_with("data:") || uri.find("://") != std::string_view::npos)
        return {};
    std::string decoded;
    for (core::usize index = 0; index < uri.size(); ++index) {
        if (uri[index] == '%' && index + 2 < uri.size()) {
            const std::string hex(uri.substr(index + 1, 2));
            decoded.push_back(static_cast<char>(std::strtol(hex.c_str(), nullptr, 16)));
            index += 2;
            continue;
        }
        decoded.push_back(uri[index] == '\\' ? '/' : uri[index]);
    }
    const std::filesystem::path joined =
        (folder.empty() ? std::filesystem::path(decoded) : std::filesystem::path(folder) / decoded).lexically_normal();
    const std::string text = joined.generic_string();
    return text.starts_with("..") ? std::string{} : text;
}

// A file name from a material's name.
[[nodiscard]] std::string materialStem(std::string_view name, core::usize index)
{
    std::string out;
    for (const char c : name) {
        const auto byte = static_cast<unsigned char>(c);
        out.push_back(std::isalnum(byte) != 0 || c == '-' || c == '_' ? static_cast<char>(std::tolower(byte)) : '-');
    }
    return out.empty() ? "material-" + std::to_string(index) : out;
}

} // namespace

std::string ModelMaterials::whole() const
{
    if (bySubmesh.empty())
        return {};
    for (const std::string& path : bySubmesh) {
        if (path != bySubmesh.front())
            return {};
    }
    return bySubmesh.front();
}

std::string ModelMaterials::ofPiece(std::string_view piece) const
{
    for (core::usize index = 0; index < submeshNames.size() && index < bySubmesh.size(); ++index) {
        if (submeshNames[index] == piece)
            return bySubmesh[index];
    }
    return {};
}

ModelMaterials writeModelMaterials(const std::filesystem::path& contentRoot, const std::string& modelRelative)
{
    ModelMaterials out;
    const std::filesystem::path source = contentRoot / std::filesystem::path(modelRelative);
    std::vector<std::byte> bytes;
    if (!platform::readFile(source, bytes))
        return out;

    // Which source material each submesh draws with -- the importer's own
    // answer, so a piece and its material are one reading of the file.
    asset::Model model;
    if (asset::importGltf(bytes, source.parent_path(), asset::GltfImportOptions{}, model).has_value() ||
        model.materialSources.size() != model.materials.size())
        return out;

    core::JsonDocument document;
    if (!document.parse(gltfJson(bytes), modelRelative))
        return out;
    const core::JsonValue gltf = document.root();
    const core::JsonValue materials = gltf["materials"];
    const core::JsonValue textures = gltf["textures"];
    const core::JsonValue images = gltf["images"];

    const std::string folder = std::filesystem::path(modelRelative).parent_path().generic_string();
    const std::string stem = std::filesystem::path(modelRelative).stem().string();

    // The content path of the image a texture reference names, or empty.
    const auto mapOf = [&](const core::JsonValue& info, bool& unresolved) -> std::string {
        if (info.type() != core::JsonType::Object)
            return {};
        const core::i64 texture = info["index"].asInteger(-1);
        const core::i64 imageIndex = texture >= 0 && static_cast<core::usize>(texture) < textures.size()
                                         ? textures.at(static_cast<core::usize>(texture))["source"].asInteger(-1)
                                         : -1;
        const std::string path =
            imageIndex >= 0 && static_cast<core::usize>(imageIndex) < images.size()
                ? contentPathOf(folder, images.at(static_cast<core::usize>(imageIndex))["uri"].asString())
                : std::string{};
        if (path.empty())
            unresolved = true;
        return path.empty() ? std::string{} : std::string(asset::AssetScheme) + path;
    };

    std::vector<std::string> bySource(materials.size());
    std::vector<std::string> usedStems;
    for (core::usize index = 0; index < materials.size(); ++index) {
        const core::JsonValue material = materials.at(index);
        const core::JsonValue pbr = material["pbrMetallicRoughness"];
        asset::MaterialAsset asset;
        asset.written = asset::AllMaterialFields;
        asset::MaterialProperties& p = asset.properties;
        // glTF's own defaults where the file says nothing: metal and rough.
        p.metalness = static_cast<core::f32>(pbr["metallicFactor"].asNumber(1.0));
        p.roughness = static_cast<core::f32>(pbr["roughnessFactor"].asNumber(1.0));
        const std::string_view alphaMode = material["alphaMode"].asString("OPAQUE");
        p.alphaMode = alphaMode == "MASK" ? 1 : alphaMode == "BLEND" ? 2 : 0;
        p.alphaCutoff = static_cast<core::f32>(material["alphaCutoff"].asNumber(0.5));
        p.doubleSided = material["doubleSided"].asBool(false);
        if (const core::JsonValue factor = pbr["baseColorFactor"]; factor.size() >= 3) {
            p.color = core::Color3{static_cast<core::f32>(factor.at(0).asNumber(1.0)),
                                   static_cast<core::f32>(factor.at(1).asNumber(1.0)),
                                   static_cast<core::f32>(factor.at(2).asNumber(1.0))};
            // Alpha is see-through only where the file blends it.
            if (factor.size() >= 4 && p.alphaMode == 2)
                p.transparency = 1.0f - static_cast<core::f32>(factor.at(3).asNumber(1.0));
        }
        if (const core::JsonValue factor = material["emissiveFactor"]; factor.size() >= 3) {
            p.emissive = core::Color3{static_cast<core::f32>(factor.at(0).asNumber(0.0)),
                                      static_cast<core::f32>(factor.at(1).asNumber(0.0)),
                                      static_cast<core::f32>(factor.at(2).asNumber(0.0))};
        }
        bool unresolved = false;
        p.colorMap = mapOf(pbr["baseColorTexture"], unresolved);
        p.metallicRoughnessMap = mapOf(pbr["metallicRoughnessTexture"], unresolved);
        p.normalMap = mapOf(material["normalTexture"], unresolved);
        p.normalScale = static_cast<core::f32>(material["normalTexture"]["scale"].asNumber(1.0));
        p.emissiveMap = mapOf(material["emissiveTexture"], unresolved);
        if (unresolved)
            continue;

        std::string name = materialStem(material["name"].asString(), index);
        for (int suffix = 2; std::find(usedStems.begin(), usedStems.end(), name) != usedStems.end(); ++suffix)
            name = materialStem(material["name"].asString(), index) + "-" + std::to_string(suffix);
        usedStems.push_back(name);

        const std::string relative = "materials/" + stem + "/" + name + std::string(asset::MaterialSuffix);
        const std::filesystem::path absolute = contentRoot / std::filesystem::path(relative);
        std::error_code ec;
        if (!std::filesystem::exists(absolute, ec)) {
            if (!platform::createDirectories(absolute.parent_path()) ||
                !platform::writeTextFile(absolute, asset::writeMaterialAsset(asset)))
                continue;
            out.written.push_back(relative);
        }
        bySource[index] = relative;
    }

    out.submeshNames = model.submeshNames;
    for (const asset::Submesh& submesh : model.mesh.submeshes) {
        const core::u32 origin = submesh.material < model.materialSources.size()
                                     ? model.materialSources[submesh.material]
                                     : asset::Model::NoSourceMaterial;
        out.bySubmesh.push_back(origin < bySource.size() ? bySource[origin] : std::string{});
    }
    return out;
}

} // namespace luaug::app
