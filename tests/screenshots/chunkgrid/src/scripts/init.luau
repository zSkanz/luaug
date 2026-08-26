--!strict

-- The evidence scene for the STREAMING GRID: is there a picture of the chunk
-- state at all?
--
-- **E5 owed this screenshot and could not take it.** The gate row is "a
-- screenshot of the chunk-state overlay is attached to the gate record", and the
-- overlay it meant was an ImGui panel -- which cannot render headlessly, so the
-- row sat PENDING from the day the milestone closed. Drawing the grid through
-- `DebugDraw` instead puts it in the ordinary renderer, where
-- `--headless --screenshot` already reaches, so the version that is better for a
-- person looking at a world is also the version a gate can capture.
--
-- The claim is a differential and it is the opposite of a golden's: the same
-- frame with the grid on and with it off must NOT be the same image. That is the
-- shape `run_local_shadow_gate` uses, and for the reason D043 records -- three
-- green instruments once agreed with an empty frame because none of them
-- compared an image against an image.
--
-- The frame number selects which:
--
--   `--frames=1`  no grid: the world alone, and the reference
--   `--frames=3`  the grid, drawn over it
--
-- The camera does not move between them and nothing else in the scene changes,
-- so a difference can only be the grid.

local DebugService = game:GetService("DebugService")
local RunService = game:GetService("RunService")
local StreamingService = game:GetService("StreamingService")
local Workspace = game:GetService("Workspace")

local camera = Workspace.CurrentCamera
if camera == nil then
    return
end

-- **The world streams around a focus, and this one is the camera.** Without a
-- focus nothing is wanted, nothing loads, and every cell in the picture would be
-- the same colour -- which would be a differential that passed while showing
-- nothing worth seeing.
StreamingService:AddFocus(camera)

-- Chosen against the scene's own size: three cells each way at the engine's own
-- 256-metre grid, with the camera over one corner. A load radius that covered
-- all nine would make every cell the same green; one that covered none would
-- make them all grey. These put the boundary inside the picture, which is the
-- whole point of it.
StreamingService.MinRadius = 420
StreamingService.LoadRadius = 760

local ticks = 0
RunService.Heartbeat:Connect(function()
    ticks += 1
    -- On the second tick rather than the first, so the frame the reference is
    -- taken from is already settled: whatever streaming was going to do on tick
    -- one has happened in both runs, and the only difference left is the lines.
    if ticks == 2 then
        DebugService:ShowPanel("Streaming")
    end
end)
