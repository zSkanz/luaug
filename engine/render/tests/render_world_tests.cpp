#include "luaug/core/types.h"
#include "luaug/render/render_world.h"
#include "luaug/render/transform_history.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/components.h"
#include "luaug/scene/enum_registry.h"
#include "luaug/scene/world.h"

#include <array>
#include <doctest/doctest.h>
#include <ostream>

#include "luaug_test_nearly.h"

using namespace luaug;

namespace {

// These cases are all about the debug-part path, which needs no meshes and no
// camera. One shared empty library says that once rather than at every call.
const render::MeshLibrary kNoMeshes;

using luaug::testing::nearly;

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
        part.attachComponents = [](scene::World& w, core::InstanceId id) { w.parts().add(id, scene::PartComponent{}); };
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
    scene::ClassId pointLightClass = scene::InvalidClass;

    void registerRenderClasses()
    {
        scene::ClassDescriptor workspace;
        workspace.name = atoms.intern("Workspace");
        workspace.super = instanceClass;
        workspace.defaultName = workspace.name;
        workspace.attachComponents = [](scene::World& w, core::InstanceId id) {
            w.workspaces().add(id, scene::WorkspaceComponent{});
        };
        workspace.detachComponents = [](scene::World& w, core::InstanceId id) { w.workspaces().remove(id); };
        workspaceClass = classes.registerClass(workspace);

        scene::ClassDescriptor camera;
        camera.name = atoms.intern("Camera");
        camera.super = instanceClass;
        camera.defaultName = camera.name;
        camera.attachComponents = [](scene::World& w, core::InstanceId id) {
            w.cameras().add(id, scene::CameraComponent{});
        };
        camera.detachComponents = [](scene::World& w, core::InstanceId id) { w.cameras().remove(id); };
        cameraClass = classes.registerClass(camera);

        scene::ClassDescriptor meshPart;
        meshPart.name = atoms.intern("MeshPart");
        meshPart.super = partClass;
        meshPart.defaultName = meshPart.name;
        meshPart.attachComponents = [](scene::World& w, core::InstanceId id) {
            w.meshParts().add(id, scene::MeshPartComponent{});
        };
        meshPart.detachComponents = [](scene::World& w, core::InstanceId id) { w.meshParts().remove(id); };
        meshPartClass = classes.registerClass(meshPart);

        scene::ClassDescriptor pointLight;
        pointLight.name = atoms.intern("PointLight");
        pointLight.super = instanceClass;
        pointLight.defaultName = pointLight.name;
        pointLight.attachComponents = [](scene::World& w, core::InstanceId id) {
            w.pointLights().add(id, scene::PointLightComponent{});
        };
        pointLight.detachComponents = [](scene::World& w, core::InstanceId id) { w.pointLights().remove(id); };
        pointLightClass = classes.registerClass(pointLight);
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
    render::extract(fixture.world, root, core::InstanceId{}, kNoMeshes, 1.0f, 0.0f, nullptr, 0.0f, nullptr, snapshot);

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
    render::extract(fixture.world, root, core::InstanceId{}, kNoMeshes, 1.0f, 0.0f, nullptr, 0.0f, nullptr, snapshot);

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
    render::extract(fixture.world, {}, core::InstanceId{}, kNoMeshes, 1.0f, 0.0f, nullptr, 0.0f, nullptr, snapshot);

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
    render::extract(fixture.world, root, core::InstanceId{}, kNoMeshes, 1.0f, 0.0f, nullptr, 0.0f, nullptr, snapshot);
    render::extract(fixture.world, root, core::InstanceId{}, kNoMeshes, 1.0f, 0.0f, nullptr, 0.0f, nullptr, snapshot);
    CHECK(snapshot.parts.size() == 1);

    fixture.world.destroy(root);
    fixture.world.retireDestroyed();
    render::extract(fixture.world, root, core::InstanceId{}, kNoMeshes, 1.0f, 0.0f, nullptr, 0.0f, nullptr, snapshot);
    // A retired root is not a root: nothing resolves through it, so nothing is
    // in the world.
    CHECK(snapshot.parts.empty());
}

TEST_CASE("drawSortKey: pass, then pipeline, then material, then mesh, then depth")
{
    // Asserted on the key rather than on a sorted list, because a sorted list
    // only proves that *something* was consistent. These are the ordering
    // contract every backend inherits.
    CHECK(render::drawSortKey(0, 9, 9, 9, 600.0f) < render::drawSortKey(1, 0, 0, 0, 0.0f));
    CHECK(render::drawSortKey(0, 0, 9, 9, 600.0f) < render::drawSortKey(0, 1, 0, 0, 0.0f));
    CHECK(render::drawSortKey(0, 0, 0, 9, 600.0f) < render::drawSortKey(0, 0, 1, 0, 0.0f));

    // The geometry field, added at M7.5, and the property that makes an
    // instanced run possible at all: two pieces of geometry sharing one material
    // must NOT interleave by depth, or a run of one is chopped into pieces by
    // draws of the other (ADR 0043).
    CHECK(render::drawSortKey(0, 0, 0, 0, 600.0f) < render::drawSortKey(0, 0, 0, 1, 0.0f));

    // And SECTION is part of it, which is the half that was missing and was
    // caught by measuring rather than by reading: a mesh with two sections and
    // one material interleaved its halves by depth, so every run was one draw
    // long and the instanced path drew nothing.
    CHECK(render::drawGeometryKey(7, 0) != render::drawGeometryKey(7, 1));
    CHECK(render::drawGeometryKey(7, 1) < render::drawGeometryKey(8, 0));
    CHECK(render::drawGeometryKey(4095, 15) <= 0xFFFFu);
    CHECK(render::drawSortKey(0, 0, 0, 0, 1.0f) < render::drawSortKey(0, 0, 0, 0, 2.0f));

    // Quantized, so a sub-millimetre camera wobble cannot reorder two draws and
    // change a golden command stream. One unit is a centimetre.
    CHECK(render::drawSortKey(0, 0, 0, 0, 1.0f) == render::drawSortKey(0, 0, 0, 0, 1.000001f));
    CHECK(render::drawSortKey(0, 0, 0, 0, 1.0f) != render::drawSortKey(0, 0, 0, 0, 1.02f));

    // Saturating rather than wrapping: a draw beyond the quantization range must
    // sort last, not first.
    CHECK(render::drawSortKey(0, 0, 0, 0, 5000.0f) >= render::drawSortKey(0, 0, 0, 0, 654.0f));
    CHECK(render::drawSortKey(0, 0, 0, 0, -5.0f) == render::drawSortKey(0, 0, 0, 0, 0.0f));

    // Each field is bounded, and a value past its width must not climb into the
    // one above it -- a material index of 65,536 that reordered the passes would
    // be a scene drawn in the wrong order for a reason nobody could see.
    CHECK(render::drawSortKey(0, 0, 65536, 0, 0.0f) < render::drawSortKey(0, 1, 0, 0, 0.0f));
    CHECK(render::drawSortKey(0, 0, 0, 65536, 0.0f) < render::drawSortKey(0, 0, 1, 0, 0.0f));
}

TEST_CASE("extraction resolves the camera, and answers nothing without one")
{
    Fixture fixture;
    fixture.registerRenderClasses();
    const core::InstanceId workspace = fixture.world.create(fixture.workspaceClass);

    render::RenderWorld snapshot;

    SUBCASE("no camera means no view, and therefore no draws")
    {
        render::extract(fixture.world, workspace, core::InstanceId{}, kNoMeshes, 1.0f, 0.0f, nullptr, 0.0f, nullptr,
                        snapshot);
        CHECK_FALSE(snapshot.camera.valid);
        CHECK(snapshot.draws.empty());
    }

    SUBCASE("a destroyed camera is the same as no camera")
    {
        const core::InstanceId camera = fixture.cameraLookingDownNegativeZ(workspace);
        fixture.world.destroy(camera);
        render::extract(fixture.world, workspace, core::InstanceId{}, kNoMeshes, 1.0f, 0.0f, nullptr, 0.0f, nullptr,
                        snapshot);
        // The failure this guards is a renderer drawing through a camera whose
        // instance was retired, which is a stale InstanceId reaching the GPU.
        CHECK_FALSE(snapshot.camera.valid);
    }

    SUBCASE("the origin is the camera position, so the GPU never sees a world coordinate")
    {
        (void)fixture.cameraLookingDownNegativeZ(workspace, core::DVec3{1000000.0, 0.0, 0.0});
        render::extract(fixture.world, workspace, core::InstanceId{}, kNoMeshes, 1.0f, 0.0f, nullptr, 0.0f, nullptr,
                        snapshot);
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
        render::extract(fixture.world, workspace, core::InstanceId{}, meshes, 1.0f, 0.0f, nullptr, 0.0f, nullptr,
                        snapshot);
        CHECK(snapshot.draws.size() == 1);
        CHECK(snapshot.candidateDraws == 1);
        CHECK(snapshot.culledDraws == 0);
    }

    SUBCASE("behind the camera is culled, and counted as culled")
    {
        (void)fixture.meshPartAt(workspace, core::DVec3{0.0, 0.0, 10.0}, content);
        render::extract(fixture.world, workspace, core::InstanceId{}, meshes, 1.0f, 0.0f, nullptr, 0.0f, nullptr,
                        snapshot);
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
        render::extract(fixture.world, workspace, core::InstanceId{}, meshes, 1.0f, 0.0f, nullptr, 0.0f, nullptr,
                        snapshot);
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
    render::extract(fixture.world, workspace, core::InstanceId{}, meshes, 1.0f, 0.0f, nullptr, 0.0f, nullptr, snapshot);
    REQUIRE(snapshot.draws.size() == 3);
    CHECK(snapshot.draws[0].sortKey < snapshot.draws[1].sortKey);
    CHECK(snapshot.draws[1].sortKey < snapshot.draws[2].sortKey);
    CHECK(nearly(snapshot.draws[0].transform.m[3][2], -10.0f));
    CHECK(nearly(snapshot.draws[2].transform.m[3][2], -50.0f));

    // R10: the same world extracted twice gives the same order, every time.
    render::RenderWorld again;
    render::extract(fixture.world, workspace, core::InstanceId{}, meshes, 1.0f, 0.0f, nullptr, 0.0f, nullptr, again);
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
    lighting.attachComponents = [](scene::World& w, core::InstanceId id) {
        w.lighting().add(id, scene::LightingComponent{});
    };
    lighting.detachComponents = [](scene::World& w, core::InstanceId id) { w.lighting().remove(id); };
    const scene::ClassId lightingClass = fixture.classes.registerClass(lighting);

    const core::InstanceId workspace = fixture.world.create(fixture.workspaceClass);
    const core::InstanceId host = fixture.world.create(lightingClass);
    scene::LightingComponent* component = fixture.world.lighting().find(host);
    REQUIRE(component != nullptr);
    component->clockTime = 6.0f;
    component->geographicLatitude = 0.0f;

    render::RenderWorld snapshot;
    render::extract(fixture.world, workspace, host, kNoMeshes, 1.0f, 0.0f, nullptr, 0.0f, nullptr, snapshot);
    // Six in the morning at the equator: the sun is due east, which is +X.
    CHECK(nearly(snapshot.environment.sunDirection.x, 1.0f));
    CHECK(nearly(snapshot.environment.sunDirection.y, 0.0f));

    // An engine with no render module registers no Lighting at all, and an
    // invalid host must read as "use the defaults" rather than as an error.
    render::RenderWorld without;
    render::extract(fixture.world, workspace, core::InstanceId{}, kNoMeshes, 1.0f, 0.0f, nullptr, 0.0f, nullptr,
                    without);
    CHECK(nearly(without.environment.sunDirection.y, 1.0f));
}

TEST_CASE("Transparency reaches the draw, picks the pass, and reverses the sort")
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

    SUBCASE("an opaque part is in the opaque pass with alpha one")
    {
        (void)fixture.meshPartAt(workspace, core::DVec3{0.0, 0.0, -10.0}, content);
        render::extract(fixture.world, workspace, core::InstanceId{}, meshes, 1.0f, 0.0f, nullptr, 0.0f, nullptr,
                        snapshot);
        REQUIRE(snapshot.draws.size() == 1);
        CHECK_FALSE(snapshot.draws[0].transparent);
        CHECK(nearly(snapshot.draws[0].alpha, 1.0f));
        CHECK((snapshot.draws[0].sortKey >> 56) == render::kOpaquePass);
    }

    SUBCASE("Transparency reaches the draw as one minus itself")
    {
        const core::InstanceId id = fixture.meshPartAt(workspace, core::DVec3{0.0, 0.0, -10.0}, content);
        fixture.world.parts().find(id)->transparency = 0.25f;
        render::extract(fixture.world, workspace, core::InstanceId{}, meshes, 1.0f, 0.0f, nullptr, 0.0f, nullptr,
                        snapshot);
        REQUIRE(snapshot.draws.size() == 1);
        CHECK(snapshot.draws[0].transparent);
        CHECK(nearly(snapshot.draws[0].alpha, 0.75f));
        CHECK((snapshot.draws[0].sortKey >> 56) == render::kTransparentPass);
    }

    SUBCASE("the material's own alpha multiplies with the part's")
    {
        render::MeshLibrary::Entry translucent = entry;
        translucent.sectionMaterial = {0};
        render::RenderMaterial material;
        material.uniforms.baseColor[3] = 0.5f;
        translucent.materials = {material};
        render::MeshLibrary library;
        library.set(content, translucent);

        const core::InstanceId id = fixture.meshPartAt(workspace, core::DVec3{0.0, 0.0, -10.0}, content);
        fixture.world.parts().find(id)->transparency = 0.5f;
        render::extract(fixture.world, workspace, core::InstanceId{}, library, 1.0f, 0.0f, nullptr, 0.0f, nullptr,
                        snapshot);
        REQUIRE(snapshot.draws.size() == 1);
        // A glTF material can be see-through on its own and a script can make an
        // opaque mesh see-through; honouring one and not the other leaves a case
        // that renders wrong, so the two multiply.
        CHECK(nearly(snapshot.draws[0].alpha, 0.25f));
        CHECK(snapshot.draws[0].transparent);
    }

    SUBCASE("a fully transparent part is not drawn at all")
    {
        const core::InstanceId id = fixture.meshPartAt(workspace, core::DVec3{0.0, 0.0, -10.0}, content);
        fixture.world.parts().find(id)->transparency = 1.0f;
        render::extract(fixture.world, workspace, core::InstanceId{}, meshes, 1.0f, 0.0f, nullptr, 0.0f, nullptr,
                        snapshot);
        // Neither pass, and the debug path's existing rule: `submitWorld` skips
        // a part at `transparency >= 1`. A shadow cast by something nobody can
        // see is a defect whoever sees it reports.
        CHECK(snapshot.draws.empty());
    }

    SUBCASE("the transparent pass runs after the opaque one and sorts far to near")
    {
        (void)fixture.meshPartAt(workspace, core::DVec3{0.0, 0.0, -40.0}, content);
        const core::InstanceId near = fixture.meshPartAt(workspace, core::DVec3{0.0, 0.0, -10.0}, content);
        const core::InstanceId far = fixture.meshPartAt(workspace, core::DVec3{0.0, 0.0, -30.0}, content);
        fixture.world.parts().find(near)->transparency = 0.4f;
        fixture.world.parts().find(far)->transparency = 0.6f;

        render::extract(fixture.world, workspace, core::InstanceId{}, meshes, 1.0f, 0.0f, nullptr, 0.0f, nullptr,
                        snapshot);
        REQUIRE(snapshot.draws.size() == 3);

        // Opaque first, because the blended pass tests against the depth the
        // opaque one wrote.
        CHECK_FALSE(snapshot.draws[0].transparent);
        CHECK(snapshot.draws[1].transparent);
        CHECK(snapshot.draws[2].transparent);

        // And back to front within it, which is the opposite of the opaque
        // order. Read off the transform rather than off the key, so this fails
        // if the inversion is dropped even though the keys stay ordered.
        CHECK(nearly(snapshot.draws[1].transform.m[3][2], -30.0f));
        CHECK(nearly(snapshot.draws[2].transform.m[3][2], -10.0f));
    }
}

TEST_CASE("a MeshPart's wire box appears only while its mesh has not loaded")
{
    Fixture fixture;
    fixture.registerRenderClasses();
    const core::InstanceId workspace = fixture.world.create(fixture.workspaceClass);
    (void)fixture.cameraLookingDownNegativeZ(workspace);

    const core::NameAtom content = fixture.atoms.intern("asset://models/box.glb");
    (void)fixture.meshPartAt(workspace, core::DVec3{0.0, 0.0, -10.0}, content);

    render::RenderWorld snapshot;

    SUBCASE("nothing loaded: the box is the only sign the part exists")
    {
        render::extract(fixture.world, workspace, core::InstanceId{}, kNoMeshes, 1.0f, 0.0f, nullptr, 0.0f, nullptr,
                        snapshot);
        CHECK(snapshot.parts.size() == 1);
        CHECK(snapshot.draws.empty());
    }

    SUBCASE("loaded: the real geometry replaces it")
    {
        render::MeshLibrary meshes;
        render::MeshLibrary::Entry entry;
        entry.mesh = render::MeshHandle{0, 1};
        entry.bounds = core::AABB::fromCenterSize(core::Vec3{}, core::Vec3{1.0f, 1.0f, 1.0f});
        entry.sectionCount = 1;
        meshes.set(content, entry);

        render::extract(fixture.world, workspace, core::InstanceId{}, meshes, 1.0f, 0.0f, nullptr, 0.0f, nullptr,
                        snapshot);
        // `Size` does not scale a mesh -- the file's bounds do -- so a wire box
        // built from it would be a unit cube describing nothing on screen.
        CHECK(snapshot.parts.empty());
        CHECK(snapshot.draws.size() == 1);
    }

    SUBCASE("an ordinary Part still gets its box either way")
    {
        const core::InstanceId plain = fixture.world.create(fixture.partClass);
        (void)fixture.world.setParent(plain, workspace);
        render::extract(fixture.world, workspace, core::InstanceId{}, kNoMeshes, 1.0f, 0.0f, nullptr, 0.0f, nullptr,
                        snapshot);
        CHECK(snapshot.parts.size() == 2);
    }
}

// --- Render interpolation (D047, architecture.md §3) -------------------------
//
// The simulation is a fixed 60 Hz and a display is not, so a frame between two
// ticks has to be drawn between two states or the world steps while the camera
// does not. `Frame::alpha` has existed since M1 and had no consumer until M8.

TEST_CASE("a frame between two ticks is drawn between two states")
{
    Fixture fixture;
    const core::InstanceId root = fixture.world.create(fixture.folderClass);
    const core::InstanceId part = fixture.part(root);

    scene::PartComponent* component = fixture.world.parts().find(part);
    REQUIRE(component != nullptr);
    component->cframe.position = core::DVec3{0.0, 0.0, 0.0};

    // The tick boundary: capture where it is, then move it, exactly as the
    // frame loop does.
    render::TransformHistory history;
    history.capture(fixture.world);
    component->cframe.position = core::DVec3{10.0, 0.0, 0.0};

    render::RenderWorld halfway;
    render::extract(fixture.world, root, core::InstanceId{}, kNoMeshes, 1.0f, 0.0f, nullptr, 0.5f, &history, halfway);
    REQUIRE(halfway.parts.size() == 1);
    CHECK(nearly(static_cast<core::f32>(halfway.parts[0].cframe.position.x), 5.0f));

    // And the two ends are the two ticks themselves.
    render::RenderWorld atTick;
    render::extract(fixture.world, root, core::InstanceId{}, kNoMeshes, 1.0f, 0.0f, nullptr, 0.0f, &history, atTick);
    CHECK(atTick.parts[0].cframe.position.x == 10.0);

    render::RenderWorld nextTick;
    render::extract(fixture.world, root, core::InstanceId{}, kNoMeshes, 1.0f, 0.0f, nullptr, 1.0f, &history, nextTick);
    CHECK(nearly(static_cast<core::f32>(nextTick.parts[0].cframe.position.x), 10.0f));
}

TEST_CASE("no history is the world exactly as the last tick left it")
{
    // **The property every golden in this repository depends on.** A headless
    // run drives one fixed step per frame and passes zero, so nothing it records
    // can move by a fraction of a tick.
    Fixture fixture;
    const core::InstanceId root = fixture.world.create(fixture.folderClass);
    const core::InstanceId part = fixture.part(root);

    scene::PartComponent* component = fixture.world.parts().find(part);
    REQUIRE(component != nullptr);
    component->cframe.position = core::DVec3{0.0, 0.0, 0.0};

    render::TransformHistory history;
    history.capture(fixture.world);
    component->cframe.position = core::DVec3{10.0, 0.0, 0.0};

    render::RenderWorld withoutHistory;
    render::extract(fixture.world, root, core::InstanceId{}, kNoMeshes, 1.0f, 0.0f, nullptr, 0.5f, nullptr,
                    withoutHistory);
    CHECK(withoutHistory.parts[0].cframe.position.x == 10.0);
}

TEST_CASE("something that has just arrived is drawn where it is, not smeared in from nowhere")
{
    Fixture fixture;
    const core::InstanceId root = fixture.world.create(fixture.folderClass);

    // The capture happens BEFORE the part exists, which is what a chunk
    // streaming in looks like: there is no previous position to come from, and
    // interpolating from a stale slot would drag it across the world.
    render::TransformHistory history;
    history.capture(fixture.world);

    const core::InstanceId part = fixture.part(root);
    scene::PartComponent* component = fixture.world.parts().find(part);
    REQUIRE(component != nullptr);
    component->cframe.position = core::DVec3{600.0, 0.0, 0.0};

    render::RenderWorld snapshot;
    render::extract(fixture.world, root, core::InstanceId{}, kNoMeshes, 1.0f, 0.0f, nullptr, 0.5f, &history, snapshot);
    REQUIRE(snapshot.parts.size() == 1);
    CHECK(snapshot.parts[0].cframe.position.x == 600.0);
}

TEST_CASE("a slot reused by a different instance has no history")
{
    // The generation check, and it is not theoretical: the ECS reclaims slots,
    // so a part destroyed and another created can land on the same index within
    // one tick -- and the new one would inherit the old one's position.
    Fixture fixture;
    const core::InstanceId root = fixture.world.create(fixture.folderClass);
    const core::InstanceId first = fixture.part(root);
    scene::PartComponent* component = fixture.world.parts().find(first);
    REQUIRE(component != nullptr);
    component->cframe.position = core::DVec3{5.0, 0.0, 0.0};

    render::TransformHistory history;
    history.capture(fixture.world);

    const core::InstanceId stale{first.index, first.generation + 1};
    CHECK(history.previous(stale) == nullptr);
    CHECK(history.previous(first) != nullptr);
}

// --- D070: a history whose world has been replaced under it -----------------
//
// A snapshot restore preserves generations, precisely so that an `InstanceId`
// means the same thing after one as before it -- which is also what let a
// `TransformHistory` entry go on answering for a part the restore had moved
// metres. `world.h` states the obligation the frame loop owes here in so many
// words: these caches are "rebuilt from the tree rather than restored ... safe
// order: restore, then rebuild".
//
// The symptom was the flagship's character capsule flickering after a stop,
// interpolated every frame between where it had walked to and where it was put
// back -- with an alpha that goes on sweeping [0, 1) because the frame
// scheduler drains its accumulator whether or not the editor let a tick
// through.
TEST_CASE("clearing the history is what makes a restored world stop interpolating")
{
    Fixture fixture;
    const core::InstanceId root = fixture.world.create(fixture.folderClass);
    const core::InstanceId part = fixture.part(root);

    scene::PartComponent* component = fixture.world.parts().find(part);
    REQUIRE(component != nullptr);
    component->cframe.position = core::DVec3{40.0, 0.0, 0.0};

    render::TransformHistory history;
    history.capture(fixture.world);

    // The restore. Same id, same generation -- that is the whole difficulty --
    // and a position nowhere near what the history holds.
    component->cframe.position = core::DVec3{0.0, 0.0, 0.0};

    render::RenderWorld stale;
    render::extract(fixture.world, root, core::InstanceId{}, kNoMeshes, 1.0f, 0.0f, nullptr, 0.5f, &history, stale);
    REQUIRE(stale.parts.size() == 1);
    // Twenty metres from anywhere the world says it is, and a different number
    // every frame as alpha sweeps.
    CHECK(nearly(static_cast<core::f32>(stale.parts[0].cframe.position.x), 20.0f));

    history.clear();

    render::RenderWorld settled;
    render::extract(fixture.world, root, core::InstanceId{}, kNoMeshes, 1.0f, 0.0f, nullptr, 0.5f, &history, settled);
    REQUIRE(settled.parts.size() == 1);
    CHECK(settled.parts[0].cframe.position.x == 0.0);
}

// --- E2: the selection reaches the renderer as a flag on the draw -----------
//
// The silhouette pass walks the same draw list every other pass walks and draws
// the ones marked here. That the mark ARRIVES is the half of it a headless test
// can hold; what the mask and the dilate then make of it needs a device and, in
// the end, a person looking at it.
//
// The differential is the point (MASTER_PROMPT.md §8): extracting the same world
// twice, once with a selection and once without, must not produce the same draw
// list.
TEST_CASE("a selected instance comes out marked, and nothing else does")
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

