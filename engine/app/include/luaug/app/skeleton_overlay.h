// Drawing a rig as lines, for the viewport's `View > Skeletons` (E9 step 9).
//
// **A skeleton is the one thing in a scene with no visual at all.** Joints live
// in a flat array inside a render-side library keyed by URN, so before this the
// only way to learn what a rig called its hand was to type a guess into
// `Bone.JointName` and watch whether `JointIndex` stopped saying -1. Every
// editor that animates draws the armature.
//
// Lines rather than instances, which is E9's assumption 2 made visible: the
// horse that opened this milestone carries 677 joints, and one `Bone` per joint
// would be 677 Explorer rows, 677 entries in the change queue and 677
// contributions to the world hash bought for a picture. `Bone` stays created on
// demand, for the joints somebody actually welds something to.
//
// **Its own unit rather than a lambda in the frame loop** because the one thing
// in it that can be wrong is the composition -- a joint is in the MESH's space
// and the picture is in the world's -- and that is exactly what got a horse
// drawn on its back once already. A free function over an interface is a
// function a test can drive with a rig it invented.
#pragma once

namespace luaug::render {
class DebugDraw;
}

namespace luaug::scene {
class World;
class SkeletonHost;
} // namespace luaug::scene

namespace luaug::app {

// Appends one cross per joint and one line per bone, in WORLD space -- which is
// what every other debug line records, because `DebugDraw` rebases the whole
// buffer once after extraction, when the camera is finally known (D011).
//
// Appends nothing for a world with no skinned mesh in it, which is most of them.
void drawSkeletons(const scene::World& world, const scene::SkeletonHost& skeleton, render::DebugDraw& draw);

} // namespace luaug::app
