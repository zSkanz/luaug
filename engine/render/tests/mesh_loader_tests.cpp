// How a material's maps get onto the GPU, and when.
//
// The case this file exists for is a measurement: an ordinary 1024-square PNG
// costs 14 to 36 ms to decode, and `syncTextures` used to load every missing map
// it found in one frame with no budget at all. Pointing a part at a four-map
// material therefore froze the frame after the write for a tenth of a second --
// which is how an editor whose frame times all look fine comes to feel like it
// reloads the world whenever you touch it.
//
// So there are two modes, and both are asserted here: one that finishes before
// the frame does, because a capture records the frame it was told to, and one
// that lets the frame finish first, because a person is watching.
#include "luaug/asset/content.h"
#include "luaug/asset/gltf.h"
#include "luaug/asset/mesh_format.h"
#include "luaug/core/content_hash.h"
#include "luaug/core/i18n.h"
#include "luaug/jobs/jobs.h"
#include "luaug/platform/async_io.h"
#include "luaug/platform/file.h"
#include "luaug/render/mesh_cache.h"
#include "luaug/render/mesh_loader.h"
#include "luaug/rhi/backends.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/components.h"
#include "luaug/scene/enum_registry.h"
#include "luaug/scene/world.h"

#include <chrono>
#include <doctest/doctest.h>
#include <filesystem>
#include <span>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

using namespace luaug;

namespace {

struct Fixture
{
    core::AtomTable atoms;
    scene::ClassRegistry classes;
    scene::EnumRegistry enums;
    scene::ClassId materialClass = scene::InvalidClass;

    rhi::DeviceResult device = rhi::createNullDevice({.backend = rhi::BackendId::Null});
    rhi::ICmdList* cmd = nullptr;

    Fixture()
    {
        materialClass = classes.registerClass({
            .name = atoms.intern("Material"),
            .defaultName = atoms.intern("Material"),
            .attachComponents = [](scene::World& w,
                                   core::InstanceId id) { w.materials().add(id, scene::MaterialComponent{}); },
            .detachComponents = [](scene::World& w, core::InstanceId id) { w.materials().remove(id); },
        });
        REQUIRE(device != nullptr);
        cmd = device->beginFrame();
        REQUIRE(cmd != nullptr);
    }

    // A world holding one material that names `image` as its base colour. The
    // URN is the path itself, which is what `resolve` falls back to when no
    // mount answers -- exactly the dev-mode path ADR 0010 keeps forever.
    [[nodiscard]] scene::World worldNaming(const std::filesystem::path& image)
    {
        scene::World world(classes, enums, atoms, 1234u);
        const core::InstanceId id = world.create(materialClass);
        scene::MaterialComponent* material = world.materials().find(id);
        REQUIRE(material != nullptr);
        material->colorMap = world.atoms().intern(image.generic_string());
        return world;
    }
};

} // namespace

TEST_CASE("a texture is on the GPU before the frame that asked for it ends")
{
    // The default, and the mode every golden depends on: a capture records the
    // frame it was told to record, and a texture that arrives two frames later
    // is a different picture.
    Fixture fixture;
    const std::filesystem::path image(LUAUG_RENDER_TEST_IMAGE);
    REQUIRE(std::filesystem::exists(image));

    scene::World world = fixture.worldNaming(image);
    render::MeshLoader loader;
    render::TextureLibrary library;

    CHECK(loader.syncTextures(*fixture.device, *fixture.cmd, world, library) == 1);
    CHECK(library.size() == 1);
    CHECK(loader.texturesInFlight() == 0);
    loader.destroy(*fixture.device);
}

