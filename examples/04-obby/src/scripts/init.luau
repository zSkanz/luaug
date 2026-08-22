--!strict
--- examples/04-obby — the M6 deliverable.
---
--- A game. Not a demonstration of a system: a course with a start and a finish,
--- a menu that tweens in front of it, a HUD that says where you are, platforms
--- that move because a tween drives them, sounds that mark what happened, and a
--- character with something animated riding on it. Every one of M6's five
--- systems is load-bearing here, and the point is that they have to work
--- TOGETHER — each of them is easy to certify against itself.
---
--- **It is also the milestone's end-to-end gate.** `tests/replay/obby` drives
--- this file from a recorded input stream, headless, and requires the run to end
--- with `game:SetAttribute("ObbyFinished", true)`. A determinism hash cannot say
--- whether the flag was reached — three runs that all fall in the same hole
--- agree perfectly — so the scenario names the fact and this file sets it.
---
--- Read it for these five things, in the order they appear:
---
---   1. **The course** is `Part`s. They are solid now (M6), which is what makes
---      this readable at all: the same scene at M5 was a wireframe.
---   2. **The platforms** are tweened `CFrame`s on anchored parts. A tween
---      writes through the same property setter a script writes through, so a
---      moving platform is kinematic to the solver without a second code path.
---   3. **The UI** is a `ScreenGui` tree with a `UIListLayout`, and the menu
---      arrives by tweening a `UDim2`. The HUD's jump button drives a real
---      `InputAction` through `SetVirtualState`, which is the non-device seam
---      the roadmap asked for and this is its proving caller.
---   4. **The sounds** are `Sound` instances on the SimClock. They play a
---      generated tone in v1 — M7's pipeline is what hands over a file — and
---      their timeline is the simulation's, so `Ended` lands on the same tick in
---      a replay as it does live.
---   5. **The animation** is a skinned glTF welded to the character, playing a
---      looping clip. `AnimationPlayer` under the `MeshPart`, one
---      `AnimationTrack`, and nothing per frame.

local AudioService = game:GetService("AudioService")
local InputService = game:GetService("InputService")
local Lighting = game:GetService("Lighting")
local RunService = game:GetService("RunService")
local TweenService = game:GetService("TweenService")
local UIService = game:GetService("UIService")

-- --- The course --------------------------------------------------------------

local Ground = Color3.fromRGB(84, 96, 88)
local Platform = Color3.fromRGB(196, 176, 120)
local Moving = Color3.fromRGB(120, 168, 214)
local Checkpoint = Color3.fromRGB(96, 212, 132)
local Finish = Color3.fromRGB(232, 196, 72)
local Hazard = Color3.fromRGB(214, 96, 72)

--- Below this and you have fallen off. One number rather than a trigger volume
--- under the course: a trigger you can miss is a way to fall forever.
local DeathHeight = -18

local function block(name: string, position: vector, size: vector, color: Color3): Part
    local part = Instance.new("Part")
    part.Name = name
    part.Size = size
    part.Position = position
    part.Color = color
    part.Anchored = true
    part.Parent = workspace
    return part
end

