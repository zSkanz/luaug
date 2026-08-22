--!strict

-- The evidence scene for CONTACT SHADOWS (D051).
--
-- A human sent a close-up of the flagship's character and a boulder, both with a
-- band of lit ground between the object and where its shadow begins, so both
-- looked like they were hovering a few centimetres above the floor. That is
-- peter-panning, and it is the price of every technique that keeps shadow acne
-- away: each of them pushes the comparison, and pushing it detaches the contact.
--
-- **This scene exists because the artifact is only visible up close.** The same
-- objects at `tests/screenshots/specular`'s framing show a gap of about one
-- pixel, which is why it survived a milestone whose gate looked at that scene.
-- Here the camera is three metres away and the sun is low enough that a
-- centimetre of detachment is a centimetre of bright floor.
--
-- One frame per run, like `daystrip` and `specular`, and the FRAME NUMBER
-- selects the hour: `--frames=N` puts the sun at `6 + (N-1)/4` o'clock. That is
-- what makes one scene cover the whole range the artifact depends on -- a
-- detached contact is worst at a grazing sun, and so is the acne that every cure
-- for it risks bringing back.

local Lighting = game:GetService("Lighting")
local RunService = game:GetService("RunService")

local floor = Instance.new("Part")
floor.Name = "Floor"
floor.Size = Vector3.new(40, 1, 40)
floor.CFrame = CFrame.new(0, -0.5, 0)
floor.Color = Color3.fromRGB(96, 104, 92)
floor.Anchored = true
floor.Parent = workspace

-- A capsule and a ball, which is exactly what the report showed. Both sit ON the
-- floor: the capsule's centre is half its height up, the ball's is its radius.
local capsule = Instance.new("Part")
capsule.Name = "Capsule"
capsule.Shape = Enum.PartShape.Capsule
capsule.Size = Vector3.new(1.4, 3.2, 1.4)
capsule.CFrame = CFrame.new(-1.6, 1.6, 0)
capsule.Color = Color3.fromRGB(224, 208, 170)
capsule.Anchored = true
capsule.Parent = workspace

local ball = Instance.new("Part")
ball.Name = "Ball"
ball.Shape = Enum.PartShape.Ball
ball.Size = Vector3.new(2.4, 2.4, 2.4)
ball.CFrame = CFrame.new(1.8, 1.2, -1.2)
ball.Color = Color3.fromRGB(120, 122, 148)
ball.Anchored = true
ball.Parent = workspace

-- A box, because a flat-bottomed contact is the harshest case: a sphere touches
-- at one point and a box touches along an edge, so any detachment shows as a
-- bright line the whole width of it.
local block = Instance.new("Part")
block.Name = "Block"
block.Size = Vector3.new(2, 1.6, 2)
block.CFrame = CFrame.new(0.4, 0.8, 3)
block.Color = Color3.fromRGB(150, 132, 110)
block.Anchored = true
block.Parent = workspace

local camera = Instance.new("Camera")
camera.FieldOfView = 45
camera.NearPlane = 0.05
camera.FarPlane = 200
camera.CFrame = CFrame.lookAt(Vector3.new(-2.2, 2.6, 7.4), Vector3.new(0, 0.9, 0))
camera.Parent = workspace
workspace.CurrentCamera = camera

-- Mid-morning: high enough that each object's shadow is beside it rather than
-- stretched to the horizon, and low enough that a centimetre of detachment is a
-- centimetre of bright floor.
Lighting.GeographicLatitude = 20
Lighting.Ambient = Color3.fromRGB(18, 20, 26)
Lighting.Brightness = 2.4
Lighting.FogStart = 120
Lighting.FogEnd = 400

-- A quarter of an hour per tick, from six in the morning. Applied on the boot
-- drain as well as on every tick, so a one-frame run renders hour six rather
-- than the default.
local function applyClock()
    Lighting.ClockTime = 6 + math.floor(RunService.SimTime * 60 + 0.5) * 0.25
end

applyClock()
RunService.Heartbeat:Connect(applyClock)
