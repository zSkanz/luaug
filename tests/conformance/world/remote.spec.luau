--!strict
--- Conformance: `RemoteEvent`, played solo (N2, ADR 0077).
---
--- **One script runs solo and networked.** Solo, this machine is its own
--- authority and its own client, so every message is delivered here -- which
--- is what lets these cases exercise the whole of the script side: what
--- travels, what arrives, what is refused, and who the sender is.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect

local NetworkService = game:GetService("NetworkService")
local PhysicsService = game:GetService("PhysicsService")

local function step(ticks: number)
    task.wait(ticks * PhysicsService.FixedTimestep)
end

describe("remote events", function()
    it("delivers FireServer to the authority's own ServerReceived, from the local player", function()
        local remote = Instance.new("RemoteEvent")
        remote.Parent = workspace
        local crate = Instance.new("Part")
        crate.Parent = workspace

        local from: any = nil
        local received: { any }? = nil
        remote.ServerReceived:Connect(function(player, ...)
            from = player
            received = { ... }
        end)
        remote:FireServer("hello", 42, vector.create(1, 2, 3), { nested = { list = { 1, 2, 3 } }, crate = crate }, true)
        step(2)

        expect(from):ToBe(NetworkService.LocalPlayer)
        expect(received ~= nil):ToBe(true)
        if received then
            expect(received[1]):ToBe("hello")
            expect(received[2]):ToBe(42)
            expect(received[3]):ToBe(vector.create(1, 2, 3))
            expect(received[4].nested.list[3]):ToBe(3)
            -- An instance arrives as this machine's own copy: here, itself.
            expect(received[4].crate):ToBe(crate)
            expect(received[5]):ToBe(true)
        end
        remote:Destroy()
        crate:Destroy()
    end)

    it("delivers FireClient and FireAllClients to the local player's ClientReceived", function()
        local remote = Instance.new("RemoteEvent")
        remote.Parent = workspace
        local messages: { string } = {}
        remote.ClientReceived:Connect(function(text)
            table.insert(messages, text)
        end)
        remote:FireClient(NetworkService.LocalPlayer :: Player, "just you")
        remote:FireAllClients("everyone")
        step(2)
        expect(#messages):ToBe(2)
        expect(messages[1]):ToBe("just you")
        expect(messages[2]):ToBe("everyone")
        remote:Destroy()
    end)

    it("refuses at the call what cannot travel", function()
        local remote = Instance.new("RemoteEvent")
        remote.Parent = workspace
        local anyRemote = remote :: any
        expect(function()
            anyRemote:FireServer(function() end)
        end):ToThrow()
        local selfish: { [string]: any } = {}
        selfish.me = selfish
        expect(function()
            anyRemote:FireServer(selfish)
        end):ToThrow()
        expect(function()
            anyRemote:FireServer({ [{}] = 1 })
        end):ToThrow()
        expect(function()
            anyRemote:FireServer(string.rep("x", 70000))
        end):ToThrow()
        expect(function()
            anyRemote:FireClient(remote, "not a player")
        end):ToThrow()
        remote:Destroy()
    end)
end)
