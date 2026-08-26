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
local StartHour = 22.5

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
--- The spawn is in the SCENE now, and this comment is the record of where it
--- came from: `16, 16`, which is the centre of a ground tile for the reason
--- above. Somebody who moves the character in the editor and saves moves the
--- spawn, which is what a scene being the world means.

--- Below this you have fallen out of the world, which in a streamed world means
--- you outran the ground. Recovered rather than fatal.
local DeathHeight = -60

-- --- The character -----------------------------------------------------------

--- The island from `tools/generate_world.luau`, repeated here rather than
--- shared. **Deliberate**: the generator runs in Lute at build time and this
--- runs in the engine at play time, so there is no module both can require
--- without the world's shape becoming a runtime dependency of the build. Two
--- copies, with this comment on both, beats a coupling.
---
--- **They have to move together, and three other things move with them**: the
--- spawn in the scene, the four lantern posts in `Scenery`, and
--- `AutopilotFlyRadius` below. Changing the field and not the scene buries the
--- character in the ground -- which is what happened the first time this island
--- was generated, and is why they are listed here rather than remembered.
local function heightAt(x: number, z: number): number
    local distance = math.sqrt(x * x + z * z)
    local land = math.clamp(1 - distance / 820, 0, 1)
    land = land * land * (3 - 2 * land)
    local seabed = math.clamp((distance - 820) / 260, 0, 1)
    seabed = seabed * seabed * (3 - 2 * seabed)
    return 13 * land + 1.4 * math.sin(x / 400) * math.cos(z / 360) * land - 9 * seabed
end

--- **The world this project starts with is a FILE, and this script is what it
--- does with it** (ADR 0047). `content/scenes/main.scene.json` holds four things
--- at the top of `Workspace` and nothing loose:
---
---   `Player`    a `Model`: the character, the capsule welded to it, and the
---               lantern on its shoulder -- each weld parented to the part it
---               attaches rather than lying beside them
---   `Scenery`   what is AUTHORED rather than streamed. Four lantern posts,
---               placed as stamps of `content/stamps/lanternpost.stamp.json`,
---               so changing that one file moves all four
---   `Input`     the action map, in a folder, because it is configuration and
---               not world
---   the camera
---
--- `luaug.toml` names the scene a run begins with, and the editor writes back to
--- that same file.
---
--- **That is the arrangement Unity and Unreal have**, and the reason it is worth
--- the migration is not tidiness: while this script BUILT the world, saving a
--- scene of it and reopening produced two of everything -- the file put a
--- character back and the code made another, and no engine can merge those
--- (D074). A world with one source has one character.
---
--- Nothing here is a compromise for the editor's sake. A part moved with the
--- manipulator, a colour typed into the properties grid and a `PointLight`
--- somebody adds are all kept by Save, and this script finds whatever the file
--- holds by the names it agreed to.
--- **A dotted path rather than a name**, because the scene now has a shape.
--- The rig is a `Model`, the input map is in a `Folder`, and every weld lives on
--- the part it attaches -- which is what a person expects to find in an Explorer
--- and what `Model.StreamingMode` will need when E5 gives the model a say in
--- what streams together (ADR 0053).
---
--- The assert names the path rather than the leaf: `Player.Walker` missing and
--- `Walker` missing are two different mistakes, and the second one used to be
--- reported for both.
local function fromScene(path: string): Instance
    local at: Instance = workspace
    for segment in string.gmatch(path, "[^%.]+") do
        local found = at:FindFirstChild(segment)
        assert(found ~= nil, `the scene has no "{path}" -- stopped at "{segment}" under {at.Name}`)
        at = found
    end
    return at
end

local character = fromScene("Player.Walker") :: CharacterBody
local lantern = fromScene("Player.LanternPost.Lantern") :: PointLight

-- **Hot reload keeps your place.** `SaveState` takes numbers and tables of
-- them, not a `vector` -- the value is copied engine-side at the moment of the
-- call, and a vector is not one of the types that survives that. Three numbers
-- is the whole of it.
local saved = HotReloadService:LoadState("walker")
if HotReloadService:IsReload() and typeof(saved) == "table" then
    character.Position = vector.create(saved[1] :: number, saved[2] :: number, saved[3] :: number)
end
-- **A cold boot leaves the character where the SCENE put it.** It used to be
-- moved to `StartX, StartZ` here every time, which was right while this script
-- built the world and is wrong now that a file does: somebody who drags the
-- character somewhere in the editor and saves would have it teleported back on
-- the next run, which is a save that did not save. The spawn is in the scene,
-- and `StartX`/`StartZ` are what generated it.

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

--- **The input map is in the scene too**, because it is a fact about this
--- world rather than a thing this script decides -- an `InputContext` under
--- `Workspace` is an object like any other, and the file holds it. What stays
--- here is what the actions MEAN, which is the half a file cannot say.
---
--- The two contexts are separate for the reason the comments below the lookups
--- keep: movement and jump run on the SimClock where a replay can see them, and
--- the camera runs on the render clock because a camera should turn at the
--- frame rate (ADR 0039).
local Gameplay = fromScene("Input.Gameplay")
local CameraControls = fromScene("Input.CameraControls")

local function action(context: Instance, name: string): InputAction
    local found = context:FindFirstChild(name)
    assert(found ~= nil, `the scene's {context.Name} has no "{name}" action`)
    return found :: InputAction
end

local MoveAction = action(Gameplay, "Move")
local JumpAction = action(Gameplay, "Jump")
local SprintAction = action(Gameplay, "Sprint")
--- Left Alt locks and unlocks the pointer. On the gameplay context rather than
--- the camera one because it is a state change rather than a camera motion, and
--- on the SIM clock so a replay sees it happen on a tick like everything else.
local PointerAction = action(Gameplay, "TogglePointer")

local TurnAction = action(CameraControls, "Turn")
--- The mouse, and it is a SEPARATE action from `Turn` on purpose.
---
--- A stick is a rate and a mouse is a displacement: `MouseMovement` reports the
--- pointer's motion this tick in pixels, which is already the amount to turn by.
--- Feeding both into one action would mean one of them was scaled by `dt`
--- wrongly -- and a mouse multiplied by a frame time turns twice as far on a
--- slow machine, which is the most common way a look control feels broken.
--- `rig:Turn` takes the rate and `rig:Look` takes the displacement.
local LookAction = action(CameraControls, "Look")

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
-- Just inside the shore, so the flight arcs over the coastline rather than
-- out across empty water: the beach meeting the sea is the thing worth
-- seeing from the air, and past 820 m there is nothing but ocean.
local AutopilotFlyRadius = 780
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
