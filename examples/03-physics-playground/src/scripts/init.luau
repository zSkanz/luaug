--!strict
--- examples/03-physics-playground — the M5 deliverable.
---
--- A world with mass. Everything here is a thing the simulation does rather
--- than a thing a script animates:
---
---   * **Stacks.** Three towers of crates. Walk into one and it comes down.
---   * **Ramps.** Two slopes, one inside `MaxSlopeAngle` and one outside it, so
---     the difference between ground and wall is something you can walk at.
---   * **A step.** A kerb below `AutoStepHeight` beside one above it: the single
---     number that separates a character from a capsule.
---   * **A seesaw.** A plank resting on a fulcrum. Not a hinge — M5 ships no
---     constraints, and a plank on a wedge tips for the same reason a real one
---     does.
---   * **A welded banner** on the character, which is why `Weld` exists: it is
---     driven by the capsule and never simulated, so it stays put whatever the
---     capsule walks into.
---
--- **Controls.** W/A/S/D walk relative to where the camera is looking, Space
--- jumps, and the arrow keys or the right stick move the camera. Every one of
--- them goes through the **Input Action System** (ADR 0029), which is the
--- engine's only input path. It went through `KeyboardService` until M6 deleted
--- that scaffold, and this file is the migration the M6 gate asks for.
---
--- Worth reading as an example rather than as plumbing: the two camera actions
--- sit in their own context at `Enum.InputRate.Render`, because a camera should
--- turn at the frame rate rather than at the tick rate, while movement and jump
--- stay on `Simulation`, where the replay can see them (ADR 0039). That split
--- is a property on a context, and it is the whole of what it costs.

local DebugService = game:GetService("DebugService")
local Lighting = game:GetService("Lighting")
local RunService = game:GetService("RunService")

-- The solver's own wireframe, over the top of the rendered world. It is the one
-- picture that can disagree with what you see -- a collider that is not where
-- its part is looks exactly like a part that is not where it looks -- and this
-- example exists to be looked at, so it asks for it.
DebugService:ShowPanel("Physics")

--- Metres per second the camera turns at, and how far behind the character it
--- sits. Named because they are the two numbers anyone playing this will want
--- to change first.
local CameraTurnSpeed = 2.2
local CameraDistance = 14
local CameraMinHeight = 3
local CameraMaxHeight = 20

local Ground = Color3.fromRGB(96, 104, 96)
local Crate = Color3.fromRGB(176, 132, 84)
local Ramp = Color3.fromRGB(120, 128, 150)
local Accent = Color3.fromRGB(214, 96, 72)

local function block(name: string, cframe: CFrame, size: vector, color: Color3, anchored: boolean): Part
    local part = Instance.new("Part")
    part.Name = name
    part.Size = size
    part.CFrame = cframe
    part.Color = color
    part.Anchored = anchored
    part.Parent = workspace
    return part
end

-- The floor. Anchored, so it is scenery rather than the largest falling object
-- in the world.
block("Ground", CFrame.new(0, -1, 0), vector.create(200, 2, 200), Ground, true)

-- Three towers. Deliberately not perfectly aligned: a column that is exactly
-- square resolves in one solver pass and stands there looking painted on.
for tower = 0, 2 do
    for layer = 0, 5 do
        local jitter = (layer % 2 == 0) and 0.04 or -0.04
        block(
            `Crate{tower}_{layer}`,
            CFrame.new(-18 + tower * 5 + jitter, 1 + layer * 2.02, -12),
            vector.create(2, 2, 2),
            Crate,
            false
        )
    end
end

-- Two ramps. Twenty degrees is inside the default MaxSlopeAngle of 46 and
-- sixty is outside it, so one is a floor and the other is a wall -- which is a
-- thing you can walk at rather than a number in a document.
block(
    "RampWalkable",
    CFrame.new(10, 1.6, 8) * CFrame.fromEuler(math.rad(-20), 0, 0, Enum.RotationOrder.YXZ),
    vector.create(8, 1, 14),
    Ramp,
    true
)
block(
    "RampTooSteep",
    CFrame.new(22, 3.4, 8) * CFrame.fromEuler(math.rad(-60), 0, 0, Enum.RotationOrder.YXZ),
    vector.create(8, 1, 10),
    Ramp,
    true
)

-- A kerb below AutoStepHeight, and a wall above it, side by side.
block("Kerb", CFrame.new(-6, 0.2, 10), vector.create(10, 0.8, 6), Ramp, true)
block("Wall", CFrame.new(-6, 1.5, 18), vector.create(10, 3, 2), Accent, true)

-- The seesaw: a fulcrum and a plank resting on it. No constraint, because M5
-- ships none -- what makes it tip is a contact and a centre of mass.
block("Fulcrum", CFrame.new(6, 0.5, -14), vector.create(1.2, 1, 6), Accent, true)
local plank = block("Plank", CFrame.new(6, 1.3, -14), vector.create(14, 0.5, 4), Crate, false)
plank.Density = 0.6

-- Something to put on one end of it.
block("Weight", CFrame.new(11, 3, -14), vector.create(2, 2, 2), Crate, false)

local character = Instance.new("CharacterBody")
character.Name = "Player"
character.Size = vector.create(2, 5, 2)
character.Position = vector.create(0, 6, 0)
character.Color = Color3.fromRGB(96, 172, 220)
character.Parent = workspace

-- A banner welded to the character. This is what `Weld` is for and what nothing
-- else in v1 can do: the banner is driven by the capsule every tick and is never
-- simulated, so it does not lag, fall, or get knocked off by a crate.
local banner = block("Banner", CFrame.new(0, 0, 0), vector.create(0.4, 1.6, 0.4), Accent, false)
local weld = Instance.new("Weld")
weld.Part0 = character
weld.Part1 = banner
weld.C0 = CFrame.new(0, 4.2, 0)
weld.Parent = workspace

