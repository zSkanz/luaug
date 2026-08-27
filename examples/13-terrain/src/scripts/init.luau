--!strict
--[[
	Terrain, sculpted from a script (F1, ADR 0067).

	**The point of this example is that there is nothing special here.** A
	`Terrain` is an ordinary instance, its verbs are ordinary method calls, and
	the ground they produce is drawn, collided and hashed by the same code as
	everything else in the engine. What a sculpting brush does in the editor is
	exactly what this file does, one call at a time.

	It also demonstrates the thing a height map cannot do: the cave under the
	hill is a hole with ground above AND below it, which is why the owner's
	answer to "height field or voxel" decided the representation.
]]

local Workspace = game:GetService("Workspace")

--- A material id. Zero means "remove", so every non-zero value is some kind of
--- ground; the palette is a game's business and one is enough to show a shape.
local Rock = 1
--- A second material, so painting has somewhere to go. What a palette means is
--- a game's business; two is enough to show that changing one is not a sculpt.
local Sand = 2

local terrain = Instance.new("Terrain")
terrain.Name = "Ground"
-- Reserved BEFORE anything is sculpted, and it cannot be widened afterwards: a
-- collider's height precision is spread across this range when it is built.
terrain.MinHeight = -64
terrain.MaxHeight = 64
terrain.Parent = Workspace

-- **A plateau standing on the world's floor, not a slab floating above it.**
--
-- The height encoding means "solid for every y below H", so ground has to reach
-- all the way down: a block with air under it is genuinely not a height
-- function, and every column of it would carry voxels. So this spans from BELOW
-- `MinHeight` up to zero -- past the floor rather than onto it, because a brush
-- that stops exactly at the floor leaves the sample there outside itself.
--
-- The rule, once: a fill that reaches the floor makes ground, and one that does
-- not makes a slab. Both are legal and the second costs voxels.
terrain:FillBlock(vector.create(0, -40, 0), vector.create(48, 80, 48), Rock)

-- A hill on top of it, as three overlapping balls. Still cheap, for the same
-- reason: adding to the top of ground moves the surface up and leaves one
-- surface.
terrain:FillBall(vector.create(0, 2, 0), 9, Rock)
terrain:FillBall(vector.create(6, 1, 4), 6, Rock)
terrain:FillBall(vector.create(-5, 1, -3), 7, Rock)

-- **And the cave, which is the whole reason this is a volume.** Removing a ball
-- from INSIDE the hill leaves air with ground above it, so those columns stop
-- being a height function and promote to voxels -- and a height map could not
-- express this at all.
terrain:FillBall(vector.create(0, -1, 0), 4, 0)

local height = terrain:HeightAt(0, 0)

-- **Painting, which is the third verb and the one that changes nothing about
-- where the ground is.** It writes material where there is already ground and
-- nowhere else -- so it creates no voxel brick, promotes no column, and leaves
-- this hill exactly as cheap as it was a line ago. That is why it is a verb of
-- its own rather than a `FillBall` with a different number: sculpting and
-- recolouring are two gestures a person makes for two reasons.
--
-- Material zero is refused here rather than treated as erase. Zero means erase
-- to `FillBall`, and picking the first entry of a material list must not delete
-- the hillside you were about to paint.
local painted = terrain:PaintBall(vector.create(0, height or 0, 0), 7, Sand)

-- The height is the top of the first ball -- centre 2, radius 9 -- so it should
-- read exactly eleven metres. It read 10.52 until D153 was fixed, because a
-- sample lying exactly ON a flat brush face was not counted as the brush's and
-- the surface crossing was found a voxel low.
print(
    string.format(
        "terrain: %d cell(s), ground at the middle is %.2f m, %d cell(s) painted",
        terrain.CellCount,
        height or 0,
        painted
    )
)

-- Something to stand on it, so the collider is visible as behaviour rather than
-- only as a picture.
local crate = Instance.new("Part")
crate.Name = "Crate"
crate.Size = vector.create(2, 2, 2)
crate.Position = vector.create(0, 20, 0)
crate.Color = Color3.fromRGB(200, 120, 60)
crate.Parent = Workspace

local camera = Instance.new("Camera")
camera.Name = "Camera"
camera.CFrame = CFrame.lookAt(vector.create(28, 22, 34), vector.create(0, 2, 0))
camera.Parent = Workspace
Workspace.CurrentCamera = camera

local light = Instance.new("PointLight")
light.Name = "Sun"
light.Brightness = 4
light.Range = 120
light.Parent = crate
