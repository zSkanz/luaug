--!strict
local tree = Instance.new("MeshPart")
tree.MeshContent = "asset://models/tree.glb"
tree.CollisionFidelity = Enum.CollisionFidelity.Hull
tree.Position = vector.create(12, 0, -4)
tree.Anchored = true
tree.Parent = workspace