local camera = Instance.new("Camera")
camera.Name = "FollowCamera"
camera.FieldOfView = 65
camera.NearPlane = 0.1
camera.FarPlane = 600
camera.Parent = workspace
workspace.CurrentCamera = camera

Lighting.ClockTime = 10
Lighting.GeographicLatitude = 30
Lighting.Ambient = Color3.fromRGB(12, 13, 16)
Lighting.Brightness = 2.4
Lighting.FogColor = Color3.fromRGB(160, 178, 200)
Lighting.FogStart = 90
Lighting.FogEnd = 320

--- Where the camera is looking, in radians about Y. The character walks
--- relative to this, which is what makes W mean "away from the camera" rather
--- than "towards positive Z".
local cameraYaw = 0
local cameraHeight = 7

-- Gameplay: the sim clock, which is where R10 holds and where the recorded
-- input replay can see what the player did.
local Gameplay = Instance.new("InputContext")
Gameplay.Name = "Gameplay"
Gameplay.Parent = workspace

local MoveAction = Instance.new("InputAction")
MoveAction.Name = "Move"
MoveAction.Type = Enum.InputActionType.Direction2D
MoveAction.Parent = Gameplay

local MoveKeys = Instance.new("InputBinding")
MoveKeys.Up = Enum.KeyCode.W
MoveKeys.Down = Enum.KeyCode.S
MoveKeys.Left = Enum.KeyCode.A
MoveKeys.Right = Enum.KeyCode.D
MoveKeys.DisplayName = "WASD"
MoveKeys.Parent = MoveAction

-- A second binding on the same action, and that is the point of the indirection:
-- a stick and four keys are two ways to do one thing, not two code paths.
local MoveStick = Instance.new("InputBinding")
MoveStick.KeyCode = Enum.KeyCode.LeftThumbstick
MoveStick.Parent = MoveAction

local JumpAction = Instance.new("InputAction")
JumpAction.Name = "Jump"
JumpAction.Type = Enum.InputActionType.Bool
JumpAction.Parent = Gameplay

local JumpKey = Instance.new("InputBinding")
JumpKey.KeyCode = Enum.KeyCode.Space
JumpKey.DisplayName = "Space"
JumpKey.Parent = JumpAction

local JumpButton = Instance.new("InputBinding")
JumpButton.KeyCode = Enum.KeyCode.ButtonSouth
JumpButton.Parent = JumpAction

-- The camera: the RENDER clock, because a camera that turned at the tick rate
-- would step rather than sweep on a machine running above 60 fps.
local CameraControls = Instance.new("InputContext")
CameraControls.Name = "CameraControls"
CameraControls.Rate = Enum.InputRate.Render
CameraControls.Parent = workspace

local TurnAction = Instance.new("InputAction")
TurnAction.Name = "Turn"
TurnAction.Type = Enum.InputActionType.Direction2D
TurnAction.Parent = CameraControls

local TurnKeys = Instance.new("InputBinding")
TurnKeys.Up = Enum.KeyCode.Up
TurnKeys.Down = Enum.KeyCode.Down
TurnKeys.Left = Enum.KeyCode.Left
TurnKeys.Right = Enum.KeyCode.Right
TurnKeys.Parent = TurnAction

local TurnStick = Instance.new("InputBinding")
TurnStick.KeyCode = Enum.KeyCode.RightThumbstick
TurnStick.Parent = TurnAction

RunService.Heartbeat:Connect(function(dt: number)
    -- +X turns left, +Y raises. One Direction2D action instead of four booleans,
    -- which is what an action system buys even before anything is rebound.
    local turn = TurnAction:GetState() :: Vector2
    cameraYaw += CameraTurnSpeed * turn.X * dt
    cameraHeight = math.clamp(cameraHeight + 6 * turn.Y * dt, CameraMinHeight, CameraMaxHeight)

    -- Forward is where the camera looks, flattened onto the ground plane: a
    -- character that walked into the sky because the camera was tilted up is
    -- the bug this line exists to not have.
    local forward = vector.create(-math.sin(cameraYaw), 0, -math.cos(cameraYaw))
    local right = vector.create(math.cos(cameraYaw), 0, -math.sin(cameraYaw))

    -- The action answers in screen space -- +Y forward, +X right -- and this is
    -- the one line that maps it onto the ground plane the camera defines.
    local axis = MoveAction:GetState() :: Vector2
    local move = forward * axis.Y + right * axis.X

    -- Normalised so that walking diagonally is not faster than walking
    -- straight, which is the oldest bug in this shape of code.
    if vector.magnitude(move) > 0.001 then
        character:Move(vector.normalize(move))
    end
    if JumpAction:GetState() :: boolean and character.Grounded then
        -- The `Grounded` check is the GAME's since M7: `Jump` fires wherever the
        -- character is, and this one line is the ground-only rule a playground
        -- wants. A platformer would drop it and count jumps instead.
        character:Jump()
    end

    -- The camera follows the character rather than being parented to it: a
    -- follow camera that snapped to a capsule's every solver correction would
    -- shake, and this one moves a fraction of the way each tick instead.
    local focus = character.Position + vector.create(0, 3, 0)
    local wanted = focus - forward * CameraDistance + vector.create(0, cameraHeight, 0)
    -- Written out rather than `:Lerp`, because `vector`'s convenience members
    -- exist at runtime and are not type-checkable (U-54): `vector` is a Luau
    -- builtin and a `declare extern type` does not augment it.
    local eye = camera.CFrame.Position
    eye += (wanted - eye) * math.min(1, 8 * dt)
    camera.CFrame = CFrame.lookAt(eye, focus)
end)
