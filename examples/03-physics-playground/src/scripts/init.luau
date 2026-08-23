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

--- **The world this project starts with is a FILE** (ADR 0047).
--- `content/scenes/main.scene.json` holds the ground, the three towers, the two
--- ramps, the kerb and its wall, the seesaw, the character, the welded banner,
--- the follow camera and the input map; `luaug.toml` names it as the scene a run
--- begins with, and the editor writes back to that same file.
---
--- **The physics is not in the file and cannot be.** What a scene stores is a
--- world's starting state -- where a crate is, how heavy the plank is, whether
--- something is anchored -- and everything this example exists to show is what
--- the SOLVER then does with it. Migrating the props out of this script did not
--- move one line of that: the stack still comes down because it was never
--- scripted to.
---
--- Why it moved at all: a project whose code builds its world cannot be saved
--- from the editor. Save a scene of it and reopen, and the file puts a world
--- back while the code makes another -- two of everything, and no engine can
--- merge them (D074). A world with one source has one character.
local function fromScene(name: string): Instance
    local found = workspace:FindFirstChild(name)
    assert(found ~= nil, `the scene is missing "{name}" -- content/scenes/main.scene.json is this project's world`)
    return found :: Instance
end

local character = fromScene("Player") :: CharacterBody
local camera = fromScene("FollowCamera") :: Camera

--- **Lighting is not in the scene and that is the format's own boundary.** A
--- scene file is the contents of `Workspace`; a service's properties are the
--- project's settings, and `scene_file.h` says so where it decides what to
--- write. So these stay here, exactly as they are in the flagship.
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

--- The two input contexts the scene carries, and the actions inside them.
---
--- **The split is still the thing worth reading**, and it survived the move
--- unchanged: `Gameplay` runs on the sim clock, where R10 holds and where a
--- recorded replay can see what the player did, and `CameraControls` runs at
--- `Enum.InputRate.Render`, because a camera that turned at the tick rate would
--- step rather than sweep above 60 fps (ADR 0039). It is a property on a
--- context, it is now a field in a file, and it is still the whole of what it
--- costs.
local Gameplay = fromScene("Gameplay")
local CameraControls = fromScene("CameraControls")

local function action(context: Instance, name: string): InputAction
    local found = context:FindFirstChild(name)
    assert(found ~= nil, `the scene's {context.Name} has no "{name}" action`)
    return found :: InputAction
end

local MoveAction = action(Gameplay, "Move")
local JumpAction = action(Gameplay, "Jump")
local TurnAction = action(CameraControls, "Turn")

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
