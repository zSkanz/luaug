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
        local body = character(vector.create(0, 2.6, 0))
        expect(body.WalkSpeed):ToBe(16)
        expect(body.JumpSpeed):ToBe(8)
        expect(body.MaxSlopeAngle):ToBe(46)
        expect(body.AutoStepHeight):ToBe(0.5)
        expect(body.Grounded):ToBe(false)
        expect(body.State):ToBe(Enum.CharacterState.Airborne)
    end)

    it("is a BasePart, and there is no Humanoid", function()
        local body = character(vector.create(0, 2.6, 0))
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
        -- Its Position is the CENTRE of its Size, like every other BasePart's,
        -- so a five-metre character standing on a floor whose top face is at
        -- zero has its centre at two and a half.
        expect(body.Position.y):ToBeCloseTo(2.5, 0.2)
    end)

    it("stops when it is told nothing", function()
        ground()
        local body = character(vector.create(0, 2.6, 0))
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
        local fast = character(vector.create(-8, 2.6, 0))
        local slow = character(vector.create(8, 2.6, 0))
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
        local body = character(vector.create(0, 2.6, 0))
        task.wait(1)
        expect(body.Grounded):ToBe(true)

        body:Jump()
        task.wait(0.25)
        expect(body.Position.y > 3):ToBe(true)
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
        local body = character(vector.create(0, 2.6, 0))
        -- Slowed to four metres a second so that a second and a half leaves the
        -- character ON the ledge. At the default sixteen it walks twenty-four
        -- metres, crosses the ledge, comes down the far side and reports
        -- standing on the floor -- a pass condition a character that never
        -- climbed anything would also satisfy.
        body.WalkSpeed = 4
        task.wait(0.5)

        -- Two and a half seconds at four metres a second puts the character ten
        -- metres in, which is the MIDDLE of a ledge spanning five to eleven.
        -- The first version walked six metres and measured the character with
        -- one metre of ledge under a capsule two metres wide -- mid-climb, at
        -- the edge, reading as "never climbed".
        walk(body, vector.create(0, 0, 1), 2.5)
        expect(body.Position.y):ToBeCloseTo(2.9, 0.25)
        expect(body.Position.z > 7):ToBe(true)
        expect(body.Position.z < 11):ToBe(true)
    end)

    it("is stopped by a wall taller than it can step onto", function()
        ground()
        block("Wall", vector.create(0, 2, 8), vector.create(10, 4, 2))
        local body = character(vector.create(0, 2.6, 0))
        task.wait(0.5)

        walk(body, vector.create(0, 0, 1), 1.5)
        -- The wall's near face is at z = 7 and the character is a metre wide,
        -- so it stops around six.
        expect(body.Position.z < 6.5):ToBe(true)
    end)

    it("is blocked by another character, and neither pushes the other", function()
        ground()
        local left = character(vector.create(-3, 2.6, 0))
        local right = character(vector.create(3, 2.6, 0))
        task.wait(0.5)

        local ticks = math.floor(3 / game:GetService("PhysicsService").FixedTimestep)
        for _ = 1, ticks do
            left:Move(vector.create(1, 0, 0))
            right:Move(vector.create(-1, 0, 0))
            task.wait()
        end

        -- Two metres wide each, so touching means their centres are two apart.
        -- What holds this is the inner rigid body every `CharacterBody` carries:
        -- a `CharacterVirtual` alone is not a `Body` and would not appear in the
        -- other one's sweep at all.
        expect(right.Position.x - left.Position.x > 1.9):ToBe(true)
        -- Neither has crossed the other, and both are still near the middle
        -- rather than shoved out of it.
        expect(left.Position.x < 0):ToBe(true)
        expect(right.Position.x > 0):ToBe(true)
    end)

    it("does not push a character that is standing still", function()
        ground()
        local still = character(vector.create(0, 2.6, 0))
        local charger = character(vector.create(-8, 2.6, 0))
        task.wait(0.5)

        -- Four seconds of a character running at its full WalkSpeed into one
        -- that is told nothing at all. Characters BLOCK; they do not shove.
        -- Knockback is a game rule, and a game writes it by moving the other
        -- character itself.
        walk(charger, vector.create(1, 0, 0), 4)
        expect(still.Position.x):ToBeCloseTo(0, 0.01)
        expect(charger.Position.x):ToBeCloseTo(-2.02, 0.05)
    end)

    it("obeys CollisionGroup, against scenery and against another character", function()
        local PhysicsService = game:GetService("PhysicsService")
        PhysicsService:RegisterCollisionGroup("Spectators")
        PhysicsService:CollisionGroupSetCollidable("Spectators", "Spectators", false)

        ground()
        local wall = block("Wall", vector.create(0, 4, 8), vector.create(20, 8, 1))
        wall.CollisionGroup = "Spectators"

        local walker = character(vector.create(0, 2.6, 0))
        walker.CollisionGroup = "Spectators"
        local other = character(vector.create(0, 2.6, 12))
        other.CollisionGroup = "Spectators"
        task.wait(0.5)

        -- Through the wall AND through the other character: a group that says
        -- "do not collide" has to mean it for the thing a player controls, and
        -- for a pair of them. Before the character's sweep was given the world's
        -- own layer filter it was the one thing in the world that ignored this
        -- property.
        walk(walker, vector.create(0, 0, 1), 2.5)
        expect(walker.Position.z > 12):ToBe(true)
    end)

    it("refuses a MaxSlopeAngle of ninety degrees or more, because then nothing is a wall", function()
        local body = character(vector.create(0, 2.6, 0))
        expect(function()
            body.MaxSlopeAngle = 90
        end):ToThrow()
        expect(body.MaxSlopeAngle):ToBe(46)
    end)
end)
