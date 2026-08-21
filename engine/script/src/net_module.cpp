// `@std/net` -- the game VM's network surface (api-design.md §7, ADR 0012).
//
// **What v1 ships is client-side and nothing else.** `net.request` is an HTTP
// client; `net.serve` does NOT exist. That is a decision rather than an
// omission, and it is worth having written down:
//
//   - The roadmap's M7 bullet is "the minimal socket/HTTP surface … (loopback
//     echo example only -- replication is post-v1)". A server is not minimal:
//     it is a listener, a connection lifetime, a request router and a
//     concurrency model, all of which are design nobody has approved.
//   - ADR 0035 already decided that the ENGINE never listens, in any profile,
//     because a loopback listener is reachable by every process on the machine.
//     A `net.serve` in the game VM does not contradict that decision, but it
//     does need its own version of it, and a permission flag with nothing
//     behind it yet is worse than an absent function.
//   - `serve` is reserved rather than removed: api-design.md §7 lists it, and
//     when it lands it lands behind `[permissions] net_serve`.
//
// **Naming follows Lute, not ADR 0034**, and this is the one place that is
// right. `@std/*` exists to be the Lute-compatible surface (ADR 0030) so that
// utility and backend code runs unchanged on both runtimes; a `net.request`
// that returned `StatusCode` where Lute returns `statusCode` would make the
// portability the namespace is FOR into a lie. CLAUDE.md's own rule already
// defers to this -- "whatever `@std/*` and `@luaug/*` export".
//
// **R10.** A response arrives when a server answers, which is a wall-clock fact.
// Nothing here reaches simulation state, and a script that writes a response
// into the world has taken its replay's determinism into its own hands. That is
// the game's decision to make and the reason it is stated rather than prevented:
// the alternative is a network API no backend-talking game can use.
#include "luaug/script/net_module.h"

#include "luaug/core/i18n.h"
#include "luaug/net/async_client.h"
#include "luaug/script/binding.h"
#include "luaug/script/modules.h"
#include "luaug/script/services.h"
#include "luaug/script/signals.h"

#include <lua.h>
#include <lualib.h>

#include <string>
#include <string_view>
#include <vector>

namespace luaug::script {
namespace {

[[nodiscard]] ServiceState& services(lua_State* L) noexcept
{
    return *context(L).services;
}

[[nodiscard]] net::AsyncClient& client(lua_State* L)
{
    ServiceState& state = services(L);
    if (state.netClient == nullptr) {
        state.netClient = std::make_unique<net::AsyncClient>();
    }
    return *state.netClient;
}

// Reads an optional string field. Returns false when the field is absent, and
// RAISES when it is present with the wrong type -- the two are different
// mistakes and only one of them is the caller forgetting something.
[[nodiscard]] bool optionalString(lua_State* L, int table, const char* field, std::string& out)
{
    lua_getfield(L, table, field);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    // `lua_type` rather than `lua_isstring`, which answers true for a NUMBER --
    // Lua coerces, and a `url = 8080` that silently became "8080" would be a
    // request to a host called 8080 rather than the mistake it is.
    if (lua_type(L, -1) != LUA_TSTRING) {
        const core::I18nArg args[] = {{"field", std::string_view{field}}};
        lua_pop(L, 1);
        raise(L, LUAUG_TR("script.err.net_field_type"), args);
    }
    size_t length = 0;
    const char* text = lua_tolstring(L, -1, &length);
    out.assign(text, length);
    lua_pop(L, 1);
    return true;
}

[[nodiscard]] bool optionalNumber(lua_State* L, int table, const char* field, f64& out)
{
    lua_getfield(L, table, field);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    if (lua_type(L, -1) != LUA_TNUMBER) {
        const core::I18nArg args[] = {{"field", std::string_view{field}}};
        lua_pop(L, 1);
        raise(L, LUAUG_TR("script.err.net_field_type"), args);
    }
    out = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return true;
}

void readHeaders(lua_State* L, int table, std::vector<net::HttpHeader>& out)
{
    lua_getfield(L, table, "headers");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return;
    }
    if (!lua_istable(L, -1)) {
        const core::I18nArg args[] = {{"field", std::string_view{"headers"}}};
        lua_pop(L, 1);
        raise(L, LUAUG_TR("script.err.net_field_type"), args);
    }

