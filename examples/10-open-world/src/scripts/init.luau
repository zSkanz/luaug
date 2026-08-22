--!strict
--- `examples/10-open-world` — the v1 flagship.
---
--- A third-person character walking a world larger than the engine keeps
--- resident, under a sun that moves, with a HUD, ambient sound, physics, and
--- hot reload that puts you back where you were standing.
---
--- **Every milestone is load-bearing here, and that is the point.** Each earlier
--- example proved one system against itself; this one is the first scene where
--- all of them have to be true at the same time:
---
---   M2  the Instance tree, deferred signals, `task`
---   M4  meshes, materials, camera, `Lighting`
---   M5  Jolt: the character stands on ground that streamed in a moment ago
---   M6  input actions, UI, tweens, audio
---   M7  the asset pipeline, async IO, chunk streaming, the floating origin
---   M7.5 cascades, clustered lights, image-based lighting, the post chain
---   M8  the graphics settings this runs under, and the folder it ships in
---
--- **The character is the streaming focus**, which is the thing `05-streaming`
--- deliberately did not do — it used the camera, and noted that a game would
--- hand it the character instead. This is that game.
---
--- What to watch for, in the order it becomes visible:
---
---   1. **The world arrives before you reach it.** `MinRadius` is the ring that
---      must be resident before the focus may advance into it, and
---      `PauseOutsideLoadedArea` is what happens when it is not — a pause rather
---      than a character falling through ground that has not loaded.
---   2. **The sun moves and everything follows it.** The sky, the shadow
---      direction, the fog, the reflection in the tower faces, and the exposure.
---      One property drives all of it.
---   3. **Hot reload keeps your place.** Edit this file and save: the world is
---      rebuilt from scratch, and the character is standing where it was. That
---      is `HotReloadService:SaveState` and nothing else.

local cameraRigs = require("@luaug/camera")

local AudioService = game:GetService("AudioService")
local DebugService = game:GetService("DebugService")
local HotReloadService = game:GetService("HotReloadService")
local InputService = game:GetService("InputService")
local Lighting = game:GetService("Lighting")
local RunService = game:GetService("RunService")
local StreamingService = game:GetService("StreamingService")
local UIService = game:GetService("UIService")

-- --- Constants ---------------------------------------------------------------

--- How long a full day takes, in seconds of SIMULATION time.
---
--- **Ten minutes rather than four, and a human playing it is why** (D048). At
--- four minutes the sun crosses the sky ninety times faster than the real one:
--- every shadow in the frame visibly swings and stretches while you stand
--- still, which reads as something moving on the ground rather than as a time
--- of day. Ten minutes still shows a sunrise, a noon and a dusk inside the soak
--- and inside a session somebody would actually sit through.
local DayLength = 600

--- The hour a fresh run opens on. Mid-morning: the sun is high enough to light
--- the ground and low enough to throw the shadows that say where the towers
--- are. Every screenshot in `docs/images/m8` is this hour, which is what makes
--- them comparable.
local StartHour = 9.5

--- Where a new game starts. On the terrain rather than above it: the height
--- field is the one in `tools/generate_world.luau`, and standing a character in
--- the air at boot would make the first second of every session a fall.
---
--- **And on the CENTRE of a ground tile, which is not decoration (D048).** The
--- floor is 32 m tiles whose tops sit at the height field's value at their own
--- centre, so the four tiles meeting at the world origin have four slightly
--- different heights -- and a character spawned exactly there stands on a step
--- and slides off it forever, at about three centimetres a second. It is
--- invisible in the position readout and it moves the CAMERA, which moves the
--- shadow map's texel lattice, which makes every shadow edge in the frame
--- dither. Measured: still to nine decimal places here, drifting at the corner.
local StartX = 16
local StartZ = 16

--- Below this you have fallen out of the world, which in a streamed world means
--- you outran the ground. Recovered rather than fatal.
local DeathHeight = -60

-- --- The character -----------------------------------------------------------

--- The height field from `tools/generate_world.luau`, repeated here rather than
--- shared. **Deliberate**: the generator runs in Lute at build time and this
--- runs in the engine at play time, so there is no module both can require
--- without the world's shape becoming a runtime dependency of the build. Two
--- copies of six lines, with this comment on both, beats a coupling.
local function heightAt(x: number, z: number): number
    return 8 * math.sin(x / 1200) * math.cos(z / 1000) + 3 * math.sin((x + z) / 500)
end

