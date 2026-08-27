// The cut-over, end to end (E9 step 14).
//
// **The claim under test is that a loose `.gltf` no longer feeds the runtime**,
// and the sharp way to assert it is from the other side: compile a project, then
// DELETE the source files, and require that everything still draws. A test that
// only checked "the mesh loaded" would pass on a build where the loose reader
// was still there, because the loose reader loaded meshes perfectly well -- that
// was the problem with it.
//
// This is also where `import_matches_build` lives, the last of E9's declared
// verification. The editor's import and a command-line build go through one
// function by construction (`assetc::importOne`), which is a structural argument
// rather than an asserted fact -- so it is asserted here, per URN and per blob.
#include "luaug/app/content_import.h"
#include "luaug/asset/content.h"
#include "luaug/core/i18n.h"
#include "luaug/platform/file.h"
#include "luaug/render/mesh_cache.h"
#include "luaug/render/mesh_loader.h"
#include "luaug/rhi/backends.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/components.h"
#include "luaug/scene/enum_registry.h"
#include "luaug/scene/world.h"

#if LUAUG_DEBUG_UI
#include "luaug/assetc/compiler.h"
#endif

#include <doctest/doctest.h>
#include <filesystem>
#include <span>
#include <string>
#include <system_error>
#include <vector>

using namespace luaug;

namespace {

// The REAL catalog, not a fixture -- so a key this path raises and en.json does
// not carry is a failure here rather than a `[i18n:missing:...]` line nobody
// reads. `render.err.mesh_not_compiled` is new with the cut-over and is raised
// by the third case below.
void seedRealCatalog()
{
    const auto result = core::engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
}

// A scratch project: `content/models/quad.gltf` and `content/textures/base.png`,
// copied from `asset`'s own fixtures so this needs no checked-in binary of its
// own and no generator that could disagree with the real files.
struct Project
{
    std::filesystem::path root;

