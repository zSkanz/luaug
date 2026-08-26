# examples/12-ragdoll — a character that stops being animated and starts falling

Six figures on a platform. Press Space and they go down; press it again and they
get up. What happens in between is the whole example: the pose stops coming from
the skeleton and starts coming from a set of parts the physics solver is pushing
around, and the mesh follows.

```
examples\12-ragdoll\run.bat
```

**Controls** — Space drops them and stands them back up · F shoves them while
they are down · G shows the simulation capsules the mesh is following.

## What `Ragdoll:Build` actually does

It creates instances. A `Part` per joint, a `Bone` on each saying which joint it
stands for, and a constraint holding it to the limb above — sixteen limbs and
fifteen joints for the shipped humanoid profile, all of them ordinary things you
can select in the editor, move, retune and delete.

```lua
local Ragdoll = require("@luaug/ragdoll")

local ragdoll = Instance.new("Ragdoll")
ragdoll.Parent = characterMesh     -- the MeshPart whose skeleton
ragdoll:Build(Ragdoll.Humanoid)
ragdoll.Enabled = true
```

**There is no hidden body list.** A class that owned its own bodies would be a
second owner of things the physics mirror already creates from the tree, and two
owners of one body is a rule broken. What `Build` saves you is the arithmetic:
where each limb goes, how long it is, which way it points, and where its joint
frame has to face.

Run it once in the editor and save the scene and the ragdoll is part of the
prefab, with no script involved at all.

## Four things this example is actually about

**`Blend` is a POSE blend, not a solver one.** The limbs fall exactly as hard at
0.5 as at 1; what changes is how far the drawn joint is carried towards them.
That is why going down here is a ramp over about a fifth of a second rather than
a flag — at 1 on the frame the key is pressed, a character snaps from standing to
sprawled in a sixtieth of a second, and every ragdoll that looks cheap looks
cheap for that reason. Driving the *solver* towards a pose instead is a motorised
joint, and it is a different feature.

**A profile is data, and one profile fits several exporters.** Each limb names
its joint with a *list* of spellings, because the same shoulder is
`mixamorig:LeftArm`, `LeftArm` or `upper_arm.L` depending on who exported the
rig. A limb the rig has no joint for is *skipped* rather than refused, and its
children hang from the nearest limb that was built — so one humanoid profile
works on a rig with no toes. Copy `Humanoid` and edit four numbers and you have a
profile for a horse.

**Sixteen limbs, not fifty.** A rig carries fingers, toes and twist bones, and
simulating them buys nothing you can see while costing a body and a joint each.
The joints nobody drives keep their place relative to their parent when the pose
is rebuilt, so the fingers ride along on the wrist for free. `tests/bench/ragdoll10`
prices it: **about 26 µs of solver per ragdoll while it is moving**, so ten of
them is a quarter of a millisecond and thirty would still be under one. An island
that has gone to sleep costs nothing at all.

**Density is not a detail here.** `Density` defaults to 1 kg per cubic metre,
which makes a hand weigh under a gram — and a body of grams is thrown across the
world by the smallest contact. A ragdoll is the one thing in a scene made
entirely of small parts, so it is the one thing for which the default is not a
rounding difference. Every limb here is set to 1000.

## Two things that will cost you an hour if you do not know them

**`MeshContent` is a request, not a load.** Parenting the part asks the engine
for the file; the skeleton arrives when the loader has read it, which is the next
tick at the earliest. `Ragdoll:Build` reads that skeleton, so calling it from the
top of a script refuses every time — with `ragdoll_no_rig`, which is a correct
and extremely confusing answer if you do not know why. This example builds on the
first tick a rig is there and gives up after a second with a message.

**Limbs must not collide with each other.** `CollideConnected = false` covers a
*joined* pair — an upper arm and a lower arm overlap at the elbow by construction
— and does nothing for the pairs that are not joined, of which a humanoid has
plenty: a chest and the arms beside it, hips and both thighs. Left unfiltered a
ragdoll spends its first step pushing sixteen interpenetrating capsules apart
with whatever force that takes, and it takes enough to throw the character off
the platform. One collision group set non-collidable with itself is the fix, and
it is what every engine does.

## The rig

`content/models/humanoid.gltf` is a fixture, not a character: sixteen joints in
the bare CamelCase spelling the shipped profile tries, and one box per bone
weighted entirely to its own joint. The hard weighting is deliberate — a real
character blends across a joint so the elbow does not tear, and a hard weight
makes each bone's motion unmistakably its own, which is what you want to see when
you are looking at a skeleton drive a mesh.

The two skinned files this repository already had carry two joints each. They
were made to prove skinning works at all; a ragdoll profile is about shoulders,
elbows and knees.

## What is deliberately not here

There is no walk cycle, so a ragdoll goes down from a standing pose rather than
mid-stride. A clip would make the blend more convincing and would make this
example about animation instead; the seam it would exercise — `Blend` reading the
animated joint rather than the bind pose — is the same either way, and
`tests/conformance/physics/ragdoll.spec.luau` covers it.

There is also no getting up *from where you fell*. `Blend` returning to zero
hands the character back to the animation, which is standing where it started —
so it stands up on the spot rather than from the floor. Matching the animation's
root to the ragdoll's hips first is what a game does about that, and it is
gameplay rather than engine.
