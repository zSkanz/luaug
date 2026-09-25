# Ragdolls

A ragdoll makes a character's pose come from the simulation instead of from an
animation clip. It is how a character falls down, stumbles or goes limp.
`examples/12-ragdoll` has figures that fall, blend and get up again.

## It is nothing but instances

A `Ragdoll` owns nothing. It is a flag, parented to the `MeshPart` whose
skeleton it drives, that says "take the pose from the limbs under me". The limbs
are ordinary instances you can see, select, move, retune and delete:

- a **`Part`** per limb, simulated like any other part;
- a **`Bone`** on each, naming the joint of the skeleton that limb stands for;
- a **constraint** (`BallSocketConstraint` or `HingeConstraint`) holding it to
  the limb above.

Each tick, every `Bone` under the ragdoll that names a joint tells the skeleton
where that joint is: wherever the simulation put its part. Joints nobody drives
keep their place relative to their parent, so **a partial ragdoll works**:
simulate a dozen bones and the fingers ride along on the wrist.

## Building the limbs

Writing a limb for every joint by hand is arithmetic, and `Ragdoll:Build` does it
from a **profile**, a list of limbs. `@luaug/ragdoll` ships a humanoid one that
fits the common rigs.

```luau
--!strict
local RagdollProfiles = require("@luaug/ragdoll")

local mesh = Instance.new("MeshPart")
mesh.MeshContent = "asset://models/humanoid.gltf" :: Content
mesh.Anchored = true
mesh.Parent = workspace

local ragdoll = Instance.new("Ragdoll")
ragdoll.Parent = mesh

-- Build reads the skeleton, which arrives when the file has loaded, so wait
-- for it rather than building at the top of the script.
task.wait(0.5)
local limbs = ragdoll:Build(RagdollProfiles.Humanoid)
print("built", limbs, "limbs")
```

- **The limbs are placed at the rig's current pose**, so a character that goes
  down mid-stride falls from where it was standing.
- **`Build` refuses with a named error** when it cannot work: a rig with none of
  the profile's joints, a ragdoll that already has limbs, or a mesh whose file
  has not arrived yet (`ragdoll_no_rig`). The last one is the one people meet:
  `MeshContent` is a request, not a load.
- **Run it once in the editor and save the scene**, and the ragdoll is part of
  the prefab, with no script involved.

A profile entry names its joint (or a list of names, tried in order, because the
same shoulder is `mixamorig:LeftArm` or `upper_arm.L` depending on the
exporter), the limb it hangs from, and the capsule's size. The
[reference for `Ragdoll`](api:Ragdoll) lists every field.

## Tuning the limbs

The limbs are yours to adjust after `Build`, and two adjustments matter in
nearly every game:

```luau
--!strict
local mesh = workspace:FindFirstChildWhichIsA("MeshPart") :: MeshPart
local ragdoll = mesh:FindFirstChildWhichIsA("Ragdoll") :: Ragdoll
for _, child in ragdoll:GetChildren() do
    if child:IsA("BasePart") then
        local limb = child :: BasePart
        -- Flesh. The default density is 1 kg per cubic metre, which makes a
        -- hand weigh under a gram and throws a body across the world at the
        -- smallest contact.
        limb.Density = 1000
        -- Limbs of one body should not collide with each other.
        limb.CollisionGroup = "Ragdoll"
    end
end
```

together with a collision group set not to collide with itself:

```luau
--!strict
local PhysicsService = game:GetService("PhysicsService")
PhysicsService:RegisterCollisionGroup("Ragdoll")
PhysicsService:CollisionGroupSetCollidable("Ragdoll", "Ragdoll", false)
```

The limbs are usually made invisible. They are the simulation, and the mesh is
what is drawn.

## Switching it on, and blending

`Ragdoll.Enabled` hands the pose to the limbs. Switching it off hands the
character back to whatever clip is playing, from wherever it is.

**`Ragdoll.Blend`** (0 to 1) is how much of the pose comes from the simulation,
and it is **a pose blend, not a solver one**. It interpolates each driven joint
between where the clip put it and where the limb ended up. The limbs fall exactly
as hard at any value, so:

- ramping it from 0 to 1 over a few tenths of a second is how a character goes
  down without a visible snap, and ramping it back is how one gets up;
- a partial value is a stumble: the character keeps running and sags.

Driving the *solver* towards a pose, a motorised joint, is a different feature
and is not here.

## Bones on their own

A `Bone` is useful without a ragdoll. Parented to a `MeshPart`, it follows the
named joint as the animation moves it. Weld a sword to a hand, hang a light off a
swinging arm, or read where a foot is for a footstep. Bones are made on demand,
only for the joints you need.

`Bone.Transform` is an offset on top of the animated pose, in the joint's own
space: bend an arm towards a target, or turn a head to look at something. It
composes with the clip rather than replacing it.

## Where to look next

- [Welds and constraints](manual:physics/joints): what holds the limbs together
- [Skeletal animation](manual:animation/skeletal): the clips a ragdoll hands back to
- [Collisions and contact](manual:physics/collisions): collision groups
