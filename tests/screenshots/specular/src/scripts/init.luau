--!strict

-- The evidence scene for GEOMETRIC SPECULAR ANTIALIASING (M7.5 follow-up).
--
-- M7.5 gave surfaces a specular reflection of an environment, and specular
-- reflection is the one term that produces detail SMALLER THAN A PIXEL. A
-- polished sphere turns its normal through a hemisphere across its silhouette,
-- so the further away it is the more directions each pixel covers -- and past
-- some distance the GGX lobe is narrower than the pixel it lands in. The
-- highlight then has nowhere stable to sit: the camera moves a fraction of a
-- pixel and it crawls.
--
-- **`fxaa.hlsl` cannot reach this and no threshold on it will.** It looks for a
-- contrast step along an EDGE; this is a point flickering in the middle of a
-- smooth surface with no edge under it. The fix is in the shading
-- (`antiAliasedAlpha` in `luaug_brdf.hlsli`), and this scene is how it is
-- judged.
--
-- Eight balls receding, so ONE frame contains the whole range from "comfortably
-- larger than the lobe" to "far smaller". The near balls are the control: if
-- they lose their reflection, the correction is too strong and has become the
-- global roughness raise it exists to avoid.
--
-- **One frame per run**, like `tests/screenshots/daystrip`: the camera is a
-- function of the tick count, so `--frames=N` selects the frame and the
-- screenshot the host writes at exit is that frame's. A shimmer is a difference
-- BETWEEN frames, so the harness renders a run of consecutive N and differences
-- them.

local Lighting = game:GetService("Lighting")
local RunService = game:GetService("RunService")

--- How far the camera slides sideways per frame, in metres.
---
--- Deliberately SUB-PIXEL. At the far end of the row a ball is about nine
--- pixels across and this moves the image about a fifth of a pixel a frame,
--- which is the regime specular aliasing lives in -- a large motion would
--- resample the highlight somewhere else entirely and hide the crawl inside
--- legitimate change.
local MetresPerFrame = 0.004

local balls = {}
for index = 0, 7 do
    local ball = Instance.new("MeshPart")
    ball.Name = string.format("Ball%d", index)
    ball.MeshContent = "asset://models/ball.gltf" :: Content
    -- An arc rather than a line, so no ball hides behind the one in front of it
    -- and each is seen against the sky rather than against its neighbour.
    ball.CFrame = CFrame.new(3.0 * math.sin(index * 0.9), 1.0, -6.0 * index)
    ball.Anchored = true
    ball.Parent = workspace
    balls[index + 1] = ball
end

local floor = Instance.new("Part")
floor.Name = "Floor"
floor.Size = Vector3.new(80, 0.5, 120)
floor.CFrame = CFrame.new(0, 0.35, -21)
floor.Color = Color3.fromRGB(38, 42, 50)
floor.Anchored = true
floor.Parent = workspace

local camera = Instance.new("Camera")
camera.FieldOfView = 40
camera.NearPlane = 0.1
camera.FarPlane = 400
camera.Parent = workspace
workspace.CurrentCamera = camera

-- Fixed at mid-morning: the sun sits high enough to put its disc in the top of
-- every ball and low enough to cast a shadow that says where the ground is. The
-- clock does not advance, because a moving sun would be a second variable in a
-- test whose whole measurement is the difference between two frames.
Lighting.ClockTime = 9.5
Lighting.GeographicLatitude = 20
Lighting.Ambient = Color3.fromRGB(8, 9, 12)
Lighting.Brightness = 2.6
Lighting.FogColor = Color3.fromRGB(150, 178, 214)
Lighting.FogStart = 90
Lighting.FogEnd = 400

local tick = 0

local function apply()
    local slide = tick * MetresPerFrame
    camera.CFrame = CFrame.lookAt(Vector3.new(slide, 2.4, 7.0), Vector3.new(slide, 1.0, -24.0))
end

-- On the boot drain, so a one-frame run renders frame zero rather than the
-- default camera.
apply()

RunService.Heartbeat:Connect(function()
    tick += 1
    apply()
end)
