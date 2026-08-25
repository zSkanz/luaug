// `require`, and the entry scripts the tree is mounted from (api-design.md §3).
//
// **Written rather than delegated to `Luau.Require`**, and the four reasons are
// all in `docs/research/luau-c-api-2026.md` §3:
//
//   - Its cache key is the resolved filesystem path and its reference navigator
//     is CWD-dependent (U-41). A cache key that changes with the working
//     directory is a cache key R10 cannot live with.
//   - A registered module is matched *before* `is_require_allowed` runs (U-39),
//     so the permission gate api-design.md §7 describes cannot be built on it.
//   - Cyclic requires do not work at this pin under any flag combination, and
//     there is no guard: a cycle recurses until the C stack dies (U-36).
//   - Linking `Luau.Require` drags `Luau.Compiler` in as a propagated link
//     requirement (U-38), which would put the compiler in a shipping build that
//     ADR 0002 exists to keep it out of.
//
// So resolution is the host's, the cache key is the **project-relative path**,
// and a cycle is a keyed error rather than a crash.
#pragma once

#include "luaug/core/id.h"
#include "luaug/script/binding.h"

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct lua_State;

namespace luaug::script {

// Where module source comes from. `script` never opens a file: the seam is what
// keeps L5 free of a filesystem, and it is what lets a test mount a project
// that exists only in memory.
//
// `resolve` turns a specifier into a canonical, project-relative, '/'-separated
// path -- the cache key -- and `read` fetches that path's source. They are
// separate so a cache hit costs no file read.
struct ModuleLoader
{
    void* user = nullptr;
    bool (*resolve)(void* user, std::string_view fromPath, std::string_view specifier, std::string& outPath) = nullptr;
    bool (*read)(void* user, std::string_view path, std::string& outSource) = nullptr;
};

// One entry script, as the host found it. `path` is project-relative with '/'
// separators and is what fixes the mount order: api-design.md §3 starts entry
// scripts in path-sorted order, so the order is a property of the tree rather
// than of whatever the directory walk happened to yield (R10).
struct MountedScript
{
    // Project-relative with '/' separators. This is the chunk name, the base a
    // relative `require` resolves against, and the key the start order sorts on.
    std::string path;

    // Where it goes in the tree, relative to `ScriptService` -- normally `path`
    // with `src/scripts/` stripped. Separate from `path` because the two answer
    // different questions: a script at `src/scripts/enemy/patrol.luau` mounts as
    // `ScriptService/enemy/patrol`, and a require written inside it still
    // resolves against `src/scripts/enemy/`.
    std::string mountPath;

    std::string source;
};

// Per-VM. Held by `VmContext` as a pointer and owned by `ScriptRuntime`.
class ModuleRegistry
{
public:
    ModuleLoader loader;

    // A module that has been evaluated, or has failed. Both are cached: a
    // module that errors propagates to its requirer and the failure is cached
    // (api-design.md §3), which is a thing the vendored implementation does NOT
    // do (U-35) and we do.
    struct Module
    {
        std::string path;
        int resultRef = -1;
        // Being evaluated right now. A second require of it is a cycle.
        bool loading = false;
        bool failed = false;
        std::string error;
    };

    // A vector plus an index rather than a bare map, so that anything walking
    // the modules walks them in first-required order rather than in a hash
    // order R10 forbids from reaching observable state.
    std::vector<Module> modules;
    std::unordered_map<std::string, usize> byPath;

    // `@luaug/…` and `@std/…`. Matched before the loader is consulted and never
    // reaching it, because they have no path and no file.
    struct Registered
    {
        std::string name;
        // Luau source, for a module that ships as content (`@luaug/testing`).
        std::string source;
        // A C opener that pushes the module table, for one that cannot be
        // written in Luau because it is a binding (`@std/net`). Exactly one of
        // the two is set, and `source` is checked first only because it came
        // first -- a module is one kind or the other, never both.
        int (*opener)(lua_State*) = nullptr;
        int resultRef = -1;
        bool loading = false;
    };
    std::vector<Registered> registered;

    // A `ModuleScript` that has been required, keyed by the instance rather
    // than by a path -- because it has no path (ADR 0050). Same three states as
    // a file-backed module: a value, a cached failure, and "being evaluated",
    // which is a cycle.
    //
    // A vector rather than a map for the reason `modules` is one: anything
    // walking these walks them in first-required order rather than in a hash
    // order R10 forbids from reaching observable state. A world holds tens of
    // modules, not thousands.
    struct TreeModule
    {
        core::InstanceId instance;
        int resultRef = -1;
        bool loading = false;
        bool failed = false;
        std::string error;
    };
    std::vector<TreeModule> treeModules;

    // The mounted entry scripts, in path order, with the `Script` instance each
    // became.
    //
    // **No copy of the text.** The mount writes the file into the instance's
    // `Source` and the instance is what runs (ADR 0057), so what is worth
    // remembering here is which FILE an instance came from -- which is the
    // question `Ctrl+S` in the script editor asks, and the only one a second
    // copy of the source could not answer.
    struct Entry
    {
        std::string path;
        core::InstanceId instance;
    };
    std::vector<Entry> entries;

