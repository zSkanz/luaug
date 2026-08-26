--!strict
--- `@luaug/ragdoll` — profiles for `Ragdoll:Build`.
---
--- A profile is an ordered list of limbs, and `Ragdoll:Build` turns it into
--- parts, bones and joints. **Data rather than a built-in default**, and that is
--- the point: a profile is a set of judgements about a particular kind of body —
--- how thick an arm is, how far an elbow bends, which joints are worth
--- simulating at all — and the right answer for a horse is not the right answer
--- for a person. Copying `Humanoid` and editing four numbers is the intended way
--- to get a second one.
---
--- ```lua
--- local Ragdoll = require("@luaug/ragdoll")
--- character.Ragdoll:Build(Ragdoll.Humanoid)
--- character.Ragdoll.Enabled = true
--- ```
---
--- Run it once in the editor and save the scene, and the ragdoll is part of the
--- prefab with no script involved — everything `Build` makes is an ordinary
--- instance.

--- One limb: which joint it stands for, what it hangs from, and how it bends.
---
--- `Joint` is a name or a list of names tried in order. A list because the same
--- shoulder is `mixamorig:LeftArm`, `LeftArm` or `upper_arm.L` depending on who
--- exported the rig, and a profile naming one spelling would be a profile for
--- one exporter.
---
--- `Parent` is the 1-based index of the limb this hangs from, and must point at
--- one declared BEFORE it. Absent means the root.
---
--- Angles are in degrees, like every other angle a person types in this API.
export type Limb = {
    Joint: string | { string },
    Parent: number?,
    Radius: number?,
    LeafLength: number?,
    Kind: ("BallSocket" | "Hinge" | "Fixed")?,
    Swing: number?,
    Twist: number?,
    LimitLow: number?,
    LimitHigh: number?,
}

export type Profile = { Limb }

--- A sixteen-limb humanoid: hips, a two-piece spine, a head, two arms with
--- hands, and two legs with feet.
---
--- **Sixteen and not fifty.** A rig carries fingers, toes, twist bones and
--- often a face, and simulating them buys nothing you can see while costing a
--- body and a joint each. The joints nobody drives keep their place relative to
--- their parent when the pose is rebuilt, so the fingers ride along on the wrist
--- for free — which is the whole reason a partial ragdoll is the normal case
--- rather than a compromise.
---
--- The limits are a real body's, roughly: a knee and an elbow are hinges that
--- bend one way, a shoulder swings wide and twists, a neck barely does either.
--- Getting them wrong is what makes a ragdoll look like a bag rather than a
--- person, so they are worth reading before copying.
---
--- The names cover the three conventions that account for nearly every rig on
--- the asset stores: Mixamo's `mixamorig:` prefix, Blender's Rigify `.L`/`.R`
--- suffix, and the bare CamelCase most exporters emit.
local Humanoid: Profile = {
    -- 1. The root. Fixed rather than free: it is what everything else hangs
    -- from, and a root with a joint would need something to join it to.
    { Joint = { "mixamorig:Hips", "Hips", "pelvis", "Pelvis" }, Radius = 0.13, Kind = "Fixed" },

    -- 2-3. The spine, in two pieces. One piece would make a character fold at
    -- the waist and nowhere else, which reads as a hinge rather than a back.
    {
        Joint = { "mixamorig:Spine1", "Spine1", "spine.001", "Spine" },
        Parent = 1,
        Radius = 0.12,
        Swing = 25,
        Twist = 20,
    },
    {
        Joint = { "mixamorig:Spine2", "Spine2", "spine.003", "Chest" },
        Parent = 2,
        Radius = 0.13,
        Swing = 20,
        Twist = 20,
    },

    -- 4. The head. A neck barely swings and barely twists, and a ragdoll whose
    -- head can reach its own back is the single thing that most reads as wrong.
    {
        Joint = { "mixamorig:Head", "Head", "head" },
        Parent = 3,
        Radius = 0.10,
        LeafLength = 0.22,
        Swing = 35,
        Twist = 45,
    },

    -- 5-8. The arms. A shoulder is the widest joint in a body; an elbow is a
    -- hinge that bends one way and stops, which is what the negative low limit
    -- says -- it may not bend backwards at all.
    { Joint = { "mixamorig:LeftArm", "LeftArm", "upper_arm.L" }, Parent = 3, Radius = 0.055, Swing = 80, Twist = 50 },
    {
        Joint = { "mixamorig:LeftForeArm", "LeftForeArm", "forearm.L" },
        Parent = 5,
        Radius = 0.045,
        Kind = "Hinge",
        LimitLow = 0,
        LimitHigh = 140,
    },
    { Joint = { "mixamorig:RightArm", "RightArm", "upper_arm.R" }, Parent = 3, Radius = 0.055, Swing = 80, Twist = 50 },
    {
        Joint = { "mixamorig:RightForeArm", "RightForeArm", "forearm.R" },
        Parent = 7,
        Radius = 0.045,
        Kind = "Hinge",
        LimitLow = 0,
        LimitHigh = 140,
    },

    -- 9-10. The hands, as leaves. Short and thin: what they are for is giving
    -- the forearm something to end at, so a wrist stops mid-air rather than
    -- ending in a capsule the length of a guess.
    {
        Joint = { "mixamorig:LeftHand", "LeftHand", "hand.L" },
        Parent = 6,
        Radius = 0.035,
        LeafLength = 0.14,
        Swing = 45,
        Twist = 30,
    },
    {
        Joint = { "mixamorig:RightHand", "RightHand", "hand.R" },
        Parent = 8,
        Radius = 0.035,
        LeafLength = 0.14,
        Swing = 45,
        Twist = 30,
    },

    -- 11-14. The legs. A hip swings far forward and little back; a knee is a
    -- hinge, and it bends the OTHER way from an elbow -- which is why its range
    -- is negative and the elbow's is not.
    { Joint = { "mixamorig:LeftUpLeg", "LeftUpLeg", "thigh.L" }, Parent = 1, Radius = 0.075, Swing = 60, Twist = 35 },
    {
        Joint = { "mixamorig:LeftLeg", "LeftLeg", "shin.L" },
        Parent = 11,
        Radius = 0.06,
        Kind = "Hinge",
        LimitLow = -140,
        LimitHigh = 0,
    },
    { Joint = { "mixamorig:RightUpLeg", "RightUpLeg", "thigh.R" }, Parent = 1, Radius = 0.075, Swing = 60, Twist = 35 },
    {
        Joint = { "mixamorig:RightLeg", "RightLeg", "shin.R" },
        Parent = 13,
        Radius = 0.06,
        Kind = "Hinge",
        LimitLow = -140,
        LimitHigh = 0,
    },

    -- 15-16. The feet. Present because a body that ends at the ankle slides
    -- rather than catches, and catching is most of what a fall looks like.
    {
        Joint = { "mixamorig:LeftFoot", "LeftFoot", "foot.L" },
        Parent = 12,
        Radius = 0.045,
        LeafLength = 0.18,
        Swing = 40,
        Twist = 20,
    },
    {
        Joint = { "mixamorig:RightFoot", "RightFoot", "foot.R" },
        Parent = 14,
        Radius = 0.045,
        LeafLength = 0.18,
        Swing = 40,
        Twist = 20,
    },
}

return {
    Humanoid = Humanoid,
}
