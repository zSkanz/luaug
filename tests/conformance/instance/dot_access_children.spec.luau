--!strict
--- Dot access to a child (ADR 0078, reversing ADR 0061 on the owner's word).
---
--- `workspace.Baseplate` reaches the first child called Baseplate. A member
--- always wins over a child of the same name, a name that is neither is still
--- the plain unknown-member error, and assigning to a child's name is refused
--- with a message that says it is a child.
---
--- The views through `any` stand for code the analyzer did not type: a child a
--- script made at run time has no declared name, and the tree a scene declares
--- is typed separately, from the scene itself.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local beforeEach, afterEach = testing.beforeEach, testing.afterEach

describe("Dot access to a child", function()
    local parent: Folder = nil :: any
    local child: Part = nil :: any

    beforeEach(function()
        parent = Instance.new("Folder")
        parent.Name = "DotAccessSpecFolder"
        parent.Parent = workspace

        child = Instance.new("Part")
        child.Name = "Nested"
        child.Parent = parent
    end)

    afterEach(function()
        parent:Destroy()
    end)

    it("reaches the child by its name", function()
        expect((parent :: any).Nested):ToBe(child)
    end)

    it("reaches through more than one level", function()
        local inner = Instance.new("Part")
        inner.Name = "Bolt"
        inner.Parent = child
        expect((parent :: any).Nested.Bolt):ToBe(inner)
        expect((workspace :: any).DotAccessSpecFolder.Nested.Bolt):ToBe(inner)
    end)

    it("prefers a real property over a child with the same name", function()
        -- A child called `Name` must not shadow the property.
        local confusing = Instance.new("Part")
        confusing.Name = "Name"
        confusing.Parent = parent
        expect(parent.Name):ToBe("DotAccessSpecFolder")
        confusing:Destroy()
    end)

    it("still says plainly that a name which is nothing at all is nothing at all", function()
        expect(function()
            local _ = (parent :: any).NoSuchThing
        end):ToThrow("scene.err.unknown_member")
    end)

    it("is asked at the moment of the read: a child that is gone is gone", function()
        child:Destroy()
        expect(function()
            local _ = (parent :: any).Nested
        end):ToThrow("scene.err.unknown_member")
    end)

    it("refuses an assignment to a child's name, and says it is a child", function()
        expect(function()
            (parent :: any).Nested = Instance.new("Part")
        end):ToThrow("scene.err.child_not_member")
    end)
end)