--- The course, in the order you meet it.
---
--- **Every number here is derived from what the character can do rather than
--- chosen.** Gravity is 9.81 m/s^2 and this engine's units are METRES: at
--- `JumpSpeed` 5.5 a jump rises 1.54 m, stays up 1.12 s and carries 5.6 m at a
--- level landing, which was measured off a traced run rather than worked out on
--- paper. So the rule the whole course obeys is **a gap of at most 3 m and a
--- rise of at most 0.8 m**, which spends about half a jump. This is the engine's
--- example, not a test of anybody's timing.
---
--- `position` is the part's CENTRE and every plank is a metre thick, so the
--- surface you stand on is `y + 0.5` and a four-metre character standing on it
--- reads `Position.y = y + 2.5`.
---
--- **A gap of half a metre is a walk and not a jump.** The character is a
--- two-metre capsule, so its underside bridges anything narrower than its own
--- radius; that is what joins the moving platforms below to the ledges they
--- serve without either overlapping the other, which two solid bodies must not
--- do.
local Course = {
    -- Wide enough to stand on while the menu is up.
    { name = "Start", position = vector.create(0, 0.0, 0), size = vector.create(12, 1, 12), color = Ground },

    -- Four hops, each three metres out and eighty centimetres up. Four rather
    -- than M6's three, and the size is 4x4 rather than 6x6: a landing pad wider
    -- than the jump is not a jump.
    { name = "Hop1", position = vector.create(0, 0.0, -11), size = vector.create(4, 1, 4), color = Platform },
    { name = "Hop2", position = vector.create(0, 0.8, -18), size = vector.create(4, 1, 4), color = Platform },
    { name = "Hop3", position = vector.create(0, 1.6, -25), size = vector.create(4, 1, 4), color = Platform },
    { name = "Hop4", position = vector.create(0, 2.4, -32), size = vector.create(4, 1, 4), color = Platform },

    -- The first landing: wide, flat, and a metre from Hop4 so it is stepped onto
    -- rather than jumped to. A checkpoint belongs where you can stand still.
    { name = "Ledge1", position = vector.create(0, 2.4, -40), size = vector.create(10, 1, 10), color = Ground },

    -- The second landing, at the top of the lift.
    { name = "Ledge2", position = vector.create(0, 5.9, -57), size = vector.create(10, 1, 10), color = Ground },

    -- **The gorge floor, and it is why the ferry is not a trap.** M6's ferry
    -- crossed open air: a player who walked off the ledge while it was away fell
    -- to the death height and started the section again, with nothing to say
    -- that waiting was the answer. Five and a half metres down there is a floor
    -- now, and beside it a staircase back up -- so missing the ferry costs a
    -- climb rather than the run. The drop is deliberately more than the
    -- character is tall, or the ferry would sweep through anybody standing on
    -- the floor beneath it.
    { name = "Gorge", position = vector.create(0, 0.4, -70), size = vector.create(16, 1, 26), color = Ground },
    { name = "Climb1", position = vector.create(7, 1.6, -64), size = vector.create(3, 1, 3), color = Platform },
    { name = "Climb2", position = vector.create(7, 2.8, -61), size = vector.create(3, 1, 3), color = Platform },
    { name = "Climb3", position = vector.create(7, 4.0, -58), size = vector.create(3, 1, 3), color = Platform },
    { name = "Climb4", position = vector.create(7, 5.2, -55), size = vector.create(3, 1, 3), color = Platform },

    -- The far side of the ferry, and the last place to stand still.
    { name = "Summit", position = vector.create(0, 5.9, -85), size = vector.create(12, 1, 12), color = Ground },

    -- The run home: three more hops to the flag, so the course ends on the thing
    -- it opened with instead of on a walk.
    { name = "Stone1", position = vector.create(0, 6.7, -95), size = vector.create(4, 1, 4), color = Platform },
    { name = "Stone2", position = vector.create(0, 7.5, -102), size = vector.create(4, 1, 4), color = Platform },
    { name = "Stone3", position = vector.create(0, 8.3, -109), size = vector.create(4, 1, 4), color = Platform },
    { name = "FinishLedge", position = vector.create(0, 8.3, -117), size = vector.create(8, 1, 8), color = Ground },
}

for _, piece in Course do
    block(piece.name, piece.position, piece.size, piece.color)
end

-- A hazard, beside the path rather than across it. Touching it costs a
-- checkpoint's worth of progress, and walking down the middle misses it by
-- nearly two metres -- which is what makes it a thing to notice rather than a
-- thing to time. It sits further out than M6's did, because `Touched` fires for
-- a SIDE contact now (D028) and a hazard you can brush is a hazard that fires
-- when you did nothing wrong.
local spike = block("Spike", vector.create(3.8, 3.2, -40), vector.create(2, 0.6, 8), Hazard)

