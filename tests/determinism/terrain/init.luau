--!strict
--- Terrain under R10 (F1's declared verification).
---
--- **What this catches that nothing else does.** A terrain field is two sorted
--- vectors of tiles and bricks, and the whole reason it is sorted vectors rather
--- than hash maps is R10: a hash map's iteration order is a fact about its
--- allocator, and the world hash walks that order. Every claim below rests on
--- it, and none of them can be asserted from a single process:
---
--- * the field's digest is over tiles, bricks and their KEYS, walked in order;
--- * which columns carry voxels is part of the world's state, because promotion
---   is decided by examining the resulting field and is therefore hashed;
--- * the collider rebuild is budgeted by a COUNT per tick and never by a clock,
---   so which tiles are current on tick N is a property of the operation
---   sequence rather than of how fast the machine was;
--- * and a body resting on ground built by one order of edits has to rest in
---   exactly the same place as one resting on ground built by the same order on
---   another run.
---
--- **The edits are driven by seeded randomness, on purpose.** A fixed script of
--- twenty balls would exercise twenty shapes; a seeded walk exercises whatever
--- the seed says, including the cases nobody would think to write -- a ball that
--- straddles a tile boundary, one that reaches the floor, one that hollows a
--- column that a previous ball had just promoted. `Random.new(seed)` is the
--- engine's own generator and is part of the recorded state.
---
--- `sameBuildOnly`, like `character` and `ragdoll`: this settles rigid bodies
--- onto a sampled surface, so the last-ULP disagreement between two libms
--- survives into a position instead of being rounded away (README, "Why the
--- traces are per-platform").
---
--- The assertions are `error` calls, and the harness fails a run that logged
--- one -- which is what stops a scenario dying on tick one and having its
--- emptiness recorded as a golden.

local RunService = game:GetService("RunService")

local Rng = Random.new(20260827)

--- Materials. Zero means remove to `FillBall`, so every other value is ground.
local Rock = 1
local Sand = 2

local terrain = Instance.new("Terrain")
terrain.Name = "Ground"
-- Reserved before anything is sculpted and never widened afterwards: a
-- collider's height precision is spread across this range at construction
-- (ADR 0066).
terrain.MinHeight = -48
terrain.MaxHeight = 48
terrain.Parent = workspace

-- **A plateau standing on the world's floor**, which is what makes the bulk of
-- this world height-encoded. Past the floor rather than onto it: a brush that
-- stops exactly at the floor leaves the sample there outside itself.
terrain:FillBlock(vector.create(0, -60, 0), vector.create(96, 120, 96), Rock)

if terrain:HeightAt(0, 0) == nil then
    error("the plateau made no ground, so everything after this is meaningless")
end

-- **Twenty-four edits from the seed**, half adding and half removing, over a
-- region wide enough to cross tile and brick boundaries in both axes. The
-- promotion rule is what is under test: which of these columns end up carrying
-- voxels has to be the same on every run and on every machine.
for _ = 1, 24 do
    local x = Rng:NextNumber(-36, 36)
    local z = Rng:NextNumber(-36, 36)
    local y = Rng:NextNumber(-8, 8)
    local radius = Rng:NextNumber(3, 9)
    if Rng:NextNumber() < 0.5 then
        terrain:FillBall(vector.create(x, y, z), radius, Rock)
    else
        terrain:FillBall(vector.create(x, y, z), radius, 0)
    end
end

-- Painting, which must change the hash -- a material is world state -- while
-- changing no distance and promoting no column.
for _ = 1, 6 do
    local x = Rng:NextNumber(-30, 30)
    local z = Rng:NextNumber(-30, 30)
    terrain:PaintBall(vector.create(x, 0, z), Rng:NextNumber(2, 6), Sand)
end

if terrain.CellCount == 0 then
    error("no terrain cells, so the collider rebuild below has nothing to do")
end

-- **Twelve crates dropped onto the sculpted ground**, which is the half a field
-- digest cannot assert on its own. A hash proves the FIELD is the same; a body
-- coming to rest in the same place proves the collider built from it is, tile
-- by tile, including which tiles the per-tick budget got to first.
--
-- Placed in a fixed lattice rather than randomly, so a divergence names a crate
-- rather than a seed.
for row = 0, 3 do
    for column = 0, 2 do
        local crate = Instance.new("Part")
        crate.Name = string.format("Crate_%d_%d", row, column)
        crate.Size = vector.create(2, 2, 2)
        crate.Position = vector.create(-20 + row * 14, 30 + column * 3, -14 + column * 14)
        crate.Color = Color3.fromRGB(200, 120, 60)
        crate.Parent = workspace
    end
end

-- **And an edit while the world is running**, at a tick chosen so that bodies
-- have already settled. Digging the ground out from under a resting crate is
-- what exercises the rebuild path rather than only the initial build: the tile's
-- collider has to be replaced, the body has to wake, and both have to happen on
-- the same tick every run.
--- 600 of the scenario's 1200 ticks, so there are six hundred more in which a
--- divergence introduced by the rebuild can grow into a visible one.
local DigAt = 600

local tick = 0
RunService.Heartbeat:Connect(function()
    tick += 1
    if tick == DigAt then
        terrain:FillBall(vector.create(-20, 4, -14), 8, 0)
    end
end)
