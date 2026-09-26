--!strict
--- examples/23-surfaces -- three small surface shaders (ADR 0091), each a file
--- in this project's `content/shaders/`, each on a material in
--- `content/materials/`.
---
---   * **A flag** (`flag.surface.hlsl`): vertex displacement on something that
---     is not water. A flat grid on a pole, rippling away from it.
---   * **A dissolve** (`dissolve.surface.hlsl`): a Mask surface that eats itself
---     away. Each block wears its own clone of the material, and this script
---     moves the clone's `Threshold` -- a parameter of the SHADER, set with
---     `SetShaderParameter`.
---   * **Glass** (`glass.surface.hlsl`): a blended surface that reads the scene
---     behind it and bends it.
---
--- Edit any of the three while the editor runs and it recompiles on save.

local Lighting = game:GetService("Lighting")
local RunService = game:GetService("RunService")

Lighting.ClockTime = 15.5

local floor = Instance.new("Part")
floor.Name = "Floor"
floor.Anchored = true
floor.Size = vector.create(40, 1, 30)
floor.Position = vector.create(0, -0.5, 0)
floor:SetMaterialParameter("Color", Color3.fromRGB(96, 104, 92))
floor.Parent = workspace

-- --- The flag ---------------------------------------------------------------

local pole = Instance.new("Part")
pole.Name = "Pole"
pole.Anchored = true
pole.Size = vector.create(0.2, 7, 0.2)
pole.Position = vector.create(-9, 3.5, 0)
pole.Parent = workspace

-- A grid is one metre across in x and z, facing up; the flag stands it on its
-- side against the pole, three metres by two.
local flag = Instance.new("MeshPart")
flag.Name = "Flag"
flag.MeshContent = "luaug://mesh/grid-64"
flag.Size = vector.create(3, 1, 2)
flag.CFrame = CFrame.new(-7.5, 5.8, 0) * CFrame.fromEuler(math.pi / 2, 0, 0)
flag.Material = Material.load("asset://materials/flag.material.json")
flag.Anchored = true
flag.CanCollide = false
flag.Parent = workspace

-- --- The dissolve -----------------------------------------------------------

local dissolveMaterial = Material.load("asset://materials/dissolve.material.json")
local blocks: { { material: Material, phase: number } } = {}
for index = 0, 2 do
    local block = Instance.new("Part")
    block.Name = `Dissolving{index}`
    block.Anchored = true
    block.Size = vector.create(2, 2, 2)
    block.Position = vector.create(-1 + index * 2.8, 1, -3)
    -- A clone each, so each block has a threshold of its own.
    local own = dissolveMaterial:Clone()
    block.Material = own
    block.Parent = workspace
    table.insert(blocks, { material = own, phase = index * 1.3 })
end

-- --- The glass --------------------------------------------------------------

local colours = { Color3.fromRGB(220, 70, 60), Color3.fromRGB(240, 200, 70), Color3.fromRGB(70, 150, 230) }
for index, colour in colours do
    local stripe = Instance.new("Part")
    stripe.Name = `Behind{index}`
    stripe.Anchored = true
    stripe.Size = vector.create(0.8, 3, 0.8)
    stripe.Position = vector.create(7 + (index - 2) * 1.4, 1.5, 0)
    stripe:SetMaterialParameter("Color", colour)
    stripe.Parent = workspace
end

local pane = Instance.new("Part")
pane.Name = "Pane"
pane.Anchored = true
pane.Size = vector.create(5, 3.5, 0.1)
pane.Position = vector.create(7, 1.75, 2.5)
pane.Material = Material.load("asset://materials/glass.material.json")
pane.Parent = workspace

-- --- Camera and time --------------------------------------------------------

local camera = Instance.new("Camera")
camera.FieldOfView = 55
camera.CFrame = CFrame.lookAt(vector.create(0, 5, 14), vector.create(0, 2, 0))
camera.Parent = workspace
workspace.CurrentCamera = camera

RunService.Heartbeat:Connect(function()
    local now = RunService.SimTime
    for _, block in blocks do
        -- In, hold, out, hold: a threshold from 0 to 1 and back, every six seconds.
        local cycle = (now + block.phase) % 6
        local threshold = math.clamp(if cycle < 3 then cycle / 2.4 else (6 - cycle) / 2.4, 0, 1)
        block.material:SetShaderParameter("Threshold", threshold)
    end
end)

print("23-surfaces: a flag, a dissolve and a pane of glass -- three surface shaders in content/shaders")
