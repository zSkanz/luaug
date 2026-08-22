--!strict
--- Conformance: `Touched` and `TouchEnded` (api-design.md §2.2).
---
--- The contract these pin, in the document's own words: the signal fires ONCE
--- when a pair begins touching and once when it stops, including across the
--- moment the simulation puts both parts to sleep; a part that cannot collide
--- still reports a touch; and destroying a part does not fire `TouchEnded` for
--- its contacts, because the instance is gone.
---
--- Deferred like every signal here (ADR 0015), so a handler runs at the next
--- resumption point rather than inside the solver -- which is why every case
--- waits before asserting.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local beforeEach, afterEach = testing.beforeEach, testing.afterEach

local PhysicsService = game:GetService("PhysicsService")

local function step(ticks: number)
    task.wait(ticks * PhysicsService.FixedTimestep)
end

local spawned: { Instance } = {}
local connections: { Connection } = {}

local function part(name: string, position: vector, size: vector): Part
    local created = Instance.new("Part")
    created.Name = name
    created.Size = size
    created.Position = position
    created.Parent = workspace
    table.insert(spawned, created)
    return created
end

local function floor(): Part
    local ground = part("Ground", vector.create(0, -1, 0), vector.create(60, 2, 60))
    ground.Anchored = true
    return ground
end

local function track(connection: Connection): Connection
    table.insert(connections, connection)
    return connection
end

describe("Touched", function()
    afterEach(function()
        for _, connection in connections do
            connection:Disconnect()
        end
        table.clear(connections)
        for _, instance in spawned do
            instance:Destroy()
        end
        table.clear(spawned)
    end)

    it("fires once when a pair begins touching, and names the other part", function()
        local ground = floor()
        local crate = part("Crate", vector.create(0, 4, 0), vector.create(2, 2, 2))

        local hits = 0
        local other: BasePart? = nil
        track(crate.Touched:Connect(function(touched: BasePart)
            hits += 1
            other = touched
        end))

        task.wait(2)
        expect(hits):ToBe(1)
        expect(other):ToBe(ground)
    end)

    it("fires on both parts of the pair", function()
        local ground = floor()
        local crate = part("Crate", vector.create(0, 4, 0), vector.create(2, 2, 2))

        local onCrate = 0
        local onGround = 0
        track(crate.Touched:Connect(function()
            onCrate += 1
        end))
        track(ground.Touched:Connect(function()
            onGround += 1
        end))

        task.wait(2)
        expect(onCrate):ToBe(1)
        expect(onGround):ToBe(1)
    end)

    it("does not fire again while a resting pair stays in contact", function()
        floor()
        local crate = part("Crate", vector.create(0, 4, 0), vector.create(2, 2, 2))

        local hits = 0
        track(crate.Touched:Connect(function()
            hits += 1
        end))

        -- Four seconds is long enough for the crate to settle and for the
        -- simulation to put it to sleep, which is the case that turned a
        -- resting contact into a stream of Touched/TouchEnded pairs the first
        -- time this was written.
        task.wait(4)
        expect(hits):ToBe(1)
    end)

    it("fires for a part that cannot collide, which is what makes a trigger work", function()
        local trigger = part("Trigger", vector.create(0, 0, 0), vector.create(20, 2, 20))
        trigger.Anchored = true
        trigger.CanCollide = false
        local crate = part("Crate", vector.create(0, 6, 0), vector.create(2, 2, 2))

        local touched = false
        track(trigger.Touched:Connect(function()
            touched = true
        end))

        -- Two seconds: the crate needs about nine tenths of one to reach the
        -- trigger's top face, and the signal is deferred to the next resumption
        -- point after that. A one-second wait asserted on the tick the contact
        -- happened, which is a race the spec would have lost half the time.
        task.wait(2)
        expect(touched):ToBe(true)
        -- Through, not onto.
        expect(crate.Position.y < -1):ToBe(true)
    end)
end)

