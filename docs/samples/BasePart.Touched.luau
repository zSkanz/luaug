--!strict
local pad = Instance.new("Part")
pad.Size = vector.create(4, 0.4, 4)
pad.Anchored = true
pad.CanCollide = false -- things pass through, and it still notices
pad.Parent = workspace

pad.Touched:Connect(function(other: BasePart)
    print(other.Name, "stepped on the pad")
end)
