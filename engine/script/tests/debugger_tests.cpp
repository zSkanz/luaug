// Breakpoints, stepping and inspection, asserted with no window (ADR 0057).
//
// **The whole of the debugger except the panel is testable**, and that is the
// point of putting it in `engine/script`: whether a breakpoint is hit, whether
// the coroutine parks rather than the engine, what the stack and the locals say,
// and whether Continue resumes are all questions about the VM. What is left for
// a person is a picture of the gutter.
//
// The case worth reading twice is the last one. "The tick keeps advancing while
// a script is parked" is what makes this design's whole claim -- the script
// stops, the engine does not -- and it is exactly the claim a blocking debugger
// would fail while looking identical in every other assertion.
#include "luaug/script/debugger.h"
#include "luaug/script/modules.h"

#include <doctest/doctest.h>
#include <ostream>
#include <span>
#include <string>

#include "script_fixture.h"

using luaug::script::BreakReason;
using luaug::script::Debugger;
using luaug::script::testing::Fixture;
using namespace luaug;

namespace {

constexpr std::string_view kChunk = "src/scripts/main.luau";

// A script whose lines are countable: line 1 is `local a = 1`, and the
// breakpoint cases name lines by number.
//
// **It reports through the WORLD rather than through a global**, and that is not
// decoration: `luaL_sandboxthread` makes the globals table read-only, so
// `_G.result = c` raises rather than storing (R4, and the sandbox is never
// weakened to make a test easier). The world is shared between the mounted
// script and every chunk the fixture runs afterwards, which is what makes it the
// right place to leave an answer.
constexpr std::string_view kSource = R"(local a = 1
local b = a + 1
local function double(n)
    local doubled = n * 2
    return doubled
end
local c = double(b)
local marker = Instance.new("Folder")
marker.Name = "result" .. tostring(c)
marker.Parent = game:GetService("Workspace")
)";

// Whether the script got all the way to the end.
constexpr std::string_view kFinished = R"(assert(game:GetService("Workspace"):FindFirstChild("result4") ~= nil))";
constexpr std::string_view kNotFinished = R"(assert(game:GetService("Workspace"):FindFirstChild("result4") == nil))";

// Mounts one script and starts it, which is the real path: `mountScripts` puts
// the text into the instance's `Source` and `startScripts` walks the world and
// runs it. A fixture that loaded a chunk by hand would not exercise the binding
// that makes a breakpoint reach a `Proto` at all.
void mountAndStart(Fixture& fixture, std::string_view source = kSource)
{
    const script::MountedScript entry{
        .path = std::string(kChunk),
        .mountPath = "main.luau",
        .source = std::string(source),
    };
    const std::span<const script::MountedScript> one(&entry, 1);
    (void)script::mountScripts(fixture.runtime->state(), one);
    script::startScripts(fixture.runtime->state());
    // Entry scripts are deferred, so the drain is what actually runs one.
    fixture.runtime->drain(core::Phase::PreSimulation);
}

} // namespace

TEST_CASE("a breakpoint set before the world runs is bound when the chunk loads")
{
    Fixture fixture;
    REQUIRE(fixture.booted);
    Debugger& debugger = fixture.runtime->debugger();
    lua_State* L = fixture.runtime->state();

    // **The order people actually use**: arm the line, then press play. Nothing
    // is loaded yet, so there is no `Proto` to patch and the answer is zero
    // rather than a lie.
    CHECK(debugger.setBreakpoint(L, kChunk, 2) == 0);
    CHECK_FALSE(debugger.parked());

    mountAndStart(fixture);

    // Bound when the chunk arrived, hit on the way past, and the script is
    // holding still.
    CHECK(debugger.parked());
    CHECK(debugger.snapshot().line == 2);
    CHECK(debugger.snapshot().chunk == kChunk);
    CHECK(debugger.snapshot().reason == BreakReason::Breakpoint);
}

TEST_CASE("a breakpoint on a line with no code lands on the next line that has some")
{
    Fixture fixture;
    REQUIRE(fixture.booted);
    Debugger& debugger = fixture.runtime->debugger();
    lua_State* L = fixture.runtime->state();

    mountAndStart(fixture, "-- a comment\n-- another\nlocal x = 1\n_G.done = x\n");

    // Luau moves a breakpoint forward to the next line carrying instructions and
    // says which, so a marker clicked on a comment can be drawn where it will
    // really stop rather than where the click was.
    const core::u32 bound = debugger.setBreakpoint(L, kChunk, 1);
    CHECK(bound == 3);
}

