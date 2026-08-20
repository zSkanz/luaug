--!strict
--- Conformance: `Workspace:Raycast`, `:Spherecast`, `:GetBodiesInBox` and the
--- two datatypes they speak (api-design.md §2.1 and §2.3).
---
--- What the document promises and this pins: the direction is not normalised,
--- so its length is the range; a miss is nil rather than an empty result; the
--- filter covers descendants, so filtering a Model filters its parts; Include
--- and Exclude mean what the words mean, including the empty cases; `CanQuery`
--- removes a part from every query and nothing else; and `GetBodiesInBox`
--- reports one entry per part.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local afterEach = testing.afterEach

local spawned: { Instance } = {}

local function block(name: string, position: vector, size: vector, parent: Instance?): Part
    local created = Instance.new("Part")
    created.Name = name
    created.Size = size
    created.Position = position
    created.Anchored = true
    created.Parent = parent or workspace
    if parent == nil then
        table.insert(spawned, created)
    end
    return created
end

describe("Workspace:Raycast", function()
    afterEach(function()
        for _, instance in spawned do
            instance:Destroy()
        end
        table.clear(spawned)
    end)

    it("hits the nearest part and reports where, how far and which way", function()
        local floor = block("Floor", vector.create(0, -1, 0), vector.create(40, 2, 40))
        task.wait()

        local result = workspace:Raycast(vector.create(0, 10, 0), vector.create(0, -20, 0))
        expect(result).Never:ToBeNil()
        assert(result, "expected a hit")
        expect(result.Instance):ToBe(floor)
        expect(result.Position.y):ToBeCloseTo(0, 0.01)
        expect(result.Distance):ToBeCloseTo(10, 0.01)
        expect(result.Normal.y):ToBeCloseTo(1, 0.01)
    end)

    it("reaches exactly as far as the direction is long", function()
        block("Floor", vector.create(0, -1, 0), vector.create(40, 2, 40))
        task.wait()

        -- Nine metres of ray from ten metres up stops one metre short.
        expect(workspace:Raycast(vector.create(0, 10, 0), vector.create(0, -9, 0))):ToBeNil()
        expect(workspace:Raycast(vector.create(0, 10, 0), vector.create(0, -11, 0))).Never:ToBeNil()
    end)

    it("returns nil rather than an empty result", function()
        task.wait()
        expect(workspace:Raycast(vector.create(0, 500, 0), vector.create(0, 1, 0))):ToBeNil()
    end)

    it("hits the nearest of several", function()
        block("Low", vector.create(0, 0, 0), vector.create(10, 2, 10))
        local high = block("High", vector.create(0, 5, 0), vector.create(10, 2, 10))
        task.wait()

        local result = workspace:Raycast(vector.create(0, 20, 0), vector.create(0, -30, 0))
        assert(result, "expected a hit")
        expect(result.Instance):ToBe(high)
    end)

    it("answers the same way twice, which is what a replay depends on", function()
        block("A", vector.create(0, 0, 0), vector.create(10, 2, 10))
        block("B", vector.create(0, 0, 0), vector.create(10, 2, 10))
        task.wait()

        -- Two coincident surfaces: the tie is the case where traversal order
        -- would otherwise decide, and R10 says it may not.
        local first = workspace:Raycast(vector.create(0, 20, 0), vector.create(0, -30, 0))
        local second = workspace:Raycast(vector.create(0, 20, 0), vector.create(0, -30, 0))
        assert(first and second, "expected hits")
        expect(first.Instance):ToBe(second.Instance)
    end)
end)