    const core::InstanceId first = fixture.meshPartAt(workspace, core::DVec3{-3.0, 0.0, -10.0}, content);
    const core::InstanceId second = fixture.meshPartAt(workspace, core::DVec3{3.0, 0.0, -10.0}, content);
    (void)first;

    render::RenderWorld plain;
    render::extract(fixture.world, workspace, core::InstanceId{}, meshes, 1.0f, 0.0f, nullptr, 0.0f, nullptr, plain);
    REQUIRE(plain.draws.size() == 2);
    for (const render::DrawItem& draw : plain.draws)
        CHECK_FALSE(draw.outlined);

    const std::array<core::InstanceId, 1> selection{second};
    render::RenderWorld selected;
    render::extract(fixture.world, workspace, core::InstanceId{}, meshes, 1.0f, 0.0f, nullptr, 0.0f, nullptr, selected,
                    nullptr, selection);
    REQUIRE(selected.draws.size() == 2);

    core::usize marked = 0;
    for (const render::DrawItem& draw : selected.draws) {
        if (draw.outlined)
            ++marked;
    }
    CHECK(marked == 1);

    // And the two extractions differ, which is the whole assertion: a flag that
    // never reached the snapshot would leave these identical and every visual
    // check downstream would be looking at an image that could not have changed.
    bool differs = false;
    for (core::usize index = 0; index < plain.draws.size(); ++index)
        differs = differs || plain.draws[index].outlined != selected.draws[index].outlined;
    CHECK(differs);
}