-- --- Moving platforms --------------------------------------------------------
--
-- Anchored parts whose `CFrame` a tween writes. That IS the kinematic case: the
-- solver sees a body it does not integrate, moved between ticks, and a character
-- standing on one is carried by the contact rather than by a script parenting it
-- somewhere. The deliberate integration stressor the roadmap named.
--
-- `RepeatCount = -1` is forever and `Reverses = true` is there-and-back, so
-- neither of these needs a handler to turn it around. `Sine`/`InOut` is what
-- makes them ridable: a platform that changed direction at full speed would
-- shear a character off it, and this one comes to rest at each end.
--
-- **`DelayTime` is what makes them boardable, and it is new.** The delay runs
-- after a whole there-and-back, which puts it at the START of the travel -- so
-- each of these waits at the end you get on at. M6 had neither, and boarding was
-- a timing puzzle nothing in the level told you you were playing: arrive at the
-- wrong moment and the platform is five metres over your head.

--- How long a platform waits at the end you board it from, in seconds. Long
--- enough to walk on without hurrying and short enough not to feel broken.
local BoardingRest = 1.6

local function shuttle(name: string, from: vector, to: vector, size: vector, seconds: number): Part
    local part = block(name, from, size, Moving)
    local info = TweenInfo.new(seconds, Enum.EasingStyle.Sine, Enum.EasingDirection.InOut, -1, true, BoardingRest)
    TweenService:Create(part, info, { CFrame = CFrame.new(to) }):Play()
    return part
end

-- The lift: up and down, boarded at the bottom and left at the top.
--
-- **Its travel is three and a half metres on purpose, and that is the guard
-- rail.** The character is four metres tall, so whenever the lift is anywhere
-- but down, its underside is at chest height and the way in is blocked -- you
-- cannot walk into the shaft, ever, and there is nothing to fall into. A taller
-- lift would need a wall; this one is its own.
shuttle("Lift", vector.create(0, 2.4, -48.5), vector.create(0, 5.9, -48.5), vector.create(6, 1, 6), 2.2)

-- The ferry: along the course rather than across it, which is the shape a player
-- can board without timing a crossing. It rests against `Ledge2` for
-- `BoardingRest`, carries you ten metres, and sets you down half a metre from
-- the summit.
shuttle("Ferry", vector.create(0, 5.9, -65.5), vector.create(0, 5.9, -75.5), vector.create(6, 1, 6), 2.4)

-- --- Checkpoints -------------------------------------------------------------

--- Where a fall puts you back. Starts at the start pad, which is why the first
--- entry is not a checkpoint part.
local respawn = vector.create(0, 3, 0)
local checkpointsReached = 0

--- One per section, each on a surface you can stand still on. Three rather than
--- M6's two: the run home is three jumps over open air, and a course that sent
--- you back across the ferry for missing the last one would be punishing the
--- wrong mistake.
local CheckpointSpots = {
    vector.create(0, 3.1, -40),
    vector.create(0, 6.6, -57),
    vector.create(0, 6.6, -85),
}

-- --- Sounds ------------------------------------------------------------------
--
-- On the SimClock, so `Ended` lands on the same tick in a replay. `Content` is
-- stored and not yet read (M7), so each of these plays a generated tone of its
-- declared length -- which is enough to hear that the right thing happened at
-- the right moment, and is the honest state of the audio path in v1.

local function sound(name: string, volume: number, speed: number): Sound
    local instance = Instance.new("Sound")
    instance.Name = name
    instance.Volume = volume
    instance.PlaybackSpeed = speed
    instance.Parent = AudioService
    return instance
end

local jumpSound = sound("Jump", 0.25, 1.6)
local checkpointSound = sound("Checkpoint", 0.4, 1.2)
local fallSound = sound("Fall", 0.35, 0.6)
local landSound = sound("Land", 0.2, 0.5)
local finishSound = sound("Finish", 0.5, 0.9)

-- --- The character -----------------------------------------------------------

