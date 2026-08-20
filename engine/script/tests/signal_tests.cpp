#include <doctest/doctest.h>

#include <ostream>

#include "script_fixture.h"

using luaug::script::testing::Fixture;

// Every case here is a rule from api-design.md §3.1, which was written before
// the implementation existed. A test that disagrees with the document is a bug
// in one of the two, and the document is what the conformance specs were
// authored against.

TEST_CASE("nothing runs before the next drain")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local signal = Signal.new()
        local ran = false
        signal:Connect(function()
            ran = true
        end)

        signal:Fire()
        -- Deferred-only (ADR 0015): a fire enqueues and nothing more.
        assert(ran == false)

        task.wait()
        assert(ran == true)
    )") == "");

    fixture.tick(2);
    CHECK(fixture.errors() == "");
}

TEST_CASE("handlers of one fire run in the order they were connected")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local signal = Signal.new()
        local seen = {}
        for index = 1, 4 do
            signal:Connect(function()
                table.insert(seen, index)
            end)
        end

        signal:Fire()
        task.wait()

        assert(#seen == 4, tostring(#seen))
        for index = 1, 4 do
            assert(seen[index] == index, tostring(index) .. " got " .. tostring(seen[index]))
        end
    )") == "");

    fixture.tick(2);
    CHECK(fixture.errors() == "");
}

TEST_CASE("a fire captures the connection list at the moment it was raised")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local signal = Signal.new()
        local early, late = 0, 0
        signal:Connect(function()
            early += 1
        end)

        signal:Fire()
        -- Connected AFTER the fire: it was not listening when the thing
        -- happened, so it does not run for it.
        signal:Connect(function()
            late += 1
        end)

        task.wait()
        assert(early == 1, tostring(early))
        assert(late == 0, tostring(late))

        -- And it does run for the next one.
        signal:Fire()
        task.wait()
        assert(early == 2 and late == 1)
    )") == "");

    fixture.tick(4);
    CHECK(fixture.errors() == "");
}

TEST_CASE("Disconnect is reliable, not advisory")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local signal = Signal.new()
        local ran = 0
        local connection = signal:Connect(function()
            ran += 1
        end)

        assert(connection.Connected == true)
        signal:Fire()
        connection:Disconnect()
        assert(connection.Connected == false)

        task.wait()
        -- Disconnected before it was invoked, so it does not run -- even though
        -- the fire captured it.
        assert(ran == 0, tostring(ran))

        -- Idempotent: cleanup code never has to guard it.
        connection:Disconnect()
        assert(connection.Connected == false)
    )") == "");

    fixture.tick(2);
    CHECK(fixture.errors() == "");
}

TEST_CASE("a handler that disconnects a later one stops it running in the same fire")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local signal = Signal.new()
        local secondRan = false
        local second
        signal:Connect(function()
            second:Disconnect()
        end)
        second = signal:Connect(function()
            secondRan = true
        end)

        signal:Fire()
        task.wait()
        assert(secondRan == false)
    )") == "");

    fixture.tick(2);
    CHECK(fixture.errors() == "");
}

TEST_CASE("Once disconnects on invocation, so two fires before a drain invoke it once")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local signal = Signal.new()
        local ran = 0
        signal:Once(function()
            ran += 1
        end)

        signal:Fire()
        signal:Fire()
        task.wait()
        assert(ran == 1, tostring(ran))

        signal:Fire()
        task.wait()
        assert(ran == 1, tostring(ran))
    )") == "");

    fixture.tick(3);
    CHECK(fixture.errors() == "");
}

TEST_CASE("arguments are carried, and carried by reference")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local signal = Signal.new()
        local seenA, seenB, seenC
        signal:Connect(function(a, b, c)
            seenA, seenB, seenC = a, b, c
        end)

        local shared = { value = 1 }
        signal:Fire("text", 42, shared)
        -- Mutating between the fire and the drain is visible to the handlers:
        -- arguments are captured, not copied.
        shared.value = 2

        task.wait()
        assert(seenA == "text")
        assert(seenB == 42)
        assert(seenC == shared and seenC.value == 2)
    )") == "");

    fixture.tick(2);
    CHECK(fixture.errors() == "");
}

TEST_CASE("Wait resumes with the fire's arguments, in registration order")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local signal = Signal.new()
        local seen = {}

        signal:Connect(function()
            table.insert(seen, "A")
        end)

        -- Registered between A and B, so it resumes between them.
        task.spawn(function()
            local value = signal:Wait()
            table.insert(seen, "wait:" .. tostring(value))
        end)

        signal:Connect(function()
            table.insert(seen, "B")
        end)

        signal:Fire(7)
        task.wait()

        assert(#seen == 3, tostring(#seen))
        assert(seen[1] == "A", seen[1])
        assert(seen[2] == "wait:7", seen[2])
        assert(seen[3] == "B", seen[3])
    )") == "");

    fixture.tick(2);
    CHECK(fixture.errors() == "");
}