TEST_CASE("a deferred texture costs the frame that asked for it nothing")
{
    Fixture fixture;
    const std::filesystem::path image(LUAUG_RENDER_TEST_IMAGE);
    REQUIRE(std::filesystem::exists(image));
    REQUIRE(platform::initIo());

    scene::World world = fixture.worldNaming(image);
    render::MeshLoader loader;
    loader.setDeferredTextures(true);
    render::TextureLibrary library;

    // Nothing loaded, and nothing decoded: the frame queued a read and moved on.
    // The surface draws in its own numbers meanwhile, which is what the
    // synchronous path already promised for a map that had not arrived.
    CHECK(loader.syncTextures(*fixture.device, *fixture.cmd, world, library) == 0);
    CHECK(library.size() == 0);
    CHECK(loader.texturesInFlight() == 1);

    // And it does arrive. **Bounded**, because a test that spins forever on a
    // defect reports as a hung machine rather than as a failure; and it sleeps,
    // because the whole point is that the work is on other threads and a loop
    // spinning as fast as it can would be asserting they finish in nanoseconds.
    for (int frame = 0; frame < 2000 && library.size() == 0; ++frame) {
        (void)loader.syncTextures(*fixture.device, *fixture.cmd, world, library);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(library.size() == 1);
    CHECK(loader.texturesInFlight() == 0);
    loader.destroy(*fixture.device);
}

TEST_CASE("a map that is not there is refused once, in either mode")
{
    // A material without its texture still draws, in its own numbers -- and a
    // missing file must cost one attempt and one message rather than one of each
    // per frame forever. The deferred path has its own way to fail and needs its
    // own assertion that it fails the same way.
    Fixture fixture;
    REQUIRE(platform::initIo());
    const std::filesystem::path absent =
        std::filesystem::path(LUAUG_RENDER_TEST_IMAGE).parent_path() / "not-a-real-file.png";

    for (const bool deferred : {false, true}) {
        scene::World world = fixture.worldNaming(absent);
        render::MeshLoader loader;
        loader.setDeferredTextures(deferred);
        render::TextureLibrary library;

        for (int frame = 0; frame < 2000; ++frame) {
            CHECK(loader.syncTextures(*fixture.device, *fixture.cmd, world, library) == 0);
            if (loader.texturesInFlight() == 0 && frame > 4)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        CHECK(library.size() == 0);
        // Nothing left in the pipeline: a file that is not there must not sit in
        // it forever holding one of the four slots.
        CHECK(loader.texturesInFlight() == 0);
        loader.destroy(*fixture.device);
    }
}

TEST_CASE("a world naming more maps than the pipeline holds still loads them all")
{
    // The bound is what stops a world of four hundred textures opening four
    // hundred files at once. A bound that never drains would be a world whose
    // surfaces are permanently white, which is worse than the freeze it
    // replaced.
    Fixture fixture;
    const std::filesystem::path image(LUAUG_RENDER_TEST_IMAGE);
    REQUIRE(std::filesystem::exists(image));
    REQUIRE(platform::initIo());

    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const std::filesystem::path folder = image.parent_path();
    core::usize named = 0;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".png")
            continue;
        const core::InstanceId id = world.create(fixture.materialClass);
        world.materials().find(id)->colorMap = world.atoms().intern(entry.path().generic_string());
        ++named;
    }
    REQUIRE(named > 0);

    render::MeshLoader loader;
    loader.setDeferredTextures(true);
    render::TextureLibrary library;

    for (int frame = 0; frame < 4000 && library.size() < named; ++frame) {
        (void)loader.syncTextures(*fixture.device, *fixture.cmd, world, library);
        // Never more than the bound, on any frame.
        REQUIRE(loader.texturesInFlight() <= render::MeshLoader::MaxTexturesInFlight);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(library.size() == named);
    loader.destroy(*fixture.device);
}

TEST_CASE("tearing down while a texture is on its way in leaves nothing behind")
{
    // **The shutdown case, which is the one that reads as "it crashed when I
    // closed it" and points at nothing.** A decode job writes into buffers the
    // loader owns, so a teardown that returned while one was running would free
    // the memory the pool is writing into -- reproducing on a fast machine and
    // never on a slow one.
    //
    // The job pool is uninitialised here and therefore serial, so what this
    // actually exercises is the read half: a request queued and then abandoned
    // must be cancelled rather than left holding a slot.
    Fixture fixture;
    const std::filesystem::path image(LUAUG_RENDER_TEST_IMAGE);
    REQUIRE(std::filesystem::exists(image));
    REQUIRE(platform::initIo());

    scene::World world = fixture.worldNaming(image);
    {
        render::MeshLoader loader;
        loader.setDeferredTextures(true);
        render::TextureLibrary library;

        CHECK(loader.syncTextures(*fixture.device, *fixture.cmd, world, library) == 0);
        REQUIRE(loader.texturesInFlight() == 1);

        loader.destroy(*fixture.device);
        CHECK(loader.texturesInFlight() == 0);
    }

    // And a loader that goes out of scope without `destroy` being called at all,
    // which is what a stack unwind does.
    {
        render::MeshLoader loader;
        loader.setDeferredTextures(true);
        render::TextureLibrary library;
        CHECK(loader.syncTextures(*fixture.device, *fixture.cmd, world, library) == 0);
    }
}

TEST_CASE("the pipeline works with real threads, and with the queue growing under it")
{
    // **Every other case here runs on the serial pool**, where a job finishes on
    // the calling thread before `schedule` returns -- which is the mode a test
    // wants and is also the mode in which no threading defect can appear. This
    // one starts real workers.
    //
    // What it exercises on purpose: more maps are asked for while earlier ones
    // are still decoding, so `pendingTextures_` reallocates underneath jobs that
    // are running. Everything a job touches lives in one heap allocation for
    // exactly that reason; the first version of this pipeline kept a `bool` in
    // the vector element and handed the job its address.
    Fixture fixture;
    const std::filesystem::path image(LUAUG_RENDER_TEST_IMAGE);
    REQUIRE(std::filesystem::exists(image));
    REQUIRE(platform::initIo());

    const bool startedPool = !jobs::initialized();
    if (startedPool)
        jobs::init(4);

    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const std::filesystem::path folder = image.parent_path();
    std::vector<std::filesystem::path> images;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder)) {
        if (entry.is_regular_file() && entry.path().extension() == ".png")
            images.push_back(entry.path());
    }
    REQUIRE_FALSE(images.empty());

    render::MeshLoader loader;
    loader.setDeferredTextures(true);
    render::TextureLibrary library;

    // One material named per frame, so the queue is still being written to while
    // the pool is reading from it.
    core::usize named = 0;
    for (int frame = 0; frame < 4000; ++frame) {
        if (named < images.size() * 4) {
            const core::InstanceId id = world.create(fixture.materialClass);
            world.materials().find(id)->colorMap = world.atoms().intern(images[named % images.size()].generic_string());
            ++named;
        }
        (void)loader.syncTextures(*fixture.device, *fixture.cmd, world, library);
        REQUIRE(loader.texturesInFlight() <= render::MeshLoader::MaxTexturesInFlight);
        if (named >= images.size() * 4 && loader.texturesInFlight() == 0 && library.size() >= images.size())
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // One entry per distinct file, however many materials named it: the library
    // is keyed by URN, and four materials sharing a map is one texture.
    CHECK(library.size() == images.size());
    loader.destroy(*fixture.device);

    if (startedPool)
        jobs::shutdown();
}

