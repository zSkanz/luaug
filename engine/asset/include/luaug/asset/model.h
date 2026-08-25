// CPU-side geometry and materials (architecture.md §2 `asset`, ADR 0010).
//
// This module produces these; `render` owns every GPU upload and this header
// mentions no GPU type at all. That split is what lets a mesh arrive from a
// glTF file, from a procedural generator, or from a voxel mesher without the
// renderer knowing which -- the M4 brief's Decision 4, and one of the three
// seams the roadmap requires stay open.
//
// One glTF file is one mesh. `api-design.md` §2.6's prefab example settles it:
// a `MeshPart` names `asset://models/trunk.glb` and a second `MeshPart` names
// `leaves.glb`, so a multi-part model is multiple MeshParts rather than one
// file addressed piecewise. A file whose mesh has several primitives with
// different materials is normal and becomes several submeshes below.
#pragma once

#include "luaug/asset/image.h"
#include "luaug/core/math.h"
#include "luaug/core/types.h"

#include <string>
#include <vector>

namespace luaug::asset {

using core::AABB;
using core::Color3;
using core::f32;
using core::u32;
using core::Vec3;

// The one static-mesh vertex, interleaved, 48 bytes.
//
// Interleaved rather than one stream per attribute because every pass in M4
// reads every attribute: a deinterleaved layout pays for itself when a depth
// or shadow pass reads position alone, and the moment that is measured rather
// than assumed is the moment to split it. Recorded so the choice is revisited
// with numbers instead of taste.
//
// `tangent.w` is the bitangent's handedness (+1 or -1), which is glTF's own
// convention -- storing the bitangent instead would cost 12 bytes to say what
// one sign already says.
struct Vertex
{
    Vec3 position;
    Vec3 normal;
    f32 tangent[4]{0.0f, 0.0f, 0.0f, 1.0f};
    f32 uv[2]{0.0f, 0.0f};
};

static_assert(sizeof(Vertex) == 48, "the vertex layout is a GPU buffer layout; changing it changes the shaders");

// How a material's alpha is meant to be read. glTF's three modes, kept because
// they are what the file says and translating them into something else here
// would only move the switch.
enum class AlphaMode : core::u8
{
    Opaque,
    // Alpha is a coverage test against `alphaCutoff`, not a blend.
    Mask,
    Blend,
};

// An index into `Model::images`, plus the UV set it samples. `Missing` rather
// than an optional so the struct stays trivially copyable and hashable, which
// is what a material sort key needs (Decision 7).
struct TextureRef
{
    static constexpr u32 Missing = 0xFFFFFFFFu;

    u32 image = Missing;
    u32 uvSet = 0;

    [[nodiscard]] constexpr bool present() const noexcept { return image != Missing; }
};

// The material as the file describes it, in the terms the default PBR shader
// consumes. No GPU handles, no shader objects: `render` resolves the name and
// the images into a pipeline and bindings.
//
// `shader` is a name rather than an enum, which is the roadmap's second design
// constraint (M4 brief, Decision 6): a material that wants vertex-displaced
// water names a different shader and nothing else changes. Everything a glTF
// produces names the built-in `"pbr"`.
struct MaterialDef
{
    std::string name;
    std::string shader = "pbr";

    Color3 baseColorFactor{1.0f, 1.0f, 1.0f};
    f32 baseColorAlpha = 1.0f;
    f32 metallicFactor = 1.0f;
    f32 roughnessFactor = 1.0f;
    Color3 emissiveFactor{0.0f, 0.0f, 0.0f};
    // Scales the sampled tangent-space normal's XY, per glTF's normalTexture
    // scale. 1.0 is "use the texture as authored".
    f32 normalScale = 1.0f;

    AlphaMode alphaMode = AlphaMode::Opaque;
    f32 alphaCutoff = 0.5f;
    bool doubleSided = false;

