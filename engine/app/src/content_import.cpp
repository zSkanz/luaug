#include "luaug/app/content_import.h"

#include "luaug/app/content_tree.h"
#include "luaug/core/log.h"

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

} // namespace luaug::app