TEST_CASE("the loader says WHICH meshes it loaded, not just how many")
{
    // **A count cannot answer \"which\".** The frame loop hands a mesh's vertex
    // positions to the physics mirror, and it did that by walking the whole
    // library whenever the count was non-zero -- a full copy of every loaded
    // mesh's positions on every frame anything landed. Synchronous loading hid
    // that inside one or two frames; spreading N completions over N frames turns
    // it into N(N+1)/2 copies.
    Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);

    render::MeshCache cache;
    render::MeshLibrary library;
    render::MeshLoader loader;
    std::vector<core::NameAtom> completed;

    // A world with no MeshParts loads nothing and appends nothing.
    (void)loader.sync(*fixture.device, *fixture.cmd, world, core::InstanceId{}, cache, library, nullptr, &completed);
    CHECK(completed.empty());

    // And the out-parameter is optional, which every production caller but one
    // relies on.
    (void)loader.sync(*fixture.device, *fixture.cmd, world, core::InstanceId{}, cache, library);

    loader.destroy(*fixture.device);
    cache.destroy(*fixture.device);
}

namespace {

// **An object store built out of `asset`'s own glTF fixtures** (E9 step 14).
//
// This used to point each `MeshPart` at the source file and let `resolve` fall
// through to the path -- the dev-mode feed ADR 0010 kept, and the cut-over
// deleted. What replaces it is the real compiled pipeline in miniature:
// `importGltf` reads the fixture, `compileMesh` builds its chain, `encodeMesh`
// writes the bytes, and those bytes land in a content-addressed store the loader
// mounts. Which means this case now exercises the ONLY feed there is, rather
// than the one that no longer exists.
//
// Returns the object directory and the index beside it, or an empty pair when
// this build stages no fixtures.
struct CompiledFixtures
{
    std::filesystem::path objects;
    std::filesystem::path index;
    std::vector<std::string> urns;
};

[[nodiscard]] CompiledFixtures compileFixtures(const std::filesystem::path& root)
{
    static const char* const kFiles[] = {"quad.gltf", "textured.gltf", "two_materials.gltf", "flat_normals.gltf"};

    CompiledFixtures out;
    out.objects = root / "objects";
    out.index = root / "index.json";

    std::string index = R"({"format":"luaug-content-manifest","version":1,"assets":[)";
    bool first = true;
    for (const char* file : kFiles) {
        const std::filesystem::path path = std::filesystem::path(LUAUG_RENDER_TEST_MESH_DIR) / file;
        if (!std::filesystem::exists(path))
            continue;

        std::vector<std::byte> bytes;
        if (!platform::readFile(path, bytes))
            continue;
        asset::Model model;
        if (asset::importGltf(bytes, path.parent_path(), {}, model).has_value())
            continue;
        asset::CompiledMesh compiled;
        if (asset::compileMesh(model, {}, {}, compiled).has_value())
            continue;

        const std::vector<std::byte> encoded = asset::encodeMesh(compiled);
        const core::ContentHash hash = core::hashBytes(encoded);
        const std::filesystem::path blob = asset::ContentMounts::objectPath(out.objects, hash);
        std::error_code ec;
        std::filesystem::create_directories(blob.parent_path(), ec);
        if (!platform::writeFile(blob, encoded))
            continue;

        const std::string urn = std::string("asset://") + file;
        if (!first)
            index += ",";
        first = false;
        index += R"({"urn":")" + urn + R"(","hash":")" + hash.toHex() + R"(","kind":"mesh"})";
        out.urns.push_back(urn);
    }
    index += "]}";

    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (!platform::writeFile(out.index, std::span{reinterpret_cast<const std::byte*>(index.data()), index.size()}))
        out.urns.clear();
    return out;
}

// A world of `MeshPart`s, one per compiled fixture.
[[nodiscard]] core::usize fillWithMeshes(const CompiledFixtures& compiled, scene::World& world,
                                         scene::ClassId meshPartClass, core::InstanceId workspace)
{
    core::usize named = 0;
    for (const std::string& urn : compiled.urns) {
        const core::InstanceId id = world.create(meshPartClass);
        scene::PartComponent part;
        world.parts().add(id, part);
        scene::MeshPartComponent mesh;
        mesh.meshContent = world.atoms().intern(urn);
        world.meshParts().add(id, mesh);
        (void)world.setParent(id, workspace);
        ++named;
    }
    return named;
}

} // namespace

