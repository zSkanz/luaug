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

scene::TrackId AnimationSystem::createTrack(core::InstanceId player, std::string_view clip)
{
    Track track;
    track.player = player;

    // The skeleton comes from the player's PARENT, which is the `MeshPart` that
    // names the content. An `AnimationPlayer` parented anywhere else finds
    // nothing, which is what its own doc promises.
    track.meshPart = world_->parentOf(player);
    if (const scene::MeshPartComponent* mesh = world_->meshParts().find(track.meshPart); mesh != nullptr) {
        track.content = mesh->meshContent;
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
        // Every track made against THIS mesh. Two players under one mesh are two
        // sources blending into one pose, which is what they look like on
        // screen.
        if (track.meshPart != meshPart)
            continue;
        if (track.weight <= 0.0f)
            continue;
        contributed = true;

        const asset::AnimationClip& clip = skeleton.clips[track.clip];
        const auto time = static_cast<f32>(track.time);

        for (const asset::AnimationChannel& channel : clip.channels) {
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
        poses_.erase(keyOf(meshPart));
        return;
    }

    model_.assign(jointCount, Mat4{});
    Pose& pose = poses_[keyOf(meshPart)];
    pose.palette.assign(jointCount, Mat4{});

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
        // One forward pass, parents first -- which the loader guarantees by
        // sorting the joints (asset/model.h). A graph walk per frame would be
        // the alternative, and this is the whole reason it is not needed.
        model_[joint] = bone.parent == asset::Joint::NoParent ? local : model_[bone.parent] * local;
        pose.palette[joint] = model_[joint] * bone.inverseBind;
    }
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