    Project()
    {
        std::error_code ec;
        root = std::filesystem::temp_directory_path(ec) / "luaug-content-import-tests";
        std::filesystem::remove_all(root, ec);
        std::filesystem::create_directories(root / "content" / "models", ec);
        std::filesystem::create_directories(root / "content" / "textures", ec);
        std::filesystem::copy_file(LUAUG_TEST_MESH, mesh(), std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::copy_file(LUAUG_TEST_IMAGE, image(), std::filesystem::copy_options::overwrite_existing, ec);
    }

    ~Project()
    {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    Project(const Project&) = delete;
    Project& operator=(const Project&) = delete;

    [[nodiscard]] std::filesystem::path content() const { return root / "content"; }
    [[nodiscard]] std::filesystem::path mesh() const { return content() / "models" / "quad.gltf"; }
    [[nodiscard]] std::filesystem::path image() const { return content() / "textures" / "base.png"; }

    // **What makes this test what it is.** Everything the compiler read is
    // removed, so anything that still resolves came out of the store.
    void deleteSources() const
    {
        std::error_code ec;
        std::filesystem::remove_all(content(), ec);
    }
};

struct Registries
{
    core::AtomTable atoms;
    scene::ClassRegistry classes;
    scene::EnumRegistry enums;
    scene::ClassId meshPartClass = scene::InvalidClass;
    scene::ClassId materialClass = scene::InvalidClass;
    scene::ClassId workspaceClass = scene::InvalidClass;

    Registries()
    {
        meshPartClass = classes.registerClass({
            .name = atoms.intern("MeshPart"),
            .defaultName = atoms.intern("MeshPart"),
        });
        workspaceClass = classes.registerClass({
            .name = atoms.intern("Workspace"),
            .defaultName = atoms.intern("Workspace"),
        });
        materialClass = classes.registerClass({
            .name = atoms.intern("Material"),
            .defaultName = atoms.intern("Material"),
            .attachComponents = [](scene::World& w,
                                   core::InstanceId id) { w.materials().add(id, scene::MaterialComponent{}); },
            .detachComponents = [](scene::World& w, core::InstanceId id) { w.materials().remove(id); },
        });
    }
};

} // namespace

TEST_CASE("a project compiles when it is opened, and then needs none of its sources")
{
    if (!LUAUG_DEBUG_UI) {
        MESSAGE("LUAUG_TEST_SKIP: this build carries no compiler, so a project cannot compile itself");
        return;
    }

    seedRealCatalog();

    const Project project;
    REQUIRE(std::filesystem::exists(project.mesh()));
    REQUIRE(std::filesystem::exists(project.image()));

    asset::ContentMounts mounts;
    const app::ContentImportReport report = app::openProjectContent(project.root, project.content(), mounts);

    // **It did work**, rather than finding a cache from a previous run: the
    // scratch tree is new, so every source is a miss.
    CHECK(report.failed.empty());
    CHECK(report.cacheMisses > 0);
    CHECK(report.meshes > 0);

    // Two mounts: the source tree, and the store above it.
    CHECK(mounts.mountCount() == 2);

    const asset::ResolvedContent mesh = mounts.resolve("asset://models/quad.gltf");
    REQUIRE(mesh.found());
    // `Source::Pack`, which is what a store resolves as -- so the loader takes
    // its compiled branch and there is no second answer for it to prefer.
    CHECK(mesh.source == asset::ResolvedContent::Source::Pack);
    CHECK(mesh.kind == asset::AssetKind::Mesh);

    const asset::ResolvedContent image = mounts.resolve("asset://textures/base.png");
    REQUIRE(image.found());
    CHECK(image.source == asset::ResolvedContent::Source::Pack);
    CHECK(image.kind == asset::AssetKind::Texture);

    // **And now the sources go away.** A store holds its blobs as files it owns,
    // so what resolves after this came out of the store and could have come from
    // nowhere else.
    project.deleteSources();

    asset::ContentMounts afterwards;
    (void)app::openProjectContent(project.root, project.content(), afterwards);
    // One mount, not two: there is no content directory left to mount, and the
    // store still stands.
    CHECK(afterwards.mountCount() == 1);
    CHECK(afterwards.resolve("asset://models/quad.gltf").found());
    CHECK(afterwards.resolve("asset://textures/base.png").found());
}

TEST_CASE("the loader draws a compiled mesh and a compiled map, with no source file left")
{
    if (!LUAUG_DEBUG_UI) {
        MESSAGE("LUAUG_TEST_SKIP: this build carries no compiler, so a project cannot compile itself");
        return;
    }

    seedRealCatalog();

    const Project project;
    asset::ContentMounts mounts;
    const app::ContentImportReport report = app::openProjectContent(project.root, project.content(), mounts);
    REQUIRE(report.failed.empty());

    // **The whole point of the case.** From here on there is no `.gltf` and no
    // `.png` anywhere on disk, so a loader that still had a loose reader would
    // load nothing and this would fail.
    project.deleteSources();

    Registries registries;
    scene::World world(registries.classes, registries.enums, registries.atoms, 1234u);
    const core::InstanceId workspace = world.create(registries.workspaceClass);

    const core::InstanceId part = world.create(registries.meshPartClass);
    world.parts().add(part, scene::PartComponent{});
    scene::MeshPartComponent meshPart;
    meshPart.meshContent = registries.atoms.intern("asset://models/quad.gltf");
    world.meshParts().add(part, meshPart);
    (void)world.setParent(part, workspace);

    const core::InstanceId material = world.create(registries.materialClass);
    scene::MaterialComponent* block = world.materials().find(material);
    REQUIRE(block != nullptr);
    block->colorMap = registries.atoms.intern("asset://textures/base.png");

    rhi::DeviceResult device = rhi::createNullDevice({.backend = rhi::BackendId::Null});
    REQUIRE(device != nullptr);
    rhi::ICmdList* cmd = device->beginFrame();
    REQUIRE(cmd != nullptr);

    render::MeshCache cache;
    render::MeshLibrary library;
    render::TextureLibrary textures;
    render::MeshLoader loader;
    loader.setContentMounts(&mounts);
    loader.setDeferredTextures(false);

    CHECK(loader.sync(*device, *cmd, world, workspace, cache, library) == 1u);
    CHECK(library.find(meshPart.meshContent) != nullptr);

    // **The map came out of the store, transcoded rather than decoded**, which
    // is the branch that makes "BC7 and mips reach editor content" true. It was
    // being produced before this and read by nobody: `syncTextures` went to the
    // raw PNG beside it every time.
    CHECK(loader.syncTextures(*device, *cmd, world, textures) == 1u);
    CHECK(textures.find(block->colorMap).valid());

    loader.destroy(*device);
    cache.destroy(*device);
}

TEST_CASE("a mesh with no compiled form draws nothing, rather than parsing a source file")
{
    seedRealCatalog();

    const Project project;

    // Mounted WITHOUT compiling: the source tree is there and the store is not,
    // which is precisely the state the loose feed used to serve.
    asset::ContentMounts mounts;
    mounts.mountDirectory(project.content());
    REQUIRE(mounts.resolve("asset://models/quad.gltf").found());
    // Found, and found as a LOOSE file -- so this is not a test about a missing
    // asset. The file is right there, and the runtime declines to read it.
    CHECK(mounts.resolve("asset://models/quad.gltf").source == asset::ResolvedContent::Source::Loose);

    Registries registries;
    scene::World world(registries.classes, registries.enums, registries.atoms, 1234u);
    const core::InstanceId workspace = world.create(registries.workspaceClass);
    const core::InstanceId part = world.create(registries.meshPartClass);
    world.parts().add(part, scene::PartComponent{});
    scene::MeshPartComponent meshPart;
    meshPart.meshContent = registries.atoms.intern("asset://models/quad.gltf");
    world.meshParts().add(part, meshPart);
    (void)world.setParent(part, workspace);

    rhi::DeviceResult device = rhi::createNullDevice({.backend = rhi::BackendId::Null});
    REQUIRE(device != nullptr);
    rhi::ICmdList* cmd = device->beginFrame();
    REQUIRE(cmd != nullptr);

    render::MeshCache cache;
    render::MeshLibrary library;
    render::MeshLoader loader;
    loader.setContentMounts(&mounts);

    CHECK(loader.sync(*device, *cmd, world, workspace, cache, library) == 0u);
    CHECK(library.find(meshPart.meshContent) == nullptr);

    // And it is remembered, so the refusal costs one attempt rather than one
    // per frame for ever.
    CHECK(loader.sync(*device, *cmd, world, workspace, cache, library) == 0u);

    loader.destroy(*device);
    cache.destroy(*device);
}

#if LUAUG_DEBUG_UI

TEST_CASE("import_matches_build: the editor's import and a command-line build agree, blob for blob")
{
    // E9's declared verification, and the reason it is worth asserting even
    // though `importOne` makes it structural: two entry points that agree by
    // inspection stop agreeing the first time one of them is changed.
    seedRealCatalog();

    const Project project;

    asset::ContentMounts mounts;
    const app::ContentImportReport report = app::openProjectContent(project.root, project.content(), mounts);
    REQUIRE(report.failed.empty());

    assetc::CompileOptions options;
    options.inputRoot = project.content();
    // No cache: the command-line side must produce these bytes from the sources,
    // not read back what the import just wrote.
    const assetc::CompileResult built = assetc::compile(options);
    REQUIRE_MESSAGE(built.ok, built.diagnostic);
    REQUIRE_FALSE(built.entries.empty());

    // **Per URN and per blob.** The manifest says which hash a URN has; the
    // store answers with the bytes under that hash. Both have to match, because
    // a matching hash over bytes nobody stored is a store that fails at first
    // use.
    for (const assetc::ManifestEntry& entry : built.entries) {
        const asset::ResolvedContent found = mounts.resolve(entry.urn);
        INFO("urn: ", entry.urn);
        REQUIRE(found.found());
        CHECK(found.source == asset::ResolvedContent::Source::Pack);
        CHECK(found.hash == entry.hash);
        CHECK(found.kind == entry.kind);

        const std::span<const std::byte> blob = mounts.blob(entry.hash);
        CHECK_FALSE(blob.empty());
        CHECK(blob.size() == entry.storedBytes);
    }
}

#endif
