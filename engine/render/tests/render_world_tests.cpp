#include <doctest/doctest.h>

#include <ostream>

#include "luaug/render/render_world.h"
#include "luaug/scene/class_registry.h"
#include "luaug/core/types.h"
#include "luaug/scene/components.h"
#include "luaug/scene/enum_registry.h"
#include "luaug/scene/world.h"

using namespace luaug;

namespace
{

// These cases are all about the debug-part path, which needs no meshes and no
// camera. One shared empty library says that once rather than at every call.
const render::MeshLibrary kNoMeshes;

// `doctest::Approx` takes a `double`, so comparing an f32 against one promotes
// -- and `-Wdouble-promotion -Werror` is on for Clang, which means every such
// line is a Linux-only build failure. It has caught this twice in one milestone.
// Comparing in f32 throughout also states "relative to what" in the expression
// instead of in a comment.
[[nodiscard]] bool nearly(core::f32 value, core::f32 expected, core::f32 tolerance = 1e-5f) noexcept
{
    const core::f32 difference = value > expected ? value - expected : expected - value;
    return difference <= tolerance;
}

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

    // The M4 half of the fixture. Registered lazily so the cases above stay
    // exactly the world they were written against: a Workspace that carries a
    // camera reference changes what `extract` does on its first line.
    scene::ClassId workspaceClass = scene::InvalidClass;
    scene::ClassId cameraClass = scene::InvalidClass;
    scene::ClassId meshPartClass = scene::InvalidClass;

    void registerRenderClasses()
    {
        scene::ClassDescriptor workspace;
        workspace.name = atoms.intern("Workspace");
        workspace.super = instanceClass;
        workspace.defaultName = workspace.name;
        workspace.attachComponents = [](scene::World& w, core::InstanceId id)
        { w.workspaces().add(id, scene::WorkspaceComponent{}); };
        workspace.detachComponents = [](scene::World& w, core::InstanceId id) { w.workspaces().remove(id); };
        workspaceClass = classes.registerClass(workspace);

        scene::ClassDescriptor camera;
        camera.name = atoms.intern("Camera");
        camera.super = instanceClass;
        camera.defaultName = camera.name;
        camera.attachComponents = [](scene::World& w, core::InstanceId id)
        { w.cameras().add(id, scene::CameraComponent{}); };
        camera.detachComponents = [](scene::World& w, core::InstanceId id) { w.cameras().remove(id); };
        cameraClass = classes.registerClass(camera);

        scene::ClassDescriptor meshPart;
        meshPart.name = atoms.intern("MeshPart");
        meshPart.super = partClass;
        meshPart.defaultName = meshPart.name;
        meshPart.attachComponents = [](scene::World& w, core::InstanceId id)
        { w.meshParts().add(id, scene::MeshPartComponent{}); };
        meshPart.detachComponents = [](scene::World& w, core::InstanceId id) { w.meshParts().remove(id); };
        meshPartClass = classes.registerClass(meshPart);
    }

    // A camera at the origin looking down -Z, which is the direction api-design
    // gives LookVector, with a 90-degree vertical field of view so the frustum's
    // side planes sit at 45 degrees and every expectation below is arithmetic.
    [[nodiscard]] core::InstanceId cameraLookingDownNegativeZ(core::InstanceId workspaceId, core::DVec3 at = {})
    {
        const core::InstanceId id = world.create(cameraClass);
        (void)world.setParent(id, workspaceId);
        scene::CameraComponent* component = world.cameras().find(id);
        REQUIRE(component != nullptr);
        component->cframe.position = at;
        component->fieldOfView = 90.0f;
        component->nearPlane = 1.0f;
        component->farPlane = 100.0f;
        world.workspaces().find(workspaceId)->currentCamera = id;
        return id;
    }

