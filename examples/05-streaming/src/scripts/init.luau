--!strict
--[[
	`examples/05-streaming` — the M7 deliverable.

	A world larger than the engine keeps resident, flown over by a camera that is
	the streaming focus. Nothing here builds the world: it is on disk as chunks,
	and what this script does is say WHERE to look and how far to keep loaded.

	The point of the example is what you cannot see. A world that streams
	correctly and one that pre-loaded everything look identical for the first
	minute; the difference is the memory ceiling and what happens at minute five.
	So the overlay is not decoration -- it is the only way a person can tell.
]]

local DebugService = game:GetService("DebugService")
local Lighting = game:GetService("Lighting")
local RunService = game:GetService("RunService")
local StreamingService = game:GetService("StreamingService")

-- A clear morning, so the terrain reads as terrain rather than as silhouettes.
Lighting.ClockTime = 9
Lighting.Ambient = Color3.fromRGB(90, 100, 120)
Lighting.Brightness = 2
Lighting.FogStart = 600
Lighting.FogEnd = 2600
Lighting.FogColor = Color3.fromRGB(170, 190, 215)

local camera = Instance.new("Camera")
camera.Name = "FlyCam"
camera.FieldOfView = 70
camera.Parent = workspace
workspace.CurrentCamera = camera

-- The focus is the CAMERA, which is what a fly-through wants. A game would
-- normally hand it the character instead, and the service takes either: what it
-- needs is something with a position, not something in particular.
StreamingService.LoadRadius = 1400
StreamingService.MinRadius = 500
StreamingService:AddFocus(camera)

-- A circuit rather than a straight line, so the flight leaves chunks behind and
-- comes back to them: eviction and re-entry are the halves a straight line
-- never exercises, and they are where hysteresis earns its keep.
local Radius = 1500
local Height = 210
local Period = 60

-- On the SIM clock and not on `PreRender`, and this is the deliverable's own
-- bug caught by its own differential: api-design.md 2.1 says `PreRender` never
-- fires in a headless run, so the flight simply did not happen there -- two
-- screenshots seventy metres apart came back byte-identical. The gate flies
-- this headless, so the motion has to be on a phase headless has.
--
-- `SimTime` rather than an accumulated `dt` for the same reason it exists: the
-- circuit is then a function of the tick index, so the same frame of the same
-- run is the same picture on every machine.
RunService.Heartbeat:Connect(function()
    local angle = (RunService.SimTime / Period) * math.pi * 2
    local position = vector.create(math.cos(angle) * Radius, Height, math.sin(angle) * Radius)
    -- Looking along the path rather than at the centre: a camera pointed at the
    -- middle of its own orbit sees the same hill for a minute.
    -- Barely pitched down. A camera that looks steeply at the ground sees one
    -- near tile filling half the frame and no horizon at all, which is a
    -- picture of a box rather than of a world.
    local ahead = vector.create(math.cos(angle + 0.35) * Radius, Height - 28, math.sin(angle + 0.35) * Radius)
    camera.CFrame = CFrame.lookAt(position, ahead)

    DebugService:SetCustomStat("StreamFocusX", position.x)
    DebugService:SetCustomStat("StreamFocusZ", position.z)
end)

-- The instrumentation the milestone owes. `Streaming` is a built-in panel name
-- (api-design.md §2.1) and these are the numbers that say whether it works.
DebugService.OverlayVisible = true
DebugService:ShowPanel("Streaming")
DebugService:ShowPanel("Stats")

print("05-streaming: flying a circuit; the world arrives as it is needed")
