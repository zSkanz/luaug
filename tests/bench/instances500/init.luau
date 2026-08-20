--!strict
--- The M2 gate scene: 500 scripted instances, ticking under budget.
---
--- 500 is the roadmap's number and this is the shape the gate means by it: a
--- real tree with real handlers, not 500 parts nobody touches. Every tick moves
--- every part, so the measurement is the cost of a tick that does something
--- rather than the cost of an empty one.
---
--- The budget is 4 ms per tick, a quarter of a 60 Hz frame. Simulation is not
--- the only thing in a frame -- rendering, physics and the scheduler all have to
--- fit alongside it -- and a gate that let the script layer eat the whole
--- timestep would pass right up to the moment the engine could not render.

local RunService = game:GetService("RunService")

local Rng = Random.new(4242)
local Root = Instance.new("Folder")
Root.Name = "Bench"
Root.Parent = workspace

-- Ten models of fifty parts. Grouped rather than flat because a flat list of 500
-- siblings is the easy case: `FindFirstChild` walks a name chain, ancestry walks
-- a parent pointer, and a tree with depth exercises both the way a real scene
-- does.
local Parts: { Part } = {}
for group = 1, 10 do
    local model = Instance.new("Model")
    model.Name = `Group{group}`
    model.Parent = Root

    for index = 1, 50 do
        local part = Instance.new("Part")
        part.Name = `Part{index}`
        part.Size = vector.create(1, 1, 1)
        part.CFrame = CFrame.new(vector.create(Rng:NextNumber(-40, 40), Rng:NextNumber(0, 10), Rng:NextNumber(-40, 40)))
        part.Color = Color3.fromRGB(Rng:NextInteger(0, 255), Rng:NextInteger(0, 255), Rng:NextInteger(0, 255))
        part.Parent = model
        table.insert(Parts, part)
    end
end

local Phase = 0.0

RunService.Heartbeat:Connect(function(deltaTime: number)
    Phase += deltaTime

    -- A property write per part per tick, which is the thing the M2 kernel is
    -- actually being measured on: the write goes through reflection, compares
    -- against the stored value, and enqueues a Changed fact only if it moved.
    for index, part in Parts do
        local offset = math.sin(Phase + index * 0.05) * 0.25
        part.CFrame = CFrame.new(vector.create(part.CFrame.Position.x, 5 + offset, part.CFrame.Position.z))
    end
end)
