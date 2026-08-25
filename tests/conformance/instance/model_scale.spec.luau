--!strict
--- `Model.Scale`: one number that resizes everything under a model, in place.
---
--- The property is ABSOLUTE and the fan-out is BAKED, and those two sentences
--- are what these cases hold down. Absolute: the write scales by the ratio
--- between the new value and the old, so setting 2 twice leaves the model twice
--- its built size rather than four times. Baked: the parts under the model are
--- simply at their new sizes, and nothing multiplies by `Scale` per frame --
--- which is what makes a scaled model cost the same to draw as an unscaled one,
--- and also why a part parented in afterwards is not scaled.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local beforeEach, afterEach = testing.beforeEach, testing.afterEach

-- The second argument to `ToBeCloseTo` is a TOLERANCE, not a digit count.
local Epsilon = 1e-4

local function writeAny(instance: Instance, property: string, value: any)
    return function()
        (instance :: any)[property] = value
    end
end

describe("Model.Scale", function()
    local model: Model = nil :: any
    local part: Part = nil :: any

    beforeEach(function()
        model = Instance.new("Model")
        model.Name = "ScaleSpecModel"
        model.Parent = workspace

        part = Instance.new("Part")
        part.Name = "ScaleSpecPart"
        part.Size = vector.create(2, 4, 6)
        part.CFrame = CFrame.new(vector.create(10, 0, 0))
        part.Parent = model
        model.PrimaryPart = part
    end)

    afterEach(function()
        model:Destroy()
    end)

    it("starts at one", function()
        expect(model.Scale):ToBe(1)
    end)

    it("resizes every part under it", function()
        model.Scale = 1.5
        expect(model.Scale):ToBe(1.5)
        expect(part.Size.x):ToBeCloseTo(3, Epsilon)
        expect(part.Size.y):ToBeCloseTo(6, Epsilon)
        expect(part.Size.z):ToBeCloseTo(9, Epsilon)
    end)

    it("is absolute rather than cumulative", function()
        model.Scale = 2
        model.Scale = 3
        -- THREE times the built size, not six. The second write scales by the
        -- ratio between the new value and the old one, and a cumulative setter
        -- is exactly what this number distinguishes -- two DIFFERENT values,
        -- because writing the same one twice never reaches the setter at all
        -- (`World::setProperty` reads and compares first).
        expect(part.Size.x):ToBeCloseTo(6, Epsilon)
        expect(part.Size.y):ToBeCloseTo(12, Epsilon)
        expect(model.Scale):ToBe(3)
    end)

    it("does nothing at all when the value has not changed", function()
        model.Scale = 2
        model.Scale = 2
        expect(part.Size.x):ToBeCloseTo(4, Epsilon)
        expect(model.Scale):ToBe(2)
    end)

    it("goes back exactly", function()
        model.Scale = 3
        model.Scale = 1
        expect(part.Size.x):ToBeCloseTo(2, Epsilon)
        expect(part.Size.y):ToBeCloseTo(4, Epsilon)
        expect(part.Size.z):ToBeCloseTo(6, Epsilon)
        expect(part.Position.x):ToBeCloseTo(10, Epsilon)
    end)

    it("scales about the pivot, so the model grows in place", function()
        -- The pivot is the primary part's own frame, which sits at x = 10. That
        -- part is AT the pivot, so it must not move however much it grows.
        model.Scale = 4
        expect(part.Position.x):ToBeCloseTo(10, Epsilon)

        -- A second part twice as far from the pivot ends up four times as far
        -- when the model is four times the size -- the whole reason the walk is
        -- about the pivot rather than about the origin.
        local wing = Instance.new("Part")
        wing.CFrame = CFrame.new(vector.create(20, 0, 0))
        wing.Parent = model
        model.Scale = 8
        -- 10 metres from the pivot at scale 4, so 20 at scale 8.
        expect(wing.Position.x):ToBeCloseTo(30, Epsilon)
    end)

    it("leaves rotation alone", function()
        part.CFrame = CFrame.new(vector.create(10, 0, 0)) * CFrame.fromEuler(0, math.pi / 2, 0)
        local before = part.Orientation
        model.Scale = 2.5
        expect(part.Orientation.x):ToBeCloseTo(before.x, Epsilon)
        expect(part.Orientation.y):ToBeCloseTo(before.y, Epsilon)
        expect(part.Orientation.z):ToBeCloseTo(before.z, Epsilon)
    end)

    it("does not scale a part parented in afterwards", function()
        model.Scale = 2
        local late = Instance.new("Part")
        late.Size = vector.create(1, 1, 1)
        late.Parent = model
        -- The honest cost of a baked fan-out, and the property's Doc says so.
        expect(late.Size.x):ToBeCloseTo(1, Epsilon)
    end)

    it("scales a nested model's own number without scaling twice", function()
        local inner = Instance.new("Model")
        inner.Parent = model
        local innerPart = Instance.new("Part")
        innerPart.Size = vector.create(1, 1, 1)
        innerPart.Parent = inner

        model.Scale = 3
        -- The inner model's number moved with its parts -- it reads back as
        -- three, and its part grew exactly three times rather than nine.
        expect(inner.Scale):ToBeCloseTo(3, Epsilon)
        expect(innerPart.Size.x):ToBeCloseTo(3, Epsilon)
    end)

    it("refuses a scale that is not a size", function()
        -- Zero is not a small model, it is a shape with no extent; negative is a
        -- mirror that inverts every winding under it.
        expect(writeAny(model, "Scale", 0)):ToThrow()
        expect(writeAny(model, "Scale", -1)):ToThrow()
        expect(writeAny(model, "Scale", 0 / 0)):ToThrow()
        expect(model.Scale):ToBe(1)
        expect(part.Size.x):ToBeCloseTo(2, Epsilon)
    end)
end)

describe("MeshPart.MeshSize", function()
    local mesh: MeshPart = nil :: any

    beforeEach(function()
        mesh = Instance.new("MeshPart")
        mesh.Name = "MeshSizeSpecPart"
        mesh.Parent = workspace
    end)

    afterEach(function()
        mesh:Destroy()
    end)

    it("starts at one, so a part draws as it always has", function()
        expect(mesh.MeshSize.x):ToBe(1)
        expect(mesh.MeshSize.y):ToBe(1)
        expect(mesh.MeshSize.z):ToBe(1)
    end)

    it("reads back what was written", function()
        mesh.MeshSize = vector.create(2, 3, 4)
        expect(mesh.MeshSize.x):ToBeCloseTo(2, Epsilon)
        expect(mesh.MeshSize.z):ToBeCloseTo(4, Epsilon)
    end)

    it("refuses a component that is a division rather than a size", function()
        expect(writeAny(mesh, "MeshSize", vector.create(0, 1, 1))):ToThrow()
        expect(writeAny(mesh, "MeshSize", vector.create(1, -1, 1))):ToThrow()
        expect(writeAny(mesh, "MeshSize", vector.create(1, 1, 0 / 0))):ToThrow()
        expect(mesh.MeshSize.x):ToBe(1)
    end)
end)