    [[nodiscard]] core::InstanceId meshPartAt(core::InstanceId parent, core::DVec3 at, core::NameAtom content)
    {
        const core::InstanceId id = world.create(meshPartClass);
        (void)world.setParent(id, parent);
        world.parts().find(id)->cframe.position = at;
        world.meshParts().find(id)->meshContent = content;
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
    render::extract(fixture.world, root, core::InstanceId{}, kNoMeshes, 1.0f, 0.0f, snapshot);

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
    render::extract(fixture.world, root, core::InstanceId{}, kNoMeshes, 1.0f, 0.0f, snapshot);

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
    render::extract(fixture.world, {}, core::InstanceId{}, kNoMeshes, 1.0f, 0.0f, snapshot);

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
    render::extract(fixture.world, root, core::InstanceId{}, kNoMeshes, 1.0f, 0.0f, snapshot);
    render::extract(fixture.world, root, core::InstanceId{}, kNoMeshes, 1.0f, 0.0f, snapshot);
    CHECK(snapshot.parts.size() == 1);

    fixture.world.destroy(root);
    fixture.world.retireDestroyed();
    render::extract(fixture.world, root, core::InstanceId{}, kNoMeshes, 1.0f, 0.0f, snapshot);
    // A retired root is not a root: nothing resolves through it, so nothing is
    // in the world.
    CHECK(snapshot.parts.empty());
}

TEST_CASE("drawSortKey: the pass outranks the pipeline, which outranks the material")
{
    // Asserted on the key rather than on a sorted list, because a sorted list
    // only proves that *something* was consistent. These are the ordering
    // contract every backend inherits.
    CHECK(render::drawSortKey(0, 9, 9, 600.0f) < render::drawSortKey(1, 0, 0, 0.0f));
    CHECK(render::drawSortKey(0, 0, 9, 600.0f) < render::drawSortKey(0, 1, 0, 0.0f));
    CHECK(render::drawSortKey(0, 0, 0, 600.0f) < render::drawSortKey(0, 0, 1, 0.0f));
    CHECK(render::drawSortKey(0, 0, 0, 1.0f) < render::drawSortKey(0, 0, 0, 2.0f));

    // Quantized, so a sub-millimetre camera wobble cannot reorder two draws and
    // change a golden command stream. One unit is a centimetre.
    CHECK(render::drawSortKey(0, 0, 0, 1.0f) == render::drawSortKey(0, 0, 0, 1.000001f));
    CHECK(render::drawSortKey(0, 0, 0, 1.0f) != render::drawSortKey(0, 0, 0, 1.02f));

    // Saturating rather than wrapping: a draw beyond the quantization range must
    // sort last, not first.
    CHECK(render::drawSortKey(0, 0, 0, 5000.0f) >= render::drawSortKey(0, 0, 0, 654.0f));
    CHECK(render::drawSortKey(0, 0, 0, -5.0f) == render::drawSortKey(0, 0, 0, 0.0f));
}

TEST_CASE("extraction resolves the camera, and answers nothing without one")
{
    Fixture fixture;
    fixture.registerRenderClasses();
    const core::InstanceId workspace = fixture.world.create(fixture.workspaceClass);

    render::RenderWorld snapshot;

    SUBCASE("no camera means no view, and therefore no draws")
    {
        render::extract(fixture.world, workspace, core::InstanceId{}, kNoMeshes, 1.0f, 0.0f, snapshot);
        CHECK_FALSE(snapshot.camera.valid);
        CHECK(snapshot.draws.empty());
    }

    SUBCASE("a destroyed camera is the same as no camera")
    {
        const core::InstanceId camera = fixture.cameraLookingDownNegativeZ(workspace);
        fixture.world.destroy(camera);
        render::extract(fixture.world, workspace, core::InstanceId{}, kNoMeshes, 1.0f, 0.0f, snapshot);
        // The failure this guards is a renderer drawing through a camera whose
        // instance was retired, which is a stale InstanceId reaching the GPU.
        CHECK_FALSE(snapshot.camera.valid);
    }

    SUBCASE("the origin is the camera position, so the GPU never sees a world coordinate")
    {
        (void)fixture.cameraLookingDownNegativeZ(workspace, core::DVec3{1000000.0, 0.0, 0.0});
        render::extract(fixture.world, workspace, core::InstanceId{}, kNoMeshes, 1.0f, 0.0f, snapshot);
        REQUIRE(snapshot.camera.valid);
        // ADR 0014's whole point: a million metres out, the matrices are still
        // small numbers, because the camera sits at the origin of its own space.
        CHECK(snapshot.camera.origin.x == 1000000.0);
        CHECK(snapshot.camera.view.m[3][0] == 0.0f);
        CHECK(snapshot.camera.view.m[3][1] == 0.0f);
        CHECK(snapshot.camera.view.m[3][2] == 0.0f);
    }
}

TEST_CASE("extraction culls what the camera cannot see, and keeps what it can")
{
    Fixture fixture;
    fixture.registerRenderClasses();
    const core::InstanceId workspace = fixture.world.create(fixture.workspaceClass);
    (void)fixture.cameraLookingDownNegativeZ(workspace);

    const core::NameAtom content = fixture.atoms.intern("asset://models/box.glb");
    render::MeshLibrary meshes;
    render::MeshLibrary::Entry entry;
    entry.mesh = render::MeshHandle{0, 1};
    entry.bounds = core::AABB::fromCenterSize(core::Vec3{}, core::Vec3{1.0f, 1.0f, 1.0f});
    entry.sectionCount = 1;
    meshes.set(content, entry);

    render::RenderWorld snapshot;

    SUBCASE("in front of the camera is drawn")
    {
        (void)fixture.meshPartAt(workspace, core::DVec3{0.0, 0.0, -10.0}, content);
        render::extract(fixture.world, workspace, core::InstanceId{}, meshes, 1.0f, 0.0f, snapshot);
        CHECK(snapshot.draws.size() == 1);
        CHECK(snapshot.candidateDraws == 1);
        CHECK(snapshot.culledDraws == 0);
    }

    SUBCASE("behind the camera is culled, and counted as culled")
    {
        (void)fixture.meshPartAt(workspace, core::DVec3{0.0, 0.0, 10.0}, content);
        render::extract(fixture.world, workspace, core::InstanceId{}, meshes, 1.0f, 0.0f, snapshot);
        CHECK(snapshot.draws.empty());
        // The counter is what makes "the culler did something" assertable. A
        // frame where candidates and culls are both zero is a frame the culler
        // never ran on, which passes an emptiness check and proves nothing.
        CHECK(snapshot.candidateDraws == 1);
        CHECK(snapshot.culledDraws == 1);
    }

    SUBCASE("a mesh nothing has loaded is skipped rather than drawn as a placeholder")
    {
        const core::NameAtom absent = fixture.atoms.intern("asset://absent.glb");
        (void)fixture.meshPartAt(workspace, core::DVec3{0.0, 0.0, -10.0}, absent);
        render::extract(fixture.world, workspace, core::InstanceId{}, meshes, 1.0f, 0.0f, snapshot);
        CHECK(snapshot.draws.empty());
        // Not counted as a candidate either: nothing was ever a draw.
        CHECK(snapshot.candidateDraws == 0);
        CHECK(snapshot.culledDraws == 0);
    }
}

TEST_CASE("extraction orders draws near to far, stably")
{
    Fixture fixture;
    fixture.registerRenderClasses();
    const core::InstanceId workspace = fixture.world.create(fixture.workspaceClass);
    (void)fixture.cameraLookingDownNegativeZ(workspace);

    const core::NameAtom content = fixture.atoms.intern("asset://models/box.glb");
    render::MeshLibrary meshes;
    render::MeshLibrary::Entry entry;
    entry.mesh = render::MeshHandle{0, 1};
    entry.bounds = core::AABB::fromCenterSize(core::Vec3{}, core::Vec3{1.0f, 1.0f, 1.0f});
    entry.sectionCount = 1;
    meshes.set(content, entry);

    // Created far-to-near, so a snapshot that merely preserved creation order
    // would come out backwards.
    (void)fixture.meshPartAt(workspace, core::DVec3{0.0, 0.0, -50.0}, content);
    (void)fixture.meshPartAt(workspace, core::DVec3{0.0, 0.0, -10.0}, content);
    (void)fixture.meshPartAt(workspace, core::DVec3{0.0, 0.0, -30.0}, content);

    render::RenderWorld snapshot;
    render::extract(fixture.world, workspace, core::InstanceId{}, meshes, 1.0f, 0.0f, snapshot);
    REQUIRE(snapshot.draws.size() == 3);
    CHECK(snapshot.draws[0].sortKey < snapshot.draws[1].sortKey);
    CHECK(snapshot.draws[1].sortKey < snapshot.draws[2].sortKey);
    CHECK(nearly(snapshot.draws[0].transform.m[3][2], -10.0f));
    CHECK(nearly(snapshot.draws[2].transform.m[3][2], -50.0f));

    // R10: the same world extracted twice gives the same order, every time.
    render::RenderWorld again;
    render::extract(fixture.world, workspace, core::InstanceId{}, meshes, 1.0f, 0.0f, again);
    REQUIRE(again.draws.size() == snapshot.draws.size());
    for (std::size_t index = 0; index < again.draws.size(); ++index)
        CHECK(again.draws[index].sortKey == snapshot.draws[index].sortKey);
}

TEST_CASE("extraction reads the environment from Lighting, and defaults without it")
{
    Fixture fixture;
    fixture.registerRenderClasses();

    scene::ClassDescriptor lighting;
    lighting.name = fixture.atoms.intern("Lighting");
    lighting.super = fixture.instanceClass;
    lighting.defaultName = lighting.name;
    lighting.attachComponents = [](scene::World& w, core::InstanceId id)
    { w.lighting().add(id, scene::LightingComponent{}); };
    lighting.detachComponents = [](scene::World& w, core::InstanceId id) { w.lighting().remove(id); };
    const scene::ClassId lightingClass = fixture.classes.registerClass(lighting);

    const core::InstanceId workspace = fixture.world.create(fixture.workspaceClass);
    const core::InstanceId host = fixture.world.create(lightingClass);
    scene::LightingComponent* component = fixture.world.lighting().find(host);
    REQUIRE(component != nullptr);
    component->clockTime = 6.0f;
    component->geographicLatitude = 0.0f;

    render::RenderWorld snapshot;
    render::extract(fixture.world, workspace, host, kNoMeshes, 1.0f, 0.0f, snapshot);
    // Six in the morning at the equator: the sun is due east, which is +X.
    CHECK(nearly(snapshot.environment.sunDirection.x, 1.0f));
    CHECK(nearly(snapshot.environment.sunDirection.y, 0.0f));

    // An engine with no render module registers no Lighting at all, and an
    // invalid host must read as "use the defaults" rather than as an error.
    render::RenderWorld without;
    render::extract(fixture.world, workspace, core::InstanceId{}, kNoMeshes, 1.0f, 0.0f, without);
    CHECK(nearly(without.environment.sunDirection.y, 1.0f));
}