local character = Instance.new("CharacterBody")
character.Name = "Walker"
character.Size = vector.create(1.4, 3.2, 1.4)
character.Color = Color3.fromRGB(224, 196, 132)
character.WalkSpeed = 7
-- Gravity is 9.81 m/s^2 and this engine's units are METRES (divergence 15). At
-- 6.0 a jump rises about 1.8 m, which clears a boulder and not a tower.
character.JumpSpeed = 6
-- **The number the terrain was generated against.** A chunk's floor is 32 m
-- tiles cut from a height field whose steepest step across one tile is about
-- 0.41 m; this is that, with room for the diagonal case. At the 0.5 m default a
-- walk across a slope catches on about one tile in ten, which reads as the
-- character sticking on nothing.
character.AutoStepHeight = 0.6
-- Invisible, because what you see is the capsule welded to it below. A
-- `CharacterBody` is a `BasePart` and `Shape` belongs to `Part` (§2.2), so the
-- body itself can only ever draw as a box -- and the thing it SIMULATES as is a
-- capsule. Drawing the capsule is drawing the truth.
character.Transparency = 1
character.Parent = workspace

-- The visible character: a capsule welded to the body, and `CanCollide` off.
-- Off is load-bearing rather than tidy -- a second collider occupying exactly
-- the same space as the character's own is two bodies inside each other, and
-- the solver's answer to that is to push them apart.
local body = Instance.new("Part")
body.Name = "WalkerBody"
body.Shape = Enum.PartShape.Capsule
body.Size = character.Size
body.Color = Color3.fromRGB(214, 176, 108)
body.CanCollide = false
body.Anchored = false
body.Parent = workspace

-- **A lantern, and it is the flagship's caller for clustered forward shading.**
-- M7.5 stopped the light count being eight and nothing in this demo lit
-- anything, so the path shipped in the deliverable and was never exercised by
-- it. A light hangs off a `BasePart` and takes its position from it (§2.2), so
-- parenting it to the visible body is the whole of the attachment.
--
-- It also makes night PLAYABLE. A world with no artificial light at midnight is
-- honest and it is a black screen; the point of a day/night cycle is that both
-- ends are somewhere you can be.
--
-- **It hangs BESIDE the character rather than inside them**, and that is not
-- decoration. A point light takes its position from the part it is parented to,
-- so a light at the body's own centre is a light every one of that body's
-- surfaces faces away from: at midnight the character was a black silhouette
-- standing in a pool of its own light, which is the one thing a carried lantern
-- must not look like. The post is a hand's width out and shoulder-high,
-- invisible because what a player should see is the light and not the fixture.
local lanternPost = Instance.new("Part")
lanternPost.Name = "LanternPost"
lanternPost.Size = Vector3.new(0.2, 0.2, 0.2)
lanternPost.Transparency = 1
lanternPost.CanCollide = false
lanternPost.Anchored = false
lanternPost.Parent = workspace

local lanternWeld = Instance.new("Weld")
lanternWeld.Part0 = character
lanternWeld.Part1 = lanternPost
lanternWeld.C0 = CFrame.new(0.85, 0.5, 0)
lanternWeld.Parent = workspace

local lantern = Instance.new("PointLight")
lantern.Name = "Lantern"
lantern.Color = Color3.fromRGB(255, 214, 158)
lantern.Range = 26
lantern.Brightness = 0
lantern.Parent = lanternPost

local bodyWeld = Instance.new("Weld")
bodyWeld.Part0 = character
bodyWeld.Part1 = body
bodyWeld.C0 = CFrame.new(0, 0, 0)
bodyWeld.Parent = workspace

-- **Hot reload keeps your place.** `SaveState` takes numbers and tables of
-- them, not a `vector` -- the value is copied engine-side at the moment of the
-- call, and a vector is not one of the types that survives that. Three numbers
-- is the whole of it.
local saved = HotReloadService:LoadState("walker")
if HotReloadService:IsReload() and typeof(saved) == "table" then
    character.Position = vector.create(saved[1] :: number, saved[2] :: number, saved[3] :: number)
else
    character.Position = vector.create(StartX, heightAt(StartX, StartZ) + 3, StartZ)
end

-- --- Streaming ---------------------------------------------------------------

