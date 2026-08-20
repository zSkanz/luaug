--!strict

-- examples/01-instances — the M2 deliverable.
--
-- 500 scripted cubes in a real Instance tree, reparented while they run,
-- animated from `RunService.Heartbeat`, and annotated with debug draw. Every one
-- of those is something M1 could not do at all: M1 had a `luaug` global and no
-- world, and this file is what replaced it.
--
-- What it is meant to show, and why each part is here rather than in a test:
--
--   * **A tree, not a list.** The cubes live in five `Model`s -- one concentric
--     ring each -- and the models move, so ancestry and sibling order are being
--     exercised by something you can watch instead of something you assert.
--   * **Deferred signals.** `AncestryChanged` and the property signals fire
--     after the tick that raised them, and the ring's colour is driven by that
--     handler rather than by the code that moved it. If deferral broke, the
--     colours would lead the movement by a frame.
--   * **`task`.** The rebuild is a `task.delay` chain rather than a tick
--     counter, so the example depends on the scheduler being on the SimClock.
--   * **Debug draw.** The grid and the model bounds are gizmos: annotations
--     that no `GetChildren` can find, redrawn every frame because a gizmo lasts
--     exactly one.
--   * **A project, not a file.** This lives at `src/scripts/init.luau` and
--     pulls its geometry from `@shared/ring`, so the mount rules and the
--     `.luaurc` alias resolution of §4 are part of what runs rather than
--     something only a test sees.
--
-- The camera is still fixed (M4 owns cameras), so the scene is built to be
-- legible from where it is: five flat concentric rings rather than a cube of
-- cubes, at a size where 500 objects read as 500 objects.

local Ring = require("@shared/ring")

local DebugService = game:GetService("DebugService")
local RunService = game:GetService("RunService")

local ModelCount = 5
local PerModel = 100
local CubeSize = Vector3.new(0.13, 0.13, 0.13)

-- One colour per model, so which cubes belong together is visible without
-- needing labels the engine cannot draw yet.
local ModelColors = {
    Color3.fromRGB(255, 92, 92),
    Color3.fromRGB(255, 176, 59),
    Color3.fromRGB(120, 220, 120),
    Color3.fromRGB(96, 172, 255),
    Color3.fromRGB(214, 128, 255),
}

local Root = Instance.new("Folder")
Root.Name = "Ring"
Root.Parent = workspace

-- The parking bay. Reparenting to `nil` would work too, and would prove less:
-- moving between two live parents is the case that has to keep `GetChildren`
-- and the name chain consistent on both sides.
local Bench = Instance.new("Folder")
Bench.Name = "Bench"
Bench.Parent = workspace

local Models: { Model } = {}
local Cubes: { Part } = table.create(ModelCount * PerModel)

for modelIndex = 1, ModelCount do
    local model = Instance.new("Model")
    model.Name = `Arc{modelIndex}`
    model.Parent = Root
    Models[modelIndex] = model

    for index = 1, PerModel do
        local cube = Instance.new("Part")
        cube.Name = `Cube{index}`
        cube.Size = CubeSize
        cube.Color = ModelColors[modelIndex]
        -- Anchored, from M5 on. These cubes are moved by the script below, not
        -- by the simulation, and an unanchored part is a rigid body that falls
        -- out from under whatever the script was doing to it.
        cube.Anchored = true
        cube.Parent = model
        table.insert(Cubes, cube)
    end
end

-- Which model is currently parked, and how bright the ring should be. The
-- brightness is written by the AncestryChanged handler below, never by the code
-- that does the reparenting -- so what you see is the deferred queue draining.
local ParkedModel: number? = nil
local Flash = 0.0

for modelIndex, model in Models do
    model.AncestryChanged:Connect(function(_child: Instance, parent: Instance?)
        -- Fires once per model, not once per cube: `AncestryChanged` is raised
        -- on the instance whose ancestry changed and on its descendants, and a
        -- handler connected to the model sees only the model's own.
        Flash = if parent == Bench then 1.0 else 0.35
        ParkedModel = if parent == Bench then modelIndex else nil
    end)
end

local function park(modelIndex: number)
    local model = Models[modelIndex]
    model.Parent = if model.Parent == Bench then Root else Bench

    -- A chain rather than a loop with a counter: each hop schedules the next,
    -- so the example stops if the scheduler does. `task.delay` is in seconds of
    -- SimClock time, which at the default timestep is exactly 42 ticks.
    task.delay(0.7, park, modelIndex % ModelCount + 1)
end

task.delay(0.7, park, 1)

local Phase = 0.0

RunService.Heartbeat:Connect(function(deltaTime: number)
    Phase += deltaTime
    Flash = math.max(0.35, Flash - deltaTime * 1.6)

    -- Only the cubes still on the ring are placed. A parked model keeps the
    -- positions it had, which is what makes the missing ring readable as "those
    -- went somewhere" rather than as "those stopped existing".
    --
    -- Placed from `GetChildren` rather than from the flat `Cubes` list on
    -- purpose: this walks the tree every tick, so sibling order and ancestry are
    -- being exercised by the thing you are looking at.
    for modelIndex, model in Models do
        if model.Parent ~= Root then
            continue
        end

        local children = model:GetChildren()
        for index, cube in children do
            local part = cube :: Part
            part.Position = Ring.slotPosition(modelIndex, index, #children, Phase)
            part.Color = ModelColors[modelIndex]:Lerp(Color3.new(1, 1, 1), Flash - 0.35)
        end
    end

    -- The ground plane, as a gizmo grid. Nine lines each way: enough to read as
    -- a floor, few enough that the cubes stay the subject.
    local extent = 6.0
    local step = extent / 4
    for i = -4, 4 do
        local offset = i * step
        local shade = if i == 0 then Color3.new(0.55, 0.55, 0.6) else Color3.new(0.22, 0.22, 0.26)
        DebugService:DrawLine(Vector3.new(-extent, -1.2, offset), Vector3.new(extent, -1.2, offset), shade)
        DebugService:DrawLine(Vector3.new(offset, -1.2, -extent), Vector3.new(offset, -1.2, extent), shade)
    end

    -- A box around whichever model is on the bench, so the reparenting is not
    -- something you have to infer from a gap. Drawn from `GetChildren`, which
    -- means it is drawn from the tree rather than from the bookkeeping -- if a
    -- reparent left the tree inconsistent, this box would be the wrong size.
    if ParkedModel ~= nil then
        local model = Models[ParkedModel]
        local children = model:GetChildren()
        if #children > 0 then
            local lowest = Vector3.new(math.huge, math.huge, math.huge)
            local highest = Vector3.new(-math.huge, -math.huge, -math.huge)
            for _, child in children do
                local position = (child :: Part).Position
                lowest = Vector3.new(
                    math.min(lowest.x, position.x),
                    math.min(lowest.y, position.y),
                    math.min(lowest.z, position.z)
                )
                highest = Vector3.new(
                    math.max(highest.x, position.x),
                    math.max(highest.y, position.y),
                    math.max(highest.z, position.z)
                )
            end
            local center = (lowest + highest) * 0.5
            local half = (highest - lowest) * 0.5 + Vector3.new(0.2, 0.2, 0.2)
            DebugService:DrawBox(CFrame.new(center), half * 2, Color3.fromRGB(255, 255, 160))
        end
    end
end)
