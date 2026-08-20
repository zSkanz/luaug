--!strict
--- Fifty characters in one crowd, which is the question "what happens with
--- fifty enemies" asked as a number instead of as an estimate.
---
--- A `CharacterBody` is a Jolt `CharacterVirtual` with an inner rigid body, and
--- the inner body is what one character's sweep finds when it walks into
--- another. That matters for what this scene measures: character-against-
--- character goes through the broad phase like every other pair, so the cost is
--- the tree query rather than a walk over every other character. The shape of
--- the curve is what the baseline records.
---
--- Shoulder to shoulder is the point. Two-metre capsules two metres apart are
--- inside each other's sweep every tick, and they are all told to walk in the
--- same direction into a wall, so the crowd stays a crowd for the whole run
--- instead of dispersing into fifty separate one-character problems.

local RunService = game:GetService("RunService")

local Root = Instance.new("Folder")
Root.Name = "Crowd"
Root.Parent = workspace

local ground = Instance.new("Part")
ground.Name = "Ground"
ground.Size = vector.create(200, 4, 200)
ground.Position = vector.create(0, -2, 0)
ground.Anchored = true
ground.Parent = Root

-- What the crowd walks into, so it stays packed rather than spreading out.
local wall = Instance.new("Part")
wall.Name = "Wall"
wall.Size = vector.create(4, 20, 60)
wall.Position = vector.create(16, 10, 0)
wall.Anchored = true
wall.Parent = Root

local CrowdSize = 50
local PerRow = 10
local characters: { CharacterBody } = {}
local starts: { number } = {}

for i = 0, CrowdSize - 1 do
    local body = Instance.new("CharacterBody")
    body.Name = `Enemy{i}`
    body.Size = vector.create(2, 5, 2)
    body.Position = vector.create(-18 + (i % PerRow) * 2, 2.6, -9 + math.floor(i / PerRow) * 2)
    body.WalkSpeed = 8
    body.Parent = Root
    table.insert(characters, body)
    table.insert(starts, body.Position.x)
end

assert(#characters == CrowdSize, `expected {CrowdSize} characters, built {#characters}`)

local Ticks = 0

RunService.Heartbeat:Connect(function()
    Ticks += 1
    for _, body in characters do
        body:Move(vector.create(1, 0, 0))
    end

    -- A benchmark that measures fifty characters standing in an empty room is
    -- a benchmark of nothing, and it looks exactly like this one. Checked once,
    -- late enough for the crowd to have arrived and pressed itself flat: every
    -- one of them is against the wall or against somebody who is, still upright
    -- on the floor, and no two are inside each other.
    if Ticks == 250 then
        local front = -math.huge
        for i, body in characters do
            assert(body.Position.x - starts[i] > 8, `Enemy{i - 1} barely moved: {starts[i]} to {body.Position.x}`)
            assert(body.Grounded, `Enemy{i - 1} is not on the floor`)
            front = math.max(front, body.Position.x)
            for j = i + 1, #characters do
                local other = characters[j]
                local dx = body.Position.x - other.Position.x
                local dz = body.Position.z - other.Position.z
                -- Two metres apart is touching for a two-metre capsule; 3.6 is
                -- that squared with room for Jolt's padding.
                assert(dx * dx + dz * dz > 3.6, `Enemy{i - 1} and Enemy{j - 1} overlap`)
            end
        end
        -- The wall's near face is at x = 14 and a character is a metre wide, so
        -- the front rank stands just short of thirteen. If nobody is there, the
        -- crowd never arrived and the number this scene reports is the cost of
        -- fifty characters walking across an empty floor.
        assert(front > 12, `nobody reached the wall: the leader is at {front}`)
    end
end)
