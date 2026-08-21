--!strict
--- The M5 gate scene: a character driven by a RECORDED keyboard, over sixty
--- seconds, asserting where it got to (roadmap M5, gate items one and three).
---
--- Deliberately not the deliverable. `examples/03-physics-playground` is the
--- world a human plays with, and a failure in it could be any of a dozen
--- things; this is the smallest world in which "walked up a kerb", "was stopped
--- by a wall", "jumped" and "climbed a ramp" are four separate sentences, so a
--- red gate names one of them.
---
--- **Every input comes from `inputs.txt` beside the manifest.** That is the
--- difference between this and a bot that calls `Move` directly: a bot proves
--- the simulation is deterministic and proves nothing about the path a keystroke
--- takes to reach it. The recording drives the Input Action System -- the same
--- resolver, the same contexts, the same snapshot a real keyboard produces.
--- It drove `KeyboardService` until M6 deleted that scaffold, and the recording
--- did not change: the names in `inputs.txt` are the same names.
---
--- The assertions are `error` calls, and the replay harness fails a run that
--- logged one. That is not a coincidence -- the harness counts errors precisely
--- so a scenario cannot die on its first tick and have its emptiness recorded
--- as a golden.

local RunService = game:GetService("RunService")

local Ground = Color3.fromRGB(96, 104, 96)
local Crate = Color3.fromRGB(176, 132, 84)

local function block(name: string, cframe: CFrame, size: vector, anchored: boolean): Part
    local part = Instance.new("Part")
    part.Name = name
    part.Size = size
    part.CFrame = cframe
    part.Color = anchored and Ground or Crate
    part.Anchored = anchored
    part.Parent = workspace
    return part
end

block("Ground", CFrame.new(0, -1, 0), vector.create(200, 2, 200), true)

-- A kerb whose top face is at 0.4, below the default AutoStepHeight of 0.5.
block("Kerb", CFrame.new(0, 0, 10), vector.create(20, 0.8, 6), true)
-- And a wall well above it.
block("Wall", CFrame.new(0, 2, 20), vector.create(20, 4, 2), true)
-- A twenty-degree ramp along +X: inside MaxSlopeAngle, so it is ground.
--
-- The sign is the whole of it. At -20 degrees the near end stands 5.8 m above
-- the floor and the ramp is a wall the character walks into -- which is what the
-- first version of this scene did, and what the gate reported as "walking east
-- climbs the ramp" failing at x = 9.1.
block(
    "Ramp",
    CFrame.new(18, 2.4, 0) * CFrame.fromEuler(0, 0, math.rad(20), Enum.RotationOrder.YXZ),
    vector.create(20, 1, 8),
    true
)

-- Three crates, so the run also covers bodies in contact settling, sleeping and
-- being woken -- which is the part of the physics state a hash covers and a
-- position does not.
for index = 0, 2 do
    block(`Crate{index}`, CFrame.new(-8, 1 + index * 2.02, -6), vector.create(2, 2, 2), false)
end

local character = Instance.new("CharacterBody")
character.Name = "Player"
character.Size = vector.create(2, 5, 2)
character.Position = vector.create(0, 4, 0)
-- Six metres a second rather than the default sixteen, so that the distances in
-- this scene and the tick numbers in the recording are legible to a person.
character.WalkSpeed = 6
character.Parent = workspace

--- The checks, by the tick they run at. Each one is a sentence about what the
--- recorded input should have made happen, and a failure names exactly one.
type Check = { tick: number, what: string, test: () -> boolean }

local checks: { Check } = {
    {
        tick = 130,
        what = "the character settles on the floor before the recording moves it",
        test = function()
            -- The centre of a five-metre capsule standing on a floor whose top
            -- face is at zero.
            return character.Grounded and math.abs(character.Position.y - 2.5) < 0.2
        end,
    },
    {
        tick = 220,
        what = "walking north climbs onto the kerb",
        test = function()
            -- Checked WHILE it is on the kerb, which spans z from seven to
            -- thirteen. The first version checked a hundred ticks later, by
            -- which time the character had crossed it, stepped back down onto
            -- the floor, and was standing at y = 0 -- a failure that reads as
            -- "it never climbed" and means "it climbed and kept going".
            return character.Position.y > 2.75 and character.Position.z > 6 and character.Position.z < 13
        end,
    },
    {
        tick = 330,
        what = "and is stopped by the wall behind it",
        test = function()
            return character.Position.z > 15 and character.Position.z < 19.5
        end,
    },
    {
        tick = 372,
        what = "Space leaves the ground",
        test = function()
            return character.Position.y > 3.2
        end,
    },
    {
        tick = 620,
        what = "walking south comes back down off the kerb",
        test = function()
            return character.Position.z < 4 and character.Position.y < 2.8
        end,
    },
    {
        tick = 940,
        what = "walking east climbs the ramp",
        test = function()
            return character.Position.x > 12 and character.Position.y > 3.5
        end,
    },
    {
        tick = 1200,
        what = "the crates have settled and gone to sleep",
        test = function()
            local crate = workspace:FindFirstChild("Crate0") :: Part
            return math.abs(crate.LinearVelocity.y) < 0.01
        end,
    },
}

-- The Input Action System, built exactly as a game builds it: one context, two
-- actions, one binding each. Nothing here is test-only -- that is the point of
-- migrating this scene rather than leaving it on a scaffold.
local Controls = Instance.new("InputContext")
Controls.Name = "Controls"
Controls.Parent = workspace

local MoveAction = Instance.new("InputAction")
MoveAction.Name = "Move"
MoveAction.Type = Enum.InputActionType.Direction2D
MoveAction.Parent = Controls

local MoveKeys = Instance.new("InputBinding")
MoveKeys.Up = Enum.KeyCode.W
MoveKeys.Down = Enum.KeyCode.S
MoveKeys.Left = Enum.KeyCode.A
MoveKeys.Right = Enum.KeyCode.D
MoveKeys.Parent = MoveAction

local JumpAction = Instance.new("InputAction")
JumpAction.Name = "Jump"
JumpAction.Type = Enum.InputActionType.Bool
JumpAction.Parent = Controls

local JumpKey = Instance.new("InputBinding")
JumpKey.KeyCode = Enum.KeyCode.Space
JumpKey.Parent = JumpAction

local tick = 0

RunService.Heartbeat:Connect(function()
    tick += 1

    -- The recording drives these, and nothing else does. The action's +Y is
    -- forward, which is +Z in this world -- the one place a game has to map the
    -- screen-space axis onto its own ground plane.
    local axis = MoveAction:GetState() :: Vector2
    local move = vector.create(axis.X, 0, axis.Y)
    if vector.magnitude(move) > 0.001 then
        character:Move(vector.normalize(move))
    end
    if JumpAction:GetState() :: boolean then
        character:Jump()
    end

    for _, check in checks do
        if check.tick == tick and not check.test() then
            error(
                `M5 functional gate failed at tick {tick}: {check.what}. `
                    .. `Character at ({character.Position.x}, {character.Position.y}, {character.Position.z}), `
                    .. `grounded={character.Grounded}`
            )
        end
    end
end)