TEST_CASE("meshes arrive one frame at a time, or all at once, and it is a decision")
{
    // **A parse is the largest synchronous thing left in a frame.** Measured at
    // 191 ms for the model E9 opened for, plus 21 ms to read it -- and `sync`
    // loaded every missing mesh it found in ONE frame with no budget at all, so
    // a folder of five models was one frame of about a second.
    //
    // A budget of one whole mesh rather than a millisecond count, because a
    // parse cannot be split: any budget needs a floor of one, and one already
    // exceeds a frame. What it buys is N frames instead of one frame N times as
    // long.
    Fixture fixture;
    const scene::ClassId meshPartClass = fixture.classes.registerClass({
        .name = fixture.atoms.intern("MeshPart"),
        .defaultName = fixture.atoms.intern("MeshPart"),
    });
    const scene::ClassId workspaceClass = fixture.classes.registerClass({
        .name = fixture.atoms.intern("Workspace"),
        .defaultName = fixture.atoms.intern("Workspace"),
    });

    // Compiled once for both subcases, into a directory that goes away with the
    // process. `doctest` re-enters this body per SUBCASE, so this runs twice --
    // which is fine and is also the point: writing the same bytes to the same
    // content-addressed name twice is what a re-import does.
    std::error_code ec;
    const std::filesystem::path root = std::filesystem::temp_directory_path(ec) / "luaug-mesh-loader-tests" / "import";
    std::filesystem::remove_all(root, ec);
    const CompiledFixtures compiled = compileFixtures(root);
    if (compiled.urns.size() < 2) {
        MESSAGE("LUAUG_TEST_SKIP: this build stages fewer than two glTF fixtures to compile");
        return;
    }

    asset::ContentMounts mounts;
    REQUIRE_FALSE(mounts.mountObjects(compiled.objects, compiled.index).has_value());

    SUBCASE("synchronous: everything in the frame that asked")
    {
        scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
        const core::InstanceId workspace = world.create(workspaceClass);
        const core::usize named = fillWithMeshes(compiled, world, meshPartClass, workspace);
        REQUIRE(named > 1);

        render::MeshCache cache;
        render::MeshLibrary library;
        render::MeshLoader loader;
        loader.setContentMounts(&mounts);

        (void)loader.sync(*fixture.device, *fixture.cmd, world, workspace, cache, library);
        CHECK(library.size() == named);

        loader.destroy(*fixture.device);
        cache.destroy(*fixture.device);
    }

    SUBCASE("deferred: one per call, and all of them eventually")
    {
        scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
        const core::InstanceId workspace = world.create(workspaceClass);
        const core::usize named = fillWithMeshes(compiled, world, meshPartClass, workspace);
        REQUIRE(named > 1);

        render::MeshCache cache;
        render::MeshLibrary library;
        render::MeshLoader loader;
        loader.setContentMounts(&mounts);
        loader.setDeferredMeshes(true);

        (void)loader.sync(*fixture.device, *fixture.cmd, world, workspace, cache, library);
        CHECK(library.size() == 1);

        // **And it drains.** A budget that never finishes is a world whose
        // geometry never appears, which is worse than the hitch it replaced.
        for (core::usize call = 0; call < named + 4 && library.size() < named; ++call)
            (void)loader.sync(*fixture.device, *fixture.cmd, world, workspace, cache, library);
        CHECK(library.size() == named);

        loader.destroy(*fixture.device);
        cache.destroy(*fixture.device);
    }
}

