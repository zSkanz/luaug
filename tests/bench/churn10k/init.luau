--!strict
--- The property-churn benchmark (roadmap M2: "10k parts / 1k listeners").
---
--- This is the scene the deferred-signal design was chosen against, and it is
--- here to keep that choice honest. The M2 brief's Decision 6 records the shape
--- of the risk: a property write that enqueued unconditionally would make this
--- scenario pathological, because 10,000 parts moving every tick would raise
--- 10,000 facts whether or not anything changed. What makes it survivable is
--- that a write which does not change the value raises nothing -- so the
--- *third* of the parts that hold still cost nothing but the compare.
---
--- 1,000 listeners, not 10,000: the point is that a signal with subscribers is
--- more expensive than one without, and having every part listen would measure
--- a scene nobody builds. One in ten is a plausible game.
---
--- The 16 ms budget is a whole 60 Hz frame, deliberately. This scene is not
--- meant to fit inside a frame alongside everything else -- it is a stress case
--- and the threshold is a catastrophe detector. The number that matters is the
--- one recorded in docs/perf-baselines.md; this only fails when something has
--- gone badly wrong.

local RunService = game:GetService("RunService")

local Rng = Random.new(77)
local Root = Instance.new("Folder")
Root.Name = "Churn"
Root.Parent = workspace

local PartCount = 10000
local ListenerCount = 1000

local Parts: { Part } = table.create(PartCount)
local Notified = 0

for index = 1, PartCount do
    local part = Instance.new("Part")
    part.Name = `P{index}`
    part.Size = vector.create(1, 1, 1)
    part.CFrame = CFrame.new(vector.create(Rng:NextNumber(-200, 200), 0, Rng:NextNumber(-200, 200)))
    part.Parent = Root
    Parts[index] = part
end

-- Every tenth part is watched. `GetPropertyChangedSignal` rather than a blanket
-- `Changed`, because that is the shape the API recommends and the one whose cost
-- the engine should be tuned for.
for index = 1, ListenerCount do
    local part = Parts[index * (PartCount // ListenerCount)]
    part:GetPropertyChangedSignal("CFrame"):Connect(function()
        Notified += 1
    end)
end

local Phase = 0.0

RunService.Heartbeat:Connect(function(deltaTime: number)
    Phase += deltaTime

    for index, part in Parts do
        -- Two thirds move, one third is written with the value it already has.
        -- The still third is the case the whole design rests on: the write is a
        -- compare that raises nothing, and if that ever stops being true this
        -- benchmark is where it shows.
        if index % 3 == 0 then
            part.CFrame = part.CFrame
        else
            local position = part.CFrame.Position
            part.CFrame = CFrame.new(vector.create(position.x, math.sin(Phase + index) * 2, position.z))
        end
    end
end)
