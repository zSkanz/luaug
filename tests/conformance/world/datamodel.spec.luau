--!strict
--- Conformance: the DataModel global (docs/api-design.md 1.4, 2.2, 3).
---
--- `game` is the root of the world and the only handle from which services
--- are reachable, so its own shape is contract: a `DataModel` with no parent,
--- two read-only version strings (1.4), a `Loaded` signal with a defined
--- moment (3), and `BindToClose` for shutdown work.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect

--- 3: `game.Loaded` fires after all entry scripts have had their first
--- resumption. 3.1's capture rule means only a connection made before that
--- fire can observe it -- so this one is made while the file is loading,
--- which is inside this script's first resumption, not inside a case.
local loadedFires = 0
game.Loaded:Connect(function()
    loadedFires += 1
end)

describe("game", function()
    it("reports DataModel as its ClassName", function()
        expect(game.ClassName):ToBe("DataModel")
    end)

    it("is an Instance", function()
        expect(game:IsA("Instance")):ToBe(true)
        expect(game:IsA("DataModel")):ToBe(true)
    end)

    it("has no parent", function()
        expect(game.Parent):ToBeNil()
    end)

    it("is the ancestor of every service", function()
        expect(workspace:IsDescendantOf(game)):ToBe(true)
        expect(game:GetService("TagService"):IsDescendantOf(game)):ToBe(true)
        expect(game:IsAncestorOf(workspace)):ToBe(true)
    end)

    it("has a name", function()
        expect(game.Name):ToBeA("string")
    end)
end)

describe("engine version strings", function()
    it("exposes EngineVersion as a non-empty string", function()
        expect(game.EngineVersion):ToBeA("string")
        expect(#game.EngineVersion > 0):ToBe(true)
    end)

    it("exposes LuauVersion as a non-empty string", function()
        expect(game.LuauVersion):ToBeA("string")
        expect(#game.LuauVersion > 0):ToBe(true)
    end)

    it("starts both version strings with a version number", function()
        expect(string.match(game.EngineVersion, "^%d+%.%d+")).Never:ToBeNil()
        expect(string.match(game.LuauVersion, "^%d+%.%d+")).Never:ToBeNil()
    end)

    it("returns the same EngineVersion on every read", function()
        expect(game.EngineVersion):ToBe(game.EngineVersion)
        expect(game.LuauVersion):ToBe(game.LuauVersion)
    end)

    it("rejects a write to EngineVersion", function()
        local dataModel = game :: any
        expect(function()
            dataModel.EngineVersion = "0.0.0"
        end):ToThrow()
    end)

    it("rejects a write to LuauVersion", function()
        local dataModel = game :: any
        expect(function()
            dataModel.LuauVersion = "0.0.0"
        end):ToThrow()
    end)

    it("leaves the version strings unchanged after a rejected write", function()
        local before = game.EngineVersion
        local dataModel = game :: any
        pcall(function()
            dataModel.EngineVersion = "0.0.0"
        end)
        expect(game.EngineVersion):ToBe(before)
    end)
end)

describe("game.Loaded", function()
    it("has fired by the time conformance cases run", function()
        task.wait()
        expect(loadedFires >= 1):ToBe(true)
    end)

    it("fires exactly once", function()
        task.wait()
        task.wait()
        expect(loadedFires):ToBe(1)
    end)

    it("is a connectable signal", function()
        local connection = game.Loaded:Connect(function() end)
        expect(connection.Connected):ToBe(true)
        connection:Disconnect()
        expect(connection.Connected):ToBe(false)
    end)

    -- 3.1: the fire captured the connection list as it stood, so connecting
    -- afterwards observes nothing however long the world runs.
    it("does not deliver the load to a connection made afterwards", function()
        local runs = 0
        local connection = game.Loaded:Connect(function()
            runs += 1
        end)

        task.wait()
        task.wait()
        connection:Disconnect()
        expect(runs):ToBe(0)
    end)
end)

describe("game:BindToClose", function()
    it("accepts a function", function()
        expect(function()
            game:BindToClose(function() end)
        end).Never:ToThrow()
    end)

    it("returns no value", function()
        local dataModel = game :: any
        local result = dataModel:BindToClose(function() end)
        expect(result):ToBeNil()
    end)

    it("accepts more than one handler", function()
        expect(function()
            game:BindToClose(function() end)
            game:BindToClose(function() end)
        end).Never:ToThrow()
    end)

    it("rejects a value that is not a function", function()
        local dataModel = game :: any
        expect(function()
            dataModel:BindToClose("not a function")
        end):ToThrow()
        expect(function()
            dataModel:BindToClose(nil)
        end):ToThrow()
    end)
end)
