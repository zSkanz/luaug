--!strict
-- A dedicated server that ends on a match rather than a frame count: after its
-- "match" it calls `game:Shutdown()`, and its close handler runs before the
-- process exits. `serve_ends_on_match` in `engine/app/CMakeLists.txt` runs it.
local RunService = game:GetService("RunService")

game:BindToClose(function()
    print("server closed after the match")
end)

local ticks = 0
RunService.Heartbeat:Connect(function()
    ticks += 1
    if ticks == 30 then
        game:Shutdown()
    end
end)
