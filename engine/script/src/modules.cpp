#include "luaug/script/modules.h"

#include "luaug/scene/world.h"
#include "luaug/script/instance_binding.h"
#include "luaug/script/services.h"
#include "luaug/script/signals.h"

#include <lua.h>
#include <lualib.h>

// ADR 0002: the shipping profile links Luau.VM and Luau.CodeGen only, so this
// header does not exist in that build. Everything under the guard is the one
// source-to-bytecode path this module has.
#if LUAUG_LUAU_COMPILER
#include <luacode.h>
#endif

#include <algorithm>
#include <cstdlib>

namespace luaug::script {
namespace {

[[nodiscard]] ModuleRegistry& registry(lua_State* L) noexcept
{
    return *context(L).modules;
}

[[nodiscard]] scene::World& world(lua_State* L) noexcept
{
    return *context(L).world;
}

// --- Loading -----------------------------------------------------------------

#if LUAUG_LUAU_COMPILER

// The one compile in the engine, so the options that make `Vector3.new` a
// constant live in exactly one place (ADR 0013). `chunkName` is what
// `lua_getinfo` reports back as the requiring file, which is how a relative
// specifier knows where it is relative to.
[[nodiscard]] bool loadChunk(lua_State* co, std::string_view source, std::string_view chunkName, std::string& outError)
{
    size_t bytecodeSize = 0;
    lua_CompileOptions options{};
    options.optimizationLevel = 2;
    options.debugLevel = 2;
    options.vectorLib = "Vector3";
    options.vectorCtor = "new";
    options.vectorType = "Vector3";

    const std::string chunk = "@" + std::string(chunkName);
    char* bytecode = luau_compile(source.data(), source.size(), &options, &bytecodeSize);
    if (bytecode == nullptr) {
        outError = "compilation produced no bytecode";
        return false;
    }

    const int status = luau_load(co, chunk.c_str(), bytecode, bytecodeSize, 0);
    std::free(bytecode);

    if (status != LUA_OK) {
        const char* message = lua_tostring(co, -1);
        outError = message == nullptr ? std::string{} : std::string(message);
        lua_pop(co, 1);
        return false;
    }
    return true;
}

#else

// A build with no compiler refuses source rather than quietly loading nothing.
// The refusal is placed HERE, at the single point where source would have
// become bytecode, so every caller -- `require`, the entry-script mount --
// reports it through the failure path it already has, keyed and logged the same
// way a syntax error is. The reason travels as the `{message}` of that error,
// key-prefixed, because a refusal is an engine message and R3 governs those.
[[nodiscard]] bool loadChunk(lua_State*, std::string_view, std::string_view, std::string& outError)
{
    outError = core::formatKeyPrefixed(LUAUG_TR("script.err.no_compiler"));
    return false;
}

#endif

// The chunk name of whatever called `require`, without the `@`. Empty at the
// top of a C boundary, which is what makes a relative specifier from there a
// project-root-relative one.
[[nodiscard]] std::string requiringPath(lua_State* L)
{
    lua_Debug info{};
    // Level 1 is the Luau frame that called this C function. `lua_getinfo`
    // returns 0 when the level does not resolve, which happens when `require` is
    // reached from C rather than from a script.
    if (lua_getinfo(L, 1, "s", &info) == 0 || info.source == nullptr)
        return {};

    std::string_view source{info.source};
    if (!source.empty() && source.front() == '@')
        source.remove_prefix(1);
    return std::string(source);
}

[[noreturn]] void raiseModuleError(lua_State* L, core::TextKey key, std::string_view specifier, std::string_view from)
{
    const core::I18nArg args[] = {
        {"specifier", specifier},
        {"source", from.empty() ? std::string_view{"the host"} : from},
    };
    raise(L, key, args);
}

// Runs a module body on its own sandboxed coroutine and returns its single
// value on `L`'s stack.
//
// Its own coroutine because a module is a chunk like any other and a chunk gets
// its own globals table (api-design.md §3). It must NOT yield: a module is
// evaluated once and its result cached, and a half-evaluated module in the cache
// is a module every later requirer would get wrong.
[[nodiscard]] bool evaluateModule(lua_State* L, std::string_view source, std::string_view chunkName,
                                  std::string& outError)
{
    lua_State* co = lua_newthread(L);
    const int rooted = lua_gettop(L);
    luaL_sandboxthread(co);

    if (!loadChunk(co, source, chunkName, outError)) {
        lua_remove(L, rooted);
        return false;
    }

    const int status = lua_resume(co, nullptr, 0);
    if (status == LUA_YIELD || status == LUA_BREAK) {
        outError = "module yielded during evaluation";
        lua_remove(L, rooted);
        return false;
    }
    if (status != LUA_OK) {
        const char* message = lua_tostring(co, -1);
        outError = message == nullptr ? std::string{} : std::string(message);
        lua_remove(L, rooted);
        return false;
    }

    if (lua_gettop(co) < 1) {
        outError.clear();
        lua_remove(L, rooted);
        return false;
    }

    // Exactly one value: the last, so a module that returns several is read the
    // way a `return a, b` would be assigned to one name.
    lua_xmove(co, L, 1);
    lua_remove(L, rooted);
    return true;
}

// --- require -----------------------------------------------------------------

int requireRegistered(lua_State* L, ModuleRegistry::Registered& module, std::string_view from)
{
    if (module.resultRef > 0) {
        lua_getref(L, module.resultRef);
        return 1;
    }
    if (module.loading)
        raiseModuleError(L, LUAUG_TR("script.err.require_cycle"), module.name, from);

    module.loading = true;
    if (module.opener != nullptr) {
        // A native module cannot fail to COMPILE and does not need the pcall
        // wrapper a source module gets: it raises the way any binding raises,
        // and that propagates to the requirer already keyed.
        module.loading = false;
        module.opener(L);
        module.resultRef = lua_ref(L, -1);
        return 1;
    }

    std::string error;
    const bool ok = evaluateModule(L, module.source, module.name, error);
    module.loading = false;

    if (!ok) {
        const core::I18nArg args[] = {{"source", std::string_view{module.name}}, {"message", std::string_view{error}}};
        raise(L, LUAUG_TR("script.err.runtime"), args);
    }

    module.resultRef = lua_ref(L, -1);
    return 1;
}

int scriptRequire(lua_State* L)
{
    size_t length = 0;
    const char* text = luaL_checklstring(L, 1, &length);
    const std::string_view specifier{text, length};
    const std::string from = requiringPath(L);

    ModuleRegistry& modules = registry(L);

    // Engine-provided modules are matched first and never reach the loader.
    // Deliberately the opposite way round from the vendored implementation,
    // where a registered module is matched before the permission callback runs
    // and a denied chunk can still reach `@luaug/…` (U-39) -- here there is one
    // gate and it is this function.
    if (!specifier.empty() && specifier.front() == '@') {
        for (ModuleRegistry::Registered& module : modules.registered) {
            if (module.name == specifier)
                return requireRegistered(L, module, from);
        }
        // NOT an error yet: `@` is also how a `.luaurc` alias is spelled, so an
        // unmatched one falls through to the loader. Engine modules win a
        // collision, which is what makes `@luaug/testing` mean one thing.
    }

    if (modules.loader.resolve == nullptr || modules.loader.read == nullptr)
        raiseModuleError(L, LUAUG_TR("script.err.module_not_found"), specifier, from);

    std::string path;
    if (!modules.loader.resolve(modules.loader.user, from, specifier, path))
        raiseModuleError(L, LUAUG_TR("script.err.module_not_found"), specifier, from);

    // Keyed on the resolved project-relative path, so two specifiers that name
    // the same file share one evaluation -- which is what "one evaluation per
    // module per VM" means.
    if (const auto found = modules.byPath.find(path); found != modules.byPath.end()) {
        ModuleRegistry::Module& cached = modules.modules[found->second];
        if (cached.failed) {
            // The failure is cached and re-raised, rather than the module being
            // re-run in the hope of a different answer (api-design.md §3).
            const core::I18nArg args[] = {
                {"source", std::string_view{cached.path}},
                {"message", std::string_view{cached.error}},
            };
            raise(L, LUAUG_TR("script.err.runtime"), args);
        }
        if (cached.loading)
            raiseModuleError(L, LUAUG_TR("script.err.require_cycle"), specifier, from);

        lua_getref(L, cached.resultRef);
        return 1;
    }

    std::string source;
    if (!modules.loader.read(modules.loader.user, path, source))
        raiseModuleError(L, LUAUG_TR("script.err.module_not_found"), specifier, from);

    const usize index = modules.modules.size();
    modules.modules.push_back(ModuleRegistry::Module{path, -1, true, false, {}});
    modules.byPath.emplace(path, index);

    std::string error;
    const bool ok = evaluateModule(L, source, path, error);
    modules.modules[index].loading = false;

    if (!ok) {
        modules.modules[index].failed = true;
        modules.modules[index].error = error;
        const core::I18nArg args[] = {{"source", std::string_view{path}}, {"message", std::string_view{error}}};
        raise(L, LUAUG_TR("script.err.runtime"), args);
    }

    modules.modules[index].resultRef = lua_ref(L, -1);
    return 1;
}

// --- Mounting ----------------------------------------------------------------

// `src/scripts/enemy/patrol.luau` mounts as `ScriptService/enemy/patrol`: the
// directories become `Folder`s and the extension is dropped.
[[nodiscard]] std::vector<std::string_view> pathSegments(std::string_view path)
{
    std::vector<std::string_view> segments;
    usize start = 0;
    for (usize index = 0; index <= path.size(); ++index) {
        if (index == path.size() || path[index] == '/') {
            if (index > start)
                segments.push_back(path.substr(start, index - start));
            start = index + 1;
        }
    }
    return segments;
}

[[nodiscard]] std::string_view withoutExtension(std::string_view name)
{
    const usize dot = name.rfind('.');
    return dot == std::string_view::npos ? name : name.substr(0, dot);
}

// The C function `startScripts` defers last. It runs after every entry script's
// first resumption, which is what lets a `game.Loaded:Connect` written at file
// scope be in the connection list the fire captures.
int fireLoadedTrampoline(lua_State* L)
{
    fireDataModelLoaded(L);
    return 0;
}

} // namespace

void registerRequire(lua_State* L)
{
    lua_pushcfunction(L, scriptRequire, "require");
    lua_setglobal(L, "require");
}

void registerModule(lua_State* L, std::string_view name, std::string_view source)
{
    ModuleRegistry& modules = registry(L);
    for (ModuleRegistry::Registered& module : modules.registered) {
        if (module.name == name) {
            module.source = std::string(source);
            module.opener = nullptr;
            return;
        }
    }
    modules.registered.push_back(ModuleRegistry::Registered{.name = std::string(name), .source = std::string(source)});
}

void registerNativeModule(lua_State* L, std::string_view name, int (*opener)(lua_State*))
{
    ModuleRegistry& modules = registry(L);
    for (ModuleRegistry::Registered& module : modules.registered) {
        if (module.name == name) {
            module.source.clear();
            module.opener = opener;
            return;
        }
    }
    // `.source` named explicitly, empty, because Clang's -Wmissing-field-initializers
    // counts a skipped designator as a missing one even where the default is what
    // was wanted. Naming it costs a word and keeps the Tier-2 build clean.
    modules.registered.push_back(ModuleRegistry::Registered{.name = std::string(name), .source = {}, .opener = opener});
}

void mountScripts(lua_State* L, std::span<const MountedScript> scripts)
{
    ModuleRegistry& modules = registry(L);
    scene::World& w = world(L);

    std::vector<MountedScript> ordered(scripts.begin(), scripts.end());
    // Sorted here rather than trusted from the caller: the tree's shape and the
    // start order are both observable, and a directory walk's order is exactly
    // the kind of thing R10 forbids from reaching them.
    std::sort(ordered.begin(), ordered.end(),
              [](const MountedScript& a, const MountedScript& b) { return a.path < b.path; });

    const core::InstanceId scriptService =
        w.findFirstChildOfClass(context(L).services->dataModel, w.classes().findId(w.atoms().lookup("ScriptService")));
    if (!scriptService.valid())
        return;

    const scene::ClassId folderClass = w.classes().findId(w.atoms().lookup("Folder"));
    const scene::ClassId scriptClass = w.classes().findId(w.atoms().lookup("Script"));

    for (const MountedScript& entry : ordered) {
        const std::vector<std::string_view> segments =
            pathSegments(entry.mountPath.empty() ? entry.path : entry.mountPath);
        if (segments.empty())
            continue;

        core::InstanceId parent = scriptService;
        for (usize index = 0; index + 1 < segments.size(); ++index) {
            const core::NameAtom name = w.atoms().intern(segments[index]);
            core::InstanceId folder = w.findFirstChild(parent, name);
            if (!folder.valid()) {
                folder = w.create(folderClass);
                w.setName(folder, name);
                (void)w.setParent(folder, parent);
            }
            parent = folder;
        }

        const core::InstanceId instance = w.create(scriptClass);
        w.setName(instance, w.atoms().intern(withoutExtension(segments.back())));
        (void)w.setParent(instance, parent);

        modules.entries.push_back(ModuleRegistry::Entry{entry.path, entry.source, instance});
    }

    // Consumed rather than queued: the tree was built before any script could
    // have connected to anything, so these are facts with no observer.
    (void)w.changes().take();
}

void startScripts(lua_State* L)
{
    ModuleRegistry& modules = registry(L);
    scene::World& w = world(L);
    const core::NameAtom enabled = w.atoms().intern("Enabled");

    for (const ModuleRegistry::Entry& entry : modules.entries) {
        // A Script whose `Enabled` is false at boot never starts -- it is still
        // mounted and still in the tree, but no coroutine is created for it
        // (api-design.md §3).
        const std::optional<scene::Value> value = w.getProperty(entry.instance, enabled);
        if (value.has_value()) {
            if (const auto* flag = std::get_if<bool>(&value.value()); flag != nullptr && !*flag)
                continue;
        }

        lua_State* co = lua_newthread(L);
        const int rooted = lua_gettop(L);
        luaL_sandboxthread(co);

        // BEFORE the load. `luaL_sandboxthread` marks the new globals table
        // safeenv, which makes the compiler's import fast path resolve globals
        // at load time -- so a `script` set afterwards would be invisible to
        // every `script.Name` the chunk contains.
        pushInstance(co, entry.instance);
        lua_setglobal(co, "script");

        std::string error;
        if (!loadChunk(co, entry.source, entry.path, error)) {
            const core::I18nArg args[] = {
                {"source", std::string_view{entry.path}},
                {"message", std::string_view{error}},
            };
            core::logText(core::LogLevel::Error, core::formatKeyPrefixed(LUAUG_TR("script.err.syntax"), args));
            ++modules.loadFailures;
            lua_remove(L, rooted);
            continue;
        }

        // Deferred rather than resumed here, so every entry script's first
        // resumption happens inside a drain and in the order they were mounted.
        const int threadRef = lua_ref(L, rooted);
        if (!enqueueTaskCallback(L, threadRef, 0, 0))
            (void)lua_unref(L, threadRef);
        lua_remove(L, rooted);
    }

    lua_State* loaded = lua_newthread(L);
    const int rooted = lua_gettop(L);
    lua_pushcfunction(loaded, fireLoadedTrampoline, "loaded");
    const int loadedRef = lua_ref(L, rooted);
    if (!enqueueTaskCallback(L, loadedRef, 0, 0))
        (void)lua_unref(L, loadedRef);
    lua_remove(L, rooted);
}

usize mountedScriptCount(lua_State* L)
{
    return registry(L).entries.size();
}

usize scriptLoadFailures(lua_State* L)
{
    return registry(L).loadFailures;
}

} // namespace luaug::script
