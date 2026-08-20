--!strict
--- Conformance: `CharacterBody` (api-design.md §2.2).
---
--- What the document promises and this pins: `Move` sets a direction for the
--- next tick and is scaled by `WalkSpeed` rather than normalised, so a character
--- told nothing stops; `Jump` is ignored in mid-air rather than queued;
--- `Grounded` and `State` report the same fact in two shapes; `AutoStepHeight`
--- is the single number that separates a character from a capsule; and `Landed`
--- fires on the transition, with the part underneath.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local afterEach = testing.afterEach

local spawned: { Instance } = {}
local connections: { Connection } = {}

local function track(connection: Connection): Connection
    table.insert(connections, connection)
    return connection
end

local function block(name: string, position: vector, size: vector): Part
    local created = Instance.new("Part")
    created.Name = name
    created.Size = size
    created.Position = position
    created.Anchored = true
    created.Parent = workspace
    table.insert(spawned, created)
    return created
end

local function ground(): Part
    return block("Ground", vector.create(0, -1, 0), vector.create(120, 2, 120))
end

local function character(position: vector): CharacterBody
    local body = Instance.new("CharacterBody")
    body.Name = "Player"
    body.Size = vector.create(2, 5, 2)
    body.Position = position
    body.Parent = workspace
    table.insert(spawned, body)
    return body
end

--- Walks for `seconds`, telling the character every tick, which is what `Move`'s
--- contract requires of a caller.
local function walk(body: CharacterBody, direction: vector, seconds: number)
    local ticks = math.floor(seconds / game:GetService("PhysicsService").FixedTimestep)
    for _ = 1, ticks do
        body:Move(direction)
        task.wait()
    end
end

describe("CharacterBody", function()
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

    it("has the defaults api-design states", function()
        local body = character(vector.create(0, 1, 0))
        expect(body.WalkSpeed):ToBe(16)
        expect(body.JumpSpeed):ToBe(8)
        expect(body.MaxSlopeAngle):ToBe(46)
        expect(body.AutoStepHeight):ToBe(0.5)
        expect(body.Grounded):ToBe(false)
        expect(body.State):ToBe(Enum.CharacterState.Airborne)
    end)

    it("is a BasePart, and there is no Humanoid", function()
        local body = character(vector.create(0, 1, 0))
        expect(body:IsA("BasePart")):ToBe(true)
        expect(body:IsA("PVInstance")):ToBe(true)
        expect(function()
            Instance.new("Humanoid" :: any)
        end):ToThrow()
    end)

    it("falls, lands, and says what it landed on", function()
        local floor = ground()
        local body = character(vector.create(0, 8, 0))

        local landedOn: BasePart? = nil
        local landings = 0
        track(body.Landed:Connect(function(part: BasePart?)
            landings += 1
            landedOn = part
        end))

        task.wait(3)
        expect(body.Grounded):ToBe(true)
        expect(body.State):ToBe(Enum.CharacterState.Grounded)
        expect(landings):ToBe(1)
        expect(landedOn):ToBe(floor)
        -- Its position is its feet, so a landed character stands on the floor's
        -- top face rather than sinking half its height into it.
        expect(body.Position.y):ToBeCloseTo(0, 0.2)
    end)

    it("stops when it is told nothing", function()
        ground()
        local body = character(vector.create(0, 1, 0))
        task.wait(1)

        walk(body, vector.create(0, 0, 1), 0.5)
        local reached = body.Position.z
        expect(reached > 2):ToBe(true)

        -- No Move this second: the character must not coast.
        task.wait(1)
        expect(body.Position.z):ToBeCloseTo(reached, 0.3)
    end)

    it("scales the direction by WalkSpeed rather than normalising it", function()
        ground()
        local fast = character(vector.create(-8, 1, 0))
        local slow = character(vector.create(8, 1, 0))
        slow.WalkSpeed = 4

        task.wait(1)
        local fastStart = fast.Position.z
        local slowStart = slow.Position.z

        local ticks = math.floor(1 / game:GetService("PhysicsService").FixedTimestep)
        for _ = 1, ticks do
            fast:Move(vector.create(0, 0, 1))
            slow:Move(vector.create(0, 0, 1))
            task.wait()
        end

        expect((fast.Position.z - fastStart) > (slow.Position.z - slowStart) * 2):ToBe(true)
    end)

    it("jumps when grounded and ignores a jump in mid-air", function()
        ground()
        local body = character(vector.create(0, 1, 0))
        task.wait(1)
        expect(body.Grounded):ToBe(true)

        body:Jump()
        task.wait(0.25)
        expect(body.Position.y > 0.5):ToBe(true)
        expect(body.Grounded):ToBe(false)

        -- A second jump in mid-air changes nothing: it is ignored rather than
        -- queued, so the character does not launch again the moment it lands.
        local heightBefore = body.Position.y
        body:Jump()
        task.wait(0.1)
        expect(body.Position.y < heightBefore + 1):ToBe(true)
    end)

    it("walks up a step below AutoStepHeight and is stopped by one above it", function()
        ground()
        -- A 0.4 m ledge: the box is 0.8 tall and centred on the floor plane,
        -- and it spans z from five to eleven.
        block("Ledge", vector.create(0, 0, 8), vector.create(10, 0.8, 6))
        local body = character(vector.create(0, 1, 0))
        -- Slowed to four metres a second so that a second and a half leaves the
        -- character ON the ledge. At the default sixteen it walks twenty-four
        -- metres, crosses the ledge, comes down the far side and reports
        -- standing on the floor -- a pass condition a character that never
        -- climbed anything would also satisfy.
        body.WalkSpeed = 4
        task.wait(0.5)

        walk(body, vector.create(0, 0, 1), 1.5)
        expect(body.Position.y):ToBeCloseTo(0.4, 0.25)
        expect(body.Position.z > 5):ToBe(true)
        expect(body.Position.z < 11):ToBe(true)
    end)

    it("is stopped by a wall taller than it can step onto", function()
        ground()
        block("Wall", vector.create(0, 2, 8), vector.create(10, 4, 2))
        local body = character(vector.create(0, 1, 0))
        task.wait(0.5)

        walk(body, vector.create(0, 0, 1), 1.5)
        -- The wall's near face is at z = 7 and the character is a metre wide,
        -- so it stops around six.
        expect(body.Position.z < 6.5):ToBe(true)
    end)

    it("refuses a MaxSlopeAngle of ninety degrees or more, because then nothing is a wall", function()
        local body = character(vector.create(0, 1, 0))
        expect(function()
            body.MaxSlopeAngle = 90
        end):ToThrow()
        expect(body.MaxSlopeAngle):ToBe(46)
    end)
end)
