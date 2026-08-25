// Skeletal animation: clip playback and linear blending (roadmap M6,
// api-design.md §2.2's `AnimationPlayer` and `AnimationTrack`).
//
// It lives in `render` because `render` is the module that already has both
// halves: `asset`, which owns the clips a glTF file carries, and `scene`, which
// owns the tree the players sit in. `script` reaches it through
// `scene::AnimationHost`, which is the `IPhysics3D*` arrangement one layer up.
//
// **Sampling is deterministic and runs at `PreAnimation`.** Track time, `Speed`
// and `Weight` all advance on the SimClock; nothing here reads a wall clock, and
// blending walks tracks in **load order** rather than in whatever order a
// container hands them over -- R10 forbids the second, and two tracks at weight
// 0.5 have to blend the same way on every run.
//
// v1 is clip playback and linear blending. No state machines, no IK, no root
// motion and no additive or masked blending: the roadmap's words are "enough for
// idle/walk/jump".
#pragma once

#include "luaug/asset/model.h"
#include "luaug/core/id.h"
#include "luaug/core/math.h"
#include "luaug/core/name_atom.h"
#include "luaug/scene/animation_host.h"
#include "luaug/scene/skeleton_host.h"

#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace luaug::scene {
class World;
}

namespace luaug::render {

using core::f32;
using core::f64;
using core::u32;
using core::usize;

// One loaded skeleton and its clips, keyed by the content URN the `MeshPart`
// names. Populated by `MeshLoader` beside the GPU geometry, because the file is
// read once and both halves come out of it.
class SkeletonLibrary
{
public:
    struct Entry
    {
        std::vector<asset::Joint> joints;
        std::vector<asset::AnimationClip> clips;
    };

    void set(core::NameAtom content, Entry entry);
    void clear() noexcept;

    // Null for a URN nothing has loaded, or one whose file had no skeleton --
    // which is most of them.
    [[nodiscard]] const Entry* find(core::NameAtom content) const noexcept;

private:
    // Sorted by atom, like `MeshLibrary`, and for the same reason: R10 forbids
    // an unordered container's iteration reaching observable output.
    struct Slot
    {
        core::NameAtom content;
        Entry entry;
    };
    std::vector<Slot> entries_;
};

// The pose one skinned mesh is in, ready for a draw. Model space, one matrix per
// joint, `joint * inverseBind` already combined -- which is what a vertex shader
// multiplies by and is therefore the only form worth storing.
//
// **Keyed by the `MeshPart`, not by the `AnimationPlayer`.** A pose is a
// property of a skeleton, and the skeleton belongs to the mesh; two players
// parented to one mesh are two sources blending into one pose, which is what
// they look like on screen. It is also what lets `extract` ask for a pose with
// the id it already has.
struct Pose
{
    std::vector<core::Mat4> palette;

    // The same joints before `inverseBind` was folded in: joint space to MODEL
    // space, which is what anything other than a vertex shader wants. A socket
    // on a hand needs where the hand IS, and the palette says where it moved
    // FROM its bind pose -- two different matrices, and the second is useless
    // for the question.
    //
    // Free to keep: `rebuildPose` computes it into a scratch and threw it away.
    std::vector<core::Mat4> model;

    // Each joint relative to its parent, this tick.
    //
    // Kept so that an OVERRIDE is cheap: replacing one joint's model transform
    // and re-running the forward pass needs every other joint's local, and
    // recovering it from `model` would be an inverse per joint per tick.
    std::vector<core::Mat4> local;
};

// **Two hosts, one implementation**, and they are separate interfaces on
// purpose: `AnimationHost` is about tracks and weights and names no joint,
// while `SkeletonHost` is about joints and names no track. A caller that wants
// a socket on a hand should not have to link the thing that plays clips.
class AnimationSystem final : public scene::AnimationHost, public scene::SkeletonHost
{
public:
    // The world and the library are references the system keeps: it is created
    // by `app` after both exist and destroyed before either does, which is the
    // same lifetime `PhysicsSync` has.
    AnimationSystem(const scene::World& world, const SkeletonLibrary& skeletons);

