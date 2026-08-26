--!strict

-- The evidence scene for LOCAL SHADOWS: does a spot light actually occlude?
--
-- **This exists because the honest question about `SpotLight.Shadows` and
-- `PointLight.Shadows` is not "did the atlas get allocated".** Both properties
-- were stored, plumbed to `RenderLight::shadows` and read by no pass for three
-- milestones, and every check short of looking at two pictures would have passed
-- throughout. So the gate is a differential: the same frame with the property on
-- and with it off, and the two must not be the same image.
--
-- **The sun is deliberately off.** `Lighting.Brightness` is zero and the ambient
-- is almost nothing, so every lit pixel in the frame comes from the local lights
-- and nothing the cascades do can account for a difference between the two runs.
--
-- The frame number selects which light casts, which is what lets one scene
-- answer for both kinds:
--
--   `--frames=1`  the spot casts, the point does not
--   `--frames=2`  the point casts, the spot does not
--   `--frames=3`  neither casts -- the reference the other two are read against

local Lighting = game:GetService("Lighting")
local RunService = game:GetService("RunService")

local floor = Instance.new("Part")
floor.Name = "Floor"
floor.Size = Vector3.new(30, 1, 30)
floor.CFrame = CFrame.new(0, -0.5, 0)
floor.Color = Color3.fromRGB(180, 178, 172)
floor.Anchored = true
floor.Parent = workspace

-- **Between the light and the floor, and nothing else is.** A blocker that only
-- half occludes would make the difference between the two runs a matter of
-- degree; this one puts a whole shadow on the floor or none at all.
local blocker = Instance.new("Part")
blocker.Name = "Blocker"
blocker.Size = Vector3.new(3, 0.4, 3)
blocker.CFrame = CFrame.new(-3, 3.4, 0)
blocker.Color = Color3.fromRGB(90, 92, 104)
blocker.Anchored = true
blocker.Parent = workspace

-- A second blocker on the other side, for the point light. Point and spot are
-- separated in space so one picture can carry both without either shadow
-- landing on the other.
local pointBlocker = Instance.new("Part")
pointBlocker.Name = "PointBlocker"
pointBlocker.Size = Vector3.new(2.4, 0.4, 2.4)
pointBlocker.CFrame = CFrame.new(4, 3.0, 0)
pointBlocker.Color = Color3.fromRGB(104, 96, 88)
pointBlocker.Anchored = true
pointBlocker.Parent = workspace

local spotHost = Instance.new("Part")
spotHost.Name = "SpotHost"
spotHost.Size = Vector3.new(0.2, 0.2, 0.2)
-- **Aimed, not placed.** A `SpotLight` shines along its host part's -Z, so a
-- host with an identity frame points horizontally and lights nothing below it --
-- which is how the first version of this scene produced two identical pictures
-- and looked like a feature that did not work.
spotHost.CFrame = CFrame.lookAt(Vector3.new(-3, 8, 0), Vector3.new(-3, 0, 0))
spotHost.Transparency = 1
spotHost.Anchored = true
spotHost.Parent = workspace

local spot = Instance.new("SpotLight")
spot.Name = "Spot"
spot.Brightness = 40
spot.Range = 24
spot.Angle = 55
spot.Color = Color3.fromRGB(255, 246, 224)
spot.Parent = spotHost

local pointHost = Instance.new("Part")
pointHost.Name = "PointHost"
pointHost.Size = Vector3.new(0.2, 0.2, 0.2)
pointHost.CFrame = CFrame.new(4, 7, 0)
pointHost.Transparency = 1
pointHost.Anchored = true
pointHost.Parent = workspace

local pointLight = Instance.new("PointLight")
pointLight.Name = "Point"
pointLight.Brightness = 40
pointLight.Range = 22
pointLight.Color = Color3.fromRGB(224, 236, 255)
pointLight.Parent = pointHost

local camera = Instance.new("Camera")
camera.FieldOfView = 55
camera.NearPlane = 0.05
camera.FarPlane = 200
camera.CFrame = CFrame.lookAt(Vector3.new(0.5, 9.5, 14), Vector3.new(0, 0.5, 0))
camera.Parent = workspace
workspace.CurrentCamera = camera

-- **No sun at all.** Every lit pixel comes from the two lights above, so a
-- difference between two runs of this scene cannot be the cascades.
Lighting.Brightness = 0
Lighting.Ambient = Color3.fromRGB(6, 6, 8)
Lighting.FogStart = 400
Lighting.FogEnd = 800

-- Applied on the boot drain as well as on every tick, so a one-frame run gets
-- frame one rather than the defaults.
local function applyCase()
    local frame = math.floor(RunService.SimTime * 60 + 0.5) + 1
    spot.Shadows = frame == 1
    pointLight.Shadows = frame == 2
end

applyCase()
RunService.Heartbeat:Connect(applyCase)
