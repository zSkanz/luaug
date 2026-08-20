--!strict
--- Instance construction and the identity members, from api-design.md §2.2
--- ("Instance base members") and divergence #7 (single-argument `Instance.new`).

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local beforeEach, afterEach = testing.beforeEach, testing.afterEach

-- `Instance.new` is typed with string-singleton overloads (§5), so a class name
-- that is not a class cannot be spelled through them at all. The rejection
-- cases below go through this widened view of the same function.
local newByName = (Instance.new :: any) :: (className: string) -> Instance

describe("Instance construction", function()
    local root: Folder = nil :: any

    beforeEach(function()
        root = Instance.new("Folder")
        root.Name = "ConstructionSpecRoot"
        root.Parent = workspace
    end)

    afterEach(function()
        root:Destroy()
    end)

    describe("Instance.new", function()
        it("creates an instance of the requested class", function()
            expect(Instance.new("Folder").ClassName):ToBe("Folder")
            expect(Instance.new("Part").ClassName):ToBe("Part")
            expect(Instance.new("Model").ClassName):ToBe("Model")
        end)

        it("names a new instance after its class", function()
            expect(Instance.new("Folder").Name):ToBe("Folder")
            expect(Instance.new("Part").Name):ToBe("Part")
        end)

        it("leaves a new instance unparented", function()
            expect(Instance.new("Folder").Parent):ToBeNil()
            expect(Instance.new("Part").Parent):ToBeNil()
        end)

        it("returns a distinct instance on every call", function()
            local first = Instance.new("Folder")
            local second = Instance.new("Folder")
            expect(first).Never:ToBe(second)
        end)

        it("does not attach a new instance to any tree", function()
            local before = #workspace:GetChildren()
            Instance.new("Part")
            expect(#workspace:GetChildren()):ToBe(before)
        end)

        it("rejects an unknown class name", function()
            expect(function()
                return newByName("NoSuchClassExistsHere")
            end):ToThrow()
        end)

        it("rejects an empty class name", function()
            expect(function()
                return newByName("")
            end):ToThrow()
        end)

        it("rejects the abstract base classes", function()
            expect(function()
                return newByName("Instance")
            end):ToThrow()
            expect(function()
                return newByName("BasePart")
            end):ToThrow()
        end)

        it("rejects a service class name", function()
            -- Services are created by `game:GetService` (§1.2), never by hand.
            expect(function()
                return newByName("Workspace")
            end):ToThrow()
        end)

        it("rejects dynamic Script creation", function()
            -- §3: scripts are mounted from `src/scripts/**`; dynamic script
            -- creation is not supported in v1.
            expect(function()
                return newByName("Script")
            end):ToThrow()
        end)

        it("does not accept a parent as a second argument", function()
            -- Divergence #7: the two-argument form is gone, so whatever a second
            -- argument does, it must not parent the instance.
            local ok, created = pcall(function(): any
                return (Instance.new :: any)("Folder", root)
            end)
            if ok then
                expect((created :: Instance).Parent):ToBeNil()
            end
            expect(#root:GetChildren()):ToBe(0)
        end)
    end)

    describe("identity members", function()
        it("reports Instance as its typeof", function()
            expect(Instance.new("Folder")):ToBeA("Instance")
        end)

        it("exposes ClassName as read-only", function()
            local folder = Instance.new("Folder")
            expect(function()
                (folder :: any).ClassName = "Part"
            end):ToThrow()
            expect(folder.ClassName):ToBe("Folder")
        end)

        it("allows Name to be reassigned", function()
            local folder = Instance.new("Folder")
            folder.Name = "Renamed"
            expect(folder.Name):ToBe("Renamed")
            folder.Name = "RenamedAgain"
            expect(folder.Name):ToBe("RenamedAgain")
        end)

        it("allows Parent to be reassigned", function()
            local folder = Instance.new("Folder")
            folder.Parent = root
            expect(folder.Parent):ToBe(root)
            folder.Parent = nil
            expect(folder.Parent):ToBeNil()
        end)

        it("keeps ClassName stable across a rename and a reparent", function()
            local part = Instance.new("Part")
            part.Name = "Crate"
            part.Parent = root
            expect(part.ClassName):ToBe("Part")
            part.Parent = nil
            expect(part.ClassName):ToBe("Part")
        end)

        it("allows two instances to share a Name", function()
            local first = Instance.new("Folder")
            local second = Instance.new("Folder")
            first.Name = "Tree"
            second.Name = "Tree"
            first.Parent = root
            second.Parent = root
            expect(first.Name):ToBe(second.Name)
            expect(first).Never:ToBe(second)
        end)
    end)
end)