TEST_CASE("a disabled light contributes nothing, and does not spend a budget slot")
{
    // **`Enabled` is not a brightness of zero, and this is the difference.** A
    // light at zero brightness is still a light: it is extracted, it is counted,
    // and it occupies one of the slots the renderer has to spend. A disabled one
    // is skipped before any of that, so turning a room's lights off gives the
    // rest of the scene the slots back.
    Fixture fixture;
    fixture.registerRenderClasses();
    const core::InstanceId workspace = fixture.world.create(fixture.workspaceClass);
    (void)fixture.cameraLookingDownNegativeZ(workspace);

    const core::InstanceId host = fixture.part(workspace);
    fixture.world.parts().find(host)->cframe.position = core::DVec3{0.0, 0.0, -5.0};

    const core::InstanceId lamp = fixture.world.create(fixture.pointLightClass);
    (void)fixture.world.setParent(lamp, host);

    render::MeshLibrary meshes;
    render::RenderWorld lit;
    render::extract(fixture.world, workspace, core::InstanceId{}, meshes, 1.0f, 0.0f, nullptr, 0.0f, nullptr, lit);
    REQUIRE(lit.lights.size() == 1);

    // Zero brightness is still a light, which is what makes the two properties
    // different questions rather than two spellings of one.
    fixture.world.pointLights().find(lamp)->brightness = 0.0f;
    render::RenderWorld dark;
    render::extract(fixture.world, workspace, core::InstanceId{}, meshes, 1.0f, 0.0f, nullptr, 0.0f, nullptr, dark);
    CHECK(dark.lights.size() == 1);

    fixture.world.pointLights().find(lamp)->enabled = false;
    render::RenderWorld off;
    render::extract(fixture.world, workspace, core::InstanceId{}, meshes, 1.0f, 0.0f, nullptr, 0.0f, nullptr, off);
    CHECK(off.lights.empty());
}