TEST_CASE("a drain does not block on a handler that yields")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local signal = Signal.new()
        local order = {}

        signal:Connect(function()
            table.insert(order, "slow-start")
            task.wait()
            table.insert(order, "slow-end")
        end)
        signal:Connect(function()
            table.insert(order, "fast")
        end)

        signal:Fire()
        task.wait()

        -- The yielding handler was left parked and the drain moved straight on;
        -- if it had blocked, "fast" would come after "slow-end".
        assert(order[1] == "slow-start", tostring(order[1]))
        assert(order[2] == "fast", tostring(order[2]))
    )") == "");

    fixture.tick(4);
    CHECK(fixture.errors() == "");
}

TEST_CASE("a drain runs to fixpoint")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local first = Signal.new()
        local second = Signal.new()
        local ran = false

        first:Connect(function()
            second:Fire()
        end)
        second:Connect(function()
            ran = true
        end)

        first:Fire()
        -- One drain, not two: a handler that fires appends to the same queue and
        -- the drain continues rather than stopping at its original end.
        task.wait()
        assert(ran == true)
    )") == "");

    fixture.tick(2);
    CHECK(fixture.errors() == "");
}

TEST_CASE("re-entrancy is capped at eleven invocations")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local signal = Signal.new()
        local count = 0
        signal:Connect(function()
            count += 1
            signal:Fire()
        end)

        signal:Fire()
        task.wait()
        -- Depths 0 through 10 all run, so the handler is invoked exactly eleven
        -- times and the twelfth fire is the one dropped.
        assert(count == 11, tostring(count))
    )") == "");

    fixture.tick(2);
    CHECK(fixture.errors() == "");
    CHECK(fixture.logContains("re-entrancy"));
}

TEST_CASE("the cap counts task.defer too, or a self-deferring callback is a hang")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local count = 0
        local function again()
            count += 1
            task.defer(again)
        end

        task.defer(again)
        task.wait()
        assert(count == 11, tostring(count))
    )") == "");

    fixture.tick(2);
    CHECK(fixture.errors() == "");
}

TEST_CASE("an error in one handler stops nothing else")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local signal = Signal.new()
        local after = false
        local stillConnected

        stillConnected = signal:Connect(function()
            error("deliberate")
        end)
        signal:Connect(function()
            after = true
        end)

        signal:Fire()
        task.wait()

        -- The other handler ran, the drain continued, and the firing script is
        -- still going. A handler that errors stays connected: an error is a fact
        -- about one invocation, not a disconnect.
        assert(after == true)
        assert(stillConnected.Connected == true)
    )") == "");

    fixture.tick(2);
    // Contained, but not silent: it goes to the log with its traceback.
    CHECK(fixture.logContains("deliberate"));
}

TEST_CASE("a Once handler that errors has still been consumed")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local signal = Signal.new()
        local count = 0
        signal:Once(function()
            count += 1
            error("deliberate")
        end)

        signal:Fire()
        task.wait()
        signal:Fire()
        task.wait()
        assert(count == 1, tostring(count))
    )") == "");

    fixture.tick(3);
}

TEST_CASE("Signal:Destroy closes a script signal, and only a script signal")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local signal = Signal.new()
        local ran = false
        local connection = signal:Connect(function()
            ran = true
        end)

        signal:Fire()
        signal:Destroy()

        task.wait()
        -- Already-queued fires find no live connections and invoke nothing.
        assert(ran == false)
        assert(connection.Connected == false)
    )") == "");

    fixture.tick(2);
    CHECK(fixture.errors() == "");

    CHECK(fixture.raises("Instance.new('Part').ChildAdded:Destroy()", "script.err.signal_not_destroyable"));
}

// --- Instance signals --------------------------------------------------------

TEST_CASE("an instance event is the same object every time")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local part = Instance.new("Part")
        assert(typeof(part.ChildAdded) == "Signal")
        assert(part.ChildAdded == part.ChildAdded)
        assert(part.ChildAdded ~= part.ChildRemoved)
        assert(part:GetPropertyChangedSignal("Name") == part:GetPropertyChangedSignal("Name"))
        assert(part:GetAttributeChangedSignal("hp") == part:GetAttributeChangedSignal("hp"))
        assert(part:GetPropertyChangedSignal("Name") ~= part:GetPropertyChangedSignal("Size"))
    )") == "");

    // A property the class does not have raises, and the key says where
    // attributes are watched instead.
    CHECK(fixture.raises("Instance.new('Part'):GetPropertyChangedSignal('Nope')", "scene.err.unknown_property"));
}

TEST_CASE("the tree signals fire in the order one operation raises them")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local oldParent = Instance.new("Folder")
        local newParent = Instance.new("Folder")
        local child = Instance.new("Part")
        child.Parent = oldParent
        task.wait()

        local seen = {}
        oldParent.ChildRemoved:Connect(function(removed)
            assert(removed == child)
            table.insert(seen, "ChildRemoved")
        end)
        newParent.ChildAdded:Connect(function(added)
            assert(added == child)
            table.insert(seen, "ChildAdded")
        end)
        child.AncestryChanged:Connect(function(instance, parent)
            assert(instance == child)
            assert(parent == newParent)
            table.insert(seen, "AncestryChanged")
        end)

        child.Parent = newParent
        task.wait()

        -- Leaving is fully observed before arriving, and the subtree is told
        -- last, once its new ancestry is already true of it.
        assert(seen[1] == "ChildRemoved", tostring(seen[1]))
        assert(seen[2] == "ChildAdded", tostring(seen[2]))
        assert(seen[3] == "AncestryChanged", tostring(seen[3]))
    )") == "");

    fixture.tick(3);
    CHECK(fixture.errors() == "");
}