-- The focus is the CHARACTER. `MinRadius` is a promise -- everything inside it
-- is resident before the focus may advance into it -- and `LoadRadius` is the
-- best-effort ring beyond it. The gap between them is hysteresis: a single
-- radius makes a character standing on a boundary load and evict the same chunk
-- every frame, and the symptom of that is not a wrong world, it is a stutter
-- nobody can find.
StreamingService.LoadRadius = 700
StreamingService.MinRadius = 320
-- A pause rather than a character falling through ground that has not arrived.
-- On a machine that can keep up it never fires; it is here because the one that
-- cannot is the machine this is for.
StreamingService.PauseOutsideLoadedArea = true
StreamingService:AddFocus(character)

-- --- Camera and sky ----------------------------------------------------------

-- The third-person rig from `@luaug/camera`, which exists because three
-- examples in this repository hand-rolled the same forty lines before it did.
-- It creates the `Camera`, assigns it to `workspace.CurrentCamera`, and places
-- it before the first frame rather than easing it in from the world origin.
local rig = cameraRigs.thirdPerson({
    Subject = character,
    Distance = 11,
    Height = 6,
    Focus = 2,
    FarPlane = 1200,
})

Lighting.GeographicLatitude = 34
Lighting.Ambient = Color3.fromRGB(16, 18, 24)
Lighting.Brightness = 2.3
-- **The artist control on top of the metered exposure**, in EV stops, and this
-- scene needs one for a reason worth writing down: automatic exposure maps the
-- frame's average onto a key, and half of this frame is SKY. A field of grass
-- under an open sky meters far brighter than the grass alone, so the aperture
-- opens about a stop and the ground comes back as pale mint with no contrast in
-- it. Measured rather than guessed: the same frame with metering disabled lands
-- at a gain of 0.45, and the metered one at about 1.0.
--
-- Changing the albedos does NOT fix this, which is the trap. Metering
-- normalises whatever it is shown, so a darker field simply opens the aperture
-- further and arrives at the same picture -- which is what the first two
-- attempts at this scene proved, once by halving every albedo and once by
-- doubling them.
Lighting.ExposureCompensation = -1.1
Lighting.FogColor = Color3.fromRGB(176, 194, 214)
-- Far enough that the fog hides the streaming boundary rather than the world:
-- chunks stop arriving at 700 m, so the horizon has to go grey before that or a
-- player sees the edge of what exists.
-- **Fog is a streaming budget here, not an effect.** Chunks stop arriving at
-- `LoadRadius`, so the horizon has to be gone before that or a player sees the
-- edge of what exists. It starts late enough that the middle distance keeps its
-- contrast -- the first tuning had it opening at 260 m and turned a forest into
-- a wash.
Lighting.FogStart = 380
Lighting.FogEnd = 720

-- --- Ambient sound -----------------------------------------------------------
--
-- On the SimClock, so a replay hears the same thing on the same tick. `Content`
-- is stored and not yet read, so these play generated tones of their declared
-- length -- which is the honest state of the audio path in v1, and is enough to
-- mark what happened when.

local wind = Instance.new("Sound")
wind.Name = "Wind"
wind.Volume = 0.12
wind.PlaybackSpeed = 0.25
wind.Looped = true
wind.Parent = AudioService
wind:Play()

local function oneShot(name: string, volume: number, speed: number): Sound
    local sound = Instance.new("Sound")
    sound.Name = name
    sound.Volume = volume
    sound.PlaybackSpeed = speed
    sound.Parent = AudioService
    return sound
end

local jumpSound = oneShot("Jump", 0.18, 1.7)
local landSound = oneShot("Land", 0.16, 0.55)
local recoverSound = oneShot("Recover", 0.3, 0.8)

-- --- Input -------------------------------------------------------------------
--
-- Movement and jump on the SimClock, where a replay can see them; the camera on
-- the render clock, because a camera should turn at the frame rate (ADR 0039).

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

local MoveStick = Instance.new("InputBinding")
MoveStick.KeyCode = Enum.KeyCode.LeftThumbstick
MoveStick.Parent = MoveAction

local JumpAction = Instance.new("InputAction")
JumpAction.Name = "Jump"
JumpAction.Parent = Gameplay

local JumpKey = Instance.new("InputBinding")
JumpKey.KeyCode = Enum.KeyCode.Space
JumpKey.DisplayName = "Space"
JumpKey.Parent = JumpAction

local JumpPad = Instance.new("InputBinding")
JumpPad.KeyCode = Enum.KeyCode.ButtonSouth
JumpPad.Parent = JumpAction

local SprintAction = Instance.new("InputAction")
SprintAction.Name = "Sprint"
SprintAction.Parent = Gameplay

