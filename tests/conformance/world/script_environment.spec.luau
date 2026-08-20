--!strict
--- Conformance: the script environment (docs/api-design.md 2.2, 2.5, 3).
---
--- Section 3 makes three promises about the shape of running code: the mount
--- is a tree of `Script`s and `Folder`s under `ScriptService`, the `script`
--- global is the running one of those, and modules are files reached by
--- string -- there is no `ModuleScript` class and no `require(instance)`
--- (divergence 6). Dynamic script creation is out of scope for v1, which is
--- the same statement read from the other side.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local beforeEach = testing.beforeEach

describe("the script global", function()
    it("is a Script instance", function()
        expect(script).Never:ToBeNil()
        expect(script:IsA("Script")):ToBe(true)
    end)

    it("reports Script as its ClassName", function()
        expect(script.ClassName):ToBe("Script")
    end)

    it("has a non-empty Name", function()
        expect(script.Name):ToBeA("string")
        expect(#script.Name > 0):ToBe(true)
    end)

    it("is a descendant of the DataModel", function()
        expect(script:IsDescendantOf(game)):ToBe(true)
    end)

    it("is mounted under ScriptService", function()
        expect(script:IsDescendantOf(game:GetService("ScriptService"))):ToBe(true)
    end)

    it("is enabled while it runs", function()
        expect(script.Enabled):ToBeA("boolean")
        expect(script.Enabled):ToBe(true)
    end)

    it("is not the DataModel or a service", function()
        expect(script).Never:ToBe(game)
        expect(script).Never:ToBe(workspace)
        expect(script).Never:ToBe(game:GetService("ScriptService"))
    end)

    -- Section 3 points script authors at attributes for per-script config, so
    -- the running Script has to accept them like any other instance.
    it("carries attributes like any other instance", function()
        expect(script:GetAttribute("ConformanceProbe")):ToBeNil()
        script:SetAttribute("ConformanceProbe", 3)
        expect(script:GetAttribute("ConformanceProbe")):ToBe(3)
        script:SetAttribute("ConformanceProbe", nil)
        expect(script:GetAttribute("ConformanceProbe")):ToBeNil()
    end)

    it("carries tags like any other instance", function()
        expect(script:HasTag("ConformanceProbeTag")):ToBe(false)
        script:AddTag("ConformanceProbeTag")
        expect(script:HasTag("ConformanceProbeTag")):ToBe(true)
        script:RemoveTag("ConformanceProbeTag")
        expect(script:HasTag("ConformanceProbeTag")):ToBe(false)
    end)
end)

describe("ScriptService", function()
    local scriptService: ScriptService

    beforeEach(function()
        scriptService = game:GetService("ScriptService")
    end)

    it("is a singleton child of game", function()
        expect(game:GetService("ScriptService")):ToBe(scriptService)
        expect(scriptService.Parent):ToBe(game)
        expect(scriptService.ClassName):ToBe("ScriptService")
    end)

    it("holds at least the running script", function()
        expect(#scriptService:GetDescendants() > 0):ToBe(true)
    end)

    -- Section 3: every mounted file is a `Script` and every source
    -- subdirectory is a `Folder`. Nothing else has a reason to be in the
    -- mount, so anything else there is a mounting bug.
    it("contains only Script and Folder instances", function()
        for _, descendant in scriptService:GetDescendants() do
            local className = descendant.ClassName
            expect(className == "Script" or className == "Folder"):ToBe(true)
        end
    end)

    it("gives every mounted Script an Enabled boolean", function()
        for _, descendant in scriptService:GetDescendants() do
            if descendant:IsA("Script") then
                -- `IsA` answers a boolean rather than narrowing the type, so the cast
                -- is what a caller writes here too.
                expect((descendant :: Script).Enabled):ToBeA("boolean")
            end
        end
    end)

    it("names every mounted instance", function()
        for _, descendant in scriptService:GetDescendants() do
            expect(descendant.Name):ToBeA("string")
            expect(#descendant.Name > 0):ToBe(true)
        end
    end)
end)

describe("the module model", function()
    it("has no ModuleScript class", function()
        local instanceLibrary = Instance :: any
        expect(function()
            instanceLibrary.new("ModuleScript")
        end):ToThrow()
    end)

    it("does not support dynamic script creation", function()
        local instanceLibrary = Instance :: any
        expect(function()
            instanceLibrary.new("Script")
        end):ToThrow()
    end)

    it("refuses require of an Instance", function()
        local requireByValue = require :: any
        expect(function()
            requireByValue(script)
        end):ToThrow()
    end)

    it("refuses require of an unknown module path", function()
        local requireByValue = require :: any
        expect(function()
            requireByValue("@luaug/there-is-no-such-module")
        end):ToThrow()
    end)

    it("returns the same table for two requires of one module", function()
        expect(require("@luaug/testing")):ToBe(testing)
    end)
end)