local character = Instance.new("CharacterBody")
character.Name = "Player"
character.Size = vector.create(2, 4, 2)
character.Position = respawn
character.Color = Color3.fromRGB(96, 172, 220)
character.WalkSpeed = 5
-- Gravity is 9.81 m/s^2 and this engine's units are METRES (divergence 15), so a
-- jump speed of 5.5 is a rise of 1.5 m and about 1.1 s in the air. The number that
-- looks right from a platformer measured in studs is four times too big here,
-- and a character that clears the whole course in one leap is what that mistake
-- looks like.
character.JumpSpeed = 5.5
character.Parent = workspace

-- The animated part. A skinned glTF welded to the capsule: the weld drives it
-- every tick and never simulates it, so it rides the character through every
-- contact, and the `AnimationPlayer` under it plays a looping clip.
local waver = Instance.new("MeshPart")
waver.Name = "Waver"
waver.MeshContent = "asset://models/waver.gltf" :: Content
waver.Anchored = false
waver.Parent = workspace

local waverWeld = Instance.new("Weld")
waverWeld.Part0 = character
waverWeld.Part1 = waver
waverWeld.C0 = CFrame.new(0, 3.4, 0)
waverWeld.Parent = workspace

local waverPlayer = Instance.new("AnimationPlayer")
waverPlayer.Parent = waver

-- --- Camera and sky ----------------------------------------------------------

local camera = Instance.new("Camera")
camera.Name = "FollowCamera"
camera.FieldOfView = 65
camera.NearPlane = 0.1
camera.FarPlane = 600
camera.Parent = workspace
workspace.CurrentCamera = camera

Lighting.ClockTime = 9.5
Lighting.GeographicLatitude = 25
Lighting.Ambient = Color3.fromRGB(13, 15, 18)
Lighting.Brightness = 2.6
Lighting.FogColor = Color3.fromRGB(168, 186, 208)
Lighting.FogStart = 120
Lighting.FogEnd = 400

-- --- Input -------------------------------------------------------------------
--
-- Movement and jump on the SimClock, where a replay can see them; the camera on
-- the render clock, because a camera should turn at the frame rate (ADR 0039).
-- The same split `examples/03-physics-playground` demonstrates, and this file
-- adds the third case: a virtual channel a HUD button writes into.

local Gameplay = Instance.new("InputContext")
Gameplay.Name = "Gameplay"
Gameplay.Enabled = false
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

-- **The proving caller for the non-device seam.** The HUD button below writes
-- `Virtual1`, and this binding is what makes that a real press of the same
-- action the keyboard drives: same context, same clock, same sinking, and in the
-- recorded stream, which is why the replay gate can see it.
local JumpTouch = Instance.new("InputBinding")
JumpTouch.KeyCode = Enum.KeyCode.Virtual1
JumpTouch.DisplayName = "ui.obby.jump"
JumpTouch.Parent = JumpAction

local CameraControls = Instance.new("InputContext")
CameraControls.Name = "CameraControls"
CameraControls.Rate = Enum.InputRate.Render
CameraControls.Enabled = false
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

-- --- The UI ------------------------------------------------------------------

local screen = Instance.new("ScreenGui")
screen.Name = "Obby"
screen.Parent = UIService

--- The menu panel, off the top of the screen until it tweens in. A `UDim2` with
--- a scale is what makes that one property rather than four.
local menu = Instance.new("Frame")
menu.Name = "Menu"
menu.Size = UDim2.new(0, 360, 0, 220)
menu.Position = UDim2.new(0.5, 0, -0.5, 0)
menu.AnchorPoint = Vector2.new(0.5, 0.5)
menu.BackgroundColor = Color3.fromRGB(24, 28, 40)
menu.BackgroundTransparency = 0.1
menu.Parent = screen

local menuCorner = Instance.new("UICorner")
menuCorner.CornerRadius = UDim.new(0, 12)
menuCorner.Parent = menu

