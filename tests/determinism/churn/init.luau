--!strict
--- The determinism scenario (M2 gate: same script + same seed, twice, 10,000
--- ticks, identical hash).
---
--- A scenario is only worth its runtime if it exercises the things that can
--- actually diverge, so this one is built backwards from R10's list rather than
--- being a script that happens to be long:
---
---   * **RNG** -- an unseeded stream, or one drawn from a clock, diverges on the
---     first draw. Every decision below comes from one `Random`.
---   * **Iteration order** -- `GetChildren`, `GetDescendants` and
---     `TagService:GetTagged` all answer from containers whose order has to be
---     a property of the operation sequence and not of an address.
---   * **Slot reuse** -- destroying and recreating drives the SlotMap's free
---     list, which is exactly where a hash that reproduces on one run and not
---     the next comes from.
---   * **Deferred ordering** -- signals raised from inside handlers, so the
---     drain-to-fixpoint order is part of the hash rather than an untested
---     detail.
---   * **Float accumulation** -- CFrame composition tick over tick, so a
---     changed order of operations shows up as a changed hash.
---
--- The world is deliberately small. 10,000 ticks is the gate, and the gate is
--- about reproducing, not about load -- the load case is `bench/churn`.

local RunService = game:GetService("RunService")
local TagService = game:GetService("TagService")

local Rng = Random.new(90210)
local Root = Instance.new("Folder")
Root.Name = "Churn"
Root.Parent = workspace

local Pool: { Part } = {}
local Tick = 0
local Moved = 0

-- A part per slot, half of them tagged. The tag half is what makes
-- `GetTagged`'s order observable in the hash.
for i = 1, 24 do
    local part = Instance.new("Part")
    part.Name = `Part{i}`
    part.Size = vector.create(1 + Rng:NextNumber(), 1 + Rng:NextNumber(), 1 + Rng:NextNumber())
    part.CFrame = CFrame.new(vector.create(Rng:NextNumber(-8, 8), Rng:NextNumber(0, 4), Rng:NextNumber(-8, 8)))
    part.Color = Color3.fromRGB(Rng:NextInteger(0, 255), Rng:NextInteger(0, 255), Rng:NextInteger(0, 255))
    -- Anchored, from M5 on, and for the same reason `churn10k` is: this
    -- scenario is about TREE and PROPERTY churn, and its committed trace is
    -- compared against a build made by a different compiler on CI. A falling
    -- part's position depends on floating-point code generation, so leaving
    -- these simulated would trade a cross-build regression detector for a
    -- physics test this file was never written to be.
    part.Anchored = true
    part.Parent = Root
    if i % 2 == 0 then
        part:AddTag("Even")
    end
    table.insert(Pool, part)
end

-- Deferred work raised from inside a handler: the attribute write below fires
-- AttributeChanged, whose handler writes another attribute, so each churn tick
-- drains more than one generation and the drain order is under test.
local function watch(part: Part)
    part:GetAttributeChangedSignal("Phase"):Connect(function()
        Moved += 1
        part:SetAttribute("Seen", Moved)
    end)
end

for _, part in Pool do
    watch(part)
end

RunService.Heartbeat:Connect(function()
    Tick += 1

    -- Composition rather than assignment: an accumulated transform is where a
    -- reordered multiply or a changed rounding shows up, and a fresh
    -- `CFrame.new` every tick would hide both.
    for index, part in Pool do
        local turn = CFrame.fromEuler(0, 0.01 * index, 0, Enum.RotationOrder.XYZ)
        part.CFrame = part.CFrame * turn
    end

    -- Every 20th tick, destroy one part and build a replacement. This is the
    -- slot-reuse driver: the new instance lands in the freed slot, and it lands
    -- in the SAME freed slot on every run or the hash moves.
    if Tick % 20 == 0 then
        local victim = Pool[Rng:NextInteger(1, #Pool)]
        local wasTagged = victim:HasTag("Even")
        victim:Destroy()

        local replacement = Instance.new("Part")
        replacement.Name = `Rebuilt{Tick}`
        replacement.Size = vector.create(1, 1, 1)
        replacement.CFrame = CFrame.new(vector.create(Rng:NextNumber(-8, 8), 1, Rng:NextNumber(-8, 8)))
        replacement.Anchored = true
        replacement.Parent = Root
        if wasTagged then
            replacement:AddTag("Even")
        end
        watch(replacement)

        for index, part in Pool do
            if part == victim then
                Pool[index] = replacement
                break
            end
        end
    end

    -- Reparenting under a sibling and back, which moves a child within its
    -- parent's sibling chain -- the order `GetChildren` answers in, and part of
    -- the hash.
    if Tick % 7 == 0 then
        local children = Root:GetChildren()
        if #children > 1 then
            local subject = children[(Tick // 7) % #children + 1]
            subject.Parent = nil
            subject.Parent = Root
        end
    end

    -- Attribute churn on the tagged half, walked in `GetTagged` order.
    for _, instance in TagService:GetTagged("Even") do
        instance:SetAttribute("Phase", Tick * 0.5)
    end
end)
