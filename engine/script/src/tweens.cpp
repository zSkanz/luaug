#include "luaug/script/tweens.h"

#include "luaug/scene/world.h"
#include "luaug/script/binding.h"
#include "luaug/script/datatypes.h"
#include "luaug/script/instance_binding.h"
#include "luaug/script/services.h"

#include <lua.h>
#include <lualib.h>

#include <algorithm>
#include <cmath>

// scene's generated enum ids, through the include directory `luaug_scene`
// exports.
#include "class_descriptors.gen.h"

namespace luaug::script {
namespace {

using core::EasingDirection;
using core::EasingStyle;

[[nodiscard]] TweenSystem& tweens(lua_State* L)
{
    return context(L).services->tweens;
}

[[nodiscard]] scene::World& world(lua_State* L) noexcept
{
    return *context(L).world;
}

[[nodiscard]] TweenId checkTweenId(lua_State* L, int index)
{
    return *static_cast<TweenId*>(luaL_checkudatatagged(L, index, static_cast<int>(UserdataTag::Tween)));
}

[[nodiscard]] TweenRecord* findTween(lua_State* L, int index)
{
    return tweens(L).tweens.find(checkTweenId(L, index));
}

[[nodiscard]] const TweenInfoData& checkTweenInfo(lua_State* L, int index)
{
    return *static_cast<TweenInfoData*>(luaL_checkudatatagged(L, index, static_cast<int>(UserdataTag::TweenInfo)));
}

void pushTweenInfo(lua_State* L, const TweenInfoData& info)
{
    void* memory =
        lua_newuserdatataggedwithmetatable(L, sizeof(TweenInfoData), static_cast<int>(UserdataTag::TweenInfo));
    new (memory) TweenInfoData(info);
}

// An enum argument, checked against the enum it must belong to. The item's
// numeric value is the contract (`enums.api.luau`'s header says so), so the
// cast is safe once the id matches and the registry knows the item.
[[nodiscard]] i32 checkEnumArgument(lua_State* L, int index, scene::EnumId enumId, const char* enumName)
{
    const scene::EnumValue item = checkEnumItem(L, index);
    if (item.enumId != enumId || world(L).enums().findValue(enumId, item.value) == nullptr)
        luaL_argerror(L, index, enumName);
    return item.value;
}

// --- TweenInfo ---------------------------------------------------------------

int tweenInfoNew(lua_State* L)
{
    TweenInfoData info;
    info.time = luaL_optnumber(L, 1, 1.0);
    if (!lua_isnoneornil(L, 2))
        info.style =
            static_cast<EasingStyle>(checkEnumArgument(L, 2, scene::generated::EasingStyleEnumId, "Enum.EasingStyle"));
    if (!lua_isnoneornil(L, 3))
        info.direction = static_cast<EasingDirection>(
            checkEnumArgument(L, 3, scene::generated::EasingDirectionEnumId, "Enum.EasingDirection"));
    info.repeatCount = static_cast<i32>(luaL_optinteger(L, 4, 0));
    if (!lua_isnoneornil(L, 5))
        info.reverses = luaL_checkboolean(L, 5) != 0;
    info.delayTime = luaL_optnumber(L, 6, 0.0);

    // A negative duration or delay is refused rather than clamped: it is a sign
    // error in the caller's arithmetic, and a tween that silently took zero
    // seconds would look like the property never animated at all. A negative
    // `repeatCount` is the one negative here that means something -- repeat
    // forever -- so it is not checked.
    if (!std::isfinite(info.time) || info.time < 0.0 || !std::isfinite(info.delayTime) || info.delayTime < 0.0)
        raise(L, LUAUG_TR("script.err.tween_info_range"));

    pushTweenInfo(L, info);
    return 1;
}

int tweenInfoGetTime(lua_State* L)
{
    lua_pushnumber(L, checkTweenInfo(L, 1).time);
    return 1;
}

int tweenInfoGetDelayTime(lua_State* L)
{
    lua_pushnumber(L, checkTweenInfo(L, 1).delayTime);
    return 1;
}

int tweenInfoGetRepeatCount(lua_State* L)
{
    lua_pushnumber(L, static_cast<f64>(checkTweenInfo(L, 1).repeatCount));
    return 1;
}

int tweenInfoGetReverses(lua_State* L)
{
    lua_pushboolean(L, checkTweenInfo(L, 1).reverses ? 1 : 0);
    return 1;
}

int tweenInfoGetEasingStyle(lua_State* L)
{
    pushEnumItem(L,
                 scene::EnumValue{scene::generated::EasingStyleEnumId, static_cast<i32>(checkTweenInfo(L, 1).style)});
    return 1;
}

int tweenInfoGetEasingDirection(lua_State* L)
{
    pushEnumItem(
        L, scene::EnumValue{scene::generated::EasingDirectionEnumId, static_cast<i32>(checkTweenInfo(L, 1).direction)});
    return 1;
}

int tweenInfoEq(lua_State* L)
{
    const auto* a = static_cast<TweenInfoData*>(lua_touserdatatagged(L, 1, static_cast<int>(UserdataTag::TweenInfo)));
    const auto* b = static_cast<TweenInfoData*>(lua_touserdatatagged(L, 2, static_cast<int>(UserdataTag::TweenInfo)));
    lua_pushboolean(L, a != nullptr && b != nullptr && *a == *b);
    return 1;
}

int tweenInfoTostring(lua_State* L)
{
    const TweenInfoData& info = checkTweenInfo(L, 1);
    char text[96];
    std::snprintf(text, sizeof(text), "%.6g s", info.time);
    lua_pushstring(L, text);
    return 1;
}

// --- Tween -------------------------------------------------------------------

int tweenGetInstance(lua_State* L)
{
    const TweenRecord* record = findTween(L, 1);
    if (record == nullptr || !world(L).alive(record->target)) {
        lua_pushnil(L);
        return 1;
    }
    pushInstance(L, record->target);
    return 1;
}

int tweenGetTweenInfo(lua_State* L)
{
    const TweenRecord* record = findTween(L, 1);
    pushTweenInfo(L, record == nullptr ? TweenInfoData{} : record->info);
    return 1;
}

int tweenGetPlaybackState(lua_State* L)
{
    const TweenRecord* record = findTween(L, 1);
    const auto state = record == nullptr ? PlaybackState::Cancelled : record->state;
    pushEnumItem(L, scene::EnumValue{scene::generated::PlaybackStateEnumId, static_cast<i32>(state)});
    return 1;
}

int tweenGetCompleted(lua_State* L)
{
    const TweenRecord* record = findTween(L, 1);
    if (record == nullptr) {
        // A collected tween still answers with a signal object rather than nil,
        // so `tween.Completed:Connect` never has to be guarded. It is a signal
        // nothing will fire, which is exactly true of a tween that is over.
        pushSignalObject(L, SignalId{});
        return 1;
    }
    pushSignalObject(L, record->completed);
    return 1;
}

int tweenPlay(lua_State* L)
{
    TweenRecord* record = findTween(L, 1);
    if (record == nullptr)
        return 0;

    switch (record->state) {
    case PlaybackState::Playing:
    case PlaybackState::Delayed:
        // A no-op rather than a restart. A `Play` that silently rewound would
        // make a hover effect stutter on every frame the pointer moved.
        return 0;
    case PlaybackState::Paused:
        record->state = PlaybackState::Playing;
        return 0;
    case PlaybackState::Begin:
    case PlaybackState::Completed:
    case PlaybackState::Cancelled:
        record->elapsed = 0.0;
        record->repeatsDone = 0;
        record->returning = false;
        record->captured = false;
        record->state = record->info.delayTime > 0.0 ? PlaybackState::Delayed : PlaybackState::Playing;
        return 0;
    }
    return 0;
}

int tweenPause(lua_State* L)
{
    TweenRecord* record = findTween(L, 1);
    if (record != nullptr && (record->state == PlaybackState::Playing || record->state == PlaybackState::Delayed))
        record->state = PlaybackState::Paused;
    return 0;
}

void finish(lua_State* L, TweenRecord& record, PlaybackState state)
{
    record.state = state;
    pushEnumItem(L, scene::EnumValue{scene::generated::PlaybackStateEnumId, static_cast<i32>(state)});
    fireSignal(L, record.completed, lua_gettop(L), 1);
    lua_pop(L, 1);
}

int tweenCancel(lua_State* L)
{
    TweenRecord* record = findTween(L, 1);
    if (record == nullptr || record->state == PlaybackState::Completed || record->state == PlaybackState::Cancelled)
        return 0;
    // The property keeps whatever value it had reached. Cancelling is not
    // undoing: a tween that snapped back would be impossible to interrupt
    // gracefully, which is the one thing `Cancel` is for.
    finish(L, *record, PlaybackState::Cancelled);
    return 0;
}

// --- Interpolation -----------------------------------------------------------

// Which value types a tween can move. Deliberately not "everything a property
// can hold": there is no halfway between two booleans or two strings, and a
// goal of either is a caller's mistake worth reporting at `Create`.
[[nodiscard]] bool interpolable(scene::ValueType type) noexcept
{
    switch (type) {
    case scene::ValueType::Number:
    case scene::ValueType::Vector3:
    case scene::ValueType::CFrame:
    case scene::ValueType::Color3:
    case scene::ValueType::Vector2:
    case scene::ValueType::UDim:
    case scene::ValueType::UDim2:
    case scene::ValueType::Rect:
        return true;
    case scene::ValueType::Nil:
    case scene::ValueType::Bool:
    case scene::ValueType::String:
    case scene::ValueType::Instance:
    case scene::ValueType::EnumItem:
        return false;
    }
    return false;
}

[[nodiscard]] f32 mix(f32 from, f32 to, f32 t) noexcept
{
    return from + (to - from) * t;
}

[[nodiscard]] core::Vec2 mix(core::Vec2 from, core::Vec2 to, f32 t) noexcept
{
    return core::Vec2{mix(from.x, to.x, t), mix(from.y, to.y, t)};
}

[[nodiscard]] core::UDim mix(core::UDim from, core::UDim to, f32 t) noexcept
{
    return core::UDim{mix(from.scale, to.scale, t), mix(from.offset, to.offset, t)};
}

[[nodiscard]] scene::Value blend(const scene::Value& from, const scene::Value& to, f32 t)
{
    switch (scene::valueType(to)) {
    case scene::ValueType::Number:
        return scene::Value{std::get<f64>(from) + (std::get<f64>(to) - std::get<f64>(from)) * static_cast<f64>(t)};
    case scene::ValueType::Vector3: {
        const core::Vec3& a = std::get<core::Vec3>(from);
        const core::Vec3& b = std::get<core::Vec3>(to);
        return scene::Value{core::Vec3{mix(a.x, b.x, t), mix(a.y, b.y, t), mix(a.z, b.z, t)}};
    }
    case scene::ValueType::CFrame: {
        // Position and basis both, through the CFrame's own Lerp: a rotation
        // interpolated component-wise stops being a rotation halfway through.
        // The widening is written out: `CFrameD::lerp` takes an f64 because the
        // translation is one, and `-Wdouble-promotion` is an error on `engine/`
        // precisely so a narrow value entering a wide computation is a decision
        // somebody made rather than one the compiler made.
        return scene::Value{
            core::lerp(std::get<core::CFrameD>(from), std::get<core::CFrameD>(to), static_cast<f64>(t))};
    }
    case scene::ValueType::Color3:
        return scene::Value{core::lerp(std::get<core::Color3>(from), std::get<core::Color3>(to), t)};
    case scene::ValueType::Vector2:
        return scene::Value{mix(std::get<core::Vec2>(from), std::get<core::Vec2>(to), t)};
    case scene::ValueType::UDim:
        return scene::Value{mix(std::get<core::UDim>(from), std::get<core::UDim>(to), t)};
    case scene::ValueType::UDim2: {
        const core::UDim2& a = std::get<core::UDim2>(from);
        const core::UDim2& b = std::get<core::UDim2>(to);
        return scene::Value{core::UDim2{mix(a.x, b.x, t), mix(a.y, b.y, t)}};
    }
    case scene::ValueType::Rect: {
        const core::Rect& a = std::get<core::Rect>(from);
        const core::Rect& b = std::get<core::Rect>(to);
        return scene::Value{core::Rect{mix(a.min, b.min, t), mix(a.max, b.max, t)}};
    }
    default:
        return to;
    }
}

} // namespace

// --- TweenService ------------------------------------------------------------

int tweenServiceGetValue(lua_State* L)
{
    const auto alpha = static_cast<f32>(luaL_checknumber(L, 2));
    const auto style =
        static_cast<EasingStyle>(checkEnumArgument(L, 3, scene::generated::EasingStyleEnumId, "Enum.EasingStyle"));
    const auto direction = static_cast<EasingDirection>(
        checkEnumArgument(L, 4, scene::generated::EasingDirectionEnumId, "Enum.EasingDirection"));
    lua_pushnumber(L, static_cast<f64>(core::ease(alpha, style, direction)));
    return 1;
}

int tweenServiceCreate(lua_State* L)
{
    const core::InstanceId target = checkInstance(L, 2);
    const TweenInfoData info = checkTweenInfo(L, 3);
    luaL_checktype(L, 4, LUA_TTABLE);

    scene::World& w = world(L);
    const scene::ClassId classId = w.classOf(target);

    TweenRecord record;
    record.target = target;
    record.info = info;

    // Every goal is validated NOW rather than at the first write. A tween that
    // played for half a second and then reported a typo would be reporting it
    // from the wrong place and at the wrong time.
    lua_pushnil(L);
    while (lua_next(L, 4) != 0) {
        if (lua_type(L, -2) != LUA_TSTRING)
            raise(L, LUAUG_TR("script.err.tween_goal_name"));

        size_t length = 0;
        const char* name = lua_tolstring(L, -2, &length);
        const core::NameAtom atom = w.atoms().intern(std::string_view{name, length});

        const scene::PropertyDesc* property = w.classes().findProperty(classId, atom);
        const core::I18nArg args[] = {
            {"className", w.atoms().text(w.classes().find(classId)->name)},
            {"property", std::string_view{name, length}},
        };
        if (property == nullptr)
            raise(L, LUAUG_TR("scene.err.unknown_property"), args);
        if (property->readOnly || property->set == nullptr)
            raise(L, LUAUG_TR("scene.err.read_only_property"), args);
        if (!interpolable(property->type))
            raise(L, LUAUG_TR("script.err.tween_goal_type"), args);

        const std::optional<scene::Value> goal = toValue(L, -1, property->type);
        if (!goal.has_value())
            raise(L, property->errKeyOnInvalidSet, args);

        record.goals.push_back(TweenGoal{atom, scene::Value{}, *goal});
        lua_pop(L, 1);
    }

    // Sorted by the property's atom, so two tweens built from the same table
    // write in the same order on every run. Luau's `next` walks a table's
    // internal layout, and R10 forbids that reaching observable order.
    std::stable_sort(record.goals.begin(), record.goals.end(),
                     [](const TweenGoal& a, const TweenGoal& b) { return a.property.id < b.property.id; });

    record.completed = createScriptSignal(L);

    const TweenId id = tweens(L).tweens.insert(std::move(record));
    void* memory = lua_newuserdatataggedwithmetatable(L, sizeof(TweenId), static_cast<int>(UserdataTag::Tween));
    *static_cast<TweenId*>(memory) = id;
    return 1;
}

void registerTweenTypes(lua_State* L)
{
    VmContext& ctx = context(L);
    core::AtomTable& atoms = ctx.world->atoms();

    MemberTable& infoGetters = ctx.getters[static_cast<usize>(UserdataTag::TweenInfo)];
    addMember(infoGetters, atoms, "Time", tweenInfoGetTime);
    addMember(infoGetters, atoms, "DelayTime", tweenInfoGetDelayTime);
    addMember(infoGetters, atoms, "RepeatCount", tweenInfoGetRepeatCount);
    addMember(infoGetters, atoms, "Reverses", tweenInfoGetReverses);
    addMember(infoGetters, atoms, "EasingStyle", tweenInfoGetEasingStyle);
    addMember(infoGetters, atoms, "EasingDirection", tweenInfoGetEasingDirection);
    installTagMetatable(L, UserdataTag::TweenInfo, tweenInfoEq, tweenInfoTostring);

    const luaL_Reg constructors[] = {
        {"new", tweenInfoNew},
        {nullptr, nullptr},
    };
    luaL_register(L, "TweenInfo", constructors);
    lua_setreadonly(L, -1, true);
    lua_pop(L, 1);

    MemberTable& tweenGetters = ctx.getters[static_cast<usize>(UserdataTag::Tween)];
    addMember(tweenGetters, atoms, "Instance", tweenGetInstance);
    addMember(tweenGetters, atoms, "TweenInfo", tweenGetTweenInfo);
    addMember(tweenGetters, atoms, "PlaybackState", tweenGetPlaybackState);
    addMember(tweenGetters, atoms, "Completed", tweenGetCompleted);

    MemberTable& tweenMethods = ctx.methods[static_cast<usize>(UserdataTag::Tween)];
    addMember(tweenMethods, atoms, "Play", tweenPlay);
    addMember(tweenMethods, atoms, "Pause", tweenPause);
    addMember(tweenMethods, atoms, "Cancel", tweenCancel);

    // No `__eq`: two handles to one tween compare by their bits, which is what
    // a trivially-copyable payload gives for free, and there is no second tween
    // that could be "equal" to one.
    installTagMetatable(L, UserdataTag::Tween, nullptr, nullptr);
}

void stepTweens(lua_State* L, f64 fixedDt)
{
    TweenSystem& system = tweens(L);
    scene::World& w = world(L);

    // Slot order, which is a pure function of the operation sequence, so two
    // tweens writing the same property write in the same order on every run.
    system.tweens.forEach([&](TweenId, TweenRecord& record) {
        if (record.state != PlaybackState::Playing && record.state != PlaybackState::Delayed)
            return;

        // A tween whose target is gone stops rather than raising. The target is
        // a reference and not an ownership: a tween is not a reason to keep an
        // instance alive.
        if (!w.alive(record.target)) {
            finish(L, record, PlaybackState::Cancelled);
            return;
        }

        record.elapsed += fixedDt;

        if (record.state == PlaybackState::Delayed) {
            if (record.elapsed < record.info.delayTime)
                return;
            record.elapsed -= record.info.delayTime;
            record.state = PlaybackState::Playing;
        }

        // Captured at the first tick that writes, not at `Create`: a tween
        // created now and played in three seconds should move from where the
        // property IS then.
        if (!record.captured) {
            for (TweenGoal& goal : record.goals) {
                const scene::PropertyDesc* property = w.classes().findProperty(w.classOf(record.target), goal.property);
                if (property != nullptr && property->get != nullptr)
                    goal.start = property->get(w, record.target);
            }
            record.captured = true;
        }

        const f64 duration = record.info.time;
        bool traversalDone = false;
        f64 raw = 1.0;
        if (duration <= 0.0) {
            // A zero-length tween lands on its goal in one tick rather than
            // dividing by zero. It is a legal thing to ask for -- "set this, but
            // through the tween system so `Completed` fires".
            traversalDone = true;
        }
        else if (record.elapsed >= duration) {
            traversalDone = true;
        }
        else {
            raw = record.elapsed / duration;
        }

        const f64 progress = record.returning ? 1.0 - raw : raw;
        const f32 t = core::ease(static_cast<f32>(progress), record.info.style, record.info.direction);

        for (const TweenGoal& goal : record.goals) {
            // Through `setProperty`, which is the same call the binding makes
            // for `part.Size = v`: same equality filter, same change queue, same
            // range refusal. A second route would make a tweened write mean
            // something different from an assigned one.
            (void)w.setProperty(record.target, goal.property, blend(goal.start, goal.goal, t));
        }

        if (!traversalDone)
            return;

        if (record.info.reverses && !record.returning) {
            record.returning = true;
            record.elapsed = 0.0;
            return;
        }

        record.returning = false;
        record.elapsed = 0.0;
        record.captured = false;
        record.repeatsDone += 1;

        // A negative `RepeatCount` repeats forever, which is the one negative
        // number in `TweenInfo` that means something.
        const bool forever = record.info.repeatCount < 0;
        if (forever || record.repeatsDone <= record.info.repeatCount) {
            record.state = record.info.delayTime > 0.0 ? PlaybackState::Delayed : PlaybackState::Playing;
            return;
        }

        finish(L, record, PlaybackState::Completed);
    });
}

} // namespace luaug::script
