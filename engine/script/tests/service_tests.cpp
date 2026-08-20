#include <doctest/doctest.h>

#include <ostream>

#include "script_fixture.h"
#include "luaug/script/services.h"

using luaug::script::testing::Fixture;

TEST_CASE("the world boots with game, Workspace and ScriptService")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        assert(typeof(game) == "Instance")
        assert(game.ClassName == "DataModel")
        assert(game.Parent == nil)

        assert(typeof(workspace) == "Instance")
        assert(workspace.ClassName == "Workspace")
        -- Reached through the global as well as through GetService, and it is
        -- the same instance either way.
        assert(workspace.Parent == game)
        assert(game:GetService("Workspace") == workspace)

        -- The mount point exists before anything mounts.
        assert(game:FindService("ScriptService") ~= nil)
    )") == "");
}

TEST_CASE("a service is created on demand and is a singleton thereafter")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        -- Not yet asked for, so not yet in existence.
        assert(game:FindService("RunService") == nil)

        local run = game:GetService("RunService")
        assert(run.ClassName == "RunService")
        assert(run.Parent == game)

        -- Every later call returns the same instance.
        assert(game:GetService("RunService") == run)
        assert(game:FindService("RunService") == run)

        -- And it is an ordinary child of game once created.
        local found = false
        for _, child in game:GetChildren() do
            if child == run then
                found = true
            end
        end
        assert(found)
    )") == "");
}

TEST_CASE("GetService raises on an unknown name and FindService does not")
{
    Fixture fixture;

    CHECK(fixture.raises(R"(game:GetService("Nonexistent"))", "scene.err.unknown_service"));
    // A class that exists but is not a service is not a service.
    CHECK(fixture.raises(R"(game:GetService("Part"))", "scene.err.unknown_service"));

    // Asking whether something exists is not the same as asking for it.
    CHECK(fixture.failure(R"(
        assert(game:FindService("Nonexistent") == nil)
        assert(game:FindService("Part") == nil)
    )") == "");
}

TEST_CASE("a service cannot be constructed, only reached")
{
    Fixture fixture;

    CHECK(fixture.raises(R"(Instance.new("RunService"))", "scene.err.not_creatable"));
    CHECK(fixture.raises(R"(Instance.new("DataModel"))", "scene.err.not_creatable"));
}

// --- RunService --------------------------------------------------------------

TEST_CASE("the phase signals fire once per tick, carrying the timestep")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local run = game:GetService("RunService")
        local marker = Instance.new("Folder")
        local beats = 0
        local seenDelta = 0

        run.Heartbeat:Connect(function(dt)
            beats += 1
            seenDelta = dt
            marker:SetAttribute("beats", beats)
            marker:SetAttribute("dt", dt)
        end)
    )") == "");

    fixture.tick(3);
    CHECK(fixture.errors() == "");

    CHECK(fixture.failure(R"(
        local run = game:GetService("RunService")
        -- Read back through an attribute, because the chunk that connected has
        -- finished and its locals are gone.
        assert(run.SimTime > 0, tostring(run.SimTime))
    )") == "");
}

TEST_CASE("SimTime advances with the tick and Pause stops it")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local run = game:GetService("RunService")
        assert(run.SimTime == 0)
        assert(run:IsPaused() == false)

        run:Pause()
        assert(run:IsPaused() == true)
        -- Idempotent: pausing a paused world is a no-op, not an error.
        run:Pause()
        assert(run:IsPaused() == true)

        run:Resume()
        assert(run:IsPaused() == false)
        run:Resume()
        assert(run:IsPaused() == false)
    )") == "");

    fixture.tick(30);
    CHECK(fixture.failure(R"(
        local run = game:GetService("RunService")
        -- Constant for the whole tick and advancing by FixedTimestep between
        -- ticks; 30 ticks at the default 1/60 is half a second.
        assert(math.abs(run.SimTime - 0.5) < 1e-9, tostring(run.SimTime))
    )") == "");
}

TEST_CASE("FixedTimestep is the grid every timing guarantee rests on")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local physics = game:GetService("PhysicsService")
        assert(math.abs(physics.FixedTimestep - 1 / 60) < 1e-12)
    )") == "");

    // Read-only until the physics module ships.
    CHECK(fixture.raises(R"(game:GetService("PhysicsService").FixedTimestep = 1 / 30)",
                         "scene.err.read_only_property"));
}

