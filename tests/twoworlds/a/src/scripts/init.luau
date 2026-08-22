--!strict

-- World A of the editor-seam proof (`engine/app/src/two_worlds.cpp`).
--
-- Two projects rather than one project run twice with different seeds: a world
-- that differs only in its seed would make "the two images differ" depend on
-- the scripts having read the seed, and then the proof would be about
-- `math.random` rather than about isolation.
--
-- What matters about this scene is that it is UNMISTAKABLE. A warm scene of
-- boxes seen from the left; B is a cold scene of a different shape seen from
-- the right. If any part of B reaches this world, it does not look like a
-- rounding difference -- it looks wrong.

local Lighting = game:GetService("Lighting")

for index = 0, 5 do
    local block = Instance.new("Part")
    block.Name = string.format("BlockA%d", index)
    block.Shape = Enum.PartShape.Block
    block.Size = Vector3.new(1.6, 1.6 + index * 0.6, 1.6)
    block.CFrame = CFrame.new(-4.5 + index * 1.8, 0.8 + index * 0.3, 0)
    block.Color = Color3.fromRGB(214, 96, 42)
    block.Anchored = true
    block.Parent = workspace
end

local floor = Instance.new("Part")
floor.Name = "FloorA"
floor.Size = Vector3.new(40, 0.5, 40)
floor.CFrame = CFrame.new(0, -0.25, 0)
floor.Color = Color3.fromRGB(72, 60, 48)
floor.Anchored = true
floor.Parent = workspace

local camera = Instance.new("Camera")
camera.FieldOfView = 55
camera.NearPlane = 0.1
camera.FarPlane = 200
camera.CFrame = CFrame.lookAt(Vector3.new(-9, 5, 11), Vector3.new(0, 1.5, 0))
camera.Parent = workspace
workspace.CurrentCamera = camera

-- Late afternoon, so the sun is low and the blocks throw long shadows across
-- the floor. B is set to morning; the two skies alone would separate the
-- images, and the shadows make the separation structural rather than a tint.
Lighting.ClockTime = 16.5
Lighting.GeographicLatitude = 20
Lighting.Ambient = Color3.fromRGB(18, 14, 12)
Lighting.Brightness = 2.6