    const int headers = lua_gettop(L);
    lua_pushnil(L);
    while (lua_next(L, headers) != 0) {
        // Key and value both have to be strings. A number key here means the
        // caller passed an ARRAY of headers, which is a different shape and
        // silently produces a request with a header called "1".
        if (lua_type(L, -2) != LUA_TSTRING || lua_type(L, -1) != LUA_TSTRING) {
            lua_pop(L, 3);
            raise(L, LUAUG_TR("script.err.net_header_shape"));
        }
        size_t nameLength = 0;
        const char* name = lua_tolstring(L, -2, &nameLength);
        size_t valueLength = 0;
        const char* value = lua_tolstring(L, -1, &valueLength);
        out.push_back({std::string(name, nameLength), std::string(value, valueLength)});
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

int netRequest(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);

    net::HttpRequest request;
    if (!optionalString(L, 1, "url", request.url)) {
        raise(L, LUAUG_TR("script.err.net_url_required"));
    }
    (void)optionalString(L, 1, "method", request.method);
    (void)optionalString(L, 1, "body", request.body);
    readHeaders(L, 1, request.headers);

    f64 timeout = 0.0;
    if (optionalNumber(L, 1, "timeout", timeout)) {
        // SECONDS at the boundary and milliseconds underneath, because every
        // other duration a script writes in this engine is in seconds
        // (`task.wait`, `TweenInfo`) and a lone millisecond field is the kind of
        // inconsistency that costs somebody an afternoon.
        if (!(timeout > 0.0)) {
            raise(L, LUAUG_TR("scene.err.number_positive"));
        }
        request.timeoutMs = static_cast<u32>(timeout * 1000.0);
    }

    const net::NetTicket ticket = client(L).submit(std::move(request));
    if (!ticket.valid()) {
        raise(L, LUAUG_TR("script.err.net_client_closed"));
    }

    ServiceState::NetWaiter waiter;
    waiter.ticket = ticket;
    lua_pushthread(L);
    waiter.threadRef = lua_ref(L, -1);
    lua_pop(L, 1);
    services(L).netWaiters.push_back(waiter);

    // Yields ALWAYS, including for a request that fails before it leaves --
    // a bad URL is discovered on the worker, not here. A function that
    // sometimes yields and sometimes returns is one whose callers have to be
    // written for both, which is the trap `WaitForChild` avoids the same way.
    return lua_yield(L, 0);
}

void pushHeaders(lua_State* L, const std::vector<net::HttpHeader>& headers)
{
    lua_createtable(L, 0, static_cast<int>(headers.size()));
    for (const net::HttpHeader& header : headers) {
        lua_pushlstring(L, header.value.data(), header.value.size());
        lua_setfield(L, -2, header.name.c_str());
    }
}

// Lute's response shape, field for field. See the file header for why this is
// camelCase where the rest of the engine's API is not.
void pushResult(lua_State* L, const net::NetResult& result)
{
    lua_createtable(L, 0, 5);

    if (result.error.has_value()) {
        lua_pushboolean(L, 0);
        lua_setfield(L, -2, "ok");
        lua_pushlstring(L, result.error->message.data(), result.error->message.size());
        lua_setfield(L, -2, "statusMessage");
        lua_pushinteger(L, 0);
        lua_setfield(L, -2, "statusCode");
        lua_pushliteral(L, "");
        lua_setfield(L, -2, "body");
        lua_createtable(L, 0, 0);
        lua_setfield(L, -2, "headers");
        return;
    }

    // `ok` is about the TRANSPORT and not the status code -- a 404 is a server
    // answering. Lute's meaning, and the one that lets a caller tell a missing
    // resource from a dead host.
    lua_pushboolean(L, result.response.ok ? 1 : 0);
    lua_setfield(L, -2, "ok");
    lua_pushinteger(L, static_cast<int>(result.response.statusCode));
    lua_setfield(L, -2, "statusCode");
    lua_pushlstring(L, result.response.statusMessage.data(), result.response.statusMessage.size());
    lua_setfield(L, -2, "statusMessage");
    lua_pushlstring(L, result.response.body.data(), result.response.body.size());
    lua_setfield(L, -2, "body");
    pushHeaders(L, result.response.headers);
    lua_setfield(L, -2, "headers");
}

} // namespace

int openStdNet(lua_State* L)
{
    lua_createtable(L, 0, 1);
    lua_pushcfunction(L, netRequest, "request");
    lua_setfield(L, -2, "request");

    // Frozen, so a script cannot add a `serve` of its own and hand it to code
    // written against the real one later. `@std/*` naming a thing is a contract
    // about what that name means.
    lua_setreadonly(L, -1, true);
    return 1;
}

void registerStdModules(lua_State* L)
{
    registerNativeModule(L, "@std/net", openStdNet);
}

void resumeNetWaiters(lua_State* L)
{
    ServiceState& state = services(L);
    if (state.netWaiters.empty() || state.netClient == nullptr) {
        return;
    }

    // Collected first, for the reason `resumeAreaWaiters` gives: a resumed
    // coroutine may issue another request, and the vector it would push onto is
    // the one being walked.
    struct Ready
    {
        int threadRef = -1;
        net::NetResult result;
    };
    std::vector<Ready> ready;

    for (usize index = 0; index < state.netWaiters.size();) {
        net::NetResult result;
        if (!state.netClient->take(state.netWaiters[index].ticket, result)) {
            ++index;
            continue;
        }
        ready.push_back({state.netWaiters[index].threadRef, std::move(result)});
        state.netWaiters.erase(state.netWaiters.begin() + static_cast<std::ptrdiff_t>(index));
    }

    for (Ready& entry : ready) {
        lua_getref(L, entry.threadRef);
        lua_State* co = lua_tothread(L, -1);
        if (co == nullptr) {
            lua_pop(L, 1);
            (void)lua_unref(L, entry.threadRef);
            continue;
        }
        pushResult(co, entry.result);
        const bool finished = resumeScheduled(L, co, 1);
        lua_pop(L, 1);
        if (finished) {
            (void)lua_unref(L, entry.threadRef);
        }
    }
}

usize outstandingNetRequests(lua_State* L)
{
    const ServiceState& state = *context(L).services;
    return state.netClient != nullptr ? state.netClient->outstanding() : 0;
}

} // namespace luaug::script
