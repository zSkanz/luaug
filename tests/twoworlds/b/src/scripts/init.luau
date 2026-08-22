--!strict

-- World B of the editor-seam proof (`engine/app/src/two_worlds.cpp`).
--
-- Deliberately unlike A in every axis a frame can carry: a different primitive,
-- a different arrangement, a camera on the other side, a different hour. See
-- `a/src/scripts/init.luau` for why the two are separate projects.

local Lighting = game:GetService("Lighting")

for index = 0, 7 do
    local sphere = Instance.new("Part")
    sphere.Name = string.format("SphereB%d", index)
    sphere.Shape = Enum.PartShape.Ball
    sphere.Size = Vector3.new(2.2, 2.2, 2.2)
    -- A ring, so the silhouette is nothing A's row of blocks could produce even
    -- if the two scenes were somehow blended.
    sphere.CFrame = CFrame.new(math.cos(index * 0.785) * 5.0, 1.4, math.sin(index * 0.785) * 5.0)
    sphere.Color = Color3.fromRGB(48, 120, 220)
    sphere.Anchored = true
    sphere.Parent = workspace
end

local floor = Instance.new("Part")
floor.Name = "FloorB"
floor.Size = Vector3.new(40, 0.5, 40)
floor.CFrame = CFrame.new(0, -0.25, 0)
floor.Color = Color3.fromRGB(40, 52, 68)
floor.Anchored = true
floor.Parent = workspace

local camera = Instance.new("Camera")
camera.FieldOfView = 70
camera.NearPlane = 0.1
camera.FarPlane = 200
camera.CFrame = CFrame.lookAt(Vector3.new(10, 7, -9), Vector3.new(0, 1.0, 0))
camera.Parent = workspace
workspace.CurrentCamera = camera

Lighting.ClockTime = 8.0
Lighting.GeographicLatitude = 55
Lighting.Ambient = Color3.fromRGB(12, 16, 22)
Lighting.Brightness = 3.1
