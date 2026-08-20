#include <doctest/doctest.h>
#include <ostream>

#include "script_fixture.h"

using luaug::script::testing::Fixture;

// api-design.md §3.2. The timing rules are the ones worth the most care: a
// deadline is a tick index and never a float compared against a float, because
// `task.wait(1)` lasting 61 ticks would break every recorded replay in a way
// nothing else would notice.

TEST_CASE("the scheduling surface is exactly task, with no legacy globals")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        assert(type(task) == "table")
        assert(type(task.spawn) == "function")
        assert(type(task.defer) == "function")
        assert(type(task.delay) == "function")
        assert(type(task.wait) == "function")
        assert(type(task.cancel) == "function")
    )") == "");

    // The absence of `wait`/`spawn`/`delay`/`tick` is a VM-level property and
    // is tested in `sandbox_tests.cpp` against the global table, not here:
    // naming an undeclared global is itself a strict-mode error, so a spec
    // cannot legally reference `wait` to prove it is gone (M2 brief, ruling R-D).
}

TEST_CASE("task.spawn runs immediately and synchronously")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local ran = false
        task.spawn(function(a, b)
            assert(a == 1 and b == "two", "a=" .. tostring(a) .. " b=" .. tostring(b))
            ran = true
        end, 1, "two")
        -- The one non-deferred call, and deliberately so: it has already run by
        -- the time this line executes.
        assert(ran == true)
    )") == "");

    CHECK(fixture.errors() == "");
}

TEST_CASE("task.spawn's error does not propagate to its caller")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        task.spawn(function()
            error("deliberate")
        end)
        -- Returns normally either way.
        local reached = true
        assert(reached)
    )") == "");

    CHECK(fixture.logContains("deliberate"));
}

TEST_CASE("task.defer runs at the next drain, in queue order against fires")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local signal = Signal.new()
        local seen = {}
        signal:Connect(function()
            table.insert(seen, "fire")
        end)

        -- One queue, one order: the deferred callback and the fire are ordered
        -- by when each was raised, not by what kind of thing each is.
        task.defer(function()
            table.insert(seen, "before")
        end)
        signal:Fire()
        task.defer(function()
            table.insert(seen, "after")
        end)

        assert(#seen == 0)
        task.wait()

        assert(seen[1] == "before", tostring(seen[1]))
        assert(seen[2] == "fire", tostring(seen[2]))
        assert(seen[3] == "after", tostring(seen[3]))
    )") == "");

    fixture.tick(2);
    CHECK(fixture.errors() == "");
}

TEST_CASE("task.defer carries its arguments")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local seen
        task.defer(function(a, b, c)
            seen = tostring(a) .. "/" .. tostring(b) .. "/" .. tostring(c)
        end, 1, "two", true)

        task.wait()
        assert(seen == "1/two/true", tostring(seen))
    )") == "");

    fixture.tick(2);
    CHECK(fixture.errors() == "");
}

TEST_CASE("task.wait(1) is exactly sixty ticks, which is the whole of the replay guarantee")
{
    Fixture fixture;

    // At dt = 1/60, `1 / (1/60)` evaluates to 60.000000000000007. A naive ceil
    // yields 61 and this test is the one that would catch it.
    CHECK(fixture.failure(R"(
        local elapsed = task.wait(1)
        assert(math.abs(elapsed - 1) < 1e-9, tostring(elapsed))
    )") == "");

    fixture.tick(59);
    CHECK(fixture.errors() == "");

    fixture.tick(1);
    CHECK(fixture.errors() == "");
    // The chunk resumed and its assertion held; if the deadline had been 61
    // ticks the assertion would not have run at all by now.
    CHECK(fixture.world->engineState().tick == 60);
}

TEST_CASE("task.wait(0) and a negative duration both mean the next tick")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local elapsed = task.wait(0)
        -- `wait` must yield, so "at or after 0 seconds" cannot be satisfied by
        -- the phase you are standing in.
        assert(elapsed > 0, tostring(elapsed))
        assert(math.abs(elapsed - 1 / 60) < 1e-9, tostring(elapsed))

        local negative = task.wait(-5)
        assert(math.abs(negative - 1 / 60) < 1e-9, tostring(negative))
    )") == "");

    fixture.tick(3);
    CHECK(fixture.errors() == "");
}

TEST_CASE("a duration between ticks rounds up, because the guarantee is at-or-after")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        -- 1.4 ticks at the default timestep: one tick is too early.
        local elapsed = task.wait(1.4 / 60)
        assert(math.abs(elapsed - 2 / 60) < 1e-9, tostring(elapsed))
    )") == "");

    fixture.tick(3);
    CHECK(fixture.errors() == "");
}