describe("TouchEnded", function()
    afterEach(function()
        for _, connection in connections do
            connection:Disconnect()
        end
        table.clear(connections)
        for _, instance in spawned do
            instance:Destroy()
        end
        table.clear(spawned)
    end)

    it("fires when a pair that was touching stops", function()
        floor()
        local crate = part("Crate", vector.create(0, 4, 0), vector.create(2, 2, 2))

        local ended = 0
        track(crate.TouchEnded:Connect(function()
            ended += 1
        end))

        task.wait(2)
        expect(ended):ToBe(0)

        -- Lifted away rather than nudged: a teleport is the one movement whose
        -- separation cannot be confused with the solver settling.
        crate.Position = vector.create(0, 30, 0)
        task.wait(0.5)
        expect(ended):ToBe(1)
    end)

    it("does not fire for a part that was destroyed", function()
        floor()
        -- Deliberately NOT tracked in `spawned`: this case destroys it itself,
        -- and `afterEach` destroying it a second time is an error about the
        -- spec rather than about the engine.
        local crate = Instance.new("Part")
        crate.Size = vector.create(2, 2, 2)
        crate.Position = vector.create(0, 4, 0)
        crate.Parent = workspace

        local ended = 0
        track(crate.TouchEnded:Connect(function()
            ended += 1
        end))

        task.wait(2)
        crate:Destroy()
        task.wait(0.5)
        expect(ended):ToBe(0)
    end)
end)

describe("Touched and a CharacterBody", function()
    local ground: Part = nil :: any
    local pad: Part = nil :: any
    local character: CharacterBody = nil :: any

    beforeEach(function()
        ground = Instance.new("Part")
        ground.Name = "CharacterGround"
        ground.Size = vector.create(20, 1, 20)
        ground.Position = vector.create(0, 0, 0)
        ground.Anchored = true
        ground.Parent = workspace

        pad = Instance.new("Part")
        pad.Name = "CharacterPad"
        pad.Size = vector.create(6, 0.4, 6)
        pad.Position = vector.create(0, 0.7, -8)
        pad.Anchored = true
        pad.Parent = workspace

        character = Instance.new("CharacterBody")
        character.Name = "Walker"
        character.Size = vector.create(2, 4, 2)
        character.Position = vector.create(0, 3, 0)
        -- Slow on purpose: at the default speed a character crosses a six-metre
        -- pad in a quarter of a second, and a case that asserts "on the pad"
        -- after a fixed number of ticks would be asserting about the tick count.
        character.WalkSpeed = 4
        character.Parent = workspace
    end)

    afterEach(function()
        character:Destroy()
        pad:Destroy()
        ground:Destroy()
    end)

    it("fires for the surface under a character's feet", function()
        -- A checkpoint pad you walk onto and a hazard you step on are the two
        -- things every obby is made of, and both are ground contacts. Before
        -- M6 a `CharacterBody` fired nothing at all: the contact listener only
        -- sees rigid-body pairs and a character controller is not one.
        local touchedBy: BasePart? = nil
        local connection = pad.Touched:Connect(function(other: BasePart)
            touchedBy = other
        end)

        for _ = 1, 90 do
            character:Move(vector.create(0, 0, -1))
            step(1)
        end

        expect(touchedBy):ToBe(character :: any)
        connection:Disconnect()
    end)

    it("fires TouchEnded when the character walks off", function()
        local ended = 0
        local connection = pad.TouchEnded:Connect(function()
            ended += 1
        end)

        for _ = 1, 90 do
            character:Move(vector.create(0, 0, -1))
            step(1)
        end
        expect(ended):ToBe(0)

        for _ = 1, 120 do
            character:Move(vector.create(0, 0, -1))
            step(1)
        end
        expect(ended):ToBe(1)
        connection:Disconnect()
    end)

    it("fires for a wall the character walks into, not only the floor", function()
        -- D028, and the reason `examples/04-obby` finishes on a PAD rather than
        -- on the pole it was designed around: a character pressed against a
        -- flagpole fired nothing, because M6 covered the ground half by diffing
        -- the surface under the feet and a wall is not under anybody's feet.
        local wall = Instance.new("Part")
        wall.Name = "CharacterWall"
        wall.Size = vector.create(8, 6, 1)
        wall.Position = vector.create(0, 3, -14)
        wall.Anchored = true
        wall.Parent = workspace

        local touchedBy: BasePart? = nil
        local connection = wall.Touched:Connect(function(other: BasePart)
            touchedBy = other
        end)

        for _ = 1, 200 do
            character:Move(vector.create(0, 0, -1))
            step(1)
        end

        expect(touchedBy):ToBe(character :: any)
        connection:Disconnect()
        wall:Destroy()
    end)

    it("fires on the character too, because Touched is a fact about each part", function()
        local touchedPad = false
        local connection = character.Touched:Connect(function(other: BasePart)
            if other == (pad :: BasePart) then
                touchedPad = true
            end
        end)

        for _ = 1, 90 do
            character:Move(vector.create(0, 0, -1))
            step(1)
        end

        expect(touchedPad):ToBe(true)
        connection:Disconnect()
    end)
end)
