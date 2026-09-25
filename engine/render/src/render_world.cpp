#include "luaug/render/render_world.h"

#include "luaug/render/lighting.h"
#include "luaug/render/shader_types.h"
#include "luaug/render/terrain_loader.h"
#include "luaug/render/voxel_loader.h"
#include "luaug/scene/components.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace luaug::render {
namespace {

constexpr f32 kPi = 3.14159265358979323846f;
constexpr f32 kDegreesToRadians = kPi / 180.0f;

// Walks up rather than down. A downward walk from the root would visit every
// instance in the world to find the parts; this visits only the parts, and pays
// the depth of each. A world where that is the wrong trade is a world with deep
// trees and few parts, which is not the shape any of this is built for.
[[nodiscard]] bool inWorld(const scene::World& world, core::InstanceId id, core::InstanceId root) noexcept
{
    for (core::InstanceId cursor = id; cursor.valid(); cursor = world.parentOf(cursor)) {
        if (cursor == root)
            return true;
    }
    return false;
}

// The generated mesh for a `Part`'s shape, or null before the loader has
// uploaded them -- which is the first frame of any run, and is why the debug
// wire box is still reachable.
[[nodiscard]] const MeshLibrary::Entry* primitiveEntry(const scene::World& world, const MeshLibrary& meshes,
                                                       core::i32 shape) noexcept
{
    const char* name = primitiveContent(shape);
    if (name == nullptr)
        return nullptr;
    const MeshLibrary::Entry* entry = meshes.find(world.atoms().lookup(name));
    return entry != nullptr && entry->mesh.valid() ? entry : nullptr;
}

// What one unit mesh has to be scaled by to become this part.
//
// **Not simply `Size`**, and the two exceptions are both about agreeing with the
// collider rather than with the size box:
//
//   `Ball` -- the physics shape is a SPHERE of the largest half-extent
//   (jolt_physics.cpp), so a non-uniform `Size` gives a ball that sticks out of
//   its own box. Rendering an ellipsoid there would mean seeing one thing and
//   colliding with another, which is the worse of the two wrongs.
//
//   `Capsule` -- the unit mesh is built at the character aspect (radius 0.5,
//   total height 2), so Y is halved. Its caps still stretch away from
//   `Size.y == 2 * max(Size.x, Size.z)`; primitives.h records why and what the
//   fix would be.
// **The shape a part is DRAWN as**: its own, except a `CharacterBody`, which
// is the capsule the physics sweeps whatever its `Shape` says -- it has no
// `Shape` of its own, so it read as the default block and a character was a
// box on screen and a capsule in the world (the owner's report).
[[nodiscard]] core::i32 drawnShape(const scene::World& world, core::InstanceId id,
                                   const scene::PartComponent& part) noexcept
{
    constexpr core::i32 Capsule = 3;
    return world.characterBodies().find(id) != nullptr ? Capsule : part.shape;
}

// How far a part may move in one tick and still be drawn gliding there: its
// largest side, and never less than half a metre, so a small fast thing -- a
// ball, a bullet -- keeps its interpolation. Past it, the move is a teleport.
[[nodiscard]] core::f64 teleportReach(Vec3 size) noexcept
{
    const f32 largest = std::fmax(size.x, std::fmax(size.y, size.z));
    return static_cast<core::f64>(std::fmax(largest, 0.5f));
}

[[nodiscard]] Vec3 primitiveScale(core::i32 shape, Vec3 size) noexcept
{
    switch (shape) {
    case 1: {
        const f32 diameter = std::fmax(size.x, std::fmax(size.y, size.z));
        return Vec3{diameter, diameter, diameter};
    }
    case 2: {
        const f32 diameter = std::fmax(size.x, size.z);
        return Vec3{diameter, size.y, diameter};
    }
    case 3: {
        const f32 diameter = std::fmax(size.x, size.z);
        return Vec3{diameter, size.y * 0.5f, diameter};
    }
    default:
        return size;
    }
}

} // namespace

u64 drawSortKey(u32 pass, u32 pipeline, u32 material, u32 geometry, f32 depth) noexcept
{
    // 8 bits of pass, 8 of pipeline, 16 of material, 16 of mesh, 16 of depth.
    // Most significant first, so a single integer compare orders a frame by the
    // thing that costs most to change: the pass, then the pipeline, then the
    // bind set, then the vertex buffers.
    //
    // The geometry field -- mesh and section together -- is what makes an
    // instanced run contiguous (ADR 0043). Its cost is that the opaque pass no
    // longer walks strictly front-to-back within one material, which was worth
    // something for early-Z and is now worth nothing, because the depth prepass
    // provides it exactly.
    const u64 passBits = static_cast<u64>(pass & 0xFFu) << 56;
    const u64 pipelineBits = static_cast<u64>(pipeline & 0xFFu) << 48;
    const u64 materialBits = static_cast<u64>(material & 0xFFFFu) << 32;
    const u64 geometryBits = static_cast<u64>(geometry & 0xFFFFu) << 16;

    // Quantized deliberately. A sub-millimetre wobble in a camera position must
    // not be able to reorder two draws, because the capture golden hashes the
    // order -- and a gate that fails on the last bit of a float is a gate people
    // switch off. One unit is a centimetre out to 655 metres, and saturates
    // beyond, which is far past where ordering within a pass matters.
    const f32 clamped = depth < 0.0f ? 0.0f : (depth > 655.0f ? 655.0f : depth);
    const u64 depthBits = static_cast<u64>(clamped * 100.0f);

    return passBits | pipelineBits | materialBits | geometryBits | (depthBits & 0xFFFFu);
}

const char* primitiveContent(core::i32 shape) noexcept
{
    // Indexed by `Enum.PartShape`'s own values, which enums.api.luau's header
    // makes the contract.
    switch (shape) {
    case 0:
        return "luaug://primitive/block";
    case 1:
        return "luaug://primitive/ball";
    case 2:
        return "luaug://primitive/cylinder";
    case 3:
        return "luaug://primitive/capsule";
    case 4:
        return "luaug://primitive/wedge";
    default:
        return nullptr;
    }
}

void TextureLibrary::set(core::NameAtom content, rhi::TextureHandle texture, core::u32 width, core::u32 height)
{
    const auto position =
        std::lower_bound(entries_.begin(), entries_.end(), content,
                         [](const Slot& slot, core::NameAtom value) { return slot.content.id < value.id; });
    if (position != entries_.end() && position->content == content) {
        position->texture = texture;
        position->width = width;
        position->height = height;
        return;
    }
    entries_.insert(position, Slot{content, texture, width, height});
}

core::Vec2 TextureLibrary::sizeOf(core::NameAtom content) const noexcept
{
    const auto position =
        std::lower_bound(entries_.begin(), entries_.end(), content,
                         [](const Slot& slot, core::NameAtom value) { return slot.content.id < value.id; });
    if (position == entries_.end() || !(position->content == content))
        return core::Vec2{0.0f, 0.0f};
    return core::Vec2{static_cast<f32>(position->width), static_cast<f32>(position->height)};
}

void TextureLibrary::clear() noexcept
{
    entries_.clear();
}

rhi::TextureHandle TextureLibrary::take(core::NameAtom content) noexcept
{
    const auto position =
        std::lower_bound(entries_.begin(), entries_.end(), content,
                         [](const Slot& slot, core::NameAtom wanted) noexcept { return slot.content.id < wanted.id; });
    if (position == entries_.end() || position->content != content)
        return rhi::TextureHandle{};

    const rhi::TextureHandle held = position->texture;
    entries_.erase(position);
    return held;
}

rhi::TextureHandle TextureLibrary::find(core::NameAtom content) const noexcept
{
    const auto position =
        std::lower_bound(entries_.begin(), entries_.end(), content,
                         [](const Slot& slot, core::NameAtom value) { return slot.content.id < value.id; });
    return position != entries_.end() && position->content == content ? position->texture : rhi::TextureHandle{};
}

void MeshLibrary::set(core::NameAtom content, const Entry& entry)
{
    const auto position =
        std::lower_bound(entries_.begin(), entries_.end(), content,
                         [](const Slot& slot, core::NameAtom value) { return slot.content.id < value.id; });
    if (position != entries_.end() && position->content == content) {
        position->entry = entry;
        return;
    }
    entries_.insert(position, Slot{content, entry});
}