TEST_CASE("forgetting a mesh forgets that it failed, so a fixed file loads")
{
    // **The defect this pins defeated `forget`'s whole purpose.** `failed_`
    // remembers a URN that would not load so it costs one attempt rather than
    // one per frame for ever -- and it was cleared nowhere but `destroy`. So an
    // asset that failed once was blacklisted for the life of the loader:
    // somebody fixed the file, the watcher called `forget`, `sync` found the URN
    // still blacklisted and skipped it, and the fix did not appear until the
    // editor was restarted. ADR 0062 promises exactly that does not happen.
    Fixture fixture;
    const scene::ClassId meshPartClass = fixture.classes.registerClass({
        .name = fixture.atoms.intern("MeshPart"),
        .defaultName = fixture.atoms.intern("MeshPart"),
    });
    const scene::ClassId workspaceClass = fixture.classes.registerClass({
        .name = fixture.atoms.intern("Workspace"),
        .defaultName = fixture.atoms.intern("Workspace"),
    });

    std::error_code ec;
    const std::filesystem::path root = std::filesystem::temp_directory_path(ec) / "luaug-forget-failed" / "import";
    std::filesystem::remove_all(root, ec);
    const CompiledFixtures compiled = compileFixtures(root);
    if (compiled.urns.empty()) {
        MESSAGE("LUAUG_TEST_SKIP: this build stages no glTF fixtures to compile");
        return;
    }

    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId workspace = world.create(workspaceClass);
    const core::InstanceId part = world.create(meshPartClass);
    world.parts().add(part, scene::PartComponent{});
    scene::MeshPartComponent mesh;
    mesh.meshContent = world.atoms().intern(compiled.urns.front());
    world.meshParts().add(part, mesh);
    (void)world.setParent(part, workspace);

    render::MeshCache cache;
    render::MeshLibrary library;
    render::TextureLibrary textures;
    render::MeshLoader loader;

    // **First, with no mounts at all**, so the URN cannot resolve and is
    // blacklisted -- which is the state a broken file leaves behind.
    CHECK(loader.sync(*fixture.device, *fixture.cmd, world, workspace, cache, library) == 0u);
    CHECK(library.find(mesh.meshContent) == nullptr);

    // The mount arrives, which is the file being fixed. Without forgetting the
    // refusal this still loads nothing, for ever.
    asset::ContentMounts mounts;
    REQUIRE_FALSE(mounts.mountObjects(compiled.objects, compiled.index).has_value());
    loader.setContentMounts(&mounts);

    const std::array<core::NameAtom, 1> forgotten{mesh.meshContent};
    (void)loader.forget(*fixture.device, forgotten, textures, library, cache);

    CHECK(loader.sync(*fixture.device, *fixture.cmd, world, workspace, cache, library) == 1u);
    CHECK(library.find(mesh.meshContent) != nullptr);

    loader.destroy(*fixture.device);
    cache.destroy(*fixture.device);
}