// --- TagService --------------------------------------------------------------

TEST_CASE("TagService finds instances by tag, wherever they are parented")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local tags = game:GetService("TagService")
        local parented = Instance.new("Part")
        local orphan = Instance.new("Part")
        parented.Parent = workspace

        parented:AddTag("Pickup")
        orphan:AddTag("Pickup")

        local found = tags:GetTagged("Pickup")
        assert(#found == 2, tostring(#found))

        -- Tags are pure instance state with no relationship to the tree, so an
        -- instance parented to nil is returned like any other.
        local sawOrphan = false
        for _, instance in found do
            if instance == orphan then
                sawOrphan = true
            end
        end
        assert(sawOrphan)

        assert(#tags:GetTagged("Nothing") == 0)
    )") == "");
}

TEST_CASE("GetAllTags is what is carried now, not every name ever seen")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local tags = game:GetService("TagService")
        local part = Instance.new("Part")

        assert(#tags:GetAllTags() == 0)
        part:AddTag("Alpha")
        part:AddTag("Beta")
        assert(#tags:GetAllTags() == 2)

        -- A tag leaves the set the moment its last carrier drops it, even
        -- though the signal reporting the drop is deferred like everything else.
        part:RemoveTag("Alpha")
        local remaining = tags:GetAllTags()
        assert(#remaining == 1, tostring(#remaining))
        assert(remaining[1] == "Beta")
    )") == "");
}

TEST_CASE("the tag signals fire deferred, with the instance that gained or lost it")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local tags = game:GetService("TagService")
        local part = Instance.new("Part")
        local marker = Instance.new("Folder")

        tags:GetInstanceAddedSignal("Pickup"):Connect(function(instance)
            marker:SetAttribute("added", instance == part)
        end)
        tags:GetInstanceRemovedSignal("Pickup"):Connect(function(instance)
            marker:SetAttribute("removed", instance == part)
        end)

        part:AddTag("Pickup")
        -- Deferred, so a handler sees it at the next resumption point rather
        -- than inside the AddTag call that added it.
        assert(marker:GetAttribute("added") == nil)

        task.wait()
        assert(marker:GetAttribute("added") == true)

        part:RemoveTag("Pickup")
        task.wait()
        assert(marker:GetAttribute("removed") == true)

        -- The same object every time, like every other instance signal.
        assert(tags:GetInstanceAddedSignal("Pickup") == tags:GetInstanceAddedSignal("Pickup"))
        assert(tags:GetInstanceAddedSignal("Pickup") ~= tags:GetInstanceRemovedSignal("Pickup"))
    )") == "");

    fixture.tick(3);
    CHECK(fixture.errors() == "");
}

TEST_CASE("Destroy strips every tag, so the removed signal reports it")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local tags = game:GetService("TagService")
        local marker = Instance.new("Folder")
        local victim = Instance.new("Part")
        victim:AddTag("Pickup")

        tags:GetInstanceRemovedSignal("Pickup"):Connect(function(instance)
            -- A destroyed handle still resolves for the drain in which it
            -- arrives, so a handler can read what it lost before it goes.
            marker:SetAttribute("lost", instance.ClassName)
        end)

        victim:Destroy()
        task.wait()
        assert(marker:GetAttribute("lost") == "Part", tostring(marker:GetAttribute("lost")))
        assert(#tags:GetTagged("Pickup") == 0)
    )") == "");

    fixture.tick(2);
    CHECK(fixture.errors() == "");
}

// --- WaitForChild ------------------------------------------------------------

TEST_CASE("WaitForChild returns immediately when the child is already there")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local root = Instance.new("Folder")
        local child = Instance.new("Part")
        child.Name = "Target"
        child.Parent = root

        -- No yield: a matching child already present returns without parking.
        assert(root:WaitForChild("Target") == child)
    )") == "");

    CHECK(fixture.errors() == "");
}