void MeshLibrary::remove(core::NameAtom content)
{
    const auto position =
        std::lower_bound(entries_.begin(), entries_.end(), content,
                         [](const Slot& slot, core::NameAtom value) { return slot.content.id < value.id; });
    if (position != entries_.end() && position->content == content)
        entries_.erase(position);
}

void MeshLibrary::clear() noexcept
{
    entries_.clear();
}

const MeshLibrary::Entry* MeshLibrary::find(core::NameAtom content) const noexcept
{
    const auto position =
        std::lower_bound(entries_.begin(), entries_.end(), content,
                         [](const Slot& slot, core::NameAtom value) { return slot.content.id < value.id; });
    if (position == entries_.end() || !(position->content == content))
        return nullptr;
    return &position->entry;
}

namespace {

// The block an authored material describes (ADR 0090), with its alpha at one:
// a part's see-through goes into the DRAW's alpha, which is where the blended
// pass is decided -- the way `BasePart.Transparency` always did, and the way a
// material's own `Transparency` did not, which kept a translucent material out
// of the blended pass entirely.
//
// **Built every frame rather than cached across frames.** A material is a
// handful of floats and four texture lookups, and a cache would need
// invalidating on every file change and every clone write; the per-frame cache
// in `extract` already makes it once per distinct material.
//
// The texture handles come from the library, keyed by the URN each map names --
// interned by `MeshLoader::syncTextures`, which loads them. A map whose file has
// not loaded yet is absent and the surface draws untextured until it arrives: a
// surface that vanished while its texture loaded would be worse.
[[nodiscard]] RenderMaterial blockOf(const scene::World& world, const asset::MaterialProperties& material,
                                     const TextureLibrary* textures)
{
    RenderMaterial out;
    out.uniforms.baseColor[0] = material.color.r;
    out.uniforms.baseColor[1] = material.color.g;
    out.uniforms.baseColor[2] = material.color.b;
    out.uniforms.baseColor[3] = 1.0f;
    out.uniforms.emissive[0] = material.emissive.r;
    out.uniforms.emissive[1] = material.emissive.g;
    out.uniforms.emissive[2] = material.emissive.b;
    out.uniforms.metallicRoughnessNormalCutoff[0] = material.metalness;
    out.uniforms.metallicRoughnessNormalCutoff[1] = material.roughness;
    out.uniforms.metallicRoughnessNormalCutoff[2] = material.normalScale;
    // Read only in `Mask`, and zero everywhere else so the shader's test is
    // "cutoff > 0" rather than a second uniform saying which mode this is.
    out.uniforms.metallicRoughnessNormalCutoff[3] =
        material.alphaMode == static_cast<core::i32>(asset::MaterialAlphaMode::Mask) ? material.alphaCutoff : 0.0f;

    const auto mapOf = [&](const std::string& urn) -> rhi::TextureHandle {
        if (textures == nullptr || urn.empty())
            return rhi::TextureHandle{};
        const core::NameAtom atom = world.atoms().lookup(urn);
        return atom.valid() ? textures->find(atom) : rhi::TextureHandle{};
    };
    out.setMaps(mapOf(material.colorMap), mapOf(material.normalMap), mapOf(material.metallicRoughnessMap),
                mapOf(material.emissiveMap));
    return out;
}

// What a part draws with, split the way the draw loops consume it.
struct PartLook
{
    // True for the engine default material -- and, on a `MeshPart`, for the
    // file's own materials, which is what a mesh wearing nothing draws. Either
    // way the block is the built-in one, tinted by `tint`.
    bool builtIn = true;
    Color3 tint{1.0f, 1.0f, 1.0f};
    // Into the draw's alpha, as `BasePart.Transparency` always went.
    f32 transparency = 0.0f;
    // An authored material's block, when `builtIn` is false.
    RenderMaterial block;
    // What makes two looks one bind set: the material, and every value that
    // reaches the block. Transparency does not -- it is the draw's.
    core::NameAtom material;
    u32 clone = 0;
    Color3 color{1.0f, 1.0f, 1.0f};
    Color3 emissive{0.0f, 0.0f, 0.0f};
    f32 metalness = 0.0f;
    f32 roughness = 0.0f;
    f32 normalScale = 0.0f;
    f32 alphaCutoff = 0.0f;

    [[nodiscard]] bool sameBlock(const PartLook& other) const noexcept
    {
        return material == other.material && clone == other.clone && color == other.color &&
               emissive == other.emissive && metalness == other.metalness && roughness == other.roughness &&
               normalScale == other.normalScale && alphaCutoff == other.alphaCutoff;
    }
};

// One authored material, resolved and made into a block once per frame.
struct FrameMaterial
{
    core::NameAtom material;
    u32 clone = 0;
    asset::ResolvedMaterial resolved;
    RenderMaterial block;
};

[[nodiscard]] PartLook lookOf(const scene::World& world, const scene::PartComponent& part,
                              const TextureLibrary* textures, std::vector<FrameMaterial>& frame,
                              usize& lastFrameMaterial)
{
    PartLook look;
    const asset::MaterialOverrides& overrides = part.materialParameters;
    if (!part.material.valid()) {
        // **The engine default, which declares `Color` and `Transparency`**
        // and nothing else -- so this is `BasePart.Color` and
        // `BasePart.Transparency` exactly as they were, and every plain part
        // draws the pixels it drew before ADR 0090.
        if (overrides.has(asset::MaterialField::Color))
            look.tint = overrides.color;
        if (overrides.has(asset::MaterialField::Transparency))
            look.transparency = overrides.transparency;
        look.color = look.tint;
        return look;
    }

    // **The entry the previous part matched is asked first.** Parts that wear
    // one material tend to be made together and so sit together in the pool,
    // and the entries are unique by (material, clone), so the first match the
    // scan would find is this one whenever this one matches.
    const FrameMaterial* found = nullptr;
    if (lastFrameMaterial < frame.size() && frame[lastFrameMaterial].material == part.material &&
        frame[lastFrameMaterial].clone == part.materialClone) {
        found = &frame[lastFrameMaterial];
    }
    for (usize index = 0; found == nullptr && index < frame.size(); ++index) {
        if (frame[index].material == part.material && frame[index].clone == part.materialClone) {
            found = &frame[index];
            lastFrameMaterial = index;
        }
    }
    if (found == nullptr) {
        FrameMaterial made;
        made.material = part.material;
        made.clone = part.materialClone;
        made.resolved = world.resolveMaterial(part.material, part.materialClone);
        made.block = blockOf(world, made.resolved.properties, textures);
        frame.push_back(std::move(made));
        found = &frame.back();
        lastFrameMaterial = frame.size() - 1;
    }

    const asset::MaterialProperties& base = found->resolved.properties;
    const auto applied = static_cast<asset::MaterialFieldMask>(overrides.set & found->resolved.instanceParameters);
    const auto wants = [applied](asset::MaterialField field) { return (applied & asset::fieldBit(field)) != 0; };

    look.builtIn = false;
    look.material = part.material;
    look.clone = part.materialClone;
    look.color = wants(asset::MaterialField::Color) ? overrides.color : base.color;
    look.emissive = wants(asset::MaterialField::Emissive) ? overrides.emissive : base.emissive;
    look.metalness = wants(asset::MaterialField::Metalness) ? overrides.metalness : base.metalness;
    look.roughness = wants(asset::MaterialField::Roughness) ? overrides.roughness : base.roughness;
    look.normalScale = wants(asset::MaterialField::NormalScale) ? overrides.normalScale : base.normalScale;
    look.alphaCutoff = wants(asset::MaterialField::AlphaCutoff) ? overrides.alphaCutoff : base.alphaCutoff;
    look.transparency = wants(asset::MaterialField::Transparency) ? overrides.transparency : base.transparency;

    look.block = found->block;
    look.block.uniforms.baseColor[0] = look.color.r;
    look.block.uniforms.baseColor[1] = look.color.g;
    look.block.uniforms.baseColor[2] = look.color.b;
    look.block.uniforms.emissive[0] = look.emissive.r;
    look.block.uniforms.emissive[1] = look.emissive.g;
    look.block.uniforms.emissive[2] = look.emissive.b;
    look.block.uniforms.metallicRoughnessNormalCutoff[0] = look.metalness;
    look.block.uniforms.metallicRoughnessNormalCutoff[1] = look.roughness;
    look.block.uniforms.metallicRoughnessNormalCutoff[2] = look.normalScale;
    if (base.alphaMode == static_cast<core::i32>(asset::MaterialAlphaMode::Mask))
        look.block.uniforms.metallicRoughnessNormalCutoff[3] = look.alphaCutoff;
    return look;
}

// The built-in look's `Color`, multiplied into a block's base colour and
// emissive. On the engine default -- white, emitting nothing -- that IS the
// colour; on a `MeshPart` wearing nothing it tints the file's own materials,
// which is what `BasePart.Color` always did to an unimported mesh (ADR 0090).
//
// Emissive is tinted too: a red lamp made from a white glowing file is what
// somebody expects `Color` to do, and leaving emissive untinted would make the
// lit part red and the glow white.
// Whether two materials are one bind set but for the rgb of their base colour
// -- the one value the instanced stream carries per instance (D184).
[[nodiscard]] bool sameFamily(const RenderMaterial& a, const RenderMaterial& b) noexcept
{
    const GpuMaterialUniforms& x = a.uniforms;
    const GpuMaterialUniforms& y = b.uniforms;
    for (int i = 0; i < 4; ++i) {
        if (x.emissive[i] != y.emissive[i] ||
            x.metallicRoughnessNormalCutoff[i] != y.metallicRoughnessNormalCutoff[i] ||
            x.textureFlags[i] != y.textureFlags[i])
            return false;
    }
    return x.baseColor[3] == y.baseColor[3] && a.baseColor == b.baseColor && a.normal == b.normal &&
           a.metallicRoughness == b.metallicRoughness && a.emissive == b.emissive;
}

void tintBy(RenderMaterial& material, const Color3& color)
{
    material.uniforms.baseColor[0] *= color.r;
    material.uniforms.baseColor[1] *= color.g;
    material.uniforms.baseColor[2] *= color.b;
    material.uniforms.emissive[0] *= color.r;
    material.uniforms.emissive[1] *= color.g;
    material.uniforms.emissive[2] *= color.b;
}

} // namespace

