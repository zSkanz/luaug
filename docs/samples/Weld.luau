--!strict
local anchor = Instance.new("Part")
anchor.Anchored = true
anchor.Parent = workspace

local banner = Instance.new("Part")
banner.Size = vector.create(0.4, 1.6, 0.4)
banner.Parent = workspace

-- Holds Part0.CFrame * C0 equal to Part1.CFrame * C1.
local weld = Instance.new("Weld")
weld.Part0 = anchor
weld.Part1 = banner -- driven every tick; gravity does not reach it
weld.C0 = CFrame.new(0, 4.2, 0)
weld.Parent = workspace
