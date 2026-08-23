--!strict
local lamp = Instance.new("Part")
lamp.Size = vector.create(0.3, 0.3, 0.3)
lamp.CFrame = CFrame.new(-2.2, 3.2, 2.4)
lamp.Anchored = true
lamp.Parent = workspace

-- A light takes its position from the nearest BasePart above it.
local light = Instance.new("PointLight")
light.Color = Color3.fromRGB(255, 176, 92)
light.Brightness = 14
light.Range = 9
light.Parent = lamp