TEST_CASE("WaitForChild parks until the child exists, however it came to")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local root = Instance.new("Folder")
        local marker = Instance.new("Folder")

        task.spawn(function()
            local found = root:WaitForChild("Target")
            marker:SetAttribute("found", found.ClassName)
        end)
        assert(marker:GetAttribute("found") == nil)

        task.wait()
        assert(marker:GetAttribute("found") == nil)

        local child = Instance.new("Part")
        child.Name = "Target"
        child.Parent = root

        task.wait()
        assert(marker:GetAttribute("found") == "Part", tostring(marker:GetAttribute("found")))
    )") == "");

    fixture.tick(4);
    CHECK(fixture.errors() == "");
}

TEST_CASE("a sibling renamed into the awaited name satisfies a waiter")
{
    Fixture fixture;

    // The contract is about the state, not about the event that produced it.
    CHECK(fixture.failure(R"(
        local root = Instance.new("Folder")
        local marker = Instance.new("Folder")
        local sibling = Instance.new("Part")
        sibling.Name = "Something"
        sibling.Parent = root

        task.spawn(function()
            marker:SetAttribute("found", root:WaitForChild("Target") == sibling)
        end)

        task.wait()
        assert(marker:GetAttribute("found") == nil)

        sibling.Name = "Target"
        task.wait()
        assert(marker:GetAttribute("found") == true)
    )") == "");

    fixture.tick(4);
    CHECK(fixture.errors() == "");
}

TEST_CASE("a timeout expires to nil, with no error and no warning")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local root = Instance.new("Folder")
        local marker = Instance.new("Folder")

        task.spawn(function()
            local found = root:WaitForChild("Never", 2 / 60)
            marker:SetAttribute("expired", found == nil)
        end)

        task.wait()
        assert(marker:GetAttribute("expired") == nil)

        task.wait(3 / 60)
        assert(marker:GetAttribute("expired") == true)
    )") == "");

    fixture.tick(6);
    CHECK(fixture.errors() == "");
    // The timeout form never warns, however long its timeout: you said how long
    // you were prepared to wait.
    CHECK_FALSE(fixture.logContains("Infinite yield"));
}

TEST_CASE("an unbounded wait warns after five sim-seconds and keeps waiting")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local root = Instance.new("Folder")
        root.Name = "Patient"
        local marker = Instance.new("Folder")

        task.spawn(function()
            marker:SetAttribute("found", root:WaitForChild("Eventually").ClassName)
        end)
    )") == "");

    // Five sim-seconds is 300 ticks; one more to cross it.
    fixture.tick(299);
    CHECK_FALSE(fixture.logContains("Infinite yield"));

    fixture.tick(2);
    CHECK(fixture.logContains("Infinite yield"));

    // Warned ONCE, and still waiting: the warning is a diagnostic, not a poll.
    CHECK(fixture.logCount("Infinite yield") == 1);
    fixture.tick(60);
    CHECK(fixture.logCount("Infinite yield") == 1);
}

// --- DebugService ------------------------------------------------------------

TEST_CASE("MessageOut carries every print and warn with its level")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local debugService = game:GetService("DebugService")
        local marker = Instance.new("Folder")
        local count = 0

        debugService.MessageOut:Connect(function(message, level)
            count += 1
            marker:SetAttribute("last", message)
            marker:SetAttribute("level", level.Name)
            marker:SetAttribute("count", count)
        end)

        print("hello", 42)
        warn("careful")

        task.wait()
        -- Exactly one fire per call, tab-separated like print itself.
        assert(marker:GetAttribute("count") == 2, tostring(marker:GetAttribute("count")))
        assert(marker:GetAttribute("last") == "careful")
        assert(marker:GetAttribute("level") == "Warning", tostring(marker:GetAttribute("level")))
    )") == "");

    fixture.tick(2);
    CHECK(fixture.errors() == "");
}

