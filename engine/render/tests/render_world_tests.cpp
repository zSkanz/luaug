#include <doctest/doctest.h>

#include <ostream>

#include "luaug/render/render_world.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/enum_registry.h"
#include "luaug/scene/world.h"

using namespace luaug;

namespace
{

// A hierarchy with just enough in it to have a root and a part. Hand-built
// rather than generated, because `render` must not depend on the API definition
// files to be testable -- that would make a rendering test fail for a reason
// that has nothing to do with rendering.
struct Fixture
{
    core::AtomTable atoms;
    scene::ClassRegistry classes;
    scene::EnumRegistry enums;

    scene::ClassId instanceClass = scene::InvalidClass;
    scene::ClassId folderClass = scene::InvalidClass;
    scene::ClassId partClass = scene::InvalidClass;

    Fixture()
    {
        scene::ClassDescriptor instance;
        instance.name = atoms.intern("Instance");
        instance.defaultName = instance.name;
        instanceClass = classes.registerClass(instance);

        scene::ClassDescriptor folder;
        folder.name = atoms.intern("Folder");
        folder.super = instanceClass;
        folder.defaultName = folder.name;
        folderClass = classes.registerClass(folder);

        scene::ClassDescriptor part;
        part.name = atoms.intern("Part");
        part.super = instanceClass;
        part.defaultName = part.name;
        part.attachComponents = [](scene::World& w, core::InstanceId id)
        { w.parts().add(id, scene::PartComponent{}); };
        part.detachComponents = [](scene::World& w, core::InstanceId id) { w.parts().remove(id); };
        partClass = classes.registerClass(part);
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    scene::World world{classes, enums, atoms, 1u};

    [[nodiscard]] core::InstanceId part(core::InstanceId parent)
    {
        const core::InstanceId id = world.create(partClass);
        (void)world.setParent(id, parent);
        return id;
    }
};

} // namespace

TEST_CASE("extraction copies what rendering needs and nothing that can go stale")
{
    Fixture fixture;
    const core::InstanceId root = fixture.world.create(fixture.folderClass);

    const core::InstanceId part = fixture.part(root);
    scene::PartComponent* component = fixture.world.parts().find(part);
    REQUIRE(component != nullptr);
    component->cframe.position = core::DVec3{1.0, 2.0, 3.0};
    component->size = core::Vec3{4.0f, 5.0f, 6.0f};
    component->color = core::Color3{0.25f, 0.5f, 0.75f};
    component->transparency = 0.5f;
    component->shape = 2;

    render::RenderWorld snapshot;
    render::extract(fixture.world, root, snapshot);

    REQUIRE(snapshot.parts.size() == 1);
    CHECK(snapshot.parts[0].cframe.position.x == 1.0);
    CHECK(snapshot.parts[0].size == core::Vec3{4.0f, 5.0f, 6.0f});
    CHECK(snapshot.parts[0].color == core::Color3{0.25f, 0.5f, 0.75f});
    CHECK(snapshot.parts[0].transparency == 0.5f);
    CHECK(snapshot.parts[0].shape == 2);
}

TEST_CASE("only what is under the root is in the world")
{
    Fixture fixture;
    const core::InstanceId root = fixture.world.create(fixture.folderClass);
    const core::InstanceId elsewhere = fixture.world.create(fixture.folderClass);

    const core::InstanceId inside = fixture.part(root);
    const core::InstanceId nested = fixture.part(inside);
    (void)fixture.part(elsewhere);
    (void)fixture.world.create(fixture.partClass); // unparented
    (void)nested;

    render::RenderWorld snapshot;
    render::extract(fixture.world, root, snapshot);

    // Whatever is parented under the root is in the world and whatever is not,
    // is not -- at any depth.
    CHECK(snapshot.parts.size() == 2);
}

TEST_CASE("an invalid root extracts nothing rather than everything")
{
    Fixture fixture;
    const core::InstanceId root = fixture.world.create(fixture.folderClass);
    (void)fixture.part(root);

    render::RenderWorld snapshot;
    render::extract(fixture.world, {}, snapshot);

    // The failure mode this guards is a boot that has not created `Workspace`
    // yet drawing the whole world, unparented instances included.
    CHECK(snapshot.parts.empty());
}

TEST_CASE("extraction clears first, so one buffer serves every frame")
{
    Fixture fixture;
    const core::InstanceId root = fixture.world.create(fixture.folderClass);
    (void)fixture.part(root);

    render::RenderWorld snapshot;
    render::extract(fixture.world, root, snapshot);
    render::extract(fixture.world, root, snapshot);
    CHECK(snapshot.parts.size() == 1);

    fixture.world.destroy(root);
    fixture.world.retireDestroyed();
    render::extract(fixture.world, root, snapshot);
    // A retired root is not a root: nothing resolves through it, so nothing is
    // in the world.
    CHECK(snapshot.parts.empty());
}
