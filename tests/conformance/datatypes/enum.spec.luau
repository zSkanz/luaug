--!strict
--- Pins the `Enum` row of `api-design.md` §2.3: the global `Enum` namespace,
--- `EnumItem` = `Name`, `Value`, `EnumType`, and `Enum.X:GetEnumItems()`. Only
--- the enums this milestone declares are asserted here -- `PartShape`,
--- `RotationOrder`, `LogLevel` and the reserved `RunContext`.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect

local function tryIndex(value: any, key: string): any
    local ok, result = pcall(function(): any
        return value[key]
    end)
    return if ok then result else nil
end

local function itemsOf(enumObject: any): { any }
    return enumObject:GetEnumItems()
end

local function namesOf(enumObject: any): { string }
    local names: { string } = {}
    for _, item in itemsOf(enumObject) do
        table.insert(names, item.Name)
    end
    return names
end

--- Every enum owes the same three-field shape and the same uniqueness; asserting
--- it per enum is what makes a newly declared enum a one-line addition here.
local function expectWellFormedEnum(enumObject: any): ()
    local items = itemsOf(enumObject)
    expect(items):ToBeA("table")
    expect(#items).Never:ToBe(0)

    local seenNames: { [string]: boolean } = {}
    local seenValues: { [number]: boolean } = {}
    for _, item in items do
        local name: string = item.Name
        local value: number = item.Value
        expect(name):ToBeA("string")
        expect(value):ToBeA("number")
        expect(item.EnumType):ToBe(enumObject)
        expect(seenNames[name]).Never:ToBe(true)
        expect(seenValues[value]).Never:ToBe(true)
        seenNames[name] = true
        seenValues[value] = true
        expect(enumObject[name]):ToBe(item)
    end
end

describe("Enum", function()
    describe("the namespace", function()
        it("is reachable as a global", function()
            expect(Enum).Never:ToBeNil()
        end)

        it("carries the enums this milestone declares", function()
            expect(Enum.PartShape).Never:ToBeNil()
            expect(Enum.RotationOrder).Never:ToBeNil()
            expect(Enum.LogLevel).Never:ToBeNil()
            expect(Enum.RunContext).Never:ToBeNil()
        end)

        it("does not invent an enum that was never declared", function()
            expect(tryIndex(Enum, "NotADeclaredEnum")):ToBeNil()
        end)
    end)

    describe("EnumItem", function()
        it("reports typeof EnumItem", function()
            expect(typeof(Enum.PartShape.Ball)):ToBe("EnumItem")
        end)

        it("carries its Name as a string", function()
            expect(Enum.PartShape.Ball.Name):ToBe("Ball")
        end)

        it("carries its Value as a number", function()
            expect(Enum.PartShape.Ball.Value):ToBeA("number")
        end)

        it("points EnumType back at the enum that declares it", function()
            expect(Enum.PartShape.Ball.EnumType):ToBe(Enum.PartShape)
        end)

        it("compares equal to itself", function()
            expect(Enum.PartShape.Ball):ToBe(Enum.PartShape.Ball)
        end)

        it("is a singleton, so repeated lookups give the same item", function()
            local first = Enum.PartShape.Ball
            local second = Enum.PartShape.Ball
            expect(first):ToBe(second)
        end)

        it("does not compare equal to a sibling item", function()
            expect(Enum.PartShape.Ball).Never:ToBe(Enum.PartShape.Block)
        end)

        it("does not compare equal to its own Name or Value", function()
            expect(Enum.PartShape.Ball).Never:ToBe("Ball")
            expect(Enum.PartShape.Ball).Never:ToBe(Enum.PartShape.Ball.Value)
        end)

        it("does not resolve an unknown member of a declared enum", function()
            expect(tryIndex(Enum.PartShape, "Dodecahedron")):ToBeNil()
        end)
    end)

    describe("GetEnumItems", function()
        it("returns an array of the enum's items", function()
            local items = itemsOf(Enum.PartShape)
            expect(items):ToBeA("table")
            expect(items):ToContain(Enum.PartShape.Ball)
        end)

        it("returns the same items on every call", function()
            expect(namesOf(Enum.PartShape)):ToEqual(namesOf(Enum.PartShape))
        end)

        it("returns items whose EnumType is the enum it was called on", function()
            for _, item in itemsOf(Enum.RotationOrder) do
                expect(item.EnumType):ToBe(Enum.RotationOrder)
            end
        end)
    end)

    describe("Enum.PartShape", function()
        it("declares exactly the five shapes of the Part class", function()
            -- §2.2: `Part -- Shape: Enum.PartShape (Block/Ball/Cylinder/Capsule/Wedge)`.
            local names = namesOf(Enum.PartShape)
            expect(#names):ToBe(5)
            expect(names):ToContain("Block")
            expect(names):ToContain("Ball")
            expect(names):ToContain("Cylinder")
            expect(names):ToContain("Capsule")
            expect(names):ToContain("Wedge")
        end)

        it("is well formed", function()
            expectWellFormedEnum(Enum.PartShape)
        end)
    end)

    describe("Enum.RotationOrder", function()
        it("declares YXZ, the documented default of CFrame.fromEuler", function()
            expect(namesOf(Enum.RotationOrder)):ToContain("YXZ")
        end)

        it("is well formed", function()
            expectWellFormedEnum(Enum.RotationOrder)
        end)

        it("is accepted by CFrame.fromEuler as its order argument", function()
            local cf = CFrame.fromEuler(0.1, 0.2, 0.3, Enum.RotationOrder.YXZ)
            expect(typeof(cf)):ToBe("CFrame")
        end)
    end)

    describe("Enum.LogLevel", function()
        it("is well formed", function()
            expectWellFormedEnum(Enum.LogLevel)
        end)
    end)

    describe("Enum.RunContext", function()
        it("is declared even though v1 leaves it unset", function()
            -- §3 mounts scripts with "`RunContext` (reserved enum; unset in v1)",
            -- so the enum exists before anything reads it.
            expectWellFormedEnum(Enum.RunContext)
        end)

        it("reserves Client and Server as its values", function()
            local names = namesOf(Enum.RunContext)
            expect(names):ToContain("Client")
            expect(names):ToContain("Server")
        end)
    end)
end)
