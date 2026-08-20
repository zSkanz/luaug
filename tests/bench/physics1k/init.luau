--!strict
--- The M5 gate scene: "physics tick budget for 1,000 active bodies recorded and
--- regression-gated" (roadmap M5).
---
--- **Active** is the load-bearing word, and it is why this scene is a tower
--- rather than a field. A thousand crates spread over a floor settle in under a
--- second and the solver puts them to sleep, after which the benchmark measures
--- a world where nothing is happening. Stacked, they keep nudging each other:
--- the island graph stays large, the contact cache stays full, and every tick
--- pays broadphase, narrowphase and solve for a thousand bodies -- which is the
--- number the gate is asking for.
---
--- The 16 ms budget is a whole 60 Hz frame, and it is a catastrophe detector
--- rather than an instrument, exactly as `churn10k`'s is: CI runners differ by
--- more than a factor of two between one job and the next. The number that
--- matters is the one recorded in `docs/perf-baselines.md`, broken into the
--- three stages that are separable at the physics seam.

local Rng = Random.new(1000)

local Root = Instance.new("Folder")
Root.Name = "Physics"
Root.Parent = workspace

-- The floor. Anchored, so it is a static body: it is in the broad phase and not
-- in the island graph, which is what an anchored part means.
local ground = Instance.new("Part")
ground.Name = "Ground"
ground.Size = vector.create(400, 4, 400)
ground.Position = vector.create(0, -2, 0)
ground.Anchored = true
ground.Parent = Root

local BodyCount = 1000
-- Twenty-five towers of forty. A tower is one island, so this is twenty-five
-- islands of forty bodies rather than one of a thousand -- which is the shape a
-- game actually produces, and the one the solver is tuned for.
local TowersPerSide = 5
local Layers = 40
local Spacing = 6

local created = 0
for towerX = 0, TowersPerSide - 1 do
    for towerZ = 0, TowersPerSide - 1 do
        for layer = 0, Layers - 1 do
            if created >= BodyCount then
                break
            end
            local crate = Instance.new("Part")
            crate.Name = `Crate{created}`
            crate.Size = vector.create(2, 2, 2)
            -- A small deterministic jitter, so the towers are not perfectly
            -- aligned columns that the solver resolves in one pass and sleeps.
            -- Seeded, and therefore part of the scenario rather than noise
            -- (ADR 0025).
            crate.Position = vector.create(
                (towerX - 2) * Spacing + Rng:NextNumber(-0.05, 0.05),
                1.1 + layer * 2.05,
                (towerZ - 2) * Spacing + Rng:NextNumber(-0.05, 0.05)
            )
            crate.Parent = Root
            created += 1
        end
    end
end

assert(created == BodyCount, `expected {BodyCount} bodies, built {created}`)