local menuLayout = Instance.new("UIListLayout")
menuLayout.FillDirection = Enum.FillDirection.Vertical
menuLayout.Padding = UDim.new(0, 12)
menuLayout.HorizontalAlignment = Enum.HorizontalAlignment.Center
menuLayout.VerticalAlignment = Enum.VerticalAlignment.Center
menuLayout.Parent = menu

local title = Instance.new("TextLabel")
title.Name = "Title"
title.Size = UDim2.new(1, -32, 0, 48)
title.Text = "OBBY"
title.TextSize = 40
title.TextColor = Color3.fromRGB(240, 236, 224)
title.BackgroundTransparency = 1
title.LayoutOrder = 1
title.Parent = menu

local hint = Instance.new("TextLabel")
hint.Name = "Hint"
hint.Size = UDim2.new(1, -32, 0, 40)
hint.Text = "WASD to walk, Space to jump, arrows to look.\nSeven jumps, a lift and a ferry."
hint.TextSize = 14
hint.TextColor = Color3.fromRGB(180, 188, 204)
hint.BackgroundTransparency = 1
hint.TextWrapped = true
hint.LayoutOrder = 2
hint.Parent = menu

local play = Instance.new("TextButton")
play.Name = "Play"
play.Size = UDim2.new(0, 160, 0, 44)
play.Text = "PLAY"
play.TextSize = 20
play.TextColor = Color3.fromRGB(16, 20, 28)
play.BackgroundColor = Color3.fromRGB(120, 214, 156)
play.LayoutOrder = 3
play.Parent = menu

local playCorner = Instance.new("UICorner")
playCorner.CornerRadius = UDim.new(0, 8)
playCorner.Parent = play

-- The HUD. Hidden until the menu closes, so the first frame is a menu over a
-- world rather than a menu over a HUD.
local hud = Instance.new("Frame")
hud.Name = "Hud"
hud.Size = UDim2.new(1, 0, 0, 44)
hud.Position = UDim2.new(0, 0, 0, 0)
hud.BackgroundColor = Color3.fromRGB(18, 20, 28)
hud.BackgroundTransparency = 0.35
hud.Visible = false
hud.Parent = screen

local status = Instance.new("TextLabel")
status.Name = "Status"
status.Size = UDim2.new(0, 320, 1, 0)
status.Position = UDim2.new(0, 16, 0, 0)
status.Text = "Checkpoint 0 / 3"
status.TextSize = 16
status.TextColor = Color3.fromRGB(232, 236, 244)
status.HorizontalAlignment = Enum.HorizontalAlignment.Left
status.BackgroundTransparency = 1
status.Parent = hud

--- What to do next, in words.
---
--- M6 had a status line that counted checkpoints and nothing that said what the
--- course wanted. That is fine while every obstacle is a jump; it is not fine at
--- a lift, where the correct move is to STAND STILL for a second and nothing on
--- screen says so. A player who does not know that reads a platform overhead as
--- a wall and concludes the course is broken.
local objective = Instance.new("TextLabel")
objective.Name = "Objective"
objective.Size = UDim2.new(0, 420, 1, 0)
objective.Position = UDim2.new(0, 344, 0, 0)
objective.Text = "Jump the gaps"
objective.TextSize = 16
objective.TextColor = Color3.fromRGB(196, 206, 224)
objective.HorizontalAlignment = Enum.HorizontalAlignment.Left
objective.BackgroundTransparency = 1
objective.Parent = hud

--- The touch jump button. This is the roadmap's proving caller: a `TextButton`
--- driving a real `InputAction`, useful on a desktop on its own, and going
--- through the same dispatch as the keyboard rather than calling `Jump` behind
--- the input system's back.
local touchJump = Instance.new("TextButton")
touchJump.Name = "TouchJump"
touchJump.Size = UDim2.new(0, 120, 0, 40)
touchJump.Position = UDim2.new(1, -136, 0, 2)
touchJump.Text = "JUMP"
touchJump.TextSize = 16
touchJump.TextColor = Color3.fromRGB(16, 20, 28)
touchJump.BackgroundColor = Color3.fromRGB(214, 196, 120)
touchJump.Parent = hud