local SprintKey = Instance.new("InputBinding")
SprintKey.KeyCode = Enum.KeyCode.LeftShift
SprintKey.DisplayName = "Shift"
SprintKey.Parent = SprintAction

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

--- The mouse, and it is a SEPARATE action from `Turn` on purpose.
---
--- A stick is a rate and a mouse is a displacement: `MouseMovement` reports the
--- pointer's motion this tick in pixels, which is already the amount to turn by.
--- Feeding both into one action would mean one of them was scaled by `dt`
--- wrongly -- and a mouse multiplied by a frame time turns twice as far on a
--- slow machine, which is the most common way a look control feels broken.
--- `rig:Turn` takes the rate and `rig:Look` takes the displacement.
local LookAction = Instance.new("InputAction")
LookAction.Name = "Look"
LookAction.Type = Enum.InputActionType.Direction2D
LookAction.Parent = CameraControls

local LookMouse = Instance.new("InputBinding")
LookMouse.KeyCode = Enum.KeyCode.MouseMovement
LookMouse.DisplayName = "Mouse"
LookMouse.Parent = LookAction

--- Left Alt locks and unlocks the pointer.
---
--- On the gameplay context rather than the camera one, because it is a state
--- change rather than a camera motion -- and on the SIM clock, so a replay sees
--- it happen on a tick like everything else.
local PointerAction = Instance.new("InputAction")
PointerAction.Name = "TogglePointer"
PointerAction.Parent = Gameplay

local PointerKey = Instance.new("InputBinding")
PointerKey.KeyCode = Enum.KeyCode.LeftAlt
PointerKey.DisplayName = "Left Alt"
PointerKey.Parent = PointerAction

-- **Locked at boot**, because a third-person camera that needs a click before
-- it responds is a camera that feels broken for the first second of every
-- session. `PointerVisible` follows it: a cursor drawn over a locked pointer
-- sits frozen in the middle of the screen, which reads as a stuck cursor rather
-- than as a hidden one.
--
-- Both are wishes rather than guarantees -- a headless run has no window to lock
-- a pointer to, and writing them there is a no-op rather than an error.
InputService.PointerLocked = true
InputService.PointerVisible = false

-- `StateChanged` rather than reading the state each tick, because a toggle is an
-- EDGE: `GetState` is true for as long as the key is held, so a per-tick read
-- would flip the lock sixty times a second while somebody leaned on Alt.
PointerAction.StateChanged:Connect(function()
    if not (PointerAction:GetState() :: boolean) then
        return
    end
    InputService.PointerLocked = not InputService.PointerLocked
    InputService.PointerVisible = not InputService.PointerLocked
end)

-- --- The HUD -----------------------------------------------------------------

local screen = Instance.new("ScreenGui")
screen.Name = "OpenWorld"
screen.Parent = UIService

local panel = Instance.new("Frame")
panel.Name = "Readout"
panel.Size = UDim2.new(0, 260, 0, 96)
panel.Position = UDim2.new(0, 16, 0, 16)
panel.BackgroundColor = Color3.fromRGB(12, 14, 18)
panel.BackgroundTransparency = 0.35
panel.Parent = screen

local corner = Instance.new("UICorner")
corner.CornerRadius = UDim.new(0, 8)
corner.Parent = panel

local layout = Instance.new("UIListLayout")
layout.Padding = UDim.new(0, 2)
layout.Parent = panel

local padding = Instance.new("UIPadding")
padding.PaddingTop = UDim.new(0, 10)
padding.PaddingLeft = UDim.new(0, 12)
padding.Parent = panel

local function readout(name: string): TextLabel
    local label = Instance.new("TextLabel")
    label.Name = name
    label.Size = UDim2.new(1, -24, 0, 20)
    label.BackgroundTransparency = 1
    label.TextColor = Color3.fromRGB(232, 238, 246)
    label.TextSize = 15
    label.HorizontalAlignment = Enum.HorizontalAlignment.Left
    label.Text = ""
    label.Parent = panel
    return label
end

local clockLabel = readout("Clock")
local positionLabel = readout("Position")
local hintLabel = readout("Hint")

-- A game's own user-facing text is the GAME's, not the engine's: it is authored
-- here in the language the project ships in, exactly as `examples/04-obby`
-- does. R3 governs strings the ENGINE puts on a screen.
hintLabel.Text = "WASD move · Shift sprint · Space jump · Mouse look · Left Alt frees the cursor"

