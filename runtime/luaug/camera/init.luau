--!strict
--- `@luaug/camera` — the camera rigs (api-design.md §1.1, divergence 16).
---
--- There is no `CameraType` state machine here and no hidden controller: a
--- `Camera` is a scriptable Instance with a `CFrame`, and this is the rig code
--- that would otherwise be copied into every project. Three examples in this
--- repository hand-rolled the same forty lines before it existed.
---
--- **Advance a rig on `Heartbeat`, not on `PreRender`**, and that is the one
--- thing this module exists to stop people getting wrong. A camera on the render
--- clock moves every frame while the world it looks at moves once a tick, and
--- the difference between the two reads as the whole world vibrating (D047).
--- `PreRender` also never fires in a headless run, so a camera driven from there
--- does not move at all when a test drives the game — which is how a gate ends
--- up recording a picture of the default camera standing inside the ground.
---
--- Casing follows ADR 0034: this is a module, so `camera.thirdPerson` is
--- camelCase, and the rig it returns is an object, so `rig:Update()` is not.

local Camera = {}

export type Rig = {
    --- The camera this rig writes. Assigned to `workspace.CurrentCamera` by the
    --- constructor, because a rig nothing looks through is a rig nobody can see
    --- is working.
    Camera: Camera,
    --- What it follows. Reassignable: a game that swaps the player's body swaps
    --- this rather than building a second rig.
    Subject: BasePart,

    --- Where the rig is looking, in radians, and how high above the subject it
    --- sits. Written by `Turn`, readable because a game that wants to face the
    --- character the way the camera faces needs this number.
    Yaw: number,
    Height: number,
    Distance: number,

    --- Turns the rig. `delta` is an axis pair in the range [-1, 1] -- an
    --- `InputAction` of type `Direction2D` hands you exactly this -- and `dt`
    --- makes the rate frame-independent.
    Turn: (self: Rig, delta: Vector2, dt: number) -> (),
    --- Moves the camera towards where it should be. Call once per SIMULATION
    --- tick; see the note at the top for why not `PreRender`.
    Update: (self: Rig, dt: number) -> (),
    --- The rig's own forward and right on the ground plane, which is what a
    --- character's movement should be expressed in: pressing "forward" means
    --- away from the camera, not along the world's -Z.
    Basis: (self: Rig) -> (vector, vector),
}

export type Options = {
    Subject: BasePart,
    --- Metres behind the subject. 11 is a third-person distance for a character
    --- about two metres tall.
    Distance: number?,
    --- Metres above it. The pair (distance, height) is the whole framing.
    Height: number?,
    --- Metres above the subject's origin that the camera aims at. Aiming at the
    --- feet puts the character at the top of the frame, which is the most common
    --- way a first third-person camera looks wrong.
    Focus: number?,
    --- Radians per second at full stick deflection.
    TurnRate: number?,
    --- How fast the camera closes the distance to where it should be, per
    --- second. Higher is stiffer; the clamp inside `Update` is what stops a
    --- frame-rate hitch teleporting it.
    Smoothing: number?,
    MinHeight: number?,
    MaxHeight: number?,
    FieldOfView: number?,
    NearPlane: number?,
    FarPlane: number?,
}

local RigImpl = {}
RigImpl.__index = RigImpl

function RigImpl.Turn(self: any, delta: Vector2, dt: number)
    self.Yaw += self.TurnRate * delta.X * dt
    self.Height = math.clamp(self.Height + self.TurnRate * 3 * delta.Y * dt, self.MinHeight, self.MaxHeight)
end

function RigImpl.Basis(self: any): (vector, vector)
    local forward = vector.create(-math.sin(self.Yaw), 0, -math.cos(self.Yaw))
    local right = vector.create(math.cos(self.Yaw), 0, -math.sin(self.Yaw))
    return forward, right
end

function RigImpl.Update(self: any, dt: number)
    local focus = self.Subject.Position + vector.create(0, self.Focus, 0)
    local forward = vector.create(-math.sin(self.Yaw), 0, -math.cos(self.Yaw))
    local wanted = focus - forward * self.Distance + vector.create(0, self.Height, 0)

    local eye = self.Camera.CFrame.Position
    -- Frame-rate independent, and the clamp is what keeps a hitch from
    -- teleporting the camera: at 5 fps `Smoothing * dt` exceeds one, and an
    -- unclamped lerp past its endpoint overshoots.
    eye += (wanted - eye) * math.min(1, self.Smoothing * dt)
    self.Camera.CFrame = CFrame.lookAt(eye, focus)
end

--- A third-person follow rig: behind and above the subject, turned by an axis
--- pair, easing towards where it should be.
---
--- ```lua
--- local camera = require("@luaug/camera")
--- local rig = camera.thirdPerson({ Subject = character })
---
--- RunService.Heartbeat:Connect(function(dt)
---     rig:Turn(TurnAction:GetState() :: Vector2, dt)
---     local forward, right = rig:Basis()
---     -- ... move the character in that basis ...
---     rig:Update(dt)
--- end)
--- ```
function Camera.thirdPerson(options: Options): Rig
    local instance = Instance.new("Camera")
    instance.Name = "FollowCamera"
    instance.FieldOfView = options.FieldOfView or 70
    instance.NearPlane = options.NearPlane or 0.1
    instance.FarPlane = options.FarPlane or 1200
    instance.Parent = workspace
    workspace.CurrentCamera = instance

    local rig = setmetatable({
        Camera = instance,
        Subject = options.Subject,
        Yaw = 0,
        Distance = options.Distance or 11,
        Height = options.Height or 6,
        Focus = options.Focus or 2,
        TurnRate = options.TurnRate or 2.2,
        Smoothing = options.Smoothing or 8,
        MinHeight = options.MinHeight or 3,
        MaxHeight = options.MaxHeight or 24,
    }, RigImpl)

    -- Placed before the first update rather than eased in from the origin: a
    -- rig that starts at (0, 0, 0) spends its first second flying across the
    -- world, which is what every screenshot of frame one would show.
    local rigAny = rig :: any
    rigAny.Camera.CFrame = CFrame.lookAt(
        options.Subject.Position + vector.create(0, rigAny.Height, rigAny.Distance),
        options.Subject.Position + vector.create(0, rigAny.Focus, 0)
    )

    return (rig :: any) :: Rig
end

--- An orbit rig: the same framing, turning on its own at a fixed rate. What a
--- menu background, a spectator view or a screenshot harness wants.
function Camera.orbit(options: Options & { Speed: number? }): Rig
    local rig = Camera.thirdPerson(options)
    local speed = options.Speed or 0.35
    local rigAny = rig :: any
    local advance = rigAny.Update
    rigAny.Update = function(self: any, dt: number)
        self.Yaw += speed * dt
        advance(self, dt)
    end
    return rig
end

return Camera
