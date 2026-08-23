--!strict
local scored: Signal<number, string> = Signal.new()

local connection = scored:Connect(function(points: number, reason: string)
    print(points, reason)
end)

scored:Fire(10, "checkpoint")
connection:Disconnect()
