#include "luaug/script/sandbox.h"
#include "luaug/script/stdlib.h"

#include <lua.h>
#include <luacode.h>
#include <lualib.h>

#include <algorithm>
#include <doctest/doctest.h>
#include <ostream>
#include <string>
#include <vector>

using namespace luaug::script;

namespace {

// A VM booted the way `runtime.cpp` boots one, minus everything that is not
// about the sandbox. Kept here rather than reaching for `ScriptRuntime` so that
// these tests fail for exactly one reason.
struct SandboxedVm
{
    lua_State* state = nullptr;

    SandboxedVm()
    {
        state = luaL_newstate();
        luaL_openlibs(state);
        removeUnsafeGlobals(state);
        sealGlobals(state);
    }

    ~SandboxedVm()
    {
        if (state != nullptr)
            lua_close(state);
    }

    SandboxedVm(const SandboxedVm&) = delete;
    SandboxedVm& operator=(const SandboxedVm&) = delete;

    // The keys of a global table, as the VM has them. Sorted, so the caller can
    // compare two lists rather than two orders -- `pairs` makes no promise about
    // the second, and R10 would forbid relying on it if it did.
    [[nodiscard]] std::vector<std::string> keysOf(const std::string& table)
    {
        const std::string source = "local out = {} for k in pairs(" + table +
                                   ") do table.insert(out, k) end table.sort(out) return table.concat(out, \",\")";
        const std::string joined = evaluateString(source);

        std::vector<std::string> keys;
        std::size_t at = 0;
        while (at <= joined.size() && !joined.empty()) {
            const std::size_t comma = joined.find(',', at);
            keys.push_back(joined.substr(at, comma == std::string::npos ? std::string::npos : comma - at));
            if (comma == std::string::npos)
                break;
            at = comma + 1;
        }
        return keys;
    }

    // Runs a chunk whose result is a string. Shares the loader below rather than
    // duplicating it, because there is exactly one way to run a chunk here and
    // two would drift.
    [[nodiscard]] std::string evaluateString(const std::string& source)
    {
        size_t size = 0;
        lua_CompileOptions options{};
        char* bytecode = luau_compile(source.data(), source.size(), &options, &size);
        REQUIRE(bytecode != nullptr);

        lua_State* thread = lua_newthread(state);
        luaL_sandboxthread(thread);

        const int loaded = luau_load(thread, "@test", bytecode, size, 0);
        std::free(bytecode);
        REQUIRE(loaded == LUA_OK);

        const int status = lua_resume(thread, nullptr, 0);
        REQUIRE(status == LUA_OK);
        REQUIRE(lua_isstring(thread, -1));
        std::string result = lua_tostring(thread, -1);
        lua_pop(state, 1);
        return result;
    }

    // Runs a chunk and returns its single boolean result. Everything below
    // asserts from INSIDE the VM: checking the C++ list against itself would
    // prove nothing about what a script can reach, which is the only question
    // this file exists to answer (M2 brief, ruling R-D).
    [[nodiscard]] bool evaluate(const std::string& expression)
    {
        const std::string source = "return " + expression;
        size_t size = 0;
        lua_CompileOptions options{};
        char* bytecode = luau_compile(source.data(), source.size(), &options, &size);
        REQUIRE(bytecode != nullptr);

        // Loaded onto the thread, not moved onto it: `lua_newthread` pushes the
        // thread above the function, so a load-then-move moves the wrong value.
        lua_State* thread = lua_newthread(state);
        luaL_sandboxthread(thread);

        const int loaded = luau_load(thread, "@test", bytecode, size, 0);
        std::free(bytecode);
        REQUIRE(loaded == LUA_OK);

        const int status = lua_resume(thread, nullptr, 0);
        if (status != LUA_OK) {
            const char* message = lua_tostring(thread, -1);
            FAIL_CHECK("chunk failed: " << (message == nullptr ? "?" : message));
            lua_pop(state, 1);
            return false;
        }

        const bool result = lua_toboolean(thread, -1) != 0;
        lua_pop(state, 1);
        return result;
    }

    // Whether a chunk raises, which is a different question from what it
    // returns and the only way to test that a frozen table refuses a write.
    [[nodiscard]] bool raises(const std::string& statement)
    {
        size_t size = 0;
        lua_CompileOptions options{};
        char* bytecode = luau_compile(statement.data(), statement.size(), &options, &size);
        REQUIRE(bytecode != nullptr);

        lua_State* thread = lua_newthread(state);
        luaL_sandboxthread(thread);

        const int loaded = luau_load(thread, "@test", bytecode, size, 0);
        std::free(bytecode);
        REQUIRE(loaded == LUA_OK);

        const int status = lua_resume(thread, nullptr, 0);
        lua_pop(state, 1);
        return status != LUA_OK;
    }
};

} // namespace

TEST_CASE("every removed global reads as nil from inside the VM")
{
    SandboxedVm vm;

    for (const char* const* name = RemovedGlobals; *name != nullptr; ++name) {
        // Named directly, which is the whole point: a *spec* could not do this,
        // because naming an undeclared global is a strict-mode analyzer error
        // (ruling R-D). A raw chunk is not analysed, so C++ can ask the question
        // Luau source cannot.
        CAPTURE(std::string(*name));
        CHECK(vm.evaluate(std::string(*name) + " == nil"));
    }
}