-- --- The loop ----------------------------------------------------------------

local wasGrounded = true
local sinceSave = 0

--- Whether anything is drawing. `PreRender` never fires in a headless run
--- (api-design.md §2.1), so this is how the demo knows nobody is watching and
--- takes over the controls itself (see `autopilot`).
---
--- **It is NOT what drives the camera, and that is a defect report from a human
--- playing this scene: with the camera on `PreRender` the whole world shakes as
--- soon as the character walks** (D047). The rig is advanced on `Heartbeat`,
--- with the world, and `@luaug/camera`'s own header says why in more detail.
local renderIsDriving = false

--- The hour of the day as a string a HUD can show. `ClockTime` is a float, and
--- "14.37" is a number rather than a time.
local function clockText(hour: number): string
    local whole = math.floor(hour)
    local minutes = math.floor((hour - whole) * 60)
    return string.format("%02d:%02d", whole, minutes)
end

--- **The autopilot, and why it is in the game rather than beside it.**
---
--- The M8 gate is a ten-minute soak over this demo -- a walk and a flight, with
--- bounded memory and no hitch over 33 ms. A headless run has no player, and the
--- host has no way to hand a script a route, so the demo drives itself when
--- nothing is driving it: `PreRender` never fires without a window
--- (api-design.md §2.1), so `renderIsDriving` staying false IS the test for
--- "nobody is watching".
---
--- Everything below is a function of `SimTime` and nothing else, so the soak
--- walks the same path on every machine and in every run -- which is what makes
--- a memory curve from one run comparable to a memory curve from another.
---
--- Two legs, alternating every minute:
---
---   **Walk** -- a wide circuit at running pace. This is the leg that exercises
---   the character against ground that arrived a moment ago: contacts, steps,
---   slopes, and the streaming promise that the floor is there before the focus
---   reaches it.
---
---   **Fly** -- the same circuit, wider and faster, forty metres up with the
---   position written directly. Twelve times walking pace is what makes chunks
---   arrive and leave inside seconds instead of minutes, and eviction and
---   re-entry are the halves a straight line never exercises.
--- Half a cycle, in seconds of simulation time. **Fifty seconds a cycle is
--- chosen against the gate rather than for the look**: the soak's flatten check
--- compares the second quarter of the run with the fourth, so a cycle that does
--- not divide a quarter puts a walk leg in one and a flight in the other and
--- reports the difference between them as a world that grew. Six hundred
--- seconds of soak is four quarters of a hundred and fifty, which is three
--- whole cycles each.
local AutopilotLeg = 25
local AutopilotWalkRadius = 220
local AutopilotFlyRadius = 900
local AutopilotFlySpeed = 45

local function autopilot(time: number)
    local leg = math.floor(time / AutopilotLeg) % 2
    if leg == 0 then
        -- Walking: steer, never teleport. The character has to get there on its
        -- own legs, over whatever the terrain turns out to be.
        local angle = time * 0.5
        local target = vector.create(math.cos(angle) * AutopilotWalkRadius, 0, math.sin(angle) * AutopilotWalkRadius)
        local here = character.Position
        local toTarget = vector.create(target.x - here.x, 0, target.z - here.z)
        if vector.magnitude(toTarget) > 0.5 then
            character:Move(vector.normalize(toTarget))
        end
        character.WalkSpeed = 11
        -- A jump every four seconds, so the soak covers the airborne state and
        -- the landing rather than only the walk cycle.
        if character.Grounded and time % 4 < 0.02 then
            character:Jump()
        end
        -- The camera has to follow on this leg too, or every soak screenshot is
        -- of the spot the character started from.
        rig.Yaw = angle + math.pi * 0.5
        return
    end

    -- **Spiralled out rather than jumped to**, and that is a gate detail worth
    -- keeping: a leg that begins by placing the character nine hundred metres
    -- away replaces the whole resident set in one tick, and the burst of
    -- materialisation that follows is the only thing in a ten-minute soak that
    -- misses 60 fps. A player never teleports; a soak that does is measuring
    -- something no player will ever do.
    local intoLeg = time % (AutopilotLeg * 2) - AutopilotLeg
    local reach = AutopilotWalkRadius
        + (AutopilotFlyRadius - AutopilotWalkRadius) * math.min(1, math.max(0, intoLeg) / 6)
    local angle = time * (AutopilotFlySpeed / AutopilotFlyRadius)
    local x = math.cos(angle) * reach
    local z = math.sin(angle) * reach
    character.Position = vector.create(x, heightAt(x, z) + 40, z)
    rig.Yaw = angle + math.pi * 0.5
