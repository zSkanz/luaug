--!strict
local RunService = game:GetService("RunService")

RunService.PreSimulation:Connect(function(dt: number)
    -- Intent, before physics steps.
end)

RunService.PostSimulation:Connect(function(dt: number)
    -- Results, after it.
end)

RunService.Heartbeat:Connect(function(dt: number)
    -- General per-tick work. dt is the FIXED timestep, every tick.
end)