TEST_CASE("a property write fires only when something changed")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local part = Instance.new("Part")
        part.Name = "Original"
        task.wait()

        local count = 0
        part:GetPropertyChangedSignal("Name"):Connect(function(...)
            -- Carries no value, which keeps its type independent of the
            -- property's.
            assert(select("#", ...) == 0)
            count += 1
        end)

        -- The value it already holds: nothing is enqueued.
        part.Name = "Original"
        task.wait()
        assert(count == 0, tostring(count))

        part.Name = "Changed"
        task.wait()
        assert(count == 1, tostring(count))

        -- Three distinct writes before one drain produce three fires, in write
        -- order -- there is no coalescing in the other direction.
        part.Name = "A"
        part.Name = "B"
        part.Name = "C"
        task.wait()
        assert(count == 4, tostring(count))
        assert(part.Name == "C")
    )") == "");

    fixture.tick(5);
    CHECK(fixture.errors() == "");
}

TEST_CASE("an attribute change fires the named signal before the catch-all")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local part = Instance.new("Part")
        local seen = {}

        part:GetAttributeChangedSignal("Level"):Connect(function()
            table.insert(seen, "named")
        end)
        part.AttributeChanged:Connect(function(name)
            table.insert(seen, "all:" .. name)
        end)

        part:SetAttribute("Level", 3)
        task.wait()

        assert(seen[1] == "named", tostring(seen[1]))
        assert(seen[2] == "all:Level", tostring(seen[2]))

        -- Equality-filtered like a property.
        part:SetAttribute("Level", 3)
        task.wait()
        assert(#seen == 2, tostring(#seen))
    )") == "");

    fixture.tick(3);
    CHECK(fixture.errors() == "");
}

TEST_CASE("Destroy fires Destroying and closes everything else")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local victim = Instance.new("Folder")
        local childAdded, destroying = 0, 0
        local connection = victim.ChildAdded:Connect(function()
            childAdded += 1
        end)
        victim.Destroying:Connect(function()
            destroying += 1
        end)

        local child = Instance.new("Folder")
        child.Parent = victim
        victim:Destroy()

        task.wait()
        -- The documented pair: the queued ChildAdded invokes nothing because
        -- Destroy closed that signal, and Destroying still runs.
        assert(childAdded == 0, tostring(childAdded))
        assert(destroying == 1, tostring(destroying))
        assert(connection.Connected == false)
    )") == "");

    fixture.tick(2);
    CHECK(fixture.errors() == "");
}

TEST_CASE("Destroying does not fire during the call, or twice, or for a late connection")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local victim = Instance.new("Folder")
        local count = 0
        victim.Destroying:Connect(function()
            count += 1
        end)

        victim:Destroy()
        assert(count == 0)
        -- A second Destroy is a no-op that enqueues nothing.
        victim:Destroy()

        -- Destroy enqueued Destroying first, so a connection made afterwards was
        -- not in the list that fire captured.
        local late = false
        victim.Destroying:Connect(function()
            late = true
        end)

        task.wait()
        assert(count == 1, tostring(count))
        assert(late == false)
    )") == "");

    fixture.tick(2);
    CHECK(fixture.errors() == "");
}

TEST_CASE("Destroying fires on every member of a destroyed subtree")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local parent = Instance.new("Folder")
        local child = Instance.new("Folder")
        child.Parent = parent
        task.wait()

        local seen = 0
        parent.Destroying:Connect(function()
            seen += 1
        end)
        child.Destroying:Connect(function()
            seen += 1
        end)

        parent:Destroy()
        task.wait()
        assert(seen == 2, tostring(seen))
    )") == "");

    fixture.tick(3);
    CHECK(fixture.errors() == "");
}

TEST_CASE("a quiet property write costs nothing when nobody is watching")
{
    Fixture fixture;

    // The subscription bitmask is not an optimisation to add later: it is what
    // lets 10k parts move every tick for free. Observed through the queue rather
    // than through a timing, because a timing would be a flaky test.
    CHECK(fixture.failure(R"(
        local part = Instance.new("Part")
        part.Name = "Quiet"
    )") == "");
    CHECK(fixture.world->changes().empty());

    CHECK(fixture.failure(R"(
        local part = Instance.new("Part")
        part:GetPropertyChangedSignal("Name"):Connect(function() end)
        part.Name = "Loud"
    )") == "");
    // Converted the moment it happened, which is why the scene queue is empty
    // and the deferred queue is not.
    CHECK(fixture.world->changes().empty());
    fixture.tick();
    CHECK(fixture.errors() == "");
}
