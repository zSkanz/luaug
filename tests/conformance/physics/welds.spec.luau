--!strict
--- Conformance: `Weld` and `WeldConstraint` (api-design.md §2.2, added to M5 by
--- human decision on 2026-08-20).
---
--- What the roadmap promises and this pins: a welded part is DRIVEN rather than
--- simulated, so gravity does not reach it; `Weld` is told its offset through
--- `C0`/`C1` while `WeldConstraint` captures the relationship when it becomes
--- active; a chain resolves in one tick rather than lagging a link per tick; a
--- cycle is refused at the write that would create it; and disabling a joint
--- releases the part where it stands rather than throwing it.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local afterEach = testing.afterEach

local spawned: { Instance } = {}

local function part(name: string, position: vector): Part
    local created = Instance.new("Part")
    created.Name = name
    created.Size = vector.create(2, 2, 2)
    created.Position = position
    created.Parent = workspace
    table.insert(spawned, created)
    return created
end

local function anchoredPart(name: string, position: vector): Part
    local created = part(name, position)
    created.Anchored = true
    return created
end

local function weld(part0: BasePart, part1: BasePart): Weld
    local joint = Instance.new("Weld")
    joint.Part0 = part0
    joint.Part1 = part1
    joint.Parent = workspace
    table.insert(spawned, joint)
    return joint
end

describe("Weld", function()
    afterEach(function()
        for _, instance in spawned do
            instance:Destroy()
        end
        table.clear(spawned)
    end)

    it("holds a part at the offset C0 and C1 describe", function()
        local anchor = anchoredPart("Anchor", vector.create(0, 10, 0))
        local held = part("Held", vector.create(50, 50, 50))

        local joint = weld(anchor, held)
        joint.C0 = CFrame.new(0, 0, 3)

        task.wait(0.5)
        -- Part0.CFrame * C0 == Part1.CFrame * C1, and C1 is identity, so the
        -- held part sits three metres along the anchor's Z.
        expect(held.Position.x):ToBeCloseTo(0, 0.01)
        expect(held.Position.y):ToBeCloseTo(10, 0.01)
        expect(held.Position.z):ToBeCloseTo(3, 0.01)
    end)

    it("keeps a welded part out of the simulation, so gravity does not reach it", function()
        local anchor = anchoredPart("Anchor", vector.create(0, 20, 0))
        local held = part("Held", vector.create(0, 20, 4))
        weld(anchor, held).C0 = CFrame.new(0, 0, 4)

        task.wait(2)
        expect(held.Position.y):ToBeCloseTo(20, 0.05)
    end)

    it("follows an anchor the simulation is moving", function()
        local floor = anchoredPart("Floor", vector.create(0, -1, 0))
        floor.Size = vector.create(60, 2, 60)

        local faller = part("Faller", vector.create(0, 20, 0))
        local held = part("Held", vector.create(0, 20, 3))
        weld(faller, held).C0 = CFrame.new(0, 0, 3)

        task.wait(0.5)
        expect(faller.Position.y < 19):ToBe(true)
        -- Three metres along Z of the faller, wherever the faller got to.
        expect(held.Position.y):ToBeCloseTo(faller.Position.y, 0.05)
        expect(held.Position.z):ToBeCloseTo(faller.Position.z + 3, 0.05)
    end)

    it("resolves a chain in one tick rather than lagging a link per tick", function()
        local anchor = anchoredPart("Anchor", vector.create(0, 10, 0))
        local middle = part("Middle", vector.create(99, 99, 99))
        local tip = part("Tip", vector.create(99, 99, 99))

        -- Deliberately welded tip-first, so the pool holds the chain backwards
        -- and the resolver has to sort it rather than take the order it is
        -- given.
        weld(middle, tip).C0 = CFrame.new(0, 0, 2)
        weld(anchor, middle).C0 = CFrame.new(0, 0, 2)

        task.wait(0.2)
        expect(middle.Position.z):ToBeCloseTo(2, 0.01)
        expect(tip.Position.z):ToBeCloseTo(4, 0.01)
    end)

    it("releases the part where it stands when it is disabled", function()
        local floor = anchoredPart("Floor", vector.create(0, -1, 0))
        floor.Size = vector.create(60, 2, 60)

        local anchor = anchoredPart("Anchor", vector.create(0, 20, 0))
        local held = part("Held", vector.create(0, 20, 3))
        local joint = weld(anchor, held)
        joint.C0 = CFrame.new(0, 0, 3)
        task.wait(0.5)
        expect(held.Position.y):ToBeCloseTo(20, 0.05)

        joint.Enabled = false
        task.wait(1.5)
        -- Released rather than thrown: it falls from where it was, with no
        -- velocity of its own.
        expect(held.Position.y < 15):ToBe(true)
    end)

    it("refuses a weld that would make a cycle", function()
        local a = anchoredPart("A", vector.create(0, 10, 0))
        local b = part("B", vector.create(0, 10, 3))
        weld(a, b)

        local reverse = Instance.new("Weld")
        reverse.Parent = workspace
        table.insert(spawned, reverse)
        reverse.Part0 = b
        expect(function()
            reverse.Part1 = a
        end):ToThrow()
    end)

    it("refuses a part welded to itself", function()
        local a = anchoredPart("A", vector.create(0, 10, 0))
        local joint = Instance.new("Weld")
        joint.Parent = workspace
        table.insert(spawned, joint)
        joint.Part0 = a
        expect(function()
            joint.Part1 = a
        end):ToThrow()
    end)

    it("is half-built without complaint while a script assigns its two parts", function()
        local joint = Instance.new("Weld")
        joint.Parent = workspace
        table.insert(spawned, joint)
        expect(joint.Part0):ToBeNil()
        expect(joint.Part1):ToBeNil()
        task.wait(0.2)
    end)
end)