    // Entry scripts that failed to compile at `startScripts`. Boot is
    // deliberately forgiving about this -- one bad script must not stop the
    // engine -- but a hot reload is not, because it has the world that was
    // already running to fall back on (M3 brief Decision 14).
    usize loadFailures = 0;
};

// What a script's chunk is called: the project-relative file it was mounted
// from, or its place in the tree when nothing mounted it (ADR 0057).
//
// **One function so that three callers agree.** `startScripts` puts this in
// every error message, the script editor puts it on a tab, and a breakpoint is
// keyed on it -- and a breakpoint whose key is not the name the VM reports is a
// breakpoint that never fires. Empty for an instance that is not a script.
[[nodiscard]] std::string scriptChunkName(lua_State* L, core::InstanceId instance);

// The file a script was mounted from, or empty when the scene or the editor made
// it. What `Ctrl+S` in the script editor asks, and the only question a second
// copy of the source could not have answered.
[[nodiscard]] std::string_view mountedPathOf(lua_State* L, core::InstanceId instance);

// Installs the `require` global. Runs during boot, before the sandbox.
void registerRequire(lua_State* L);

// Registers an engine-provided module by name. Evaluated on first require and
// cached like any other; the source is copied, because the caller's buffer has
// no reason to outlive the call.
void registerModule(lua_State* L, std::string_view name, std::string_view source);

// The same, for a module whose body is C rather than Luau. `opener` is called
// once, with the module table as its single return value; the result is cached
// exactly as a source module's is, so `require("@std/net")` twice is one table.
//
// This exists because `@std/net` is a BINDING and cannot be written in Luau at
// all, and because the alternative -- a global the shim reaches through -- would
// put a name on a global list api-design.md 1.1 states is exhaustive.
void registerNativeModule(lua_State* L, std::string_view name, int (*opener)(lua_State*));

// Mounts each entry as a `Script` under `ScriptService`, with subdirectories as
// `Folder`s (api-design.md §3). Sorted by path first, so the tree is the same
// whatever order the host found the files in.
// Returns the `Script` instances it created, in the order it mounted them.
//
// **The return value exists because mounting ONE script is now a thing that
// happens** (the editor's New Script writes a file and mounts it at once, so
// the row appears where the person is looking rather than after a restart), and
// a caller that has just made one instance needs to be able to select it. Boot
// ignores it, as it always did.
std::vector<core::InstanceId> mountScripts(lua_State* L, std::span<const MountedScript> scripts);

// Starts every mounted `Script` whose `Enabled` is true, each on its own
// coroutine, deferred and in path-sorted order -- then defers one more callback
// that fires `game.Loaded`.
//
// The trailing callback is not decoration. `Loaded` has to be *raised* after
// every entry script's first resumption rather than merely queued behind them,
// because a fire captures its connection list when it is raised (§3.1) and a
// `game.Loaded:Connect` at file scope would otherwise miss its own fire.
void startScripts(lua_State* L);

// Starts ONE `Script`, on its own coroutine, deferred exactly as `startScripts`
// defers each of its own -- and answers whether it did.
//
// **This exists because `Enabled` does something now** (ADR 0059). A false-to-
// true write after boot is a START rather than a resume: there is no old thread
// to hand back, and the file scope runs against the world as it is. It is the
// same path `startScripts` uses for one instance, so a script started this way
// and one started at play are the same script started the same way.
//
// False for anything that is not a `Script`, one whose `Enabled` is false, and
// one with no source -- an empty `Script` is what somebody has the moment they
// make one, and it is not a failure.
bool startScript(lua_State* L, core::InstanceId instance);

// The mount table -- which FILE each `Script` instance came from -- lifted out of
// the VM and put back into another one.
//
// **A stop rebuilds the VM and keeps the world** (ADR 0058), and this mapping is
// the one thing in the registry that describes the world rather than the VM: the
// `Script` instances it names are still there afterwards, and losing it would
// make `Ctrl+S` in the script editor and every chunk name forget which file a
// script came from. Everything else in the registry -- required modules, their
// cached results, the failure count -- is exactly what a teardown is for.
[[nodiscard]] std::vector<ModuleRegistry::Entry> mountedEntries(lua_State* L);
void adoptMountedEntries(lua_State* L, std::vector<ModuleRegistry::Entry> entries);

// --- Which script a thread belongs to (ADR 0059) -----------------------------
//
// `Enabled` decides whether a script's threads are RESUMED, so every place that
// resumes one has to be able to ask whose it is. These two are that question,
// and they are here because the answer is a property of how an entry script is
// started -- `luaL_sandboxthread` plus a `script` global -- which is what the
// file above does.

// The `Script` this thread belongs to, or invalid.
//
// Read from the thread's own globals table, and that is what makes it
// TRANSITIVE: an entry script's coroutine gets its own globals with `script` in
// them, and `lua_newthread` hands that same table to every thread created from
// it -- so a `task.defer` three calls deep inside a script still answers with
// that script. A thread the engine made for itself has the main globals, no
// `script`, and belongs to nobody.
[[nodiscard]] core::InstanceId scriptOfThread(lua_State* thread);

// The same question asked of a FUNCTION, through the environment it was loaded
// with. A signal handler is stored as a function and its thread is not made
// until it fires, so this is the only form of the question available at the
// moment a fire decides whether to invoke it.
[[nodiscard]] core::InstanceId scriptOfFunction(lua_State* L, int index);

// Whether a resumption belonging to this script must be dropped: it names a
// live `Script` whose `Enabled` is false.
//
// **Invalid is never suppressed.** A thread with no owning script is the
// engine's own, and a rule about scripts must not reach one.
[[nodiscard]] bool resumptionSuppressed(lua_State* L, core::InstanceId script);

// How many entry scripts were mounted. The conformance runner reports it, and a
// project that mounted nothing is worth saying so about.
[[nodiscard]] usize mountedScriptCount(lua_State* L);

// How many of them failed to compile when `startScripts` ran. Zero at boot is
// not required; zero after a reload is (see `ModuleRegistry::loadFailures`).
[[nodiscard]] usize scriptLoadFailures(lua_State* L);

} // namespace luaug::script