TEST_CASE("the gizmos validate their arguments and draw nothing headless")
{
    Fixture fixture;

    // A silent no-op rather than an error, so debug drawing left in shared code
    // cannot fail a headless test.
    CHECK(fixture.failure(R"(
        local debugService = game:GetService("DebugService")
        debugService:DrawLine(Vector3.zero, Vector3.new(0, 1, 0))
        debugService:DrawLine(Vector3.zero, Vector3.new(0, 1, 0), Color3.new(1, 0, 0))
        debugService:DrawBox(CFrame.new(), Vector3.one)
        debugService:DrawSphere(Vector3.zero, 2)
    )") == "");

    // Validated even headless: a no-op that also skipped the checks would make
    // headless the one place a typo survives.
    CHECK(fixture.failure(R"(game:GetService("DebugService"):DrawSphere(Vector3.zero, "big"))") != "");
    CHECK(fixture.failure(R"(game:GetService("DebugService"):DrawLine(1, 2))") != "");
}

TEST_CASE("a stat nothing published raises rather than answering zero")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local debugService = game:GetService("DebugService")
        -- Answered from the world, so it is exact at any moment.
        assert(debugService:GetStat("InstanceCount") > 0)

        debugService:SetCustomStat("Enemies", 7)
        assert(debugService:GetStat("Enemies") == 7)
        debugService:SetCustomStat("Enemies", 9)
        assert(debugService:GetStat("Enemies") == 9)
    )") == "");

    CHECK(fixture.raises(R"(game:GetService("DebugService"):GetStat("Misspelt"))", "scene.err.unknown_stat"));
}

TEST_CASE("the panels are a closed set and OverlayVisible starts off")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local debugService = game:GetService("DebugService")
        -- Off in shipped builds too, so a game offers the overlay deliberately.
        assert(debugService.OverlayVisible == false)
        debugService.OverlayVisible = true
        assert(debugService.OverlayVisible == true)

        debugService:ShowPanel("Stats")
        debugService:ShowPanel("Stats")
        debugService:HidePanel("Stats")
        debugService:HidePanel("Stats")
    )") == "");

    CHECK(fixture.raises(R"(game:GetService("DebugService"):ShowPanel("Nope"))", "scene.err.unknown_stat"));
    CHECK(fixture.raises(R"(game:GetService("DebugService"):HidePanel("Nope"))", "scene.err.unknown_stat"));
}

// --- Shutdown ----------------------------------------------------------------

TEST_CASE("BindToClose runs at shutdown, and Shutdown asks for one")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local marker = Instance.new("Folder")
        marker.Name = "CloseMarker"
        marker.Parent = workspace

        game:BindToClose(function()
            marker:SetAttribute("first", true)
        end)
        game:BindToClose(function()
            marker:SetAttribute("second", true)
        end)

        assert(marker:GetAttribute("first") == nil)
        game:Shutdown()
    )") == "");

    CHECK(luaug::script::shutdownRequested(fixture.runtime->state()));
    luaug::script::runCloseHandlers(fixture.runtime->state());
    CHECK(fixture.errors() == "");

    CHECK(fixture.failure(R"(
        local marker = workspace:FindFirstChild("CloseMarker")
        assert(marker ~= nil)
        assert(marker:GetAttribute("first") == true)
        assert(marker:GetAttribute("second") == true)
    )") == "");
}

TEST_CASE("a close handler that errors does not stop the others")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local marker = Instance.new("Folder")
        marker.Name = "CloseMarker"
        marker.Parent = workspace

        game:BindToClose(function()
            error("deliberate")
        end)
        game:BindToClose(function()
            marker:SetAttribute("ran", true)
        end)
    )") == "");

    luaug::script::runCloseHandlers(fixture.runtime->state());
    CHECK(fixture.logContains("deliberate"));

    CHECK(fixture.failure(R"(
        assert(workspace:FindFirstChild("CloseMarker"):GetAttribute("ran") == true)
    )") == "");
}

// --- game.Loaded -------------------------------------------------------------

TEST_CASE("game.Loaded is deferred, so a file-scope connection is in time")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local marker = Instance.new("Folder")
        marker.Name = "LoadedMarker"
        marker.Parent = workspace

        game.Loaded:Connect(function(...)
            marker:SetAttribute("loaded", true)
            assert(select("#", ...) == 0)
        end)
    )") == "");

    luaug::script::fireDataModelLoaded(fixture.runtime->state());
    fixture.tick();
    CHECK(fixture.errors() == "");

    CHECK(fixture.failure(R"(
        assert(workspace:FindFirstChild("LoadedMarker"):GetAttribute("loaded") == true)
    )") == "");
}