local touchCorner = Instance.new("UICorner")
touchCorner.CornerRadius = UDim.new(0, 6)
touchCorner.Parent = touchJump

--- The finish panel, off the bottom until it is earned.
local finishPanel = Instance.new("Frame")
finishPanel.Name = "Finished"
finishPanel.Size = UDim2.new(0, 320, 0, 120)
finishPanel.Position = UDim2.new(0.5, 0, 1.5, 0)
finishPanel.AnchorPoint = Vector2.new(0.5, 0.5)
finishPanel.BackgroundColor = Color3.fromRGB(28, 40, 30)
finishPanel.Parent = screen

local finishLabel = Instance.new("TextLabel")
finishLabel.Name = "Label"
finishLabel.Size = UDim2.new(1, 0, 1, 0)
finishLabel.Text = "FINISHED"
finishLabel.TextSize = 32
finishLabel.TextColor = Color3.fromRGB(180, 240, 190)
finishLabel.BackgroundTransparency = 1
finishLabel.Parent = finishPanel

-- --- Play --------------------------------------------------------------------

local playing = false
local finished = false

local function startPlaying()
    if playing then
        return
    end
    playing = true
    Gameplay.Enabled = true
    CameraControls.Enabled = true
    hud.Visible = true
    -- Out of the way rather than destroyed: a menu that comes back is one
    -- property, and `Visible` on a Frame that is off-screen costs no layout.
    TweenService:Create(menu, TweenInfo.new(0.35, Enum.EasingStyle.Back, Enum.EasingDirection.In), {
        Position = UDim2.new(0.5, 0, -0.5, 0),
    }):Play()
end

play.Activated:Connect(startPlaying)

-- The menu arrives. `Back`/`Out` overshoots, which is the whole reason the
-- easing families are conformant to a table rather than approximated.
TweenService:Create(menu, TweenInfo.new(0.6, Enum.EasingStyle.Back, Enum.EasingDirection.Out), {
    Position = UDim2.new(0.5, 0, 0.5, 0),
}):Play()

-- **A key starts the game as well as the button.** The gate's recorded stream is
-- keys, and a menu only a pointer could dismiss would make the whole course
-- unreachable headless -- which is the sort of thing that turns an end-to-end
-- gate into a test of the first four seconds.
InputService.InputBegan:Connect(function(input: InputObject, uiConsumed: boolean)
    if uiConsumed or playing then
        return
    end
    if input.KeyCode == Enum.KeyCode.Return or input.KeyCode == Enum.KeyCode.Space then
        startPlaying()
    end
end)

-- The touch button, both edges. `Activated` fires on release, so the press is
-- driven from `PointerEntered`/`PointerExited` plus the pointer's own state --
-- which for a jump is exactly right: a jump is an edge, not a hold.
touchJump.Activated:Connect(function()
    InputService:SetVirtualState(Enum.KeyCode.Virtual1, 1)
    -- Cleared on the next tick by the loop below, so the action sees one tick of
    -- press. A button that latched would be a jump held forever.
end)

-- --- Checkpoints, falling and the flag ---------------------------------------

--- What the section AFTER each checkpoint asks for. Indexed the same way, so
--- reaching checkpoint 1 puts up the instruction for the lift that follows it.
local Objectives = {
    "Wait for the lift, then ride it up",
    "Wait for the ferry, then ride it across",
    "Three jumps to the flag",
}

for index, spot in CheckpointSpots do
    local pad = block(`Checkpoint{index}`, spot, vector.create(4, 0.4, 4), Checkpoint)
    pad.Touched:Connect(function(other: BasePart)
        if other ~= (character :: BasePart) or index <= checkpointsReached then
            return
        end
        checkpointsReached = index
        respawn = spot + vector.create(0, 3, 0)
        status.Text = `Checkpoint {checkpointsReached} / {#CheckpointSpots}`
        objective.Text = Objectives[index] or "Reach the flag"
        checkpointSound:Play()
    end)