void extract(const scene::World& world, core::InstanceId root, core::InstanceId lightingHost, const MeshLibrary& meshes,
             f32 viewportAspect, f32 shadowRadius, const AnimationSystem* animation, f32 alpha,
             const TransformHistory* history, RenderWorld& out, const ViewOverride* view,
             std::span<const core::InstanceId> outlined, const TextureLibrary* materials,
             std::span<const TerrainNodeDraw> terrainNodes)
{
    out.clear();
    if (!root.valid())
        return;

    // Linear for a handful, which is the common case and the cheap answer: a
    // search structure per frame would cost more on a list of four than the
    // walk does. **Sorted once and searched past that**, because a selected
    // model outlines every part inside it, and a castle of two thousand parts
    // walked once per draw is four million comparisons a frame.
    constexpr usize kLinearOutline = 16;
    const auto byId = [](core::InstanceId a, core::InstanceId b) {
        return a.index != b.index ? a.index < b.index : a.generation < b.generation;
    };
    static thread_local std::vector<core::InstanceId> sortedOutline;
    sortedOutline.clear();
    if (outlined.size() > kLinearOutline) {
        sortedOutline.assign(outlined.begin(), outlined.end());
        std::sort(sortedOutline.begin(), sortedOutline.end(), byId);
    }
    const auto isOutlined = [outlined, &byId](core::InstanceId id) {
        if (outlined.size() > kLinearOutline)
            return std::binary_search(sortedOutline.begin(), sortedOutline.end(), id, byId);
        for (const core::InstanceId selected : outlined) {
            if (selected == id)
                return true;
        }
        return false;
    };

    // Where a thing is at the fractional time this frame is being drawn at
    // (`transform_history.h`, D047). Every transform below goes through it, so
    // the whole frame -- camera, parts, meshes, the anchors lights hang off --
    // is evaluated at one time rather than at several.
    //
    // Anything with no previous transform is drawn where it is: something that
    // streamed in this tick has no earlier position to come from, and smearing
    // it in from a stale slot would be worse than the step it replaces.
    //
    // **A move longer than `teleport` in one tick is a teleport**, drawn where
    // it landed: a part's own largest side, for a part. The owner's snake moved
    // its tail to the front of its head with one `CFrame` write, and the frames
    // between two ticks drew the tail sliding through the body to get there.
    // Something that GLIDES -- a lift, a platform, a falling crate -- moves far
    // less than its own size per tick and is interpolated as before.
    const auto at = [&](core::InstanceId id, const CFrameD& current,
                        core::f64 teleport = std::numeric_limits<core::f64>::infinity()) -> CFrameD {
        if (history == nullptr || alpha <= 0.0f)
            return current;
        const CFrameD* earlier = history->previous(id);
        if (earlier == nullptr)
            return current;
        {
            const DVec3 step = current.position - earlier->position;
            if (step.x * step.x + step.y * step.y + step.z * step.z > teleport * teleport)
                return current;
        }

        // **Two early outs, and they are the difference between this costing
        // nothing and costing two and a half milliseconds.** `core::lerp` on a
        // CFrame slerps the rotation, which is a quaternion round trip with a
        // trig pair in it -- and an open world is thousands of parts that did
        // not move at all. Measured on `examples/10-open-world`: the median
        // frame at 1080p went from 3.5 ms to 6.1 ms with the slerp on every
        // part, and back with these two comparisons in front of it.
        if (earlier->rotation == current.rotation) {
            if (earlier->position == current.position)
                return current;
            // Moved without turning, which is most of what moves: a lift, a
            // sliding platform, anything driven along a path.
            const core::f64 t = static_cast<core::f64>(alpha);
            const DVec3 delta = current.position - earlier->position;
            CFrameD moved = current;
            moved.position = DVec3{earlier->position.x + delta.x * t, earlier->position.y + delta.y * t,
                                   earlier->position.z + delta.z * t};
            return moved;
        }

        return core::lerp(*earlier, current, static_cast<core::f64>(alpha));
    };

    // --- The camera, and therefore the space everything else is expressed in --
    //
    // Resolved first because `origin` is the camera's position and every f32
    // coordinate below is relative to it.
    core::InstanceId cameraId;
    if (const scene::WorkspaceComponent* workspace = world.workspaces().find(root); workspace != nullptr)
        cameraId = workspace->currentCamera;

    const bool cameraUsable = world.alive(cameraId) && !world.destroyed(cameraId);
    const scene::CameraComponent* worldCamera = cameraUsable ? world.cameras().find(cameraId) : nullptr;

    // An override is a camera the WORLD does not contain, so it is not
    // interpolated and not looked up: there is no previous tick for a transform
    // no tick ever wrote. That is correct rather than a shortcut -- an editor's
    // camera moves on the render clock, so a frame drawn at `t + alpha` from it
    // is already the camera's position at that instant.
    scene::CameraComponent overrideCamera =
        view != nullptr ? scene::CameraComponent{view->cframe, view->fieldOfView, view->nearPlane, view->farPlane}
                        : scene::CameraComponent{};
    if (view != nullptr) {
        overrideCamera.projection = view->projection;
        overrideCamera.orthographicSize = view->orthographicSize;
    }
    const scene::CameraComponent* camera = view != nullptr ? &overrideCamera : worldCamera;

    if (camera != nullptr) {
        const CFrameD cameraFrame = view != nullptr ? camera->cframe : at(cameraId, camera->cframe);
        out.camera.valid = true;
        out.camera.origin = cameraFrame.position;
        out.camera.nearPlane = camera->nearPlane;
        out.camera.farPlane = camera->farPlane;

        // Rotation only. The camera's position is `origin` and has already been
        // subtracted out of everything else, so leaving it in the view matrix
        // would apply it twice -- which looks correct near the world origin and
        // is catastrophically wrong a kilometre out, i.e. exactly where ADR 0014
        // says the f64 path has to hold.
        CFrameD orientation = cameraFrame;
        orientation.position = DVec3{};
        out.camera.view = core::toRenderMatrix(core::inverse(orientation), DVec3{});
        const f32 aspect = viewportAspect > 0.0f ? viewportAspect : 1.0f;
        // `Enum.CameraProjection`: 0 Perspective, 1 Orthographic (the 2D layer).
        const bool orthographic = camera->projection == 1;
        out.camera.projection =
            orthographic ? core::orthographic(camera->orthographicSize, aspect, camera->nearPlane, camera->farPlane)
                         : core::perspective(camera->fieldOfView * kDegreesToRadians, aspect, camera->nearPlane,
                                             camera->farPlane);
        // The jitter, folded into the projection's translation row -- which is
        // where a sub-pixel offset belongs, because it must move the whole frustum
        // rather than the geometry inside it. Zero everywhere today, so this is a
        // pair of additions of zero and every golden is unchanged. Under an
        // orthographic projection w is 1, so the row that offsets x and y is the
        // constant one rather than the one scaled by depth.
        const int jitterRow = orthographic ? 3 : 2;
        out.camera.projection.m[jitterRow][0] += out.camera.jitter.x;
        out.camera.projection.m[jitterRow][1] += out.camera.jitter.y;
        out.camera.viewProjection = out.camera.projection * out.camera.view;
        out.camera.frustum = core::frustumFromViewProjection(out.camera.viewProjection);
    }

    const DVec3 origin = out.camera.origin;

    // --- The environment ----------------------------------------------------

    if (const scene::LightingComponent* lighting = world.lighting().find(lightingHost); lighting != nullptr) {
        out.environment.sunDirection = sunDirection(lighting->clockTime, lighting->geographicLatitude);
        out.environment.ambient = lighting->ambient;
        out.environment.outdoorAmbient = lighting->outdoorAmbient;
        out.environment.sunBrightness = lighting->brightness;
        out.environment.fogColor = lighting->fogColor;
        out.environment.fogStart = lighting->fogStart;
        out.environment.fogEnd = lighting->fogEnd;
        out.environment.exposureCompensation = lighting->exposureCompensation;
        out.environment.environmentDiffuseScale = lighting->environmentDiffuseScale;
        out.environment.environmentSpecularScale = lighting->environmentSpecularScale;
        out.environment.shadowSoftness = lighting->shadowSoftness;
        out.environment.globalShadows = lighting->globalShadows;
        out.environment.autoExposure = lighting->autoExposure;
        out.environment.simTime = world.engineState().simTime;
    }

    // The look (ADR 0096): `Lighting`'s children and the current camera's. The
    // WORLD's camera, not an editor's view override -- a viewer's effects belong
    // to the camera the game looks through, and a tool looking at the scene is
    // looking at that.
    resolveLook(world, lightingHost, cameraUsable ? cameraId : core::InstanceId{}, out.look);
    if (out.look.sky.present && materials != nullptr) {
        out.look.sky.sunImage = materials->find(out.look.sky.sunTexture);
        out.look.sky.moonImage = materials->find(out.look.sky.moonTexture);
    }

    // --- Debug parts --------------------------------------------------------
    //
    // Still here, and not culled: the debug path is how anything is seen when
    // the real one is not working, so a bug in the culler must not be able to
    // hide it.
    //
    // A `MeshPart` whose mesh HAS loaded is the exception: the real geometry is
    // on screen, and a box over it would be a second outline of the same thing
    // in a different shape -- the box is the part's, and the mesh fills only as
    // much of it as its own bounds reach. When the mesh has NOT loaded it is
    // the opposite: the only sign the part exists at all, which is exactly what
    // the debug path is for.
    // **The five primitives' meshes, resolved once for the frame.** Asking
    // `primitiveEntry` per part hashes the shape's name into the atom table and
    // searches the library, twice a part across the two loops over the pool --
    // ten thousand string lookups a frame for a city of five thousand parts, for
    // an answer that has five possible values. Nothing below may call
    // `primitiveEntry` directly.
    constexpr core::i32 kPrimitiveShapes = 5;
    std::array<const MeshLibrary::Entry*, kPrimitiveShapes> primitives{};
    bool primitivesReady = true;
    for (core::i32 shape = 0; shape < kPrimitiveShapes; ++shape) {
        primitives[static_cast<usize>(shape)] = primitiveEntry(world, meshes, shape);
        primitivesReady = primitivesReady && primitives[static_cast<usize>(shape)] != nullptr;
    }
    const auto primitiveOf = [&primitives](core::i32 shape) -> const MeshLibrary::Entry* {
        return shape >= 0 && shape < kPrimitiveShapes ? primitives[static_cast<usize>(shape)] : nullptr;
    };

    world.parts().forEach([&](core::InstanceId id, const scene::PartComponent& part) {
        // **The common case leaves before any other lookup.** A plain part, with
        // a camera to draw it through and every primitive uploaded, is drawn by
        // the solid path below and never by this one -- which is where the
        // branches further down would have sent it too, after an ancestry walk
        // and three searches. The result is the same part skipped; only the
        // order of the tests moved, and each of them only returns.
        const scene::MeshPartComponent* mesh = world.meshParts().find(id);
        if (mesh == nullptr && out.camera.valid && primitivesReady && part.shape >= 0 && part.shape < kPrimitiveShapes)
            return;
        if (!inWorld(world, id, root))
            return;
        if (mesh != nullptr) {
            const MeshLibrary::Entry* loaded = meshes.find(mesh->meshContent);
            if (loaded != nullptr && loaded->mesh.valid())
                return;
        }
        // M6: a `Part` has a solid path now, so the wire box is what it falls
        // back to rather than what it is. The path stays because it is still how
        // anything is seen when the real one is not running -- and the guard is
        // the camera, because a world with no camera is exactly the case the
        // host draws with the debug path instead of with the renderer
        // (`engine.cpp`'s `useRenderer`). Without that guard `examples/00-clear`
        // went from three wire cubes to an empty screen.
        else if (out.camera.valid && primitiveOf(drawnShape(world, id, part)) != nullptr) {
            return;
        }
        // The debug path draws the surface's colour and see-through and
        // nothing else, which is what a wire box can show of a material.
        const asset::ResolvedMaterial surface = world.surfaceOf(part);
        out.parts.push_back(RenderPart{
            .cframe = at(id, part.cframe, teleportReach(part.size)),
            .size = part.size,
            .color = surface.properties.color,
            .transparency = surface.properties.transparency,
            .shape = drawnShape(world, id, part),
        });
    });

    // --- Lights -------------------------------------------------------------

    world.pointLights().forEach([&](core::InstanceId id, const scene::PointLightComponent& light) {
        // **Before anything else, including the budget.** That is what makes
        // `Enabled` different from a brightness of zero: a disabled light does
        // not occupy a slot, so turning a room's lights off gives the rest of
        // the scene the slots back.
        if (!light.enabled)
            return;
        if (!inWorld(world, id, root))
            return;
        // Where it shines from (`lightAnchorOf`): the part, interpolated, then
        // the attachment's offset from it.
        const std::optional<LightAnchor> anchored = lightAnchorOf(world, id);
        if (!anchored.has_value())
            return;
        const CFrameD anchor =
            (anchored->part.valid() ? at(anchored->part, anchored->partFrame) : anchored->partFrame) * anchored->offset;
        out.lights.push_back(RenderLight{
            .kind = LightKind::Point,
            .position = core::toVec3(anchor.position - origin),
            .direction = Vec3{0.0f, -1.0f, 0.0f},
            .color = light.color,
            .brightness = light.brightness,
            .range = light.range,
            // -1 admits every direction; 1 would be the narrowest cone
            // expressible, which is the opposite of what a point light is.
            .spotCosHalfAngle = -1.0f,
            .shadows = light.shadows,
        });
    });

    world.spotLights().forEach([&](core::InstanceId id, const scene::SpotLightComponent& light) {
        if (!light.enabled)
            return;
        if (!inWorld(world, id, root))
            return;
        // Where it shines from (`lightAnchorOf`): the part, interpolated, then
        // the attachment's offset from it.
        const std::optional<LightAnchor> anchored = lightAnchorOf(world, id);
        if (!anchored.has_value())
            return;
        const CFrameD anchor =
            (anchored->part.valid() ? at(anchored->part, anchored->partFrame) : anchored->partFrame) * anchored->offset;
        // A spot points along its anchor's LookVector, which is -Z (ADR
        // 0013's convention, stated in core/math.h).
        const Vec3 forward = core::transformDirection(anchor, Vec3{0.0f, 0.0f, -1.0f});
        out.lights.push_back(RenderLight{
            .kind = LightKind::Spot,
            .position = core::toVec3(anchor.position - origin),
            .direction = core::normalize(forward),
            .color = light.color,
            .brightness = light.brightness,
            .range = light.range,
            .spotCosHalfAngle = std::cos(light.angle * 0.5f * kDegreesToRadians),
            .shadows = light.shadows,
        });
    });

    // --- Draws --------------------------------------------------------------

    if (!out.camera.valid)
        return;

    // Frame-local material dedup. Declared here rather than inside the lambda
    // because it spans every MeshPart.
    struct ResolvedMaterial
    {
        core::NameAtom content;
        u32 local = 0;
        u32 slot = 0;
        // The part's own contribution to the block. Two parts sharing a mesh
        // and a section but tinted differently -- or wearing different
        // materials -- are two bind sets, and collapsing them onto one slot
        // would draw the second in the first one's colour. A file's own
        // section, drawn by nothing wearing it, is the built-in look.
        PartLook look;
    };
    std::vector<ResolvedMaterial> resolved;
    // The authored materials this frame draws, each resolved once.
    std::vector<FrameMaterial> frameMaterials;
    usize lastFrameMaterial = std::numeric_limits<usize>::max();

    // **Every material a part adds goes in through here**, which files it in
    // its family (D184): the first earlier material that is the same bind set
    // but for its base colour, or itself. A linear scan over the families,
    // which are few -- one per authored material and one for the built-in look
    // -- however many colours there are.
    std::vector<u32> familyHeads;
    const auto addMaterial = [&out, &familyHeads](const RenderMaterial& material) {
        for (auto index = static_cast<u32>(out.materialFamilies.size()); index < out.materials.size(); ++index)
            out.materialFamilies.push_back(index);
        const auto slot = static_cast<u32>(out.materials.size());
        u32 family = slot;
        for (const u32 head : familyHeads) {
            if (sameFamily(out.materials[head], material)) {
                family = head;
                break;
            }
        }
        if (family == slot)
            familyHeads.push_back(slot);
        out.materials.push_back(material);
        out.materialFamilies.push_back(family);
        return slot;
    };

    world.meshParts().forEach([&](core::InstanceId id, const scene::MeshPartComponent& meshPart) {
        if (!inWorld(world, id, root))
            return;
        const scene::PartComponent* part = world.parts().find(id);
        if (part == nullptr)
            return;

        const MeshLibrary::Entry* entry = meshes.find(meshPart.meshContent);
        // Skipped rather than substituted. A missing mesh that draws a
        // placeholder cube is a missing mesh nobody notices.
        if (entry == nullptr || !entry->mesh.valid())
            return;

        // `Size / MeshSize` is what makes `Size` mean the same thing on a
        // `MeshPart` as it does on a `Part`. Both default to one, so a scene
        // written before this existed produces exactly the matrix it did then --
        // and the multiply is skipped outright when they match, which is every
        // part nobody has resized.
        Mat4 transform = core::toRenderMatrix(at(id, part->cframe, teleportReach(part->size)), origin);
        const Vec3 stretch{part->size.x / meshPart.meshSize.x, part->size.y / meshPart.meshSize.y,
                           part->size.z / meshPart.meshSize.z};
        if (stretch.x != 1.0f || stretch.y != 1.0f || stretch.z != 1.0f)
            transform = transform * core::scaling(stretch);
        // Transformed by the SCALED matrix, so the cull box and the shadow
        // cast-in test both describe what is actually drawn.
        const AABB worldBounds = core::transformed(transform, entry->bounds);

        // The palette, appended once per MESH rather than once per section: a
        // character with four submeshes is one skeleton, and uploading its pose
        // four times would be four times the bytes for one answer. Truncated at
        // `kMaxSkinJoints` rather than refused -- a rig past the budget draws
        // its first sixty-four joints posed and the rest in bind, which is
        // visibly wrong in a way that says what happened.
        u32 firstBone = 0;
        u32 boneCount = 0;
        if (animation != nullptr) {
            if (const Pose* pose = animation->pose(id); pose != nullptr && !pose->palette.empty()) {
                firstBone = static_cast<u32>(out.bones.size());
                boneCount = static_cast<u32>(std::min<usize>(pose->palette.size(), kMaxSkinJoints));
                out.bones.insert(out.bones.end(), pose->palette.begin(),
                                 pose->palette.begin() + static_cast<std::ptrdiff_t>(boneCount));
            }
        }

        // What the part itself says about its surface, resolved once for the
        // whole mesh rather than once per section: `Material` is a property of
        // the PART, and every section of it gets the same answer.
        const PartLook look = lookOf(world, *part, materials, frameMaterials, lastFrameMaterial);

        for (u32 section = 0; section < entry->sectionCount; ++section) {
            // Resolved before the cull test so that `material` is meaningful
            // on every candidate, and deduplicated across the frame by
            // (content, local index, the part's material and its tint) so the
            // sort key can group draws that share a bind set. A linear scan,
            // because a scene has a handful of materials and an unordered
            // container's iteration order must not reach observable output
            // (R10).
            u32 localMaterial = 0;
            if (section < entry->sectionMaterial.size())
                localMaterial = entry->sectionMaterial[section];

            ++out.candidateDraws;
            // Culled against the whole mesh's bounds rather than the
            // section's: the section bounds are in the library and this is
            // the loop that would have to fetch them per section. Whole-mesh
            // is conservative in the direction that never drops geometry,
            // and a per-section test is the optimization to make when a
            // profile says the draws it saves are worth the fetch.
            const bool visible = core::intersects(out.camera.frustum, worldBounds);
            if (!visible) {
                ++out.culledDraws;
                // Kept anyway when it is close enough to cast into view. The
                // first version dropped it, which deleted the shadow of
                // everything behind the camera -- an image that looks right
                // until you notice what is missing from it.
                const Vec3 toCentre = core::center(worldBounds);
                const f32 reach = shadowRadius + 0.5f * core::length(core::size(worldBounds));
                if (core::length(toCentre) > reach)
                    continue;
            }

            u32 materialSlot = 0;
            bool found = false;
            for (u32 index = 0; index < static_cast<u32>(resolved.size()); ++index) {
                if (resolved[index].content == meshPart.meshContent && resolved[index].local == localMaterial &&
                    resolved[index].look.builtIn == look.builtIn && resolved[index].look.sameBlock(look)) {
                    materialSlot = resolved[index].slot;
                    found = true;
                    break;
                }
            }
            if (!found) {
                materialSlot = static_cast<u32>(out.materials.size());
                RenderMaterial block = localMaterial < entry->materials.size()
                                           ? entry->materials[localMaterial]
                                           // A section whose material the importer did not
                                           // produce still draws, in the default: a mesh that
                                           // vanishes because one primitive lacked a material
                                           // is harder to diagnose than a white one.
                                           : RenderMaterial{};
                // A material the part wears REPLACES the file's block, for
                // every section of it. A mesh wearing none keeps what its own
                // file described, tinted by the default's `Color` -- which is
                // what an unimported mesh has always looked like.
                if (look.builtIn)
                    tintBy(block, look.tint);
                else
                    block = look.block;
                (void)addMaterial(block);
                resolved.push_back(ResolvedMaterial{meshPart.meshContent, localMaterial, materialSlot, look});
            }

            // The two sources of see-through, multiplied: the part's own
            // `Transparency` and whatever alpha the material arrived with.
            // The shader computes the same product, and it has to -- this is
            // what the draw was sorted by.
            const f32 opacity = (1.0f - look.transparency) * out.materials[materialSlot].uniforms.baseColor[3];
            // Fully invisible draws nothing at all, in either pass. That is
            // the debug path's existing rule (`submitWorld` skips a part at
            // `transparency >= 1`), and consistency with it matters more
            // here than the shadow question the roadmap left closed: a
            // shadow cast by something nobody can see is a defect whoever
            // sees it will report.
            if (opacity <= 0.0f)
                continue;

            const bool transparent = opacity < 1.0f;
            const Vec3 centre = core::center(worldBounds);
            const f32 depth = core::length(centre);
            // Back-to-front for the blended pass, and the inversion happens
            // HERE rather than as a reversed walk in a backend -- that is
            // M4's third design constraint, and a reversed walk is work
            // every future backend would repeat.
            const f32 sortDepth = transparent ? kMaxSortDepth - depth : depth;
            out.draws.push_back(DrawItem{
                // Zero for a transparent draw: see `drawSortKey`.
                // The opaque pass sorts by the material's FAMILY, so parts
                // that differ only by colour stay adjacent and batch (D184).
                .sortKey = drawSortKey(transparent ? kTransparentPass : kOpaquePass,
                                       boneCount > 0 ? kSkinnedPipeline : kStaticPipeline,
                                       transparent ? materialSlot : out.familyOf(materialSlot),
                                       transparent ? 0u : drawGeometryKey(entry->mesh.index, section), sortDepth),
                .transform = transform,
                .mesh = entry->mesh,
                .section = section,
                .material = materialSlot,
                .alpha = opacity,
                .transparent = transparent,
                .boundsCenter = core::center(worldBounds),
                .boundsRadius = 0.5f * core::length(core::size(worldBounds)),
                .inCameraFrustum = visible,
                .firstBone = firstBone,
                .boneCount = boneCount,
                .outlined = isOutlined(id),
                .terrain = false,
                .voxelBlock = false,
            });
        }
    });

    // --- Terrain (ADR 0082) ---------------------------------------------------
    //
    // **The ground is meshes**, one per node of each terrain's level-of-detail
    // quadtree, which `TerrainLoader` built and chose for this camera and hands
    // in as `terrainNodes`. This turns each into draws.
    //
    // **Terrain is not made of `MeshPart`s**, and the reason is mechanical:
    // `attachPartComponents` adds a `RigidBodyComponent` to every `BasePart`
    // with no condition, so generated parts would be phantom bodies in the
    // broadphase and rows in the Explorer. So the meshes are filed under a URN
    // of their own and this emits their draws directly.
    for (const TerrainNodeDraw& node : terrainNodes) {
        const scene::TerrainComponent* component = world.terrains().find(node.terrain);
        if (component == nullptr || !inWorld(world, node.terrain, root))
            continue;
        const scene::TerrainComponent& terrain = *component;
        const core::NameAtom urn = node.urn;
        {
            const MeshLibrary::Entry* entry = meshes.find(urn);
            // Skipped rather than substituted, exactly as a missing mesh is.
            if (entry == nullptr || !entry->mesh.valid())
                continue;

            // **The mesher works in the field's own space**, so a node's only
            // placement is the terrain's own origin -- plus the floating-origin
            // rebase every other draw gets.
            core::CFrameD placement;
            placement.position = terrain.origin;
            const Mat4 transform = core::toRenderMatrix(placement, origin);
            const AABB worldBounds = core::transformed(transform, entry->bounds);
            const bool visible = core::intersects(out.camera.frustum, worldBounds);

            for (u32 section = 0; section < entry->sectionCount; ++section) {
                // A skirt on a side whose neighbour is not coarser covers no
                // crack, and is left out (the one-sided skirt).
                if (section < entry->sectionSide.size() && entry->sectionSide[section] != 0 &&
                    (entry->sectionSide[section] & node.skirts) == 0)
                    continue;
                u32 localMaterial = 0;
                if (section < entry->sectionMaterial.size())
                    localMaterial = entry->sectionMaterial[section];

                // Deduplicated across the frame by (urn, local index) so the
                // sort key groups draws that share a bind set. A linear scan,
                // because an unordered container's iteration order must not
                // reach observable output (R10).
                u32 materialSlot = 0;
                bool found = false;
                for (usize slot = 0; slot < resolved.size(); ++slot) {
                    if (resolved[slot].content == urn && resolved[slot].local == localMaterial &&
                        resolved[slot].look.builtIn && resolved[slot].look.sameBlock(PartLook{})) {
                        materialSlot = static_cast<u32>(slot);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    materialSlot = static_cast<u32>(out.materials.size());
                    out.materials.push_back(localMaterial < entry->materials.size() ? entry->materials[localMaterial]
                                                                                    : RenderMaterial{});
                    resolved.push_back(ResolvedMaterial{urn, localMaterial, materialSlot, PartLook{}});
                }

                const Vec3 centre = core::center(worldBounds);
                const f32 depth = core::length(centre);
                out.draws.push_back(DrawItem{
                    .sortKey = drawSortKey(kOpaquePass, kStaticPipeline, materialSlot,
                                           drawGeometryKey(entry->mesh.index, section), depth),
                    .transform = transform,
                    .mesh = entry->mesh,
                    .section = section,
                    .material = materialSlot,
                    .alpha = 1.0f,
                    .transparent = false,
                    .boundsCenter = centre,
                    .boundsRadius = 0.5f * core::length(core::size(worldBounds)),
                    .inCameraFrustum = visible,
                    .firstBone = 0,
                    .boneCount = 0,
                    // **Never outlined, selected or not.** The outline draws
                    // through what is in front of it, and a node's skirts are
                    // buried in the ground: outlined, they showed through a dug
                    // crater as a tinted lid and a row of boxes (D161). A line
                    // round the whole world would say nothing anyway.
                    .outlined = false,
                    .terrain = true,
                    .voxelBlock = false,
                });
            }
        }
    }

    // --- The block world (V1) --------------------------------------------------
    //
    // `VoxelService` is a service, not a Workspace descendant, so it is found by
    // its component rather than walked to. Chunks are meshed in the grid's own
    // space, which sits at the world origin; the only placement is the
    // floating-origin rebase every draw gets.
    world.voxels().forEach([&](core::InstanceId, const scene::VoxelComponent& voxels) {
        out.voxelColors.clear();
        out.voxelBlockSize = voxels.blockSize;
        out.voxelColors.reserve(voxels.types.size());
        out.voxelTextures.clear();
        for (const scene::VoxelBlockType& type : voxels.types) {
            out.voxelColors.push_back(RenderWorld::VoxelColors{type.color, type.side, type.bottom,
                                                               type.opacity == 2 ? 1.0f - type.transparency : 1.0f});
            const auto handle = [materials](core::NameAtom urn) {
                return materials != nullptr && urn.valid() ? materials->find(urn) : rhi::TextureHandle{};
            };
            out.voxelTextures.push_back(
                RenderWorld::VoxelTextures{handle(type.texture), handle(type.sideTexture), handle(type.bottomTexture)});
        }

        u32 materialSlot = 0xFFFFFFFFu;
        const Mat4 transform = core::toRenderMatrix(core::CFrameD{}, origin);
        for (const asset::VoxelChunkKey key : voxels.grid.chunkKeys()) {
            // The opaque faces; the cutout ones, which the depth prepass must not
            // see -- it would write a leaf's holes as solid -- and the translucent
            // ones, as a blended draw that sorts with every other transparent
            // surface, back to front.
            for (int kind = 0; kind < 3; ++kind) {
                const bool cutout = kind == 1;
                const bool translucent = kind == 2;
                const core::NameAtom urn = world.atoms().lookup(translucent ? voxelTranslucentUrn(key)
                                                                : cutout    ? voxelCutoutUrn(key)
                                                                            : voxelChunkUrn(key));
                if (!urn.valid())
                    continue;
                const MeshLibrary::Entry* entry = meshes.find(urn);
                if (entry == nullptr || !entry->mesh.valid())
                    continue;
                if (materialSlot == 0xFFFFFFFFu) {
                    materialSlot = static_cast<u32>(out.materials.size());
                    out.materials.push_back(RenderMaterial{});
                }
                const AABB worldBounds = core::transformed(transform, entry->bounds);
                const Vec3 centre = core::center(worldBounds);
                out.draws.push_back(DrawItem{
                    .sortKey = drawSortKey(translucent ? kTransparentPass : kOpaquePass, kStaticPipeline, materialSlot,
                                           drawGeometryKey(entry->mesh.index, 0), core::length(centre)),
                    .transform = transform,
                    .mesh = entry->mesh,
                    .section = 0,
                    .material = materialSlot,
                    .alpha = 1.0f,
                    .transparent = translucent,
                    .boundsCenter = centre,
                    .boundsRadius = 0.5f * core::length(core::size(worldBounds)),
                    .inCameraFrustum = core::intersects(out.camera.frustum, worldBounds),
                    .firstBone = 0,
                    .boneCount = 0,
                    .outlined = false,
                    .terrain = false,
                    .voxelBlock = true,
                    .cutout = cutout,
                });
            }
        }
    });

    // --- Decals (F2) --------------------------------------------------------
    //
    // A box each, relative to the parent part when there is one so a mark on a
    // moving crate moves with it. Skipped when fully transparent, and when the
    // box is nowhere near the view -- the renderer draws every one it is given.
    world.decals().forEach([&](core::InstanceId id, const scene::DecalComponent& decal) {
        if (!inWorld(world, id, root) || decal.transparency >= 1.0f)
            return;
        CFrameD frame = decal.cframe;
        if (const scene::PartComponent* part = world.parts().find(world.parentOf(id)); part != nullptr)
            frame = at(world.parentOf(id), part->cframe) * decal.cframe;
        RenderDecal drawn;
        drawn.boxToWorld = core::toRenderMatrixScaled(frame, origin, decal.size);
        const AABB bounds =
            core::transformed(drawn.boxToWorld, AABB{Vec3{-0.5f, -0.5f, -0.5f}, Vec3{0.5f, 0.5f, 0.5f}});
        if (!core::intersects(out.camera.frustum, bounds))
            return;
        drawn.worldToBox = core::inverse(drawn.boxToWorld);
        drawn.texture =
            materials != nullptr && decal.texture.valid() ? materials->find(decal.texture) : rhi::TextureHandle{};
        drawn.color = decal.color;
        drawn.opacity = 1.0f - decal.transparency;
        drawn.axis = core::transformDirection(drawn.boxToWorld, Vec3{0.0f, 0.0f, 1.0f});
        out.decals.push_back(drawn);
    });

    // --- Sprites (the 2D layer) ----------------------------------------------
    //
    // A `Part2D` is one sprite, and a tilemap one per painted tile in a block
    // near the view. Both lie on the world's z = 0 plane. Culled by the camera's
    // frustum -- a block at a time for a tilemap, so a level of a thousand
    // blocks costs a thousand box tests and draws only what is on screen.
    {
        struct Keyed
        {
            core::i32 zIndex = 0;
            // Tilemaps beneath parts at the same `ZIndex`: ground behind what
            // stands on it.
            bool part = false;
            RenderSprite sprite;
        };
        std::vector<Keyed> keyed;
        const f32 planeZ = static_cast<f32>(-origin.z);
        const auto visible = [&](f64 x0, f64 y0, f64 x1, f64 y1) {
            const AABB bounds{Vec3{static_cast<f32>(x0 - origin.x), static_cast<f32>(y0 - origin.y), planeZ - 0.01f},
                              Vec3{static_cast<f32>(x1 - origin.x), static_cast<f32>(y1 - origin.y), planeZ + 0.01f}};
            return core::intersects(out.camera.frustum, bounds);
        };
        const auto textureOf = [&](core::NameAtom image) {
            return materials != nullptr && image.valid() ? materials->find(image) : rhi::TextureHandle{};
        };

        world.parts2d().forEach([&](core::InstanceId id, const scene::Part2DComponent& part) {
            if (!inWorld(world, id, root) || part.transparency >= 1.0f)
                return;
            // A circle is drawn as wide as its smaller side, as it collides.
            core::Vec2 half{part.size.x * 0.5f, part.size.y * 0.5f};
            if (part.shape == 1)
                half = core::Vec2{std::min(half.x, half.y), std::min(half.x, half.y)};
            const f64 hx = static_cast<f64>(half.x);
            const f64 hy = static_cast<f64>(half.y);
            const f64 reach = std::sqrt(hx * hx + hy * hy);
            const f64 cx = static_cast<f64>(part.position.x);
            const f64 cy = static_cast<f64>(part.position.y);
            if (!visible(cx - reach, cy - reach, cx + reach, cy + reach))
                return;

            Keyed entry;
            entry.zIndex = part.zIndex;
            entry.part = true;
            RenderSprite& sprite = entry.sprite;
            sprite.rect[0] = static_cast<f32>(cx - hx - origin.x);
            sprite.rect[1] = static_cast<f32>(cy - hy - origin.y);
            sprite.rect[2] = static_cast<f32>(cx + hx - origin.x);
            sprite.rect[3] = static_cast<f32>(cy + hy - origin.y);
            sprite.z = planeZ;
            const f32 angle = part.rotation * kDegreesToRadians;
            sprite.cosine = part.rotation == 0.0f ? 1.0f : std::cos(angle);
            sprite.sine = part.rotation == 0.0f ? 0.0f : std::sin(angle);
            sprite.shape = part.shape;
            sprite.texture = textureOf(part.image);
            // A pixel rectangle of the image, where one is set and the size of
            // the image is known; otherwise all of it.
            const core::Vec2 pixels = materials != nullptr ? materials->sizeOf(part.image) : core::Vec2{};
            f32 left = 0.0f;
            f32 top = 0.0f;
            f32 right = 1.0f;
            f32 bottom = 1.0f;
            if (sprite.texture.valid() && pixels.x > 0.0f && pixels.y > 0.0f && part.imageRectSize.x > 0.0f &&
                part.imageRectSize.y > 0.0f) {
                left = part.imageRectOffset.x / pixels.x;
                top = part.imageRectOffset.y / pixels.y;
                right = (part.imageRectOffset.x + part.imageRectSize.x) / pixels.x;
                bottom = (part.imageRectOffset.y + part.imageRectSize.y) / pixels.y;
            }
            sprite.uv[0] = part.flipX ? right : left;
            sprite.uv[1] = part.flipY ? bottom : top;
            sprite.uv[2] = part.flipX ? left : right;
            sprite.uv[3] = part.flipY ? top : bottom;
            sprite.color[0] = part.color.r;
            sprite.color[1] = part.color.g;
            sprite.color[2] = part.color.b;
            sprite.color[3] = 1.0f - part.transparency;
            sprite.nearest = part.filter == 1;
            keyed.push_back(entry);
        });

        world.tilemaps2d().forEach([&](core::InstanceId id, const scene::Tilemap2DComponent& tilemap) {
            if (!inWorld(world, id, root) || tilemap.chunks.empty())
                return;
            const rhi::TextureHandle texture = textureOf(tilemap.tileset);
            const core::Vec2 pixels = materials != nullptr ? materials->sizeOf(tilemap.tileset) : core::Vec2{};
            // Tiles per row of the tileset; zero draws every tile as its colour.
            const core::i32 columns = texture.valid() && tilemap.tileSize.x > 0.0f && tilemap.tileSize.y > 0.0f
                                          ? static_cast<core::i32>(pixels.x / tilemap.tileSize.x)
                                          : 0;
            // A hair inside each tile's rectangle, so a filter never reaches the
            // neighbouring tile: half a texel for Linear, a hundredth for
            // Nearest, where it only has to beat rounding.
            const f32 inset = tilemap.filter == 1 ? 0.01f : 0.5f;
            const f64 cell = static_cast<f64>(tilemap.cellSize);
            const f64 baseX = static_cast<f64>(tilemap.position.x);
            const f64 baseY = static_cast<f64>(tilemap.position.y);
            const f64 span = static_cast<f64>(scene::TileChunkEdge) * cell;
            for (const auto& [key, chunk] : tilemap.chunks) {
                const f64 chunkX = baseX + static_cast<f64>(key.x) * span;
                const f64 chunkY = baseY + static_cast<f64>(key.y) * span;
                if (!visible(chunkX, chunkY, chunkX + span, chunkY + span))
                    continue;
                for (core::i32 ly = 0; ly < scene::TileChunkEdge; ++ly) {
                    for (core::i32 lx = 0; lx < scene::TileChunkEdge; ++lx) {
                        const core::u16 tile = chunk[static_cast<usize>(ly * scene::TileChunkEdge + lx)];
                        if (tile == 0)
                            continue;
                        const core::i32 x = key.x * scene::TileChunkEdge + lx;
                        const core::i32 y = key.y * scene::TileChunkEdge + ly;
                        Keyed entry;
                        entry.zIndex = tilemap.zIndex;
                        RenderSprite& sprite = entry.sprite;
                        // Each edge from the same expression its neighbour uses.
                        sprite.rect[0] = static_cast<f32>(baseX + static_cast<f64>(x) * cell - origin.x);
                        sprite.rect[1] = static_cast<f32>(baseY + static_cast<f64>(y) * cell - origin.y);
                        sprite.rect[2] = static_cast<f32>(baseX + static_cast<f64>(x + 1) * cell - origin.x);
                        sprite.rect[3] = static_cast<f32>(baseY + static_cast<f64>(y + 1) * cell - origin.y);
                        sprite.z = planeZ;
                        if (columns > 0) {
                            const core::i32 index = static_cast<core::i32>(tile) - 1;
                            const f32 column = static_cast<f32>(index % columns);
                            const f32 row = static_cast<f32>(index / columns);
                            sprite.uv[0] = (column * tilemap.tileSize.x + inset) / pixels.x;
                            sprite.uv[1] = (row * tilemap.tileSize.y + inset) / pixels.y;
                            sprite.uv[2] = ((column + 1.0f) * tilemap.tileSize.x - inset) / pixels.x;
                            sprite.uv[3] = ((row + 1.0f) * tilemap.tileSize.y - inset) / pixels.y;
                            sprite.texture = texture;
                        }
                        sprite.color[0] = tilemap.color.r;
                        sprite.color[1] = tilemap.color.g;
                        sprite.color[2] = tilemap.color.b;
                        sprite.nearest = tilemap.filter == 1;
                        keyed.push_back(entry);
                    }
                }
            }
        });

        // Stable, so what the sort leaves tied keeps the pools' order (R10).
        std::stable_sort(keyed.begin(), keyed.end(), [](const Keyed& a, const Keyed& b) {
            return a.zIndex != b.zIndex ? a.zIndex < b.zIndex : (!a.part && b.part);
        });
        out.sprites.reserve(keyed.size());
        for (const Keyed& entry : keyed)
            out.sprites.push_back(entry.sprite);
    }

    // --- Solid parts (M6) ---------------------------------------------------
    //
    // **The renderer changes not at all for this**, which is the answer M4's
    // "engine-generated geometry must be able to reach the renderer" constraint
    // was written to get: these are ordinary `DrawItem`s naming ordinary
    // `MeshHandle`s, and colour, `Transparency` and the blended pass come free
    // because of it.
    //
    // The material is the part's own colour rather than a file's, deduplicated
    // across the frame by that colour so the sort key still groups draws that
    // share a bind set. A linear scan for the same reason the mesh loop uses
    // one: a scene has a handful of distinct colours, and an unordered
    // container's iteration order must not reach observable output (R10).
    struct ResolvedPartMaterial
    {
        PartLook look;
        u32 slot;
    };
    std::vector<ResolvedPartMaterial> partMaterials;
    usize lastPartMaterial = std::numeric_limits<usize>::max();

    world.parts().forEach([&](core::InstanceId id, const scene::PartComponent& part) {
        if (!inWorld(world, id, root))
            return;
        // A `MeshPart` is a `BasePart` and is in this pool too; its geometry
        // came from a file and the loop above already drew it.
        if (world.meshParts().find(id) != nullptr)
            return;

        // Once per part: it is a pool search, and it was asked twice here.
        const core::i32 shape = drawnShape(world, id, part);
        const MeshLibrary::Entry* entry = primitiveOf(shape);
        if (entry == nullptr)
            return;

        const PartLook look = lookOf(world, part, materials, frameMaterials, lastFrameMaterial);
        const f32 opacity = 1.0f - look.transparency;
        if (opacity <= 0.0f)
            return;

        const Mat4 transform = core::toRenderMatrixScaled(at(id, part.cframe, teleportReach(part.size)), origin,
                                                          primitiveScale(shape, part.size));
        const AABB worldBounds = core::transformed(transform, entry->bounds);

        ++out.candidateDraws;
        const bool visible = core::intersects(out.camera.frustum, worldBounds);
        if (!visible) {
            ++out.culledDraws;
            const Vec3 toCentre = core::center(worldBounds);
            const f32 reach = shadowRadius + 0.5f * core::length(core::size(worldBounds));
            if (core::length(toCentre) > reach)
                return;
        }

        // Deduplicated by the look rather than by colour alone: two parts
        // wearing one material with one set of overrides are one bind set, and
        // two sharing a tint but not a material are not.
        //
        // The previous part's entry is asked first, for the reason `lookOf`
        // gives: the entries are unique looks, so a hit there is the entry the
        // scan would have found.
        u32 materialSlot = 0;
        bool found = false;
        if (lastPartMaterial < partMaterials.size() && partMaterials[lastPartMaterial].look.builtIn == look.builtIn &&
            partMaterials[lastPartMaterial].look.sameBlock(look)) {
            materialSlot = partMaterials[lastPartMaterial].slot;
            found = true;
        }
        for (usize index = 0; !found && index < partMaterials.size(); ++index) {
            if (partMaterials[index].look.builtIn == look.builtIn && partMaterials[index].look.sameBlock(look)) {
                materialSlot = partMaterials[index].slot;
                lastPartMaterial = index;
                found = true;
            }
        }
        if (!found) {
            RenderMaterial material;
            if (look.builtIn) {
                material.uniforms.baseColor[0] = 1.0f;
                material.uniforms.baseColor[1] = 1.0f;
                material.uniforms.baseColor[2] = 1.0f;
                material.uniforms.baseColor[3] = 1.0f;
                // Dielectric and fairly rough, which is what an untextured
                // building block looks like. `RenderMaterial`'s own defaults are
                // metallic 1 and roughness 1, which is right for a glTF that
                // forgot to say and wrong for a `Part` that has no way to.
                material.uniforms.metallicRoughnessNormalCutoff[0] = 0.0f;
                material.uniforms.metallicRoughnessNormalCutoff[1] = 0.7f;
                // The default's `Color`, over its white: this IS the part's
                // colour, which is what a plain part has always drawn as.
                tintBy(material, look.tint);
            }
            else {
                material = look.block;
            }
            materialSlot = addMaterial(material);
            partMaterials.push_back(ResolvedPartMaterial{look, materialSlot});
            lastPartMaterial = partMaterials.size() - 1;
        }

        const bool transparent = opacity < 1.0f;
        const f32 depth = core::length(core::center(worldBounds));
        const f32 sortDepth = transparent ? kMaxSortDepth - depth : depth;
        out.draws.push_back(DrawItem{
            .sortKey = drawSortKey(transparent ? kTransparentPass : kOpaquePass, kStaticPipeline,
                                   transparent ? materialSlot : out.familyOf(materialSlot),
                                   transparent ? 0u : drawGeometryKey(entry->mesh.index, 0), sortDepth),
            .transform = transform,
            .mesh = entry->mesh,
            .section = 0,
            .material = materialSlot,
            .alpha = opacity,
            .transparent = transparent,
            .boundsCenter = core::center(worldBounds),
            .boundsRadius = 0.5f * core::length(core::size(worldBounds)),
            .inCameraFrustum = visible,
            .outlined = isOutlined(id),
            .terrain = false,
            .voxelBlock = false,
        });
    });

    // `stable_sort`, and the stability is the contract: two draws with equal
    // keys keep their extraction order, which is the pool's dense order and
    // therefore a pure function of the operation sequence. `std::sort` is a
    // quicksort and would order them by whatever the partition happened to do
    // (R10) -- the same trap the api-dump generator hit on the same day.
    //
    // **Sorted as (key, index) pairs and permuted once**, which is the same
    // order: the index breaks every tie exactly the way stability does. What
    // changed is what moves. A `DrawItem` is over a hundred bytes and a merge
    // sort moves each one about log2(n) times; a pair is sixteen, and each item
    // is then copied exactly once, into the buffer the previous frame left.
    out.sortScratch.resize(out.draws.size());
    for (usize index = 0; index < out.draws.size(); ++index)
        out.sortScratch[index] = RenderWorld::SortEntry{out.draws[index].sortKey, static_cast<u32>(index)};
    std::sort(out.sortScratch.begin(), out.sortScratch.end(),
              [](const RenderWorld::SortEntry& a, const RenderWorld::SortEntry& b) {
                  return a.key != b.key ? a.key < b.key : a.index < b.index;
              });
    out.drawScratch.clear();
    out.drawScratch.reserve(out.draws.size());
    for (const RenderWorld::SortEntry& entry : out.sortScratch)
        out.drawScratch.push_back(out.draws[entry.index]);
    out.draws.swap(out.drawScratch);
}

} // namespace luaug::render
