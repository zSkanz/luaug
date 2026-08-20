--!strict
--- Conformance: instance tags (docs/api-design.md 2.2, 2.6, 8).
---
--- 2.1 puts add/has/remove on `Instance` and queries on `TagService`, so this
--- file covers the instance half only: tagging is per-instance state that a
--- Clone carries, exactly like attributes. The obby template teaches
--- checkpoints as tags, which is why idempotence and Clone survival are
--- contract and not incidental.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local beforeEach, afterEach = testing.beforeEach, testing.afterEach

local function countOf(list: { string }, value: string): number
    local total = 0
    for _, entry in list do
        if entry == value then
            total += 1
        end
    end
    return total
end

describe("Instance tags", function()
    local scratch: Folder
    local subject: Folder

    beforeEach(function()
        scratch = Instance.new("Folder")
        scratch.Name = "ConformanceTags"
        scratch.Parent = workspace

        subject = Instance.new("Folder")
        subject.Name = "Subject"
        subject.Parent = scratch
    end)

    afterEach(function()
        scratch:Destroy()
    end)

    describe("AddTag, HasTag and RemoveTag", function()
        it("reports HasTag false for a tag that was never added", function()
            expect(subject:HasTag("Checkpoint")):ToBe(false)
        end)

        it("returns a boolean from HasTag", function()
            expect(subject:HasTag("Checkpoint")):ToBeA("boolean")
            subject:AddTag("Checkpoint")
            expect(subject:HasTag("Checkpoint")):ToBeA("boolean")
        end)

        it("reports HasTag true after AddTag", function()
            subject:AddTag("Checkpoint")
            expect(subject:HasTag("Checkpoint")):ToBe(true)
        end)

        it("reports HasTag false after RemoveTag", function()
            subject:AddTag("Checkpoint")
            subject:RemoveTag("Checkpoint")
            expect(subject:HasTag("Checkpoint")):ToBe(false)
        end)

        it("treats a second AddTag of the same tag as a no-op", function()
            subject:AddTag("Checkpoint")
            subject:AddTag("Checkpoint")
            subject:AddTag("Checkpoint")
            expect(subject:HasTag("Checkpoint")):ToBe(true)
            expect(#subject:GetTags()):ToBe(1)
            expect(countOf(subject:GetTags(), "Checkpoint")):ToBe(1)
        end)

        it("removes the tag with a single RemoveTag after repeated AddTag calls", function()
            subject:AddTag("Checkpoint")
            subject:AddTag("Checkpoint")
            subject:RemoveTag("Checkpoint")
            expect(subject:HasTag("Checkpoint")):ToBe(false)
        end)

        it("treats RemoveTag of a tag the instance does not carry as a no-op", function()
            subject:RemoveTag("Checkpoint")
            expect(subject:HasTag("Checkpoint")):ToBe(false)
            expect(#subject:GetTags()):ToBe(0)
        end)

        it("allows a tag to be added again after removal", function()
            subject:AddTag("Checkpoint")
            subject:RemoveTag("Checkpoint")
            subject:AddTag("Checkpoint")
            expect(subject:HasTag("Checkpoint")):ToBe(true)
        end)

        it("treats tag names as case-sensitive", function()
            subject:AddTag("Checkpoint")
            expect(subject:HasTag("checkpoint")):ToBe(false)
            expect(subject:HasTag("CHECKPOINT")):ToBe(false)
        end)

        it("keeps several tags on one instance", function()
            subject:AddTag("Checkpoint")
            subject:AddTag("Climbable")
            subject:AddTag("PreserveOnReload")

            expect(subject:HasTag("Checkpoint")):ToBe(true)
            expect(subject:HasTag("Climbable")):ToBe(true)
            expect(subject:HasTag("PreserveOnReload")):ToBe(true)
            expect(#subject:GetTags()):ToBe(3)
        end)

        it("removes only the tag it is asked to remove", function()
            subject:AddTag("Checkpoint")
            subject:AddTag("Climbable")
            subject:RemoveTag("Checkpoint")

            expect(subject:HasTag("Checkpoint")):ToBe(false)
            expect(subject:HasTag("Climbable")):ToBe(true)
        end)

        it("keeps tags of two instances independent", function()
            local other = Instance.new("Folder")
            other.Parent = scratch

            subject:AddTag("Checkpoint")
            expect(other:HasTag("Checkpoint")):ToBe(false)

            other:AddTag("Checkpoint")
            subject:RemoveTag("Checkpoint")
            expect(other:HasTag("Checkpoint")):ToBe(true)
        end)

        it("tags an instance whose Parent is nil", function()
            local orphan = Instance.new("Folder")
            orphan:AddTag("Checkpoint")
            expect(orphan:HasTag("Checkpoint")):ToBe(true)
            expect(orphan:GetTags()):ToContain("Checkpoint")
            orphan:Destroy()
        end)

        it("rejects a tag name that is not a string", function()
            local target = subject :: any
            expect(function()
                target:AddTag(1)
            end):ToThrow()
        end)
    end)

    describe("GetTags", function()
        it("returns an empty array for an untagged instance", function()
            expect(#subject:GetTags()):ToBe(0)
        end)

        it("returns every tag the instance carries", function()
            subject:AddTag("Checkpoint")
            subject:AddTag("Climbable")

            local tags = subject:GetTags()
            expect(tags):ToContain("Checkpoint")
            expect(tags):ToContain("Climbable")
            expect(#tags):ToBe(2)
        end)

        it("returns strings", function()
            subject:AddTag("Checkpoint")
            for _, tag in subject:GetTags() do
                expect(tag):ToBeA("string")
            end
        end)

        -- The caller owns the array it is handed, so mutating it cannot reach
        -- back into the instance.
        it("returns a fresh array on every call", function()
            subject:AddTag("Checkpoint")

            local first = subject:GetTags()
            table.insert(first, "Injected")

            local second = subject:GetTags()
            expect(#second):ToBe(1)
            expect(second):ToContain("Checkpoint")
            expect(subject:HasTag("Injected")):ToBe(false)
        end)
    end)

    describe("Clone", function()
        it("copies tags onto the clone", function()
            subject:AddTag("Checkpoint")
            subject:AddTag("Climbable")

            local clone = subject:Clone()
            expect(clone:HasTag("Checkpoint")):ToBe(true)
            expect(clone:HasTag("Climbable")):ToBe(true)
            expect(#clone:GetTags()):ToBe(2)
            clone:Destroy()
        end)

        it("keeps the clone's tags independent of the original", function()
            subject:AddTag("Checkpoint")

            local clone = subject:Clone()
            clone:RemoveTag("Checkpoint")
            expect(subject:HasTag("Checkpoint")):ToBe(true)

            clone:AddTag("Climbable")
            expect(subject:HasTag("Climbable")):ToBe(false)
            clone:Destroy()
        end)

        it("copies tags of descendants onto the clone", function()
            local child = Instance.new("Folder")
            child.Name = "Child"
            child:AddTag("Climbable")
            child.Parent = subject

            local clone = subject:Clone()
            local clonedChild = assert(clone:FindFirstChild("Child"), "the clone lost its child")
            expect(clonedChild:HasTag("Climbable")):ToBe(true)
            clone:Destroy()
        end)

        it("carries both tags and attributes through one Clone", function()
            subject:AddTag("Checkpoint")
            subject:SetAttribute("Order", 3)

            local clone = subject:Clone()
            expect(clone:HasTag("Checkpoint")):ToBe(true)
            expect(clone:GetAttribute("Order")):ToBe(3)
            clone:Destroy()
        end)
    end)
end)