describe("WeldConstraint", function()
    afterEach(function()
        for _, instance in spawned do
            instance:Destroy()
        end
        table.clear(spawned)
    end)

    it("captures where the two parts already are, rather than being told", function()
        local anchor = anchoredPart("Anchor", vector.create(0, 10, 0))
        local held = part("Held", vector.create(0, 10, 5))

        local joint = Instance.new("WeldConstraint")
        joint.Part0 = anchor
        joint.Part1 = held
        joint.Parent = workspace
        table.insert(spawned, joint)

        task.wait(1)
        -- Held where it was, five metres along Z, with nobody having authored
        -- an offset.
        expect(held.Position.z):ToBeCloseTo(5, 0.05)
        expect(held.Position.y):ToBeCloseTo(10, 0.05)
    end)

    it("reports Active only once it is holding", function()
        local anchor = anchoredPart("Anchor", vector.create(0, 10, 0))
        local held = part("Held", vector.create(0, 10, 5))

        local joint = Instance.new("WeldConstraint")
        joint.Parent = workspace
        table.insert(spawned, joint)
        expect(joint.Active):ToBe(false)

        joint.Part0 = anchor
        expect(joint.Active):ToBe(false)
        joint.Part1 = held
        expect(joint.Active):ToBe(true)

        joint.Enabled = false
        expect(joint.Active):ToBe(false)
    end)

    it("re-captures when it is enabled again, which is how a part is re-welded", function()
        local anchor = anchoredPart("Anchor", vector.create(0, 10, 0))
        local held = part("Held", vector.create(0, 10, 5))
        held.Anchored = true

        local joint = Instance.new("WeldConstraint")
        joint.Part0 = anchor
        joint.Part1 = held
        joint.Parent = workspace
        table.insert(spawned, joint)
        task.wait(0.2)

        joint.Enabled = false
        held.Position = vector.create(0, 10, 9)
        task.wait(0.2)
        joint.Enabled = true
        held.Anchored = false
        task.wait(0.5)

        -- Held at its NEW place rather than snapped back to the old one.
        expect(held.Position.z):ToBeCloseTo(9, 0.1)
    end)
end)
