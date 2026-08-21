--!strict
--- What D031's fix costs, measured rather than estimated.
---
--- An anchored part whose `CFrame` is written becomes a KINEMATIC body and goes
--- back to `Static` twelve ticks after the last write. Changing a body's motion
--- type is a rebuild on the backend's side, and it moves the body between the
--- two broadphase layers -- `NonMoving`, which Jolt keeps precisely so that it
--- is never re-fitted, and `Moving`, which is updated every tick. The narrow fix
--- was chosen over "anchored means kinematic always" on exactly that cost, so
--- the cost is the thing to have a number for.
---
--- **Three populations, so the row separates the transition from the traffic:**
---
---   * 200 platforms written every tick. They become kinematic once and stay
---     kinematic: this is the steady-state cost of a moving platform.
---   * 200 platforms written every FIFTEEN ticks. Fifteen is past the
---     twelve-tick hold on purpose, so each one transitions Static -> Kinematic
---     -> Static twenty times over the run. This is the transition cost, and it
---     is the pathological case the hysteresis exists to bound: at eleven ticks
---     they would never transition at all.
---   * 600 anchored parts nobody writes, which is what a world is mostly made
---     of. They must stay in the static layer and cost nothing, and a regression
---     that made `Anchored` mean kinematic would show up here rather than in the
---     two above.
---
--- The 16 ms budget is a whole frame and a catastrophe detector, like every
--- other scene here. The number that matters is the one recorded in
--- `docs/perf-baselines.md`.

local RunService = game:GetService("RunService")

local Root = Instance.new("Folder")
Root.Name = "Platforms"
Root.Parent = workspace

local ground = Instance.new("Part")
ground.Name = "Ground"
ground.Size = vector.create(400, 4, 400)
ground.Position = vector.create(0, -2, 0)
ground.Anchored = true
ground.Parent = Root

local function platform(name: string, index: number, row: number): Part
    local part = Instance.new("Part")
    part.Name = name
    part.Size = vector.create(4, 1, 4)
    part.Position = vector.create((index % 20) * 6 - 60, 2 + row * 3, math.floor(index / 20) * 6 - 30)
    part.Anchored = true
    part.Parent = Root
    return part
end

local Continuous: { Part } = {}
local Intermittent: { Part } = {}

for index = 1, 200 do
    table.insert(Continuous, platform(`Continuous{index}`, index, 0))
    table.insert(Intermittent, platform(`Intermittent{index}`, index, 1))
end

-- The still ones. Written once, at build time, and never again: they are the
-- floor and the walls of any real world, and they must stay static.
for index = 1, 600 do
    platform(`Still{index}`, index, 2 + (index % 3))
end

local ticks = 0

RunService.PreSimulation:Connect(function()
    ticks += 1

    -- Every tick: kinematic once, kinematic thereafter.
    for index, part in Continuous do
        part.Position = vector.create(part.Position.x, 2 + math.sin((ticks + index) * 0.05), part.Position.z)
    end

    -- Every fifteenth: past the twelve-tick hold, so each of these transitions
    -- both ways twenty times over three hundred ticks.
    if ticks % 15 == 0 then
        for index, part in Intermittent do
            part.Position = vector.create(part.Position.x, 5 + (ticks % 30) * 0.1 + index * 0.001, part.Position.z)
        end
    end
end)
