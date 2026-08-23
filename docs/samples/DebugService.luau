--!strict
local DebugService = game:GetService("DebugService")

DebugService.OverlayVisible = true
DebugService:ShowPanel("Stats")
DebugService:SetCustomStat("EnemiesAlive", 12)

print(DebugService:GetStat("FPS"), DebugService:GetStat("DrawCalls"))
