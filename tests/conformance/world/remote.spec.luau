--!strict
--- Conformance: `RemoteEvent` and `RemoteFunction`, played solo (N2, ADRs 0077 and 0079).
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

describe("remote functions", function()
    it("answers InvokeServerAsync with what OnServerInvoke returned, as the local player", function()
        local remote = Instance.new("RemoteFunction")
        remote.Parent = workspace
        local asker: any = nil
        remote.OnServerInvoke = function(player: Player, item: string, count: number)
            asker = player
            return `{item} x{count}`, { total = count * 2 }
        end
        local label, detail = remote:InvokeServerAsync("sword", 3)
        expect(asker):ToBe(NetworkService.LocalPlayer)
        expect(label):ToBe("sword x3")
        expect(detail.total):ToBe(6)
        remote:Destroy()
    end)

    it("reads back the handler it was given, and forgets it on nil", function()
        local remote = Instance.new("RemoteFunction")
        local handler = function(_player: Player)
            return 1
        end
        remote.OnServerInvoke = handler
        expect(remote.OnServerInvoke):ToBe(handler)
        remote.OnServerInvoke = nil
        expect(remote.OnServerInvoke):ToBe(nil)
        expect(function()
            (remote :: any).OnServerInvoke = 42
        end):ToThrow("net.err.remote_handler")
        remote:Destroy()
    end)

    it("waits for a handler that yields", function()
        local remote = Instance.new("RemoteFunction")
        remote.Parent = workspace
        remote.OnServerInvoke = function(_player: Player, ticks: number)
            step(ticks)
            return "late"
        end
        expect(remote:InvokeServerAsync(3)):ToBe("late")
        remote:Destroy()
    end)

    it("raises at the caller when the handler fails, or there is none", function()
        local remote = Instance.new("RemoteFunction")
        remote.Parent = workspace
        remote.OnServerInvoke = function(_player: Player)
            error("out of swords")
        end
        local ok, message = pcall(function()
            return remote:InvokeServerAsync()
        end)
        expect(ok):ToBe(false)
        expect(string.find(tostring(message), "net.err.remote_invoke_failed", 1, true) ~= nil):ToBe(true)
        expect(string.find(tostring(message), "out of swords", 1, true) ~= nil):ToBe(true)

        -- A handler whose answer cannot travel fails the same way.
        remote.OnServerInvoke = function(_player: Player)
            return function() end
        end
        local sent = pcall(function()
            return remote:InvokeServerAsync()
        end)
        expect(sent):ToBe(false)

        remote.OnServerInvoke = nil
        local answered, missing = pcall(function()
            return remote:InvokeServerAsync()
        end)
        expect(answered):ToBe(false)
        expect(string.find(tostring(missing), "net.err.remote_no_handler", 1, true) ~= nil):ToBe(true)
        remote:Destroy()
    end)
end)
