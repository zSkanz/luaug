--!strict
--- Conformance: service acquisition (docs/api-design.md 1.2, 2.1).
---
--- Services are the only way to reach the engine's singletons, so the three
--- promises in 1.2 -- lazy creation, one instance forever, parented to `game`
--- -- are load-bearing for every other area of the API. The reserved-name
--- cases exist because 2.1 says "do not squat these meanings": a service that
--- quietly answers to "Players" is a v1 scope breach, not a convenience.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect

describe("game:GetService", function()
    it("returns the same instance for repeated calls with the same name", function()
        local first = game:GetService("TagService")
        local second = game:GetService("TagService")
        expect(second):ToBe(first)
    end)

    -- Lazy singleton creation, observed without depending on whether some
    -- earlier spec already acquired the service: a second acquisition must add
    -- no child to `game` either way.
    it("creates at most one instance however many times it is asked for", function()
        game:GetService("TagService")
        local childCount = #game:GetChildren()
        game:GetService("TagService")
        game:GetService("TagService")
        expect(#game:GetChildren()):ToBe(childCount)
    end)

    it("parents the service to game", function()
        expect(game:GetService("TagService").Parent):ToBe(game)
        expect(game:GetService("RunService").Parent):ToBe(game)
        expect(game:GetService("ScriptService").Parent):ToBe(game)
        expect(game:GetService("DebugService").Parent):ToBe(game)
    end)

    it("makes the service findable as a child of game under its own name", function()
        local service = game:GetService("TagService")
        expect(game:FindFirstChild("TagService")):ToBe(service)
    end)

    it("returns an instance whose ClassName is the requested service name", function()
        expect(game:GetService("Workspace").ClassName):ToBe("Workspace")
        expect(game:GetService("RunService").ClassName):ToBe("RunService")
        expect(game:GetService("ScriptService").ClassName):ToBe("ScriptService")
        expect(game:GetService("TagService").ClassName):ToBe("TagService")
        expect(game:GetService("DebugService").ClassName):ToBe("DebugService")
    end)

    it("returns an instance that IsA Instance", function()
        expect(game:GetService("TagService"):IsA("Instance")):ToBe(true)
    end)

    it("names the service instance after its class", function()
        expect(game:GetService("TagService").Name):ToBe("TagService")
        expect(game:GetService("RunService").Name):ToBe("RunService")
    end)

    it("errors on an unknown service name", function()
        local dataModel = game :: any
        expect(function()
            dataModel:GetService("NoSuchService")
        end):ToThrow()
    end)

    it("errors on a class name that is not a service", function()
        local dataModel = game :: any
        expect(function()
            dataModel:GetService("Folder")
        end):ToThrow()
    end)

    it("errors on an empty service name", function()
        local dataModel = game :: any
        expect(function()
            dataModel:GetService("")
        end):ToThrow()
    end)

    -- Divergence 21: there is one spelling of every name and no camelCase
    -- alias, so the lowercase spelling of a real service is simply unknown.
    it("errors on a lowercase spelling of a real service name", function()
        local dataModel = game :: any
        expect(function()
            dataModel:GetService("workspace")
        end):ToThrow()
        expect(function()
            dataModel:GetService("runService")
        end):ToThrow()
    end)

    it("errors on every name 2.1 reserves for a later version", function()
        local dataModel = game :: any
        expect(function()
            dataModel:GetService("Players")
        end):ToThrow()
        expect(function()
            dataModel:GetService("NetworkService")
        end):ToThrow()
        expect(function()
            dataModel:GetService("ReplicationService")
        end):ToThrow()
        expect(function()
            dataModel:GetService("NavigationService")
        end):ToThrow()
    end)
end)

describe("game:FindService", function()
    it("returns the same instance as GetService once the service exists", function()
        local service = game:GetService("TagService")
        expect(game:FindService("TagService")):ToBe(service)
    end)

    -- The whole point of FindService is that asking is not acquiring: whatever
    -- the answer, the DataModel must have the same children afterwards.
    it("does not create the service it is asked about", function()
        local childCount = #game:GetChildren()
        game:FindService("DebugService")
        game:FindService("TagService")
        game:FindService("RunService")
        expect(#game:GetChildren()):ToBe(childCount)
    end)

    it("returns nil for a name 2.1 reserves for a later version", function()
        expect(game:FindService("Players")):ToBeNil()
        expect(game:FindService("NetworkService")):ToBeNil()
        expect(game:FindService("ReplicationService")):ToBeNil()
        expect(game:FindService("NavigationService")):ToBeNil()
    end)

    it("returns nil for an unknown name instead of erroring", function()
        expect(game:FindService("NoSuchService")):ToBeNil()
    end)

    it("returns nil for a lowercase spelling of a real service name", function()
        expect(game:FindService("workspace")):ToBeNil()
    end)
end)

describe("service globals", function()
    it("binds the workspace global to the Workspace service", function()
        expect(workspace):ToBe(game:GetService("Workspace"))
    end)

    it("reports Workspace as the ClassName of the workspace global", function()
        expect(workspace.ClassName):ToBe("Workspace")
    end)

    it("parents the workspace global to game", function()
        expect(workspace.Parent):ToBe(game)
    end)

    -- 1.2: "no other service gets a global". The runtime-observable half of
    -- that rule is that every other service is reached through `game` and is
    -- an ordinary child of it; the absence of the identifier itself is a
    -- property of the generated definitions.
    it("reaches TagService only as a child of game", function()
        local service = game:GetService("TagService")
        expect(service):ToBe(game:FindFirstChild("TagService"))
        expect(service.Parent):ToBe(game)
    end)

    it("reaches DebugService only as a child of game", function()
        local service = game:GetService("DebugService")
        expect(service):ToBe(game:FindFirstChild("DebugService"))
        expect(service.Parent):ToBe(game)
    end)
end)

describe("service construction", function()
    it("refuses Instance.new for a service class", function()
        local instanceLibrary = Instance :: any
        expect(function()
            instanceLibrary.new("Workspace")
        end):ToThrow()
        expect(function()
            instanceLibrary.new("TagService")
        end):ToThrow()
        expect(function()
            instanceLibrary.new("RunService")
        end):ToThrow()
        expect(function()
            instanceLibrary.new("ScriptService")
        end):ToThrow()
    end)

    it("refuses Instance.new for a name 2.1 reserves for a later version", function()
        local instanceLibrary = Instance :: any
        expect(function()
            instanceLibrary.new("Players")
        end):ToThrow()
        expect(function()
            instanceLibrary.new("NetworkService")
        end):ToThrow()
    end)

    it("keeps no reserved name in the DataModel's children", function()
        expect(game:FindFirstChild("Players")):ToBeNil()
        expect(game:FindFirstChild("NetworkService")):ToBeNil()
        expect(game:FindFirstChild("ReplicationService")):ToBeNil()
        expect(game:FindFirstChild("NavigationService")):ToBeNil()
    end)
end)