TEST_CASE("task.delay runs its callback at the deadline and not before")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local marker = Instance.new("Folder")
        local ticks = 0
        task.delay(3 / 60, function()
            marker:SetAttribute("fired", true)
        end)

        for _ = 1, 2 do
            task.wait()
            ticks += 1
            assert(marker:GetAttribute("fired") == nil, "fired at tick " .. tostring(ticks))
        end

        task.wait()
        assert(marker:GetAttribute("fired") == true)
    )") == "");

    fixture.tick(6);
    CHECK(fixture.errors() == "");
}

TEST_CASE("two things due on the same tick resume in scheduling order")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local marker = Instance.new("Folder")
        local seen = {}

        task.delay(2 / 60, function()
            table.insert(seen, "delay-first")
        end)
        task.spawn(function()
            task.wait(2 / 60)
            table.insert(seen, "wait-second")
        end)
        task.delay(2 / 60, function()
            table.insert(seen, "delay-third")
        end)

        task.wait(3 / 60)
        -- One FIFO ordered by (deadline tick, scheduling sequence), with wait
        -- resumptions and delay callbacks interleaved in it.
        assert(#seen == 3, tostring(#seen))
        assert(seen[1] == "delay-first", tostring(seen[1]))
        assert(seen[2] == "wait-second", tostring(seen[2]))
        assert(seen[3] == "delay-third", tostring(seen[3]))
    )") == "");

    fixture.tick(5);
    CHECK(fixture.errors() == "");
}

TEST_CASE("an earlier deadline resumes first however it was scheduled")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local seen = {}
        task.delay(4 / 60, function()
            table.insert(seen, "late")
        end)
        task.delay(1 / 60, function()
            table.insert(seen, "early")
        end)

        task.wait(5 / 60)
        assert(seen[1] == "early", tostring(seen[1]))
        assert(seen[2] == "late", tostring(seen[2]))
    )") == "");

    fixture.tick(7);
    CHECK(fixture.errors() == "");
}

TEST_CASE("the schedulers return the thread the callback runs on")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local spawned = task.spawn(function()
            assert(coroutine.running() ~= nil)
        end)
        assert(type(spawned) == "thread")

        local deferred = task.defer(function() end)
        assert(type(deferred) == "thread")

        local delayed = task.delay(1 / 60, function() end)
        assert(type(delayed) == "thread")
    )") == "");

    fixture.tick(3);
    CHECK(fixture.errors() == "");
}

TEST_CASE("task.cancel stops a pending resumption, whichever call scheduled it")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local ranDeferred, ranDelayed = false, false

        local deferred = task.defer(function()
            ranDeferred = true
        end)
        local delayed = task.delay(2 / 60, function()
            ranDelayed = true
        end)

        task.cancel(deferred)
        task.cancel(delayed)

        task.wait(4 / 60)
        assert(ranDeferred == false)
        assert(ranDelayed == false)
    )") == "");

    fixture.tick(6);
    CHECK(fixture.errors() == "");
}

TEST_CASE("cancelling something with no pending resumption raises")
{
    Fixture fixture;

    // A finished thread, an already-cancelled one and the running one all lack
    // the only thing `cancel` can take away.
    CHECK(fixture.raises(R"(
        local finished = task.spawn(function() end)
        task.cancel(finished)
    )",
                         "script.err.task_not_scheduled"));

    CHECK(fixture.raises(R"(
        local pending = task.defer(function() end)
        task.cancel(pending)
        task.cancel(pending)
    )",
                         "script.err.task_not_scheduled"));

    CHECK(fixture.raises("task.cancel(coroutine.running())", "script.err.task_not_scheduled"));
}

TEST_CASE("the schedulers take a function and never a thread")
{
    Fixture fixture;

    // Resuming a coroutine you already hold is `coroutine.resume`'s job, and
    // keeping the two surfaces apart is what guarantees every scheduled thread
    // is one the scheduler created and can account for.
    CHECK(fixture.failure("task.spawn(coroutine.create(function() end))") != "");
    CHECK(fixture.failure("task.defer(coroutine.create(function() end))") != "");
    CHECK(fixture.failure("task.delay(0, coroutine.create(function() end))") != "");
}

TEST_CASE("a task error is contained and does not stop the tick")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local marker = Instance.new("Folder")
        task.defer(function()
            error("deliberate")
        end)
        task.defer(function()
            marker:SetAttribute("after", true)
        end)

        task.wait()
        assert(marker:GetAttribute("after") == true)
    )") == "");

    fixture.tick(2);
    CHECK(fixture.logContains("deliberate"));
}

TEST_CASE("SimTime advances with the tick and is what a script reads the clock from")
{
    Fixture fixture;

    fixture.tick(30);
    CHECK(fixture.world->engineState().tick == 30);
    CHECK(fixture.world->engineState().simTime == doctest::Approx(0.5).epsilon(1e-9));
}