end

-- The finish. A PAD you stand on rather than a pole you walk into: `Touched`
-- fires for the surface under a character's feet and not for a wall it presses
-- against (D028), so a finish line has to be something you step on. The pole
-- behind it is the thing you aim at.
local flag = block("Finish", vector.create(0, 9.0, -117), vector.create(6, 0.4, 6), Finish)
block("Flagpole", vector.create(0, 10.5, -120), vector.create(3, 3, 0.5), Finish)

flag.Touched:Connect(function(other: BasePart)
    if other ~= (character :: BasePart) or finished then
        return
    end
    finished = true
    -- **The gate's fact.** `tests/replay/obby` requires this attribute, and it is
    -- set here rather than on the way out so that a run which never reached the
    -- flag cannot claim it did.
    game:SetAttribute("ObbyFinished", true)
    status.Text = "Finished"
    finishSound:Play()
    TweenService:Create(finishPanel, TweenInfo.new(0.5, Enum.EasingStyle.Back, Enum.EasingDirection.Out), {
        Position = UDim2.new(0.5, 0, 0.5, 0),
    }):Play()
end)

spike.Touched:Connect(function(other: BasePart)
    if other == (character :: BasePart) then
        character.Position = respawn
        fallSound:Play()
    end
end)

-- --- The frame ---------------------------------------------------------------

local cameraYaw = 0
local cameraHeight = 8
local wasGrounded = true
local clipStarted = false

RunService.Heartbeat:Connect(function(dt: number)
    -- The animation track is loaded once, on the first frame the mesh has
    -- actually been read: a track made before then finds no skeleton, and its
    -- own doc says to load one and keep it.
    if not clipStarted then
        local track = waverPlayer:LoadAnimation("Bend")
        if track.Length > 0 then
            track.Looped = true
            track:Play(0.2)
            clipStarted = true
        end
    end

    local turn = TurnAction:GetState() :: Vector2
    cameraYaw += 2.2 * turn.X * dt
    cameraHeight = math.clamp(cameraHeight + 6 * turn.Y * dt, 4, 22)

    local forward = vector.create(-math.sin(cameraYaw), 0, -math.cos(cameraYaw))
    local right = vector.create(math.cos(cameraYaw), 0, -math.sin(cameraYaw))

    local axis = MoveAction:GetState() :: Vector2
    local move = forward * axis.Y + right * axis.X
    if vector.magnitude(move) > 0.001 then
        character:Move(vector.normalize(move))
    end

    if JumpAction:GetState() :: boolean and character.Grounded then
        -- Already written this way, because it needed to know whether the jump
        -- actually happened in order to play a sound -- which is the roadmap's
        -- own observation about why the mechanism belongs to the game.
        jumpSound:Play()
        character:Jump()
    end

    -- One tick of virtual press, then cleared. The HUD button is an edge and the
    -- action reads a snapshot, so the value has to be there for exactly one
    -- dispatch and gone by the next.
    InputService:SetVirtualState(Enum.KeyCode.Virtual1, 0)

    -- Landing is a transition and `wasGrounded` is what makes it one. It was
    -- computed and never read through M6, which is the same shape as `UICorner`
    -- being stored and never drawn (D030) -- a variable nothing consumes is a
    -- feature nobody finished.
    if not character.Grounded then
        wasGrounded = false
    elseif not wasGrounded then
        wasGrounded = true
        landSound:Play()
    end

    if character.Position.y < DeathHeight then
        character.Position = respawn
        fallSound:Play()
    end

    local focus = character.Position + vector.create(0, 2.5, 0)
    local wanted = focus - forward * 14 + vector.create(0, cameraHeight, 0)
    local eye = camera.CFrame.Position
    eye += (wanted - eye) * math.min(1, 8 * dt)
    camera.CFrame = CFrame.lookAt(eye, focus)
end)
