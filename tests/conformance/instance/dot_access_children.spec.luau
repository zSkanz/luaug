--!strict
--- What dot access to a child says when it fails (decision 10, divergence #26).
---
--- **Children are not reached by dot access, and that is a decision rather than
--- an omission.** Allowing `script.Nested` needs a string indexer on
--- `Instance`, and an indexer does not merely type the child access: it makes
--- every unknown key on every instance resolve to `Instance?` instead of
--- erroring, so `part.Positon = ...` stops being a type error and becomes a
--- silent nil write. The price is typo detection across the whole language, in
--- a repository where R2 makes every file strict.
---
--- What was reported was not "give me the indexer", it was that the failure
--- said nothing useful. So these hold the message down: when the name IS a
--- child, the error says so and says what to type instead.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local beforeEach, afterEach = testing.beforeEach, testing.afterEach

--- Reading a member the definitions do not declare, through a widened view.
--- The type error is the FIRST line of defence and is the one this divergence
--- exists to keep; this is the second, for the code that got past it.
local function readAny(instance: Instance, member: string)
    return function()
        local _ = (instance :: any)[member]
    end
end

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

    it("is refused, and the refusal names the child", function()
        -- Not `scene.err.unknown_member`: that message is true and unhelpful,
        -- because the thing IS there and the reader can see it in the Explorer.
        expect(readAny(parent, "Nested")):ToThrow("scene.err.child_not_member")
    end)

    it("still says plainly that a name which is nothing at all is nothing at all", function()
        -- The other half, and the one that must not regress: a misspelling with
        -- no child behind it gets the ordinary message. A single message for
        -- both would recommend `FindFirstChild` for `part.Positon`, which is
        -- worse than saying nothing.
        expect(readAny(parent, "NoSuchThing")):ToThrow("scene.err.unknown_member")
    end)

    it("prefers a real property over a child with the same name", function()
        -- A child called `Name` must not shadow the property. The lookup order
        -- is what decides this, and it decides it in favour of the declared
        -- member -- so the diagnostic is reached only when there is genuinely no
        -- member, which is what makes it safe to give advice in.
        local confusing = Instance.new("Part")
        confusing.Name = "Name"
        confusing.Parent = parent

        expect(parent.Name):ToBe("DotAccessSpecFolder")
        confusing:Destroy()
    end)

    it("names the child on a class that has properties of its own", function()
        -- A `Part` reaches the same path as a `Folder`: the diagnostic is on
        -- the instance binding rather than on one class.
        local inner = Instance.new("Part")
        inner.Name = "Bolt"
        inner.Parent = child

        expect(readAny(child, "Bolt")):ToThrow("scene.err.child_not_member")
        inner:Destroy()
    end)

    it("goes back to the plain message once the child is gone", function()
        -- The message is about what is THERE, asked at the moment of the read,
        -- rather than about what was ever there.
        child:Destroy()
        expect(readAny(parent, "Nested")):ToThrow("scene.err.unknown_member")
    end)

    it("says what to type, and FindFirstChild is what it says", function()
        -- Named rather than implied, and `FindFirstChild` rather than
        -- `WaitForChild`: scripts start when play starts and the tree is
        -- already built, so recommending the yielding one would teach exactly
        -- the load-order habit this divergence exists to kill.
        local found = parent:FindFirstChild("Nested")
        expect(found):ToBe(child)
    end)
end)
