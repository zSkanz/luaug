--!strict
--- Conformance: `StreamingService` (docs/api-design.md 2.1).
---
--- Written against the document rather than against the implementation, which
--- is what makes it a contract: every claim below is a sentence in 2.1's
--- `StreamingService` entry or in the property's own Doc.
---
--- What is NOT here, deliberately: whether any chunk is resident. A conformance
--- run mounts no world, so a spec asserting residency would be asserting the
--- absence of content and would keep passing when streaming broke. Residency is
--- the deliverable's job and the streaming manager's unit tests'.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local beforeEach, afterEach = testing.beforeEach, testing.afterEach

describe("StreamingService", function()
    local service: StreamingService
    local scratch: Folder

    beforeEach(function()
        service = game:GetService("StreamingService")
        scratch = Instance.new("Folder")
        scratch.Name = "ConformanceStreaming"
        scratch.Parent = workspace
    end)

    afterEach(function()
        scratch:Destroy()
    end)

    it("is a service and is reached the same way every other one is", function()
        expect(service):ToBeTruthy()
        expect(service.Name):ToBe("StreamingService")
        expect(game:GetService("StreamingService")):ToBe(service)
    end)

    it("cannot be constructed", function()
        -- The cast is the point rather than a workaround: the generated
        -- definitions have no `("StreamingService") -> StreamingService`
        -- overload at all, so the analyzer refuses this before the engine gets
        -- a chance to. `NotCreatable` is enforced twice, and this pins the
        -- second half.
        expect(function()
            Instance.new(("StreamingService" :: any) :: "Folder")
        end):ToThrow()
    end)

    describe("the radii", function()
        it("round-trip", function()
            service.LoadRadius = 900
            service.MinRadius = 300
            expect(service.LoadRadius):ToBe(900)
            expect(service.MinRadius):ToBe(300)
        end)

        it("refuse a value that is not a positive number", function()
            service.LoadRadius = 900

            expect(function()
                service.LoadRadius = 0
            end):ToThrow()
            expect(function()
                service.LoadRadius = -5
            end):ToThrow()
            expect(function()
                service.LoadRadius = "far" :: any
            end):ToThrow()

            -- A refusal leaves the property alone. A partial write would hand
            -- the caller a world streaming at a radius nobody chose.
            expect(service.LoadRadius):ToBe(900)
        end)

        it("accept a MinRadius larger than the LoadRadius", function()
            -- Deliberately legal: the two are written in whichever order a
            -- script happens to write them, and refusing the first because the
            -- second has not happened yet is a property that depends on
            -- statement order. The engine takes the smaller of the two.
            service.LoadRadius = 100
            service.MinRadius = 5000
            expect(service.MinRadius):ToBe(5000)
        end)
    end)

    describe("Enabled", function()
        it("round-trips and defaults to true", function()
            expect(service.Enabled):ToBe(true)
            service.Enabled = false
            expect(service.Enabled):ToBe(false)
            service.Enabled = true
        end)

        it("refuses a non-boolean", function()
            expect(function()
                service.Enabled = 1 :: any
            end):ToThrow()
        end)
    end)

    describe("PauseOutsideLoadedArea", function()
        it("round-trips and defaults to false", function()
            expect(service.PauseOutsideLoadedArea):ToBe(false)
            service.PauseOutsideLoadedArea = true
            expect(service.PauseOutsideLoadedArea):ToBe(true)
            service.PauseOutsideLoadedArea = false
        end)
    end)

    describe("AddFocus", function()
        it("accepts something with a position", function()
            local part = Instance.new("Part")
            part.Parent = scratch
            expect(function()
                service:AddFocus(part)
            end).Never:ToThrow()
            service:RemoveFocus(part)
        end)

        it("refuses an instance that cannot be located", function()
            -- A focus that cannot be located would silently stream around the
            -- origin, which reads as "streaming is broken" rather than as
            -- "that was the wrong instance".
            local folder = Instance.new("Folder")
            folder.Parent = scratch
            expect(function()
                service:AddFocus(folder)
            end):ToThrow()
        end)

        it("is idempotent", function()
            local part = Instance.new("Part")
            part.Parent = scratch
            service:AddFocus(part)
            expect(function()
                service:AddFocus(part)
            end).Never:ToThrow()
            service:RemoveFocus(part)
        end)
    end)

    describe("RemoveFocus", function()
        it("accepts one that was never added", function()
            local part = Instance.new("Part")
            part.Parent = scratch
            expect(function()
                service:RemoveFocus(part)
            end).Never:ToThrow()
        end)
    end)

    describe("LoadAreaAsync", function()
        it("returns once the area it names is loaded", function()
            -- A conformance run has no world, so every area is trivially
            -- loaded and this returns on the next pump. What it pins is that
            -- the call YIELDS and resumes rather than blocking or erroring --
            -- a teleport calls it before moving the character.
            local returned = false
            task.spawn(function()
                service:LoadAreaAsync(vector.create(0, 0, 0), 100)
                returned = true
            end)

            expect(returned):ToBe(false)
            task.wait(0.1)
            expect(returned):ToBe(true)
        end)

        it("refuses a radius that is not positive", function()
            expect(function()
                service:LoadAreaAsync(vector.create(0, 0, 0), 0)
            end):ToThrow()
        end)
    end)

    describe("the events", function()
        it("are connectable and deferred like every other signal", function()
            local fired = false
            local connection = service.AreaLoaded:Connect(function()
                fired = true
            end)

            task.spawn(function()
                service:LoadAreaAsync(vector.create(0, 0, 0), 50)
            end)
            expect(fired):ToBe(false)
            task.wait(0.1)
            expect(fired):ToBe(true)

            connection:Disconnect()
        end)

        it("expose InstanceStreamedOut", function()
            -- Nothing streams out in a conformance run; what is pinned is that
            -- the signal EXISTS and connects, because a script writing against
            -- it should fail at the connect rather than at the first eviction.
            local connection = service.InstanceStreamedOut:Connect(function() end)
            expect(connection.Connected):ToBe(true)
            connection:Disconnect()
        end)
    end)
end)
