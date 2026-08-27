--!strict
--- What sculpting costs, measured rather than estimated (F1).
---
--- **This bench should have been the milestone's first commit and was its last.**
--- The plan said so in as many words -- "the first commit of each milestone is
--- the bench, not the feature" -- and skipping it cost the owner an editor that
--- lagged on every stroke, found by using it rather than by measuring it. What
--- the first run reported, before any of it was fixed:
---
---   * generating a 128 m square:      285 ms
---   * one `FillBall` at radius 8:     2.5 ms
---   * meshing one tile:              10.4 ms, twice a frame
---
--- Three causes, all of them the same mistake in different places: doing work
--- per column that only needed doing per tile, and per sample that only needed
--- doing per column. `docs/perf-baselines.md` carries what it costs now.
---
--- **A stroke a tick, which is what a person does.** A drag at sixty hertz with
--- the default spacing stamps every quarter of a brush width, so a bench that
--- stamped once and then idled would measure the mesher settling rather than the
--- cost of sculpting. This keeps the brush moving for the whole run.
---
--- **The radius varies, because the cost does.** A stamp is quadratic in radius
--- through the columns it touches and linear through the depth it examines, so
--- one radius is one point on a curve. Two, four and eight metres is the range a
--- person actually uses.
---
--- The 16 ms budget is a whole frame and a catastrophe detector, like every other
--- scene here. The number that matters is the one recorded in the baselines.

local RunService = game:GetService("RunService")
local Workspace = game:GetService("Workspace")

local Rng = Random.new(20260827)

local Rock = 1
local Sand = 2

local terrain = Instance.new("Terrain")
terrain.Name = "Ground"
terrain.MinHeight = -64
terrain.MaxHeight = 64
terrain.Parent = Workspace

-- A hundred and twenty-eight metres of ground, which is the size the editor's
-- own generator offers by default.
terrain:FillBlock(vector.create(0, -70, 0), vector.create(128, 140, 128), Rock)

--- The brush's own radii, cycled so no single one dominates the average.
local Radii = { 2.0, 4.0, 8.0 }

local tick = 0
RunService.Heartbeat:Connect(function()
    tick += 1

    -- A stroke that wanders, so the cost is not one tile's worth of cache.
    local angle = tick * 0.11
    local reach = 24.0 + 16.0 * math.sin(tick * 0.037)
    local x = math.cos(angle) * reach
    local z = math.sin(angle) * reach
    local radius = Radii[(tick % #Radii) + 1]

    if tick % 7 == 0 then
        -- Digging, which is the op that promotes columns to voxels and is
        -- therefore the expensive one.
        terrain:FillBall(vector.create(x, Rng:NextNumber(-3, 1), z), radius, 0)
    elseif tick % 7 == 3 then
        terrain:PaintBall(vector.create(x, 0, z), radius, Sand)
    else
        terrain:FillBall(vector.create(x, Rng:NextNumber(-1, 3), z), radius, Rock)
    end
end)
