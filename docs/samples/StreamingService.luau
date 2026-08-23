--!strict
local StreamingService = game:GetService("StreamingService")

local focus = Instance.new("Camera")
focus.Parent = workspace
workspace.CurrentCamera = focus

StreamingService.MinRadius = 320 -- guaranteed resident before a focus enters
StreamingService.LoadRadius = 700 -- best effort; the gap is hysteresis
StreamingService.PauseOutsideLoadedArea = true
StreamingService:AddFocus(focus)

StreamingService.InstanceStreamedOut:Connect(function(instance: Instance)
    -- Reparented to nil, not destroyed: the handle still resolves.
end)