end

RunService.Heartbeat:Connect(function(dt: number)
    -- **The day, and it is one property.** `SimTime` rather than an accumulated
    -- delta for the reason it exists: the hour is then a function of the tick
    -- index, so the same frame of the same run is the same picture on every
    -- machine -- which is what makes a screenshot of this scene a gate.
    local hour = (RunService.SimTime / DayLength * 24 + StartHour) % 24
    Lighting.ClockTime = hour

    -- Night is not merely darker: the air clears, the fog cools, and the wind
    -- drops. Three cheap properties, and together they are the difference
    -- between a dimmer switch and a time of day.
    local daylight = math.clamp(math.sin((hour - 6) / 24 * math.pi * 2) * 1.4 + 0.35, 0, 1)
    Lighting.FogColor = Color3.new(0.10 + 0.59 * daylight, 0.12 + 0.64 * daylight, 0.18 + 0.66 * daylight)
    wind.Volume = 0.05 + 0.1 * daylight

    -- The lantern is the inverse of the sun and it is not a step: it comes up
    -- through dusk and goes down through dawn, so there is no frame where the
    -- world changes lighting model between one tick and the next.
    lantern.Brightness = 5.5 * (1 - daylight)

    -- Camera first, because movement is expressed in the camera's frame: a
    -- character that walked in last frame's direction turns a fraction of a
    -- second after the camera does, which reads as drag on the controls.
    rig:Turn(TurnAction:GetState() :: Vector2, dt)
    -- Only while the pointer is locked. Unlocked, the motion belongs to whatever
    -- the player is doing with the cursor, and a camera that kept turning would
    -- be reading somebody else's input.
    if InputService.PointerLocked then
        rig:Look(LookAction:GetState() :: Vector2)
    end
    local forward, right = rig:Basis()

    character.WalkSpeed = if SprintAction:GetState() :: boolean then 13 else 7

    local axis = MoveAction:GetState() :: Vector2
    local move = forward * axis.Y + right * axis.X
    if vector.magnitude(move) > 0.001 then
        character:Move(vector.normalize(move))
    end

    if JumpAction:GetState() :: boolean and character.Grounded then
        jumpSound:Play()
        character:Jump()
    end

    if not character.Grounded then
        wasGrounded = false
    elseif not wasGrounded then
        wasGrounded = true
        landSound:Play()
    end

    -- Falling out of a STREAMED world means outrunning the ground rather than
    -- missing a platform, so the recovery is to put the character back on the
    -- height field above wherever it got to -- not at the start. Losing four
    -- kilometres of walking to a loading hitch would be the worse bug.
    local position = character.Position
    if position.y < DeathHeight then
        character.Position = vector.create(position.x, heightAt(position.x, position.z) + 4, position.z)
        recoverSound:Play()
    end

    -- The state a reload restores. Every half second rather than every tick: a
    -- reload is a human action and half a second of walking is a step.
    sinceSave += dt
    if sinceSave > 0.5 then
        sinceSave = 0
        HotReloadService:SaveState("walker", { position.x, position.y, position.z })
    end

    clockLabel.Text = `Day {clockText(hour)}`
    positionLabel.Text = string.format("%.0f, %.0f, %.0f", position.x, position.y, position.z)

    DebugService:SetCustomStat("WalkerX", position.x)
    DebugService:SetCustomStat("WalkerZ", position.z)
    DebugService:SetCustomStat("ClockTime", hour)

    if not renderIsDriving then
        autopilot(RunService.SimTime)
    end

    -- On the SIM clock, with the world. See `renderIsDriving` above for the
    -- measurement that put it here.
    rig:Update(dt)

    -- One tick of virtual press, then cleared, the same way `04-obby` does it:
    -- a touch button is an edge and an action reads a snapshot.
    InputService:SetVirtualState(Enum.KeyCode.Virtual1, 0)
end)

-- Connected for one purpose: to notice that something is drawing. The camera is
-- advanced on `Heartbeat` instead, which is what stops the world shaking.
RunService.PreRender:Connect(function()
    renderIsDriving = true
end)

DebugService.OverlayVisible = true
DebugService:ShowPanel("Streaming")
DebugService:ShowPanel("Stats")

print("10-open-world: walk. The world arrives as you go, and the sun does not wait.")
