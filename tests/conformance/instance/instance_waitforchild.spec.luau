--!strict
--- `WaitForChild(name, timeout?)` from api-design.md §2.2.
---
--- A yield is the observable part of this contract, so every case that expects
--- one proves it by watching deferred work (§3.1) run while the call is parked:
--- a spinning implementation would never let the creator callback execute.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local beforeEach, afterEach = testing.beforeEach, testing.afterEach

local function namedFolder(name: string, parent: Instance?): Folder
    local folder = Instance.new("Folder")
    folder.Name = name
    folder.Parent = parent
    return folder
end

describe("Instance WaitForChild", function()
    local root: Folder = nil :: any
    local holder: Folder = nil :: any

    beforeEach(function()
        root = Instance.new("Folder")
        root.Name = "WaitForChildSpecRoot"
        root.Parent = workspace
        holder = namedFolder("Holder", root)
    end)

    afterEach(function()
        root:Destroy()
    end)

    describe("with a child already present", function()
        it("returns the existing child", function()
            local child = namedFolder("Existing", holder)
            expect(holder:WaitForChild("Existing")):ToBe(child)
        end)

        it("returns without yielding", function()
            namedFolder("Existing", holder)
            local deferredRan = false
            task.defer(function()
                deferredRan = true
            end)

            holder:WaitForChild("Existing")

            expect(deferredRan):ToBeFalsy()
        end)

        it("returns the first of duplicate siblings sharing the name", function()
            local first = namedFolder("Tree", holder)
            namedFolder("Tree", holder)
            expect(holder:WaitForChild("Tree")):ToBe(first)
        end)
    end)

    describe("with a child that arrives later", function()
        it("parks until the child is added and returns it", function()
            local order: { string } = {}
            local created: Folder? = nil

            task.defer(function()
                table.insert(order, "creator")
                created = namedFolder("Late", holder)
            end)

            table.insert(order, "before")
            local found = holder:WaitForChild("Late")
            table.insert(order, "after")

            expect(found):ToBe(created)
            expect(order):ToEqual({ "before", "creator", "after" })
        end)

        it("ignores children added under other names", function()
            local order: { string } = {}

            task.defer(function()
                namedFolder("Other", holder)
                table.insert(order, "other")
            end)
            task.defer(function()
                namedFolder("Wanted", holder)
                table.insert(order, "wanted")
            end)

            -- `WaitForChild` returns `Instance?` because the timeout form can
            -- expire; the unbounded form cannot, and `assert` is how a caller
            -- says so to the checker.
            local found = assert(holder:WaitForChild("Wanted"))
            table.insert(order, "resumed")

            expect(found.Name):ToBe("Wanted")
            expect(order):ToEqual({ "other", "wanted", "resumed" })
        end)

        it("parks a separate coroutine rather than blocking it", function()
            local resolved: Instance? = nil
            task.spawn(function()
                resolved = holder:WaitForChild("Late")
            end)

            -- task.spawn resumes synchronously (§3.2), so the waiter has already
            -- reached the parked WaitForChild by the time this line runs.
            expect(resolved):ToBeNil()

            local late = namedFolder("Late", holder)
            task.wait()

            expect(resolved):ToBe(late)
        end)

        it("ignores a matching name added under a different parent", function()
            local other = namedFolder("Other", root)
            task.defer(function()
                namedFolder("Late", other)
            end)
            task.defer(function()
                namedFolder("Late", holder)
            end)

            local found = assert(holder:WaitForChild("Late"))

            expect(found.Parent):ToBe(holder)
        end)

        it("ignores a matching name added deeper in the subtree", function()
            local deep = namedFolder("Deep", holder)
            task.defer(function()
                namedFolder("Late", deep)
            end)

            expect(holder:WaitForChild("Late", 0.1)):ToBeNil()
        end)
    end)

    describe("with a timeout", function()
        it("returns nil when the child never appears", function()
            expect(holder:WaitForChild("Never", 0.1)):ToBeNil()
        end)

        it("returns the child when it appears before the timeout", function()
            local created: Folder? = nil
            task.defer(function()
                created = namedFolder("Late", holder)
            end)

            expect(holder:WaitForChild("Late", 5)):ToBe(created)
        end)

        it("returns an existing child immediately, timeout or not", function()
            local child = namedFolder("Existing", holder)
            expect(holder:WaitForChild("Existing", 0.1)):ToBe(child)
        end)

        it("is over once it has timed out", function()
            expect(holder:WaitForChild("Late", 0.1)):ToBeNil()

            local late = namedFolder("Late", holder)
            task.wait()

            expect(holder:FindFirstChild("Late")):ToBe(late)
        end)

        it("resumes for a sibling renamed into the awaited name", function()
            -- §2.2 is explicit that the contract is about the STATE and not
            -- about the event that produced it: "a sibling renamed into the
            -- awaited name satisfies a waiter exactly as a newly parented child
            -- does". This case originally asserted the opposite, reasoning from
            -- a mechanism (the ChildAdded integration) rather than from the
            -- document -- which is the one thing a conformance spec must not do.
            local existing = namedFolder("Other", holder)
            task.defer(function()
                existing.Name = "Late"
            end)

            expect(holder:WaitForChild("Late", 1)):ToBe(existing)
        end)
    end)
end)
