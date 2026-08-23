--!strict
local Lighting = game:GetService("Lighting")
local RunService = game:GetService("RunService")

Lighting.ClockTime = 8
Lighting.GeographicLatitude = 35
Lighting.Brightness = 2.6
Lighting.FogColor = Color3.fromRGB(150, 178, 214)
Lighting.FogStart = 40
Lighting.FogEnd = 220

-- A ninety-second day. ClockTime wraps on its own.
RunService.Heartbeat:Connect(function(dt: number)
    Lighting.ClockTime += (24 / 90) * dt
end)
