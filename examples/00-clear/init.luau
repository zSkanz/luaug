--!strict

-- examples/00-clear — the M1 deliverable, now driven through the M2 kernel.
--
-- M1 drove the frame through a temporary `luaug` global. That global is gone:
-- this is real Instances in a real world, animated from `RunService.Heartbeat`,
-- and the picture is the same picture. Keeping it the same is the point — the
-- render goldens were recorded from this file, so an unchanged image across a
-- total rewrite of everything underneath it is the strongest evidence the
-- rewrite kept working.
--
-- The background pulse is the engine's, not this script's. There is no
-- `Lighting` or `Camera` in the M2 surface (both are M4), so a clear colour is
-- not something a script can set yet — and inventing a way would be inventing
-- M4's seams in a hurry.

local OrbitRadius = 2.5
local CubeSize = Vector3.new(0.8, 0.8, 0.8)
local ThirdTurn = 2 * math.pi / 3

-- Fixed and saturated rather than derived from the animation. A colour that
-- follows the same wave as the background eventually matches it, and a cube you
-- cannot see is worse than no cube at all.
local CubeColors = {
    Color3.new(1.0, 0.15, 0.15),
    Color3.new(0.15, 0.4, 1.0),
    Color3.new(1.0, 1.0, 1.0),
}

local DebugService = game:GetService("DebugService")
local RunService = game:GetService("RunService")

local cubes: { Part } = {}
for index, color in CubeColors do
    local cube = Instance.new("Part")
    cube.Name = "Cube" .. index
    cube.Size = CubeSize
    cube.Color = color
    cube.Parent = workspace
    cubes[index] = cube
end

-- Where the cubes are. Called once at file scope and then every tick: an entry
-- script runs in the boot drain, which is *before* the first frame (§3), and
-- cubes that waited for their first Heartbeat would spend that frame stacked at
-- the origin.
local function place()
    -- Driven by the simulation clock, never by wall-clock time. A headless
    -- capture has to produce the same pixels on any machine at any speed, which
    -- is the property the whole golden-image gate rests on.
    local elapsed = RunService.SimTime

    for index, cube in cubes do
        local phase = elapsed + (index - 1) * ThirdTurn
        cube.Position = Vector3.new(OrbitRadius * math.cos(phase), 0, OrbitRadius * math.sin(phase))
    end
end

place()

RunService.Heartbeat:Connect(function()
    place()

    -- A world triad, so a wrong view or projection matrix is obvious rather
    -- than merely suspicious. Drawn as gizmos rather than as parts because it is
    -- an annotation and not an object: nothing should be able to find it with
    -- `workspace:GetChildren()`.
    --
    -- Deliberately NOT called at file scope beside `place`. A gizmo lasts one
    -- frame, and the boot drain is not a frame -- so one drawn there would be
    -- discarded, and calling it would suggest otherwise. The consequence is
    -- visible and correct: the very first frame of a run shows the world but
    -- not its annotations, because no tick has happened yet.
    DebugService:DrawLine(Vector3.zero, Vector3.new(1, 0, 0), Color3.new(1, 0, 0))
    DebugService:DrawLine(Vector3.zero, Vector3.new(0, 1, 0), Color3.new(0, 1, 0))
    DebugService:DrawLine(Vector3.zero, Vector3.new(0, 0, 1), Color3.new(0, 0, 1))
end)