    [[nodiscard]] scene::TrackId createTrack(core::InstanceId player, std::string_view clip) override;
    void play(scene::TrackId track, f32 fadeTime, f32 weight, f32 speed) override;
    void stop(scene::TrackId track, f32 fadeTime) override;
    void adjustWeight(scene::TrackId track, f32 weight, f32 fadeTime) override;
    void adjustSpeed(scene::TrackId track, f32 speed) override;
    void setLooped(scene::TrackId track, bool looped) override;
    [[nodiscard]] scene::TrackState state(scene::TrackId track) const override;
    void sample(f64 fixedDt) override;
    [[nodiscard]] std::span<const scene::TrackId> drainEnded() override;
    void retire(const scene::World& world) override;

    // --- scene::SkeletonHost ------------------------------------------------

    [[nodiscard]] core::u32 jointCount(core::InstanceId meshPart) const override;
    [[nodiscard]] core::i32 findJoint(core::InstanceId meshPart, std::string_view name) const override;
    [[nodiscard]] core::i32 jointParent(core::InstanceId meshPart, core::u32 joint) const override;
    [[nodiscard]] std::string_view jointName(core::InstanceId meshPart, core::u32 joint) const override;
    [[nodiscard]] bool jointModel(core::InstanceId meshPart, core::u32 joint, core::CFrameD& out) const override;
    void setJointOverride(core::InstanceId meshPart, core::u32 joint, const core::CFrameD& model) override;
    void clearJointOverrides(core::InstanceId meshPart) override;
    void commitOverrides() override;

    // The pose of one `MeshPart`, or null for a mesh with no skeleton or nothing
    // driving it. Read by the renderer; null means "draw it in bind pose", which
    // is what an unanimated skinned mesh should look like.
    [[nodiscard]] const Pose* pose(core::InstanceId meshPart) const noexcept;

private:
    struct Track
    {
        core::InstanceId player;
        // The `MeshPart` the player was parented to, and the content URN of its
        // skeleton -- both resolved once at creation. A player that is reparented
        // or whose mesh changes content keeps playing against the rig the track
        // was made for, which is honest: a different skeleton is a different rig,
        // and a joint index means nothing across the two.
        //
        // It is also what lets `retire` find the pose of a player that has
        // already been destroyed, when asking the tree is no longer possible.
        core::InstanceId meshPart;
        core::NameAtom content;

        // **The instance the player is parented to, when that is not a
        // `MeshPart`.** A character is a body, a shirt and a pair of trousers --
        // several skinned meshes wearing the same skeleton -- and one clip has
        // to drive all of them. Parent the player to the `Model` and this is the
        // model; every skinned mesh under it is driven by this track.
        //
        // Invalid for a player parented straight to a mesh, which is the older
        // and still-supported shape: that track drives that mesh and no other.
        core::InstanceId driveRoot;
        // Index into the entry's `clips`, or `NoClip`.
        u32 clip = NoClip;
        f64 time = 0.0;
        f32 length = 0.0f;
        f32 speed = 1.0f;
        f32 weight = 1.0f;
        // Where the weight is going and how long is left to get there. A target
        // and a REMAINING time rather than a start/end pair, because
        // `AdjustWeight` may retarget one mid-fade and a pair would then have to
        // invent a new start -- and rather than a rate, because a rate has to be
        // compared against the target to know when it has arrived and floating
        // point makes that comparison a coin toss.
        f32 targetWeight = 1.0f;
        f64 fadeRemaining = 0.0;
        bool looped = false;
        bool playing = false;
        // A non-looping clip that reached its end HOLDS its last frame until it
        // is stopped or replayed. `playing` is already false by then -- the two
        // are different questions, and answering the second with the first would
        // snap a character back to bind pose for the one tick between `Ended`
        // and the handler that reacts to it.
        bool holding = false;
        bool alive = true;
    };

    static constexpr u32 NoClip = 0xFFFFFFFFu;

    void rebuildPose(core::InstanceId meshPart, const SkeletonLibrary::Entry& skeleton);

    // The skeleton a `MeshPart` renders, or null. One lookup, written once.
    [[nodiscard]] const SkeletonLibrary::Entry* skeletonOf(core::InstanceId meshPart) const;

