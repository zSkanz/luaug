--!strict
--[[
	The first LuauG project whose world is DATA (ADR 0047), and the first that
	STREAMS one (ADR 0053).

	Every example before this one builds its world in code: `init.luau` runs at
	boot and calls `Instance.new` until there is something to look at. This one
	does not. Its world is `main.scene.json`, the editor authors that file, and
	this script is only what the world DOES.

	**And there is no generator anywhere in this project.** The scene holds four
	hundred parts spread over six cells of the streaming grid, and nothing sorted
	them into cells: the engine partitions the scene on the way to the first
	frame, cached by a hash of the file, so the world you build in the editor is
	the world that streams.

	What that costs a script is one sentence, and it is the only one worth
	learning: **a path into `Workspace` may be nil**, because the thing it names
	may not have arrived. Ask `TagService` instead. It answers for what is here
	now, and its added and removed signals are exactly what fires as cells come
	and go.

	**Open it with `luaug edit examples/06-scene`**, move something, press play,
	press stop, press save. Then close the editor and open it again.
]]

local RunService = game:GetService("RunService")
local StreamingService = game:GetService("StreamingService")
local TagService = game:GetService("TagService")
local Workspace = game:GetService("Workspace")

--- How fast the marked parts turn, in radians per second. A behaviour, not a
--- world: which parts turn is the scene's business and this script only asks
--- for the ones wearing the tag.
local SpinRate = 1.2

--- How far the camera walks, and how fast. Long enough to leave the first cell
--- and come back to it, which is the whole thing worth watching here.
local PatrolMetres = 700
local PatrolSpeed = 26

local camera = Workspace.CurrentCamera
if camera == nil then
    return
end

-- **The world streams around a focus, and this one is the camera.** Without a
-- focus nothing is wanted and nothing is loaded, which is a legal world and a
-- very empty one.
StreamingService:AddFocus(camera)

-- **Three radii, because a pillar and a hillside are not the same question.**
-- The base pair is the DETAIL class -- the pillars, everything small -- and the
-- two below stretch the horizon for what is larger. A zero would mean "follow
-- the base pair", which is what every class starts at.
StreamingService.MinRadius = 140
StreamingService.LoadRadius = 220
StreamingService.StructureLoadRadius = 420
StreamingService.TerrainLoadRadius = 760

-- Something arrived. The signal is `TagService`'s rather than a path watch, and
-- that is the point: a streamed world adds and removes instances as you move,
-- and this is how a script hears about it.
TagService:GetInstanceAddedSignal("Landmark"):Connect(function(instance: Instance)
    print(`06-scene: {instance.Name} came into range.`)
end)

local travelled = 0
local heading = 1

RunService.Heartbeat:Connect(function(dt: number)
    for _, instance in TagService:GetTagged("Spin") do
        if instance:IsA("BasePart") then
            local part = instance :: BasePart
            part.CFrame = part.CFrame * CFrame.fromEuler(0, SpinRate * dt, 0)
        end
    end

    -- A patrol rather than a controller, because this example is about what the
    -- world does and not about how you move through it.
    local step = PatrolSpeed * dt * heading
    travelled += step
    if travelled > PatrolMetres then
        heading = -1
    elseif travelled < 0 then
        heading = 1
    end
    -- Composed rather than added, because `CFrame` has no `+`: a frame times a
    -- frame is what moves one along its own axes, and this camera's own x is
    -- the world's, since it is pitched and never turned.
    camera.CFrame = camera.CFrame * CFrame.new(step, 0, 0)
end)

print("06-scene: the world came from a file, and the file came apart into cells.")
