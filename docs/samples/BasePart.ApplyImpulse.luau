--!strict
local crate = Instance.new("Part")
crate.Size = vector.create(2, 2, 2)
crate.Position = vector.create(0, 10, 0)
crate.Density = 0.6 -- kilograms per cubic metre; mass is this times the volume
crate.Parent = workspace

-- Kilogram-metres per second, applied at the NEXT simulation tick.
crate:ApplyImpulse(vector.create(0, 0, 12))
