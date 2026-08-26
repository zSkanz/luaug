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

} // namespace luaug::app
