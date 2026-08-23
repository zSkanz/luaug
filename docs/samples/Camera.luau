--!strict
local camera = Instance.new("Camera")
camera.FieldOfView = 55
camera.NearPlane = 0.1
camera.FarPlane = 400
camera.CFrame = CFrame.lookAt(vector.create(0, 6, 14), vector.create(0, 1, 0))
camera.Parent = workspace

workspace.CurrentCamera = camera