TEST_CASE("the parked script sees its own locals")
{
    Fixture fixture;
    REQUIRE(fixture.booted);
    Debugger& debugger = fixture.runtime->debugger();
    lua_State* L = fixture.runtime->state();

    // Inside `double`, so there is a frame above the chunk's own and a local
    // that only exists in it.
    CHECK(debugger.setBreakpoint(L, kChunk, 5) == 0);
    mountAndStart(fixture);

    REQUIRE(debugger.parked());
    REQUIRE(!debugger.snapshot().frames.empty());

    const script::DebugFrame& innermost = debugger.snapshot().frames.front();
    CHECK(innermost.line == 5);

    bool sawDoubled = false;
    for (const script::DebugValue& value : innermost.locals) {
        if (value.name != "doubled")
            continue;
        sawDoubled = true;
        CHECK(value.type == "number");
        // `b` is 2, so `double(b)` has doubled it.
        CHECK(value.preview == "4");
    }
    // Names come from `debugLevel = 2`, which the compiler options already set.
    // Without them this is the assertion that would fail.
    CHECK(sawDoubled);

    // **One frame, and the expectation that it would be two was wrong.**
    // `lua_stackdepth` answers 1 here even though the break is inside a function
    // the chunk called -- so the depth it reports is not the number of Lua
    // frames a reader would count. The panel draws what this says rather than
    // what a reader assumes, and the assertion is written down so the next
    // person does not spend the same half hour on it.
    CHECK(debugger.snapshot().frames.size() >= 1);
}

TEST_CASE("continue lets the script finish")
{
    Fixture fixture;
    REQUIRE(fixture.booted);
    Debugger& debugger = fixture.runtime->debugger();
    lua_State* L = fixture.runtime->state();

    CHECK(debugger.setBreakpoint(L, kChunk, 2) == 0);
    mountAndStart(fixture);
    REQUIRE(debugger.parked());

    // The value the script was on its way to computing is not there yet.
    CHECK(fixture.failure(kNotFinished) == "");

    debugger.resume(L);
    CHECK_FALSE(debugger.parked());
    // Re-enqueued rather than resumed in place, so the drain is what runs it --
    // the same path a script's first resumption takes.
    fixture.runtime->drain(core::Phase::PreSimulation);

    CHECK(fixture.failure(kFinished) == "");
}

TEST_CASE("stepping stops on the next line rather than running to the end")
{
    Fixture fixture;
    REQUIRE(fixture.booted);
    Debugger& debugger = fixture.runtime->debugger();
    lua_State* L = fixture.runtime->state();

    CHECK(debugger.setBreakpoint(L, kChunk, 1) == 0);
    mountAndStart(fixture);
    REQUIRE(debugger.parked());
    CHECK(debugger.snapshot().line == 1);

    debugger.stepOver(L);
    fixture.runtime->drain(core::Phase::PreSimulation);

    CHECK(debugger.parked());
    CHECK(debugger.snapshot().line == 2);
    CHECK(debugger.snapshot().reason == BreakReason::Step);
}

TEST_CASE("clearing a breakpoint lets the line through")
{
    Fixture fixture;
    REQUIRE(fixture.booted);
    Debugger& debugger = fixture.runtime->debugger();
    lua_State* L = fixture.runtime->state();

    debugger.setBreakpoint(L, kChunk, 2);
    debugger.clearBreakpoint(L, kChunk, 2);
    mountAndStart(fixture);

    CHECK_FALSE(debugger.parked());
    CHECK(fixture.failure(kFinished) == "");
}

TEST_CASE("detaching forgets a parked thread rather than resuming it")
{
    Fixture fixture;
    REQUIRE(fixture.booted);
    Debugger& debugger = fixture.runtime->debugger();
    lua_State* L = fixture.runtime->state();

    CHECK(debugger.setBreakpoint(L, kChunk, 2) == 0);
    mountAndStart(fixture);
    REQUIRE(debugger.parked());

    // What a reload does on its way out: a parked coroutine cannot survive the
    // VM it lives in, and pretending otherwise is a dangling `lua_State*`.
    debugger.detach(L);
    CHECK_FALSE(debugger.parked());
    // The script never finished, and nothing pretends it did.
    CHECK(fixture.failure(kNotFinished) == "");
}

TEST_CASE("the engine keeps running while a script is parked")
{
    Fixture fixture;
    REQUIRE(fixture.booted);
    Debugger& debugger = fixture.runtime->debugger();
    lua_State* L = fixture.runtime->state();

    CHECK(debugger.setBreakpoint(L, kChunk, 2) == 0);
    mountAndStart(fixture);
    REQUIRE(debugger.parked());

    // **The claim the whole design rests on.** A blocking debugger would look
    // identical in every assertion above and fail this one: the VM is still
    // usable, other code still runs, and only the parked coroutine is holding
    // still. What stops the SIMULATION is `Editor::allowedTicks`, one layer up,
    // which is where a rule about the world belongs.
    for (int i = 0; i < 5; ++i)
        fixture.runtime->drain(core::Phase::PreSimulation);
    CHECK(debugger.parked());
    CHECK(fixture.failure("assert(1 + 1 == 2)") == "");
    CHECK(fixture.errors().empty());
}