describe("RaycastParams", function()
    afterEach(function()
        for _, instance in spawned do
            instance:Destroy()
        end
        table.clear(spawned)
    end)

    it("defaults to an empty Exclude filter over every group", function()
        local params = RaycastParams.new()
        expect(#params.Filter):ToBe(0)
        expect(params.FilterType):ToBe(Enum.RaycastFilterType.Exclude)
        expect(params.CollisionGroup):ToBe("")
    end)

    it("reads back what it was given", function()
        local part = block("Filtered", vector.create(0, 0, 0), vector.create(2, 2, 2))
        local params = RaycastParams.new({
            Filter = { part } :: { Instance },
            FilterType = Enum.RaycastFilterType.Include,
        })
        expect(#params.Filter):ToBe(1)
        expect(params.Filter[1]):ToBe(part)
        expect(params.FilterType):ToBe(Enum.RaycastFilterType.Include)
    end)

    it("returns a fresh Filter array on every read", function()
        local part = block("Filtered", vector.create(0, 0, 0), vector.create(2, 2, 2))
        local params = RaycastParams.new({ Filter = { part } :: { Instance } })
        expect(params.Filter == params.Filter):ToBe(false)
    end)

    it("excludes what it names", function()
        local top = block("Top", vector.create(0, 5, 0), vector.create(10, 2, 10))
        local below = block("Below", vector.create(0, 0, 0), vector.create(10, 2, 10))
        task.wait()

        local params = RaycastParams.new({ Filter = { top } :: { Instance } })
        local result = workspace:Raycast(vector.create(0, 20, 0), vector.create(0, -30, 0), params)
        assert(result, "expected a hit")
        expect(result.Instance):ToBe(below)
    end)

    it("includes only what it names", function()
        block("Top", vector.create(0, 5, 0), vector.create(10, 2, 10))
        local below = block("Below", vector.create(0, 0, 0), vector.create(10, 2, 10))
        task.wait()

        local params = RaycastParams.new({
            Filter = { below } :: { Instance },
            FilterType = Enum.RaycastFilterType.Include,
        })
        local result = workspace:Raycast(vector.create(0, 20, 0), vector.create(0, -30, 0), params)
        assert(result, "expected a hit")
        expect(result.Instance):ToBe(below)
    end)

    it("hits nothing at all with an empty Include filter", function()
        block("Anything", vector.create(0, 0, 0), vector.create(10, 2, 10))
        task.wait()

        local params = RaycastParams.new({ FilterType = Enum.RaycastFilterType.Include })
        expect(workspace:Raycast(vector.create(0, 20, 0), vector.create(0, -30, 0), params)):ToBeNil()
    end)

    it("covers the descendants of what it names, so filtering a Model filters its parts", function()
        local model = Instance.new("Model")
        model.Name = "Rig"
        model.Parent = workspace
        table.insert(spawned, model)

        block("Limb", vector.create(0, 5, 0), vector.create(10, 2, 10), model)
        local below = block("Below", vector.create(0, 0, 0), vector.create(10, 2, 10))
        task.wait()

        local params = RaycastParams.new({ Filter = { model } :: { Instance } })
        local result = workspace:Raycast(vector.create(0, 20, 0), vector.create(0, -30, 0), params)
        assert(result, "expected a hit")
        expect(result.Instance):ToBe(below)
    end)

    it("refuses a collision group nobody registered", function()
        block("Anything", vector.create(0, 0, 0), vector.create(10, 2, 10))
        task.wait()

        local params = RaycastParams.new({ CollisionGroup = "NeverRegisteredForQueries" })
        expect(function()
            workspace:Raycast(vector.create(0, 20, 0), vector.create(0, -30, 0), params)
        end):ToThrow()
    end)
end)

describe("CanQuery", function()
    afterEach(function()
        for _, instance in spawned do
            instance:Destroy()
        end
        table.clear(spawned)
    end)

    it("removes a part from a cast without changing what it collides with", function()
        local invisible = block("Invisible", vector.create(0, 5, 0), vector.create(10, 2, 10))
        invisible.CanQuery = false
        local below = block("Below", vector.create(0, 0, 0), vector.create(10, 2, 10))
        task.wait()

        local result = workspace:Raycast(vector.create(0, 20, 0), vector.create(0, -30, 0))
        assert(result, "expected a hit")
        expect(result.Instance):ToBe(below)

        -- Still solid: a falling crate lands on it.
        local crate = Instance.new("Part")
        crate.Size = vector.create(2, 2, 2)
        crate.Position = vector.create(0, 12, 0)
        crate.Parent = workspace
        table.insert(spawned, crate)
        task.wait(2)
        expect(crate.Position.y):ToBeCloseTo(7, 0.3)
    end)
end)

describe("Workspace:Spherecast", function()
    afterEach(function()
        for _, instance in spawned do
            instance:Destroy()
        end
        table.clear(spawned)
    end)

    it("sweeps a shape where a ray would slip through", function()
        block("Left", vector.create(-2, 5, 0), vector.create(2, 2, 2))
        block("Right", vector.create(2, 5, 0), vector.create(2, 2, 2))
        task.wait()

        local origin = vector.create(0, 5, -10)
        local direction = vector.create(0, 0, 20)
        expect(workspace:Raycast(origin, direction)):ToBeNil()
        expect(workspace:Spherecast(origin, 1.5, direction)).Never:ToBeNil()
    end)
end)

describe("Workspace:GetBodiesInBox", function()
    afterEach(function()
        for _, instance in spawned do
            instance:Destroy()
        end
        table.clear(spawned)
    end)

    it("reports each overlapping part once, and nothing outside the box", function()
        block("Inside1", vector.create(0, 0, 0), vector.create(2, 2, 2))
        block("Inside2", vector.create(3, 0, 0), vector.create(2, 2, 2))
        block("Outside", vector.create(60, 0, 0), vector.create(2, 2, 2))
        task.wait()

        local found = workspace:GetBodiesInBox(CFrame.new(1.5, 0, 0), vector.create(12, 12, 12))
        expect(#found):ToBe(2)

        local names = {}
        for _, part in found do
            names[part.Name] = true
        end
        expect(names["Inside1"]):ToBe(true)
        expect(names["Inside2"]):ToBe(true)
        expect(names["Outside"]):ToBeNil()
    end)

    it("returns an empty array rather than nil when nothing overlaps", function()
        task.wait()
        local found = workspace:GetBodiesInBox(CFrame.new(0, 900, 0), vector.create(4, 4, 4))
        expect(#found):ToBe(0)
    end)
end)
