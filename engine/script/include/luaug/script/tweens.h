// `TweenService`, `TweenInfo` and `Tween` (api-design.md §2.1, §2.3).
//
// Two decisions shape everything here, and both are in the M6 brief:
//
//   1. **A tween writes through the property setter a script writes through.**
//      Not a second route into the components. That is worth more than the
//      equality filter it preserves (M2 Decision 6, about a third of the
//      10k-parts measurement): a second route would also bypass
//      `GetPropertyChangedSignal`, the physics mirror's change tracking and the
//      range refusals, so a tweened `Size` and an assigned `Size` would MEAN
//      different things.
//
//   2. **A tween steps on the SimClock.** `TweenInfo.Time` is in the same
//      seconds `task.wait` counts, so a tween is a whole number of ticks and a
//      replay reproduces it exactly. The cost is that a tween is as smooth as
//      the tick rate; the alternative is a system whose entire purpose is
//      writing properties reading the wall clock to decide what to write.
//      Rendering already interpolates transforms with `alpha`, so a tween on a
//      `CFrame` is smooth on screen anyway.
//
// A `Tween` is a value type rather than an Instance, like `AnimationTrack`
// (§2.2): nothing parents one and nothing finds one in the tree.
#pragma once

#include "luaug/core/easing.h"
#include "luaug/core/id.h"
#include "luaug/core/name_atom.h"
#include "luaug/core/slotmap.h"
#include "luaug/scene/value.h"
#include "luaug/script/signals.h"

#include <vector>

struct lua_State;

namespace luaug::script {

// A handle into `TweenSystem::tweens`, which is what a `Tween` userdata holds.
struct TweenId
{
    u32 index = 0;
    u32 generation = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return generation != 0; }
    [[nodiscard]] constexpr bool operator==(const TweenId&) const noexcept = default;
};

// `Enum.PlaybackState`'s values.
enum class PlaybackState : i32
{
    Begin = 0,
    Delayed = 1,
    Playing = 2,
    Paused = 3,
    Completed = 4,
    Cancelled = 5,
};

// The timing half, which is exactly `TweenInfo` and is the payload of that
// userdata tag.
struct TweenInfoData
{
    f64 time = 1.0;
    f64 delayTime = 0.0;
    i32 repeatCount = 0;
    core::EasingStyle style = core::EasingStyle::Quad;
    core::EasingDirection direction = core::EasingDirection::Out;
    bool reverses = false;

    [[nodiscard]] constexpr bool operator==(const TweenInfoData&) const noexcept = default;
};

// One property being moved. `start` is captured when the tween first begins to
// write rather than when it is created: a tween created now and played in three
// seconds should move from where the property IS then, not from where it was.
struct TweenGoal
{
    core::NameAtom property;
    scene::Value start;
    scene::Value goal;
};

struct TweenRecord
{
    core::InstanceId target;
    TweenInfoData info;
    std::vector<TweenGoal> goals;
    SignalId completed;

    // Seconds inside the current phase -- the delay, or the traversal.
    f64 elapsed = 0.0;
    // How many traversals have finished. A reversing tween counts a there-and-
    // back as one.
    i32 repeatsDone = 0;
    PlaybackState state = PlaybackState::Begin;
    // Playing the return half of a reversing traversal.
    bool returning = false;
    // Whether `start` has been captured for this traversal.
    bool captured = false;
};

// Every live tween in the VM. Held in `ServiceState` beside the rest of the
// per-VM state, and stepped once per simulation tick by `ScriptRuntime`.
struct TweenSystem
{
    core::SlotMap<TweenRecord, TweenId> tweens;
};

// Installs the `TweenInfo` and `Tween` metatables and the `TweenInfo` global.
void registerTweenTypes(lua_State* L);

// Advances every playing tween by one tick and writes what it computed. Called
// from the sim tick, before the `PreSimulation` drain, so a handler in that
// phase reads the value this tick produced.
void stepTweens(lua_State* L, f64 fixedDt);

// `TweenService`'s two methods. Declared here and registered from the one table
// in `services.cpp` that lists every instance method, so the boot-time
// cross-check against the IDL sees them like any other.
int tweenServiceCreate(lua_State* L);
int tweenServiceGetValue(lua_State* L);

} // namespace luaug::script
