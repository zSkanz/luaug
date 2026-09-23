--!strict
--- Conformance: `ReplicatedStorage` and `ServerStorage` (ADR 0080).
---
--- **Kept, not shown.** What a storage holds is not the world: it is not
--- simulated, and a template comes into the world by being cloned into
--- `Workspace`. Solo, this machine is the authority and has both.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect

local PhysicsService = game:GetService("PhysicsService")
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local ServerStorage = game:GetService("ServerStorage")

local function step(ticks: number)
    task.wait(ticks * PhysicsService.FixedTimestep)
end

describe("storage", function()
    it("exists from boot, under the data model, and is reached by a dot too", function()
        expect(ReplicatedStorage.Parent):ToBe(game)
        expect(ServerStorage.Parent):ToBe(game)
        expect((game :: any).ReplicatedStorage):ToBe(ReplicatedStorage)
        expect(ReplicatedStorage.ClassName):ToBe("ReplicatedStorage")
    end)

    it("keeps a part without simulating it, and a clone in the world falls", function()
        local template = Instance.new("Part")
        template.Name = "Crate"
        template.Anchored = false
        template.Position = vector.create(0, 200, 0)
        template.Parent = ServerStorage
        step(20)
        expect(template.Position.y):ToBe(200)

        local copy = template:Clone() :: Part
        copy.Parent = workspace
        step(20)
        expect(copy.Position.y < 200):ToBe(true)
        -- The template is where it was.
        expect(template.Position.y):ToBe(200)
        copy:Destroy()
        template:Destroy()
    end)

    it("holds a RemoteEvent as well as the world does", function()
        local remote = Instance.new("RemoteEvent")
        remote.Name = "Kept"
        remote.Parent = ReplicatedStorage
        local heard = false
        remote.ServerReceived:Connect(function()
            heard = true
        end)
        remote:FireServer()
        step(2)
        expect(heard):ToBe(true)
        remote:Destroy()
    end)
end)
