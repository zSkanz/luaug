--!strict
--[[
	Two copies of one game, one of them deciding (N1).

	The same project runs in every posture -- the command line picks which:

	    run.bat --host                    a window, and the authority
	    run.bat --join=127.0.0.1          a window showing somebody else's world
	    run.bat --serve                   the authority with no window at all

	**The script asks one question, and it is a gameplay question**: do I decide
	this world? `NetworkService.Authority` is true solo too, so this file run
	with no flag at all builds and animates the world exactly as a host does --
	the branch is present and taken whether or not anybody is networked.

	A replica builds nothing. Everything under the workspace that replicates
	arrives from the authority, parts, names, colours and motion, and the
	replica's own copy of this script only points a camera at it.
]]

local Lighting = game:GetService("Lighting")
local NetworkService = game:GetService("NetworkService")
local RunService = game:GetService("RunService")
local Workspace = game:GetService("Workspace")

--- How many crates stand in the ring, and how far out.
local RingCount = 12
local RingRadius = 9

local camera = Instance.new("Camera")
camera.Name = "Camera"
camera.CFrame = CFrame.lookAt(vector.create(0, 14, 22), vector.create(0, 2, 0))
camera.Parent = Workspace
Workspace.CurrentCamera = camera
Lighting.ClockTime = 14

print(`multiplayer: running as {NetworkService.Topology.Name}, authority = {NetworkService.Authority}`)

if not NetworkService.Authority then
    -- The replica's whole job: show what it is sent.
    return
end

local ground = Instance.new("Part")
ground.Name = "Ground"
ground.Anchored = true
ground.Size = vector.create(40, 1, 40)
ground.Position = vector.create(0, -0.5, 0)
ground.Color = Color3.fromRGB(96, 110, 96)
ground.Parent = Workspace

local ring = Instance.new("Folder")
ring.Name = "Ring"
ring.Parent = Workspace
for index = 1, RingCount do
    local angle = (index / RingCount) * math.pi * 2
    local crate = Instance.new("Part")
    crate.Name = `Crate{index}`
    crate.Size = vector.create(1.5, 1.5, 1.5)
    crate.Position = vector.create(math.cos(angle) * RingRadius, 4 + index * 0.5, math.sin(angle) * RingRadius)
    crate.Color = Color3.fromHSV(index / RingCount, 0.7, 0.9)
    crate.Parent = ring
end

-- **Moved by the authority, every tick.** Anchored, so the solver leaves it to
-- this script; the replica sees it turn because its transform is in every
-- snapshot that changes it, and nothing else.
local spinner = Instance.new("Part")
spinner.Name = "Spinner"
spinner.Anchored = true
spinner.Size = vector.create(12, 0.6, 1)
spinner.Color = Color3.fromRGB(230, 170, 40)
spinner.Parent = Workspace

RunService.Heartbeat:Connect(function()
    local turn = RunService.SimTime * 0.8
    spinner.CFrame = CFrame.new(0, 1.2, 0) * CFrame.fromEuler(0, turn, 0)
end)
