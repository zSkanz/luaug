#include "luaug/script/animation.h"

#include "luaug/scene/world.h"
#include "luaug/script/binding.h"
#include "luaug/script/datatypes.h"
#include "luaug/script/instance_binding.h"
#include "luaug/script/services.h"

#include <lua.h>
#include <lualib.h>

#include <algorithm>
#include <string>
#include <string_view>

namespace luaug::script {
namespace {

// The payload of an `AnimationTrack` userdata: an index into the VM's record
// list. Four bytes, and the `scene::TrackId` is not in it -- the record holds
// that, and putting it in both is two places to disagree.
struct TrackUserdata
{
    u32 record = 0;
};

static_assert(sizeof(TrackUserdata) == 4, "the AnimationTrack payload's size is an ABI decision");

[[nodiscard]] ServiceState& services(lua_State* L)
{
    return *context(L).services;
}

[[nodiscard]] TrackRecord* findRecord(lua_State* L, int index)
{
    const TrackUserdata payload =
        *static_cast<TrackUserdata*>(luaL_checkudatatagged(L, index, static_cast<int>(UserdataTag::AnimationTrack)));
    ServiceState& state = services(L);
    if (payload.record >= state.animationTracks.size())
        return nullptr;
    return &state.animationTracks[payload.record];
}

// The host, or null in a build with no render module. Every reader checks: a
// track with no host is a track that plays nothing, which is the same answer an
// unskinned mesh gives.
[[nodiscard]] scene::AnimationHost* host(lua_State* L)
{
    return services(L).animation;
}

[[nodiscard]] scene::TrackState stateOf(lua_State* L, const TrackRecord* record)
{
    scene::AnimationHost* animation = host(L);
    if (record == nullptr || animation == nullptr)
        return {};
    return animation->state(record->track);
}

// --- Reads -------------------------------------------------------------------

int trackGetPlaying(lua_State* L)
{
    lua_pushboolean(L, stateOf(L, findRecord(L, 1)).playing);
    return 1;
}

int trackGetLooped(lua_State* L)
{
    lua_pushboolean(L, stateOf(L, findRecord(L, 1)).looped);
    return 1;
}

int trackGetSpeed(lua_State* L)
{
    lua_pushnumber(L, static_cast<double>(stateOf(L, findRecord(L, 1)).speed));
    return 1;
}

int trackGetWeight(lua_State* L)
{
    lua_pushnumber(L, static_cast<double>(stateOf(L, findRecord(L, 1)).weight));
    return 1;
}

int trackGetLength(lua_State* L)
{
    lua_pushnumber(L, static_cast<double>(stateOf(L, findRecord(L, 1)).length));
    return 1;
}

int trackGetTimePosition(lua_State* L)
{
    lua_pushnumber(L, stateOf(L, findRecord(L, 1)).timePosition);
    return 1;
}

int trackGetEnded(lua_State* L)
{
    const TrackRecord* record = findRecord(L, 1);
    if (record == nullptr)
        return 0;
    pushSignalObject(L, record->ended);
    return 1;
}

// --- Writes ------------------------------------------------------------------

int trackSetLooped(lua_State* L)
{
    luaL_checktype(L, 3, LUA_TBOOLEAN);
    const TrackRecord* record = findRecord(L, 1);
    if (scene::AnimationHost* animation = host(L); animation != nullptr && record != nullptr)
        animation->setLooped(record->track, lua_toboolean(L, 3) != 0);
    return 0;
}

int trackSetSpeed(lua_State* L)
{
    const auto speed = static_cast<f32>(luaL_checknumber(L, 3));
    const TrackRecord* record = findRecord(L, 1);
    if (scene::AnimationHost* animation = host(L); animation != nullptr && record != nullptr)
        animation->adjustSpeed(record->track, speed);
    return 0;
}

int trackSetWeight(lua_State* L)
{
    const auto weight = static_cast<f32>(luaL_checknumber(L, 3));
    const TrackRecord* record = findRecord(L, 1);
    // No fade: an assignment is immediate, and the faded form is `Play`'s and
    // `Stop`'s first argument. A property that took a second parameter would not
    // be a property.
    if (scene::AnimationHost* animation = host(L); animation != nullptr && record != nullptr)
        animation->adjustWeight(record->track, weight, 0.0f);
    return 0;
}

// --- Methods -----------------------------------------------------------------

int trackPlay(lua_State* L)
{
    const auto fadeTime = static_cast<f32>(luaL_optnumber(L, 2, 0.0));
    const TrackRecord* record = findRecord(L, 1);
    scene::AnimationHost* animation = host(L);
    if (animation == nullptr || record == nullptr)
        return 0;

    // Weight and speed come from the track's own properties rather than from
    // arguments, so that `Play(0.2)` fades in to whatever the track was set to.
    // api-design.md §2.2 gives `Play` one parameter and this is what makes that
    // enough.
    const scene::TrackState state = animation->state(record->track);
    animation->play(record->track, fadeTime, state.weight, state.speed);
    return 0;
}

int trackStop(lua_State* L)
{
    const auto fadeTime = static_cast<f32>(luaL_optnumber(L, 2, 0.0));
    const TrackRecord* record = findRecord(L, 1);
    if (scene::AnimationHost* animation = host(L); animation != nullptr && record != nullptr)
        animation->stop(record->track, fadeTime);
    return 0;
}

} // namespace

// `AnimationPlayer:LoadAnimation(content)`.
//
// The argument is a Content URN, and everything after a `#` is the clip's name
// inside the file. A string with no `#` is a clip name in the player's own
// mesh, which is the common case and the one worth being short:
// `player:LoadAnimation("Walk")`.
//
// **A path before the `#` must be the mesh the player is under.** v1 has no
// asset pipeline (M7 is where clips become addressable on their own), so a clip
// only exists inside the glTF file its skeleton came from; a URN naming a
// different file loads nothing rather than silently playing the wrong rig.
int animationPlayerLoadAnimation(lua_State* L)
{
    const core::InstanceId player = checkInstance(L, 1);
    usize length = 0;
    const char* text = luaL_checklstring(L, 2, &length);
    const std::string_view content{text, length};

    std::string_view clip = content;
    if (const usize hash = content.rfind('#'); hash != std::string_view::npos) {
        const std::string_view path = content.substr(0, hash);
        clip = content.substr(hash + 1);

        if (!path.empty()) {
            const scene::World& w = *context(L).world;
            const scene::MeshPartComponent* mesh = w.meshParts().find(w.parentOf(player));
            if (mesh == nullptr || w.atoms().text(mesh->meshContent) != path)
                clip = {};
        }
    }

    ServiceState& state = services(L);
    TrackRecord record;
    if (scene::AnimationHost* animation = state.animation; animation != nullptr && !clip.empty())
        record.track = animation->createTrack(player, clip);
    record.ended = createScriptSignal(L);

    state.animationTracks.push_back(record);

    void* memory =
        lua_newuserdatataggedwithmetatable(L, sizeof(TrackUserdata), static_cast<int>(UserdataTag::AnimationTrack));
    static_cast<TrackUserdata*>(memory)->record = static_cast<u32>(state.animationTracks.size() - 1);
    return 1;
}

void registerAnimationTypes(lua_State* L)
{
    VmContext& ctx = context(L);
    core::AtomTable& atoms = ctx.world->atoms();

    MemberTable& getters = ctx.getters[static_cast<usize>(UserdataTag::AnimationTrack)];
    addMember(getters, atoms, "Playing", trackGetPlaying);
    addMember(getters, atoms, "Looped", trackGetLooped);
    addMember(getters, atoms, "Speed", trackGetSpeed);
    addMember(getters, atoms, "Weight", trackGetWeight);
    addMember(getters, atoms, "Length", trackGetLength);
    addMember(getters, atoms, "TimePosition", trackGetTimePosition);
    addMember(getters, atoms, "Ended", trackGetEnded);

    // The first non-empty setter table in the surface. A track is a handle, so a
    // write to one is seen by every holder of it -- which is what makes these
    // properties rather than the `SetLooped`/`AdjustSpeed` pair a value type
    // would have needed.
    MemberTable& setters = ctx.setters[static_cast<usize>(UserdataTag::AnimationTrack)];
    addMember(setters, atoms, "Looped", trackSetLooped);
    addMember(setters, atoms, "Speed", trackSetSpeed);
    addMember(setters, atoms, "Weight", trackSetWeight);

    MemberTable& methods = ctx.methods[static_cast<usize>(UserdataTag::AnimationTrack)];
    addMember(methods, atoms, "Play", trackPlay);
    addMember(methods, atoms, "Stop", trackStop);

    // No `__eq`: two handles to one track compare by their bits, which a
    // trivially-copyable payload gives for free.
    installTagMetatable(L, UserdataTag::AnimationTrack, nullptr, nullptr);
}

void fireAnimationEnded(lua_State* L, std::span<const scene::TrackId> ended)
{
    if (ended.empty())
        return;

    ServiceState& state = services(L);
    // Walked in the host's order, which is track order, which is load order --
    // R10's requirement that an observable order come from something that
    // promises one.
    for (const scene::TrackId track : ended) {
        for (const TrackRecord& record : state.animationTracks) {
            // Every handle loaded for this track fires, because two
            // `LoadAnimation` calls for one clip are two handles that both
            // connected.
            if (record.track == track && track != 0)
                fireSignal(L, record.ended, 0, 0);
        }
    }
}

} // namespace luaug::script