    TextureRef baseColor;
    TextureRef normal;
    // glTF packs occlusion, roughness and metalness into one texture's R, G and
    // B channels. One reference, because they are one image.
    TextureRef metallicRoughness;
    TextureRef emissive;
};

// A contiguous run of indices drawn with one material. A glTF primitive becomes
// one of these.
struct Submesh
{
    u32 firstIndex = 0;
    u32 indexCount = 0;
    // Index into `Model::materials`. Every submesh has one: a primitive with no
    // material in the file gets the default appended by the importer, because a
    // draw with no material is a draw the renderer cannot make.
    u32 material = 0;
    // Of this submesh alone, in the model's own space. The renderer culls per
    // draw, and a draw is a submesh.
    AABB bounds;
};

// One mesh: the geometry a single `MeshPart` renders.
//
// Indices are always u32. A 16-bit path would halve the index buffer for small
// meshes and is worth having when something measures it; until then one width
// is one code path in the importer, the uploader and every backend.
struct Mesh
{
    std::vector<Vertex> vertices;
    std::vector<u32> indices;
    std::vector<Submesh> submeshes;
    AABB bounds;
};

// What an importer produces from one file: the mesh, the materials it names,
// and the images those materials sample, decoded.
//
// Images are decoded here rather than left as encoded bytes because the caller
// that matters uploads them immediately, and because an image referenced by two
// materials should be decoded once.
// --- Skinning and animation (M6) ---------------------------------------------
//
// **A second vertex stream rather than four more fields on `Vertex`.** Joints
// and weights are sixteen bytes each, and putting them in the interleaved
// vertex would spend that on every static vertex in every world so that a
// character can have elbows (M6 brief, Decision 11). A skinned mesh carries
// both arrays and an unskinned one carries neither, so an unskinned draw is
// byte-identical to M4's.

// One vertex's four bone influences. Four is glTF's own limit per JOINTS_n set
// and is what every real-time skin uses: a fifth influence is below the noise
// floor of an 8-bit weight.
//
// **The joint indices are floats, and that is eight bytes spent on a freeze.**
// They are integers and want to be `u16`, but `rhi::VertexFormat` has no
// unsigned-integer format and that enumeration is frozen by ADR 0037 -- widening
// it is a human decision (R5), not an agent's. A `float` holds every integer up
// to 2^24 exactly, so this is correct rather than approximate; what it costs is
// eight bytes per skinned vertex, which is 160 KB on a 20k-vertex character and
// nothing at all on a world of static meshes, since an unskinned mesh carries no
// skin stream. Recorded here so the trade is revisited with the ADR rather than
// rediscovered.
struct SkinVertex
{
    f32 joints[4]{0.0f, 0.0f, 0.0f, 0.0f};
    f32 weights[4]{0.0f, 0.0f, 0.0f, 0.0f};
};

static_assert(sizeof(SkinVertex) == 32, "the skin stream is a GPU buffer layout; changing it changes the shaders");

// One joint of a skeleton.
//
// `parent` is an index into the same array and is always LESS than the joint's
// own index -- the loader sorts them so, which is what lets the pose be
// resolved in one forward pass instead of a graph walk per frame.
struct Joint
{
    static constexpr u32 NoParent = 0xFFFFFFFFu;

    // The joint's rest transform relative to its parent.
    core::CFrameD localBind;
    // Model space to joint space at bind time -- glTF's inverse bind matrix,
    // kept as it comes because it is a matrix rather than a rigid transform:
    // an exporter is free to bake scale into it and often does.
    core::Mat4 inverseBind;
    u32 parent = NoParent;
    std::string name;
};

// One animated channel: what it drives and the keys it drives it with.
//
// Times and values are parallel arrays rather than a vector of pairs because
// sampling binary-searches the times and touches the values once -- two arrays
// keep the search inside one cache line for far longer.
struct AnimationChannel
{
    enum class Target : core::u8
    {
        Translation,
        Rotation,
        Scale,
    };

    u32 joint = 0;
    Target target = Target::Translation;
    std::vector<f32> times;
    // Three floats per key for translation and scale, four (x, y, z, w) for
    // rotation. Flat rather than typed, because the sampler is one function
    // over a stride and three would be three places to get the interpolation
    // wrong.
    std::vector<f32> values;
    u32 stride = 3;
};

struct AnimationClip
{
    std::string name;
    // Seconds. The last key's time, which is what a loop wraps at.
    f32 duration = 0.0f;
    std::vector<AnimationChannel> channels;
};

struct Model
{
    Mesh mesh;
    std::vector<MaterialDef> materials;
    std::vector<Image> images;

    // Empty for a static mesh, and empty is the common case. When it is not,
    // `skin` has one entry per vertex of `mesh` -- the two are parallel by
    // construction, and the loader refuses a file where they are not.
    std::vector<SkinVertex> skin;
    std::vector<Joint> joints;

    // **The bind pose, already resolved into a palette**: one matrix per joint,
    // `jointGlobal * inverseBind`, which is what places a skinned mesh's
    // vertices when nothing is animating it.
    //
    // A skinned mesh drawn with no palette at all is not "unskinned" -- it is
    // every vertex left wherever the file's author modelled it, which for an
    // export whose skeleton hangs off a unit conversion is a model a hundred
    // times too large in the wrong orientation. glTF says a skinned node's own
    // transform MUST be ignored, so there is nothing else to place it with.
    //
    // Resolved HERE because it needs the node graph and nothing downstream has
    // one: a joint's global transform is every node from the scene root down,
    // and the joint list is not closed under parenthood -- an exporter may leave
    // an intermediate node out of the skin, and a chain rebuilt from
    // `Joint::parent` then loses everything above the break.
    //
    // Empty when the bind pose was baked into the vertices instead
    // (`GltfImportOptions::maxSkinJoints`), because then there is nothing left
    // to pose.
    std::vector<core::Mat4> restPalette;

    // How many joints the file's skin had, whether or not this model still
    // carries them. Zero for a file with no skin.
    //
    // Kept after a bake so the loader can say what it did and why: a character
    // that silently stopped being animatable is a bug report, and one that says
    // "677 joints, this build poses 64" is an answer.
    core::u32 sourceJointCount = 0;

    std::vector<AnimationClip> clips;

    [[nodiscard]] bool skinned() const noexcept { return !joints.empty() && !skin.empty(); }

    // Whether the skeleton was thrown away and its bind pose baked in. True only
    // for a rig past the caller's budget.
    [[nodiscard]] bool bakedBindPose() const noexcept { return sourceJointCount > 0 && joints.empty(); }
};

} // namespace luaug::asset
