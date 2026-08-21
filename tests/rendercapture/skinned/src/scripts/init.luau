--!strict

-- The M6 skinned-render gate: one animated mesh, four consecutive frames.
--
-- What it proves that nothing else can: the skin stream reaches the GPU as a
-- second vertex buffer, the skinned pipeline is bound for a draw that has a
-- palette, and the palette itself is uploaded -- and CHANGES from frame to
-- frame, because the capture backend records a uniform block's contents rather
-- than its size (D026's other half). A pose that never moved would be four
-- identical `bindUniforms` lines, and this golden would say so.
--
-- The rig is `skinned_bar.gltf`: two joints, six vertices in three rings, and a
-- one-second "Bend" clip that rotates the tip. Small enough that a person can
-- read the palette in the golden and tell whether it is right.
--
-- Every value is a literal at a named tick, for the reason the meshes gate
-- gives at length: an orbit driven by elapsed time makes the golden depend on
-- how many ticks had passed when it was recorded.

local Lighting = game:GetService("Lighting")
local PhysicsService = game:GetService("PhysicsService")
local RunService = game:GetService("RunService")

local bar = Instance.new("MeshPart")
bar.Name = "Bar"
bar.MeshContent = "asset://models/bar.gltf" :: Content
bar.Anchored = true
bar.Parent = workspace

-- A second copy in bind pose, with no player under it at all. It is what makes
-- the gate able to tell "the skinned path ran" from "everything went through
-- the skinned path": this one has to keep the STATIC pipeline and one vertex
-- buffer, and the golden records both draws.
local still = Instance.new("MeshPart")
still.Name = "Still"
still.MeshContent = "asset://models/bar.gltf" :: Content
still.CFrame = CFrame.new(2, 0, 0)
still.Anchored = true
still.Parent = workspace

local player = Instance.new("AnimationPlayer")
player.Parent = bar

local camera = Instance.new("Camera")
camera.FieldOfView = 55
camera.NearPlane = 0.1
camera.FarPlane = 100
camera.CFrame = CFrame.lookAt(Vector3.new(3, 2, 4), Vector3.new(0.8, 0.8, 0))
camera.Parent = workspace
workspace.CurrentCamera = camera

Lighting.ClockTime = 10.5

-- The mesh is loaded by the render loop, which runs after the first tick -- so
-- a track loaded at tick 0 would find no skeleton and play nothing. The gate
-- waits a tick and then loads, which is also what a real game does: an
-- `AnimationPlayer` under a `MeshPart` whose file is still being read is the
-- ordinary case, not the exception.
local track: AnimationTrack? = nil

local tick = 0
RunService.PreSimulation:Connect(function()
    tick += 1
    if tick == 1 then
        track = player:LoadAnimation("Bend")
        local loaded = track :: AnimationTrack
        loaded.Looped = true
        loaded:Play()
    end
end)

-- Recorded so the golden's own header says what it is looking at. `Length` is
-- zero for a clip that was not found, which is the failure this line makes
-- visible in the log rather than as four identical palettes.
RunService.Heartbeat:Connect(function()
    if tick == 2 and track ~= nil then
        print(`skinned gate: clip length {(track :: AnimationTrack).Length}s at {PhysicsService.FixedTimestep}s/tick`)
    end
end)
