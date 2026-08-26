#include "luaug/render/animation.h"

#include "luaug/scene/world.h"

#include <algorithm>
#include <cmath>

namespace luaug::render {
namespace {

using core::CFrameD;
using core::DVec3;
using core::Mat3;
using core::Mat4;
using core::Vec3;

// The key at or before `time`, by binary search. The last key for a time past
// the end, which is what a clip that is not looping should hold.
[[nodiscard]] usize keyBefore(const std::vector<f32>& times, f32 time) noexcept
{
    if (times.size() < 2)
        return 0;
    const auto upper = std::upper_bound(times.begin(), times.end(), time);
    if (upper == times.begin())
        return 0;
    return static_cast<usize>(std::distance(times.begin(), upper)) - 1;
}

[[nodiscard]] f32 fractionBetween(const std::vector<f32>& times, usize key, f32 time) noexcept
{
    if (key + 1 >= times.size())
        return 0.0f;
    const f32 span = times[key + 1] - times[key];
    if (span <= 0.0f)
        return 0.0f;
    return std::clamp((time - times[key]) / span, 0.0f, 1.0f);
}

// Linear interpolation between two keys of a channel, into `out`.
void sampleChannel(const asset::AnimationChannel& channel, f32 time, f32* out) noexcept
{
    const usize key = keyBefore(channel.times, time);
    const f32 alpha = fractionBetween(channel.times, key, time);
    const usize stride = channel.stride;
    const f32* from = &channel.values[key * stride];
    const f32* to = key + 1 < channel.times.size() ? &channel.values[(key + 1) * stride] : from;

    if (stride == 4) {
        // Two quaternions describe one rotation with opposite signs, and
        // interpolating q against -q takes the long way round -- which reads as
        // a joint spinning the wrong way for exactly one key.
        f32 dot = 0.0f;
        for (usize lane = 0; lane < 4; ++lane)
            dot += from[lane] * to[lane];
        const f32 sign = dot < 0.0f ? -1.0f : 1.0f;

        f32 length = 0.0f;
        for (usize lane = 0; lane < 4; ++lane) {
            out[lane] = from[lane] + (to[lane] * sign - from[lane]) * alpha;
            length += out[lane] * out[lane];
        }
        // Normalized linear rather than true slerp: at the key densities an
        // exported clip carries the angular error is below a tenth of a degree,
        // and a nlerp is branchless where a slerp has a small-angle case that
        // has to be got right. If a clip ever needs the difference, this is the
        // one function to change.
        length = std::sqrt(length);
        if (length <= 0.0f) {
            out[0] = out[1] = out[2] = 0.0f;
            out[3] = 1.0f;
            return;
        }
        for (usize lane = 0; lane < 4; ++lane)
            out[lane] /= length;
        return;
    }

    for (usize lane = 0; lane < stride; ++lane)
        out[lane] = from[lane] + (to[lane] - from[lane]) * alpha;
}

// Translation, rotation and scale into one column-major matrix. Written out
// rather than composed from three matrix multiplies, because it is the inner
// loop of the pose and the multiplies would be forty-eight of the sixty-four
// products doing nothing.
// A rigid transform as a matrix. `toRenderMatrix` with no origin to subtract,
// which is what a JOINT wants: a bind pose is measured from the mesh's own
// origin and has nothing to do with where in the world the mesh stands.
[[nodiscard]] Mat4 toMatrix(const core::CFrameD& frame) noexcept
{
    return core::toRenderMatrix(frame, core::DVec3{});
}

[[nodiscard]] Mat4 composeTrs(const DVec3& translation, const f32* quaternion, Vec3 scale) noexcept
{
    const Mat3 rotation = core::fromQuaternion(quaternion[0], quaternion[1], quaternion[2], quaternion[3]);
    Mat4 result;
    for (int column = 0; column < 3; ++column) {
        const f32 axisScale = column == 0 ? scale.x : (column == 1 ? scale.y : scale.z);
        for (int row = 0; row < 3; ++row)
            result.m[column][row] = rotation.m[column][row] * axisScale;
        result.m[column][3] = 0.0f;
    }
    result.m[3][0] = static_cast<f32>(translation.x);
    result.m[3][1] = static_cast<f32>(translation.y);
    result.m[3][2] = static_cast<f32>(translation.z);
    result.m[3][3] = 1.0f;
    return result;
}

[[nodiscard]] core::u64 keyOf(core::InstanceId id) noexcept
{
    return static_cast<core::u64>(id.index) | (static_cast<core::u64>(id.generation) << 32);
}

// A tick that has at least this much of the fade's remaining time in it
// finishes the fade. The slack is what stops one tick's worth of remaining time
// taking two ticks to spend: fifteen subtractions of 1/60 from 0.25 leave
// 4.9e-17 behind, and a fade that never quite finishes is a track that never
// quite stops.
constexpr f64 FadeSlack = 1.0e-6;

} // namespace

void SkeletonLibrary::set(core::NameAtom content, Entry entry)
{
    const auto slot = std::lower_bound(entries_.begin(), entries_.end(), content,
                                       [](const Slot& lhs, core::NameAtom rhs) { return lhs.content.id < rhs.id; });
    if (slot != entries_.end() && slot->content == content) {
        slot->entry = std::move(entry);
        return;
    }
    entries_.insert(slot, Slot{content, std::move(entry)});
}

void SkeletonLibrary::clear() noexcept
{
    entries_.clear();
}

const SkeletonLibrary::Entry* SkeletonLibrary::find(core::NameAtom content) const noexcept
{
    const auto slot = std::lower_bound(entries_.begin(), entries_.end(), content,
                                       [](const Slot& lhs, core::NameAtom rhs) { return lhs.content.id < rhs.id; });
    if (slot == entries_.end() || !(slot->content == content))
        return nullptr;
    return &slot->entry;
}

AnimationSystem::AnimationSystem(const scene::World& world, const SkeletonLibrary& skeletons)
    : world_(&world), skeletons_(&skeletons)
{}

scene::TrackId AnimationSystem::createTrack(core::InstanceId player, core::NameAtom content, std::string_view clip)
{
    Track track;
    track.player = player;

    // The skeleton comes from the player's PARENT when that parent is a
    // `MeshPart` -- the older shape, and the one a character made of one mesh
    // uses.
    //
    // **When it is not, the parent is a drive root and every skinned mesh under
    // it is driven by this one track.** A character is a body, a shirt and a
    // pair of trousers: several skinned meshes wearing the same skeleton, which
    // one clip has to move together. Parent the player to the `Model`.
    //
    // The clip is sourced from the FIRST skinned mesh under the root that has
    // one, in tree order -- so it does not matter which of the pieces the artist
    // exported the animation with, and it is the same choice on every machine.
    track.meshPart = world_->parentOf(player);
    if (world_->meshParts().find(track.meshPart) == nullptr && track.meshPart.valid()) {
        track.driveRoot = track.meshPart;
        track.meshPart = clipSourceUnder(track.driveRoot);
    }
    if (const scene::MeshPartComponent* mesh = world_->meshParts().find(track.meshPart); mesh != nullptr) {
        // **The clip's own file when one was named, and the mesh's otherwise**
        // (S6.8). `track.content` is what the sampler reads the clip out of, and
        // `jointMapFor` already maps that rig's joints onto whichever mesh it is
        // driving -- so a clip from elsewhere is retargeted by the same code that
        // drives a shirt from a body's skeleton.
        track.content = content.valid() ? content : mesh->meshContent;
        if (const SkeletonLibrary::Entry* entry = skeletons_->find(track.content); entry != nullptr) {
            for (u32 index = 0; index < entry->clips.size(); ++index) {
                if (clip.empty() || entry->clips[index].name == clip) {
                    track.clip = index;
                    track.length = entry->clips[index].duration;
                    break;
                }
            }
        }
    }

    tracks_.push_back(track);
    return static_cast<scene::TrackId>(tracks_.size() - 1);
}

void AnimationSystem::play(scene::TrackId id, f32 fadeTime, f32 weight, f32 speed)
{
    if (id == 0 || id >= tracks_.size())
        return;
    Track& track = tracks_[id];
    // From the beginning, every time. The opposite of `Tween:Play`, and
    // deliberately: a jump animation triggered twice should play twice.
    track.time = 0.0;
    track.speed = speed;
    track.targetWeight = weight;
    track.playing = true;
    track.holding = false;
    if (fadeTime > 0.0f) {
        track.weight = 0.0f;
        track.fadeRemaining = static_cast<f64>(fadeTime);
    }
    else {
        track.weight = weight;
        track.fadeRemaining = 0.0;
    }
}

void AnimationSystem::stop(scene::TrackId id, f32 fadeTime)
{
    if (id == 0 || id >= tracks_.size())
        return;
    Track& track = tracks_[id];
    track.targetWeight = 0.0f;
    if (fadeTime > 0.0f && track.playing) {
        track.fadeRemaining = static_cast<f64>(fadeTime);
        return;
    }
    track.weight = 0.0f;
    track.fadeRemaining = 0.0;
    track.playing = false;
    track.holding = false;
}

void AnimationSystem::adjustWeight(scene::TrackId id, f32 weight, f32 fadeTime)
{
    if (id == 0 || id >= tracks_.size())
        return;
    Track& track = tracks_[id];
    track.targetWeight = weight;
    if (fadeTime > 0.0f) {
        track.fadeRemaining = static_cast<f64>(fadeTime);
        return;
    }
    track.weight = weight;
    track.fadeRemaining = 0.0;
}

void AnimationSystem::adjustSpeed(scene::TrackId id, f32 speed)
{
    if (id == 0 || id >= tracks_.size())
        return;
    tracks_[id].speed = speed;
}

void AnimationSystem::setLooped(scene::TrackId id, bool looped)
{
    if (id == 0 || id >= tracks_.size())
        return;
    tracks_[id].looped = looped;
}

scene::TrackState AnimationSystem::state(scene::TrackId id) const
{
    scene::TrackState out;
    if (id == 0 || id >= tracks_.size())
        return out;
    const Track& track = tracks_[id];
    out.timePosition = track.time;
    out.length = track.length;
    out.speed = track.speed;
    out.weight = track.weight;
    out.looped = track.looped;
    out.playing = track.playing;
    return out;
}

void AnimationSystem::sample(f64 fixedDt)
{
    // Every skinned mesh with at least one live track under it, collected first
    // so the pose walk is one pass per mesh rather than one per track.
    meshes_.clear();

    const auto note = [this](core::InstanceId mesh) {
        if (mesh.valid() && std::find(meshes_.begin(), meshes_.end(), mesh) == meshes_.end())
            meshes_.push_back(mesh);
    };

    for (usize index = 1; index < tracks_.size(); ++index) {
        Track& track = tracks_[index];
        // A track with no clip drives nothing and is not a reason to build a
        // pose; it still keeps its clock so a script reading it sees a track
        // rather than a hole.
        if (!track.alive || track.clip == NoClip)
            continue;
        if (!track.playing) {
            note(track.meshPart);
            continue;
        }

        if (track.fadeRemaining > 0.0) {
            if (track.fadeRemaining <= fixedDt * (1.0 + FadeSlack)) {
                track.weight = track.targetWeight;
                track.fadeRemaining = 0.0;
                // A fade to zero is a stop that took a moment, which is what
                // `Stop(fadeTime)` means.
                if (track.targetWeight <= 0.0f) {
                    track.playing = false;
                    track.holding = false;
                    note(track.meshPart);
                    continue;
                }
            }
            else {
                // Towards the target by the fraction of the remaining time this
                // tick is, which lands exactly on the target whatever
                // `AdjustWeight` did to it partway.
                track.weight += (track.targetWeight - track.weight) * static_cast<f32>(fixedDt / track.fadeRemaining);
                track.fadeRemaining -= fixedDt;
            }
        }

        track.time += fixedDt * static_cast<f64>(track.speed);
        if (track.length > 0.0f && track.time >= static_cast<f64>(track.length)) {
            if (track.looped) {
                // Wrapped rather than reset, so a loop does not lose the
                // fraction of a tick it overshot by -- over a minute that is a
                // loop drifting against everything else in the scene.
                track.time = std::fmod(track.time, static_cast<f64>(track.length));
            }
            else {
                track.time = static_cast<f64>(track.length);
                track.playing = false;
                track.holding = true;
                ended_.push_back(static_cast<scene::TrackId>(index));
            }
        }

        note(track.meshPart);
    }

    // **Every mesh a drive root covers, not just the one the clip came from.**
    // `note(track.meshPart)` above collects the mesh the track was MADE against,
    // which for a player parented to a `Model` is whichever piece carried the
    // animation. The shirt is driven by the same track and would otherwise never
    // have its pose rebuilt -- a body that walks and a shirt that stands still.
    for (usize index = 1; index < tracks_.size(); ++index) {
        const Track& track = tracks_[index];
        if (!track.alive || !track.driveRoot.valid() || track.clip == NoClip)
            continue;
        std::vector<core::InstanceId> descendants;
        world_->collectDescendants(track.driveRoot, descendants);
        for (const core::InstanceId id : descendants) {
            const scene::MeshPartComponent* mesh = world_->meshParts().find(id);
            if (mesh == nullptr)
                continue;
            if (const SkeletonLibrary::Entry* entry = skeletons_->find(mesh->meshContent);
                entry != nullptr && !entry->joints.empty()) {
                note(id);
            }
        }
    }

    // A mesh with overrides and no track is deliberately NOT collected here.
    // It was, at first, on the reasoning that a limp ragdoll has no track and
    // so would never be visited -- but there is nothing to rebuild for it:
    // `commitOverrides` builds a pose from the rest chain when it finds none,
    // and a mesh no clip drives has no clip to rebuild from. Adding the visit
    // changed no observable behaviour, so it is not here.
    for (const core::InstanceId meshPart : meshes_) {
        const scene::MeshPartComponent* mesh = world_->meshParts().find(meshPart);
        if (mesh == nullptr)
            continue;
        if (const SkeletonLibrary::Entry* entry = skeletons_->find(mesh->meshContent); entry != nullptr)
            rebuildPose(meshPart, *entry);
    }
}

void AnimationSystem::rebuildPose(core::InstanceId meshPart, const SkeletonLibrary::Entry& skeleton)
{
    const usize jointCount = skeleton.joints.size();
    if (jointCount == 0)
        return;

    // Which rig this is, so a clip arriving from ANOTHER one can be remapped
    // onto it by joint name.
    const scene::MeshPartComponent* meshComponent = world_->meshParts().find(meshPart);
    const core::NameAtom content = meshComponent != nullptr ? meshComponent->meshContent : core::NameAtom{};

    // Accumulators, one currency per component. A weighted average per joint
    // rather than per track, because a joint no clip drives has to keep its rest
    // transform -- a zero-weight average would collapse it to the origin, which
    // is what makes a clip that animates one arm eat the other.
    translation_.assign(jointCount, DVec3{});
    rotation_.assign(jointCount * 4, 0.0f);
    scale_.assign(jointCount, Vec3{});
    weightT_.assign(jointCount, 0.0f);
    weightR_.assign(jointCount, 0.0f);
    weightS_.assign(jointCount, 0.0f);

    f32 sample[4]{};
    bool contributed = false;

    // **Track index order, which is load order.** R10 forbids the order coming
    // out of a container that does not promise one, and two tracks at weight
    // 0.5 have to blend the same way on every run.
    for (usize index = 1; index < tracks_.size(); ++index) {
        const Track& track = tracks_[index];
        if (!track.alive || !(track.playing || track.holding) || track.clip == NoClip)
            continue;
        // Every track made against THIS mesh, and every track whose drive root
        // this mesh is under. Two players under one mesh are two sources
        // blending into one pose, which is what they look like on screen; one
        // player over a body and a shirt is one source moving both.
        if (!drives(track, meshPart))
            continue;
        if (track.weight <= 0.0f)
            continue;

        // **The clip comes from the track's OWN rig, not from this mesh's.** A
        // shirt exported without the animation has no clips of its own, and the
        // whole point of one player over several meshes is that only one of them
        // needs to carry it.
        const SkeletonLibrary::Entry* source = skeletons_->find(track.content);
        if (source == nullptr || track.clip >= source->clips.size())
            continue;
        contributed = true;

        // Null when the clip is being applied to the rig it came from, which is
        // every character made of one mesh -- and then the channel's own index
        // is used, exactly as before.
        const JointMap* const map = jointMapFor(track.content, content);

        const asset::AnimationClip& clip = source->clips[track.clip];
        const auto time = static_cast<f32>(track.time);

        for (const asset::AnimationChannel& sourceChannel : clip.channels) {
            asset::AnimationChannel channelStorage;
            const asset::AnimationChannel* resolved = &sourceChannel;
            if (map != nullptr) {
                if (sourceChannel.joint >= map->slots.size() || map->slots[sourceChannel.joint] < 0) {
                    // A joint this rig does not have. Skipped rather than
                    // guessed: a shirt with no fingers should keep its own
                    // sleeve, not inherit a finger's rotation.
                    continue;
                }
                channelStorage = sourceChannel;
                channelStorage.joint = static_cast<u32>(map->slots[sourceChannel.joint]);
                resolved = &channelStorage;
            }
            const asset::AnimationChannel& channel = *resolved;
            if (channel.joint >= jointCount || channel.times.empty())
                continue;
            sampleChannel(channel, time, sample);

            switch (channel.target) {
            case asset::AnimationChannel::Target::Translation:
                translation_[channel.joint].x += static_cast<f64>(sample[0] * track.weight);
                translation_[channel.joint].y += static_cast<f64>(sample[1] * track.weight);
                translation_[channel.joint].z += static_cast<f64>(sample[2] * track.weight);
                weightT_[channel.joint] += track.weight;
                break;
            case asset::AnimationChannel::Target::Rotation: {
                f32* accumulator = &rotation_[channel.joint * 4];
                // Sign-aligned against whatever is already there, for the same
                // reason `sampleChannel` aligns two keys: blending q against -q
                // is the long way round, and here it would show as a joint
                // snapping when a second track faded in.
                f32 dot = 0.0f;
                for (usize lane = 0; lane < 4; ++lane)
                    dot += accumulator[lane] * sample[lane];
                const f32 sign = (weightR_[channel.joint] > 0.0f && dot < 0.0f) ? -1.0f : 1.0f;
                for (usize lane = 0; lane < 4; ++lane)
                    accumulator[lane] += sample[lane] * sign * track.weight;
                weightR_[channel.joint] += track.weight;
                break;
            }
            case asset::AnimationChannel::Target::Scale:
                scale_[channel.joint].x += sample[0] * track.weight;
                scale_[channel.joint].y += sample[1] * track.weight;
                scale_[channel.joint].z += sample[2] * track.weight;
                weightS_[channel.joint] += track.weight;
                break;
            }
        }
    }

    if (!contributed) {
        // Nothing drives this player any more. Its pose is taken away rather
        // than left holding the last thing that did -- a null pose means "bind
        // pose", and a stale palette would freeze the character mid-stride.
        //
        // **Unless something is overriding it**, and then the pose is left
        // exactly as it is. Not merely un-erased: falling through would rebuild
        // it from accumulators that are all zero weight, which IS the rest pose
        // -- the first version of this did that and lost the very thing it was
        // written to keep. What it keeps is the LOCALS. A ragdoll simulates a
        // dozen bones; the fingers it does not simulate ride on their own local
        // from this pose, so rebuilding from rest snaps every unsimulated joint
        // out of the animation it was in on the exact frame the character goes
        // limp -- a hand that springs open as the body drops.
        if (overridesFor(meshPart) == nullptr)
            poses_.erase(keyOf(meshPart));
        return;
    }

    Pose& pose = poses_[keyOf(meshPart)];
    pose.palette.assign(jointCount, Mat4{});
    // Kept rather than thrown away. `model` is what a socket asks for and
    // `local` is what an override needs to re-run the forward pass -- both were
    // already being computed into a scratch that ended at the closing brace.
    pose.model.assign(jointCount, Mat4{});
    pose.local.assign(jointCount, Mat4{});

    f32 restRotation[4]{};
    for (usize joint = 0; joint < jointCount; ++joint) {
        const asset::Joint& bone = skeleton.joints[joint];

        DVec3 translation = bone.localBind.position;
        if (weightT_[joint] > 0.0f) {
            const f64 inverse = 1.0 / static_cast<f64>(weightT_[joint]);
            translation = DVec3{translation_[joint].x * inverse, translation_[joint].y * inverse,
                                translation_[joint].z * inverse};
        }

        Vec3 boneScale{1.0f, 1.0f, 1.0f};
        if (weightS_[joint] > 0.0f) {
            const f32 inverse = 1.0f / weightS_[joint];
            boneScale = Vec3{scale_[joint].x * inverse, scale_[joint].y * inverse, scale_[joint].z * inverse};
        }

        const f32* quaternion = restRotation;
        if (weightR_[joint] > 0.0f) {
            f32* accumulator = &rotation_[joint * 4];
            f32 length = 0.0f;
            for (usize lane = 0; lane < 4; ++lane)
                length += accumulator[lane] * accumulator[lane];
            length = std::sqrt(length);
            if (length > 0.0f) {
                for (usize lane = 0; lane < 4; ++lane)
                    accumulator[lane] /= length;
                quaternion = accumulator;
            }
        }
        if (quaternion == restRotation) {
            // The rest rotation, back as a quaternion so the accumulator has one
            // currency. `CFrameD` stores a basis and reading a quaternion out of
            // it is cheaper than carrying a second representation on `Joint`.
            core::toQuaternion(bone.localBind.rotation, restRotation[0], restRotation[1], restRotation[2],
                               restRotation[3]);
        }

        const Mat4 local = composeTrs(translation, quaternion, boneScale);
        pose.local[joint] = local;
        // One forward pass, parents first -- which the loader guarantees by
        // sorting the joints (asset/model.h). A graph walk per frame would be
        // the alternative, and this is the whole reason it is not needed.
        pose.model[joint] = bone.parent == asset::Joint::NoParent ? local : pose.model[bone.parent] * local;
        pose.palette[joint] = pose.model[joint] * bone.inverseBind;
    }
}

// The first skinned mesh under `root` that carries clips, in tree order.
//
// Tree order rather than pool order, because it is the order a person sees in
// the Explorer -- and because it is what a scene file's own order produces, so
// the answer does not change when an unrelated instance is created.
core::InstanceId AnimationSystem::clipSourceUnder(core::InstanceId root) const
{
    if (world_ == nullptr || skeletons_ == nullptr)
        return {};

    std::vector<core::InstanceId> descendants;
    world_->collectDescendants(root, descendants);
    core::InstanceId firstSkinned;
    for (const core::InstanceId id : descendants) {
        const scene::MeshPartComponent* mesh = world_->meshParts().find(id);
        if (mesh == nullptr)
            continue;
        const SkeletonLibrary::Entry* entry = skeletons_->find(mesh->meshContent);
        if (entry == nullptr || entry->joints.empty())
            continue;
        if (!firstSkinned.valid())
            firstSkinned = id;
        if (!entry->clips.empty())
            return id;
    }
    // Nothing under it has clips yet -- the meshes may still be loading. The
    // first skinned one is the honest answer: the track is made against that
    // rig, and `createTrack` reports no clip, which is what its own doc says
    // happens for a name the file does not have.
    return firstSkinned;
}

bool AnimationSystem::drives(const Track& track, core::InstanceId meshPart) const
{
    if (track.meshPart == meshPart)
        return true;
    if (!track.driveRoot.valid() || world_ == nullptr)
        return false;
    // Under the root the player was parented to. Walked upwards, because a
    // character is a handful of meshes and this is once per mesh per tick.
    for (core::InstanceId cursor = meshPart; cursor.valid(); cursor = world_->parentOf(cursor)) {
        if (cursor == track.driveRoot)
            return true;
    }
    return false;
}

const AnimationSystem::JointMap* AnimationSystem::jointMapFor(core::NameAtom from, core::NameAtom to) const
{
    // A clip applied to its own rig needs no map, which is every character made
    // of one mesh.
    if (from == to || skeletons_ == nullptr)
        return nullptr;

    for (const JointMap& map : jointMaps_) {
        if (map.from == from && map.to == to)
            return &map;
    }

    const SkeletonLibrary::Entry* source = skeletons_->find(from);
    const SkeletonLibrary::Entry* target = skeletons_->find(to);
    if (source == nullptr || target == nullptr)
        return nullptr;

    JointMap made;
    made.from = from;
    made.to = to;
    made.slots.assign(source->joints.size(), -1);
    for (usize index = 0; index < source->joints.size(); ++index) {
        for (usize other = 0; other < target->joints.size(); ++other) {
            if (source->joints[index].name == target->joints[other].name) {
                made.slots[index] = static_cast<core::i32>(other);
                break;
            }
        }
    }
    jointMaps_.push_back(std::move(made));
    return &jointMaps_.back();
}

// --- scene::SkeletonHost -----------------------------------------------------

const SkeletonLibrary::Entry* AnimationSystem::skeletonOf(core::InstanceId meshPart) const
{
    if (world_ == nullptr || skeletons_ == nullptr)
        return nullptr;
    const scene::MeshPartComponent* mesh = world_->meshParts().find(meshPart);
    if (mesh == nullptr)
        return nullptr;
    const SkeletonLibrary::Entry* entry = skeletons_->find(mesh->meshContent);
    return entry != nullptr && !entry->joints.empty() ? entry : nullptr;
}

Mat4 AnimationSystem::restModelOf(const SkeletonLibrary::Entry& skeleton, core::u32 joint)
{
    // Walked from the joint UP rather than from the roots down: this answers
    // one question about one joint, and building the whole chain to reach a
    // wrist would be the rest of the skeleton's worth of work for nothing.
    Mat4 out = toMatrix(skeleton.joints[joint].localBind);
    for (core::u32 walk = skeleton.joints[joint].parent; walk != asset::Joint::NoParent;
         walk = skeleton.joints[walk].parent) {
        out = toMatrix(skeleton.joints[walk].localBind) * out;
    }
    return out;
}

core::u32 AnimationSystem::jointCount(core::InstanceId meshPart) const
{
    const SkeletonLibrary::Entry* entry = skeletonOf(meshPart);
    return entry == nullptr ? 0u : static_cast<core::u32>(entry->joints.size());
}

core::i32 AnimationSystem::findJoint(core::InstanceId meshPart, std::string_view name) const
{
    const SkeletonLibrary::Entry* entry = skeletonOf(meshPart);
    if (entry == nullptr)
        return -1;
    for (usize index = 0; index < entry->joints.size(); ++index) {
        if (entry->joints[index].name == name)
            return static_cast<core::i32>(index);
    }
    // A linear scan, because a rig has tens of joints and a `Bone` resolves its
    // name once and then holds the index.
    return -1;
}

core::i32 AnimationSystem::jointParent(core::InstanceId meshPart, core::u32 joint) const
{
    const SkeletonLibrary::Entry* entry = skeletonOf(meshPart);
    if (entry == nullptr || joint >= entry->joints.size())
        return -1;
    const core::u32 parent = entry->joints[joint].parent;
    return parent == asset::Joint::NoParent ? -1 : static_cast<core::i32>(parent);
}

std::string_view AnimationSystem::jointName(core::InstanceId meshPart, core::u32 joint) const
{
    const SkeletonLibrary::Entry* entry = skeletonOf(meshPart);
    if (entry == nullptr || joint >= entry->joints.size())
        return {};
    return entry->joints[joint].name;
}

bool AnimationSystem::jointModel(core::InstanceId meshPart, core::u32 joint, core::CFrameD& out) const
{
    const SkeletonLibrary::Entry* entry = skeletonOf(meshPart);
    if (entry == nullptr || joint >= entry->joints.size())
        return false;

    // The posed transform when there is a pose, and the REST chain when there is
    // not -- a character standing still has no pose at all, and a socket on its
    // hand still has to be somewhere.
    const auto found = poses_.find(keyOf(meshPart));
    const Mat4 model = found != poses_.end() && joint < found->second.model.size() ? found->second.model[joint]
                                                                                   : restModelOf(*entry, joint);
    // Orthonormalised on the way out: an exporter is free to bake scale into a
    // bind pose and often does, and a socket welded to a joint has to be rigid
    // or every part hanging off it inherits that scale (`core::cframeFromMatrix`).
    out = core::cframeFromMatrix(model);
    return true;
}

AnimationSystem::OverrideSet* AnimationSystem::overridesFor(core::InstanceId meshPart) noexcept
{
    return const_cast<OverrideSet*>(static_cast<const AnimationSystem*>(this)->overridesFor(meshPart));
}

const AnimationSystem::OverrideSet* AnimationSystem::overridesFor(core::InstanceId meshPart) const noexcept
{
    for (const OverrideSet& set : overrides_) {
        if (set.meshPart == meshPart)
            return &set;
    }
    return nullptr;
}

void AnimationSystem::setJointOverride(core::InstanceId meshPart, core::u32 joint, const core::CFrameD& model)
{
    if (jointCount(meshPart) <= joint)
        return;

    OverrideSet* set = overridesFor(meshPart);
    if (set == nullptr) {
        // Appended in the order meshes were first driven, which is an order the
        // caller controls and can therefore make deterministic. A map keyed on
        // the instance would put the iteration order in the hash (R10).
        overrides_.push_back(OverrideSet{meshPart, {}});
        set = &overrides_.back();
    }

    const Mat4 matrix = toMatrix(model);
    const auto at = std::lower_bound(set->joints.begin(), set->joints.end(), joint,
                                     [](const Override& entry, core::u32 key) { return entry.joint < key; });
    if (at != set->joints.end() && at->joint == joint) {
        // The last writer wins, and it wins the same way every time -- which is
        // what makes two things reaching for one joint a bug the author can see
        // rather than a bug that depends on iteration order.
        at->model = matrix;
        return;
    }
    set->joints.insert(at, Override{joint, matrix});
}

void AnimationSystem::clearJointOverrides(core::InstanceId meshPart)
{
    for (usize index = 0; index < overrides_.size(); ++index) {
        if (overrides_[index].meshPart != meshPart)
            continue;
        overrides_.erase(overrides_.begin() + static_cast<std::ptrdiff_t>(index));
        return;
    }
}

void AnimationSystem::commitOverrides()
{
    for (const OverrideSet& set : overrides_) {
        const SkeletonLibrary::Entry* entry = skeletonOf(set.meshPart);
        if (entry == nullptr || set.joints.empty())
            continue;

        const usize jointCount = entry->joints.size();
        Pose& pose = poses_[keyOf(set.meshPart)];
        if (pose.model.size() != jointCount) {
            // No pose this tick: the mesh has a rig and nothing playing, which
            // is exactly a limp ragdoll. Built from rest so the joints the
            // override does NOT name are still somewhere sensible.
            pose.palette.assign(jointCount, Mat4{});
            pose.model.assign(jointCount, Mat4{});
            pose.local.assign(jointCount, Mat4{});
            for (usize joint = 0; joint < jointCount; ++joint)
                pose.local[joint] = toMatrix(entry->joints[joint].localBind);
        }

        // **One forward pass with substitutions**, and this is the whole reason
        // an override is not just a write into the palette. A ragdoll simulates
        // a dozen bones; a hand has twenty. The fingers are not overridden, so
        // they take their parent's new model transform and their own unchanged
        // local -- which is what makes them ride along on the wrist instead of
        // staying where the clip left them.
        usize next = 0;
        for (usize joint = 0; joint < jointCount; ++joint) {
            if (next < set.joints.size() && set.joints[next].joint == joint) {
                pose.model[joint] = set.joints[next].model;
                ++next;
            }
            else {
                const core::u32 parent = entry->joints[joint].parent;
                pose.model[joint] =
                    parent == asset::Joint::NoParent ? pose.local[joint] : pose.model[parent] * pose.local[joint];
            }
            pose.palette[joint] = pose.model[joint] * entry->joints[joint].inverseBind;
        }
    }

    // Cleared, always. An override that outlived the tick that set it is a
    // ragdoll that keeps driving a character nobody is simulating any more.
    overrides_.clear();
}

std::span<const scene::TrackId> AnimationSystem::drainEnded()
{
    endedDrained_.swap(ended_);
    ended_.clear();
    return endedDrained_;
}

void AnimationSystem::retire(const scene::World& world)
{
    for (usize index = 1; index < tracks_.size(); ++index) {
        Track& track = tracks_[index];
        if (track.alive && !world.alive(track.player)) {
            // A track is a reference to a player and never a reason to keep one
            // alive. It stops rather than being erased, so that a handle a
            // script still holds keeps answering reads instead of resolving to
            // whatever took its slot.
            track.alive = false;
            track.playing = false;
            track.holding = false;
            poses_.erase(keyOf(track.meshPart));
        }
    }
}

const Pose* AnimationSystem::pose(core::InstanceId meshPart) const noexcept
{
    const auto found = poses_.find(keyOf(meshPart));
    return found == poses_.end() ? nullptr : &found->second;
}

} // namespace luaug::render