    // Whether this track drives this mesh: it is the track's own mesh, or the
    // mesh is a skinned descendant of the track's drive root.
    // The first skinned mesh under `root` that carries clips, in tree order.
    // Where a player parented to a `Model` takes its animation data from.
    [[nodiscard]] core::InstanceId clipSourceUnder(core::InstanceId root) const;

    [[nodiscard]] bool drives(const Track& track, core::InstanceId meshPart) const;

    // How the CLIP's joint indices map onto another rig's, by name.
    //
    // **By name and never by index.** A body and a shirt exported as two files
    // wear the same skeleton in the sense that matters -- the same joints, named
    // the same -- and in no other: an exporter is free to order them differently,
    // and applying a clip through the wrong index twists a sleeve in a way that
    // looks like a broken animation rather than like a mismatched rig.
    //
    // Cached per (from, to) content pair, because a crowd of a hundred
    // characters is two rigs and one map -- and computed once, since a rig does
    // not change under its own URN.
    struct JointMap
    {
        core::NameAtom from;
        core::NameAtom to;
        // `slots[i]` is the joint in `to` that joint `i` of `from` is, or -1.
        std::vector<core::i32> slots;
    };

    // The identity is returned as null: a clip applied to its own rig needs no
    // map, which is every character that is one mesh.
    [[nodiscard]] const JointMap* jointMapFor(core::NameAtom from, core::NameAtom to) const;

    // A joint's model transform with no pose at all: the rest chain, walked
    // parents-first. What a character standing in bind pose answers.
    [[nodiscard]] static core::Mat4 restModelOf(const SkeletonLibrary::Entry& skeleton, core::u32 joint);

    // One mesh's overrides for this tick. Sorted by joint, so applying them is a
    // merge against the forward pass rather than a lookup per joint -- and so
    // that two overrides of one joint resolve the same way every time (R10).
    struct Override
    {
        core::u32 joint = 0;
        core::Mat4 model;
    };

    struct OverrideSet
    {
        core::InstanceId meshPart;
        std::vector<Override> joints;
    };

    [[nodiscard]] OverrideSet* overridesFor(core::InstanceId meshPart) noexcept;
    [[nodiscard]] const OverrideSet* overridesFor(core::InstanceId meshPart) const noexcept;

    const scene::World* world_ = nullptr;
    const SkeletonLibrary* skeletons_ = nullptr;

    // Index 0 is never handed out, so a `TrackId` of 0 can mean "none" the way
    // an invalid `InstanceId` does.
    std::vector<Track> tracks_{Track{}};
    std::vector<scene::TrackId> ended_;
    std::vector<scene::TrackId> endedDrained_;

    // One pose per skinned mesh that has tracks. Keyed rather than pooled because
    // a `MeshPart` is an Instance and this is not scene's storage -- and because
    // the count is a handful even in a crowd.
    std::unordered_map<core::u64, Pose> poses_;

    // Scratch, reused so a steady-state tick allocates nothing.
    //
    // The pose accumulates per COMPONENT rather than per joint transform,
    // because a joint no channel drives has to keep its rest transform: one
    // weighted average over all three would collapse an undriven joint to the
    // origin, which is what makes a clip that animates one arm eat the other.
    // The meshes whose pose this tick has to rebuild, collected before the walk
    // so it is one pass per mesh rather than one per track.
    std::vector<core::InstanceId> meshes_;
    std::vector<core::DVec3> translation_;
    std::vector<f32> rotation_;
    std::vector<core::Vec3> scale_;
    std::vector<f32> weightT_;
    std::vector<f32> weightR_;
    std::vector<f32> weightS_;
    std::vector<core::Mat4> model_;

    // A vector rather than a map, and sorted by instance: a scene has a handful
    // of ragdolls, and R10 forbids an unordered container's iteration order
    // reaching observable output -- which a pose most certainly is.
    std::vector<OverrideSet> overrides_;

    // Mutable because it is a memo: asking for a map is a read, and building one
    // the first time is what makes the second read free.
    mutable std::vector<JointMap> jointMaps_;
};

} // namespace luaug::render