TEST_CASE("the three globals Luau really defines are gone")
{
    SandboxedVm vm;

    // These are the ones that were actually there: the rest of the removal list
    // is a guard against a future Luau defining them. M0 found all three by
    // running the VM rather than by reading the sandbox's documentation.
    CHECK(vm.evaluate("getfenv == nil"));
    CHECK(vm.evaluate("setfenv == nil"));
    CHECK(vm.evaluate("newproxy == nil"));
}

TEST_CASE("_G exists, is empty, and refuses a write")
{
    SandboxedVm vm;

    // Four separate claims, and an implementation can satisfy one without the
    // others -- an empty table that accepts writes would pass the first three
    // and be a back channel between scripts.
    CHECK(vm.evaluate("type(_G) == \"table\""));
    CHECK(vm.evaluate("next(_G) == nil"));
    CHECK(vm.evaluate("_G.print == nil"));
    CHECK(vm.raises("_G.leak = 1"));

    // And it is not the globals table wearing a disguise.
    CHECK(vm.evaluate("_G.string == nil"));
}

TEST_CASE("os keeps exactly clock, time and date")
{
    SandboxedVm vm;

    CHECK(vm.evaluate("type(os.clock) == \"function\""));
    CHECK(vm.evaluate("type(os.time) == \"function\""));
    CHECK(vm.evaluate("type(os.date) == \"function\""));
    CHECK(vm.evaluate("os.difftime == nil"));
}

TEST_CASE("everything the document says survives, survives")
{
    SandboxedVm vm;

    // The direction nobody writes a test for. A removal pass that took out too
    // much is exactly as broken as one that took out too little, and it fails
    // later and further from its cause.
    // `warn` and `require` are NOT here even though api-design.md §1.1 lists
    // them as builtins: Luau defines neither, and the engine installs them in
    // `ScriptRuntime::boot`. This VM is deliberately booted without the runtime
    // so that a failure here has exactly one possible cause, which means the
    // engine-provided globals belong to a runtime test rather than this one.
    const char* const survivors[] = {
        "assert",   "error",  "print",  "pcall",    "xpcall", "select",       "next",         "pairs",
        "ipairs",   "rawget", "rawset", "rawequal", "rawlen", "getmetatable", "setmetatable", "tonumber",
        "tostring", "type",   "typeof", "unpack",   "gcinfo", nullptr,
    };
    for (const char* const* name = survivors; *name != nullptr; ++name) {
        CAPTURE(std::string(*name));
        CHECK(vm.evaluate(std::string("type(") + *name + ") == \"function\""));
    }

    const char* const libraries[] = {
        "table", "string", "math", "coroutine", "utf8", "buffer", "bit32", "os", "vector", "debug", nullptr,
    };
    for (const char* const* name = libraries; *name != nullptr; ++name) {
        CAPTURE(std::string(*name));
        CHECK(vm.evaluate(std::string("type(") + *name + ") == \"table\""));
    }

    CHECK(vm.evaluate("type(_VERSION) == \"string\""));
}

TEST_CASE("the sandbox freezes what it is supposed to freeze")
{
    SandboxedVm vm;

    // A new global does NOT raise, and that is per-script sandboxing working:
    // `luaL_sandboxthread` gives each script its own globals table with the
    // real one behind it, so a write lands in the script's own table. The
    // guarantee is isolation, not immutability, and the two are easy to confuse
    // -- the assertion below is the one that matters.
    CHECK_FALSE(vm.raises("newGlobal = 1"));
    // And the library tables one level down, so nothing monkey-patches string.
    CHECK(vm.raises("string.format = nil"));
    CHECK(vm.raises("table.insert = nil"));

    // The string metatable, which `luaL_sandbox` does handle -- the one thing
    // its name promises that it actually delivers.
    CHECK(vm.raises("getmetatable(\"\").__index = {}"));
}

// **The list the editor offers, checked against the VM that has to answer for
// it** (`stdlib.h`). Both directions, because either one alone is half a test:
// a name this claims and the VM lacks is a completion that inserts something
// broken, and a name the VM has and this lacks is a completion that silently
// stopped keeping up with the pin.
TEST_CASE("every name the standard-library list claims is really there")
{
    SandboxedVm vm;

    for (const StdName& global : stdGlobals()) {
        CAPTURE(std::string(global.name));
        CHECK(vm.evaluate("type(" + std::string(global.name) + ") == \"" + std::string(global.type) + "\""));
    }

    for (const StdLibrary& library : stdLibraries()) {
        CAPTURE(std::string(library.name));
        CHECK(vm.evaluate("type(" + std::string(library.name) + ") == \"table\""));
        for (const StdName& member : library.members) {
            const std::string path = std::string(library.name) + "." + std::string(member.name);
            CAPTURE(path);
            CHECK(vm.evaluate("type(" + path + ") == \"" + std::string(member.type) + "\""));
        }
    }
}

TEST_CASE("the standard-library list has everything the VM has")
{
    // The direction that catches a Luau bump. Gaining `math.fma` would leave the
    // editor a version behind and nothing would say so -- unless this fails,
    // which is the whole reason a written list is allowed to exist at all.
    SandboxedVm vm;

    for (const StdLibrary& library : stdLibraries()) {
        const std::vector<std::string> keys = vm.keysOf(std::string(library.name));
        REQUIRE_FALSE(keys.empty());
        for (const std::string& key : keys) {
            CAPTURE(std::string(library.name) + "." + key);
            const bool listed = std::any_of(library.members.begin(), library.members.end(),
                                            [&key](const StdName& member) { return member.name == key; });
            CHECK(listed);
        }
    }
}
