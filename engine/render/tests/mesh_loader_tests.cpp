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
#include "luaug/core/i18n.h"
#include "luaug/platform/async_io.h"
#include "luaug/render/mesh_loader.h"
#include "luaug/rhi/backends.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/components.h"
#include "luaug/scene/enum_registry.h"
#include "luaug/scene/world.h"

#include <chrono>
#include <doctest/doctest.h>
#include <filesystem>
#include <thread>

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
