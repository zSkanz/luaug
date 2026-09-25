--!strict
--- What a replica is told about the world (ADR 0069).
---
--- **Every concrete class that exists is a class this file decides about**, and
--- the answers are `Classes`, `Excluded` and `ExcludedByTag`. Absence is not an
--- answer: a class nobody thought about and a class deliberately left out look
--- identical in a schema, and `wirecheck` refuses a tree where one is missing.
---
--- **The ids are permanent.** Not "stable", not "please do not change" -- an id
--- that means one thing to a server and another to a replica produces a symptom
--- nobody can debug from the outside: a position that decodes as a colour, a
--- health that decodes as a rotation. Removing a field means moving it to
--- `Retired`, which claims its id for ever.

local Schema = require("./schema")

-- Every list below carries its element type, and that is load-bearing rather
-- than decoration -- `api/defs/services.api.luau` explains it at length. Without
-- the annotation the new solver infers a list's element type from the entries
-- themselves, which widens every string literal to `string`: `Delivery` stops
-- being one of three words and becomes any word at all. Naming each list is an
-- assertion rather than a cast, so a wrong field name still fails analysis.

--- **Not a `Class` entry for `Instance`.** Every class extends `Instance`, so a
--- row for it would make every class in the engine decided by inheritance, and
--- the one check this file exists for would pass on an empty schema.
local Common: { Schema.Field } = {
    {
        Name = "Name",
        Id = 1,
        Encoding = "NameAtom",
        Source = "Property",
        Doc = "Scripts find things by name, so a replica whose names differ is a replica where "
            .. "`FindFirstChild` answers differently -- which is a divergence in the code rather than "
            .. "in the picture.",
    },
    {
        Name = "Parent",
        Id = 2,
        Encoding = "InstanceRef",
        Source = "Property",
        Doc = "**A network id, never a path.** A path is a string that goes stale the moment anything "
            .. "is renamed, and it is O(depth) to resolve on a replica that may not even hold the "
            .. "ancestors -- interest management gives it a subset by design.",
    },
}

local BasePartFields: { Schema.Field } = {
    {
        Name = "CFrame",
        Id = 1,
        Encoding = "CFrameD",
        Source = "Component",
        Component = "parts",
        Doc = "Where the part is. **Read from the component and not through a property**, because "
            .. "`PhysicsSync` writes it there directly under a comment naming itself the QUIET write "
            .. "-- a schema that could only name properties could not replicate motion at all "
            .. "(ADR 0069).",
    },
    {
        Name = "Size",
        Id = 2,
        Encoding = "Vector3",
        Source = "Component",
        Component = "parts",
        Doc = "Rarely changes and is cheap to diff, but a part that resized on the authority and not "
            .. "on the replica is a collision mismatch a player experiences as being stopped by "
            .. "nothing.",
    },
    {
        Name = "Anchored",
        Id = 5,
        Encoding = "Bool",
        Source = "Component",
        Component = "rigidBodies",
        Doc = "Decides whether the replica's own body is kinematic-driven or asleep, so a stale value "
            .. "costs a frame of the wrong physics. **In `rigidBodies` and not in `parts`**, which "
            .. "is where the schema first said it was: a part's simulation state is a separate "
            .. "component from its shape, because `scene` must not learn what a body is.",
    },
    {
        Name = "CanCollide",
        Id = 6,
        Encoding = "Bool",
        Source = "Component",
        Component = "rigidBodies",
        Doc = "A door that opened on the authority and not on the replica is a player walking into a "
            .. "wall that is not there.",
    }, -- **The surface, which the wire never carried** (ADR 0090). A part's
    -- `Color` and `Transparency` went out and its `Material` never did, so a
    -- replica drew every part bare; a part wears a material now, and all of
    -- what it wears travels.
    {
        Name = "Material",
        Id = 7,
        Encoding = "NameAtom",
        Source = "Component",
        Component = "parts",
        Doc = "The material asset the part wears, by URN, or none for the engine default. A name rather "
            .. "than the material itself: the file is content both machines have, as a mesh is.",
    },
    {
        Name = "MaterialParameters",
        Id = 8,
        Encoding = "MaterialOverrides",
        Source = "Component",
        Component = "parts",
        Doc = "What the part overrides -- the tint and fade a game signals state with, which is gameplay "
            .. "rather than decoration.",
    },
    {
        Name = "MaterialClone",
        Id = 9,
        Encoding = "U32",
        Source = "Component",
        Component = "parts",
        Doc = "Which runtime copy of the material the part wears, by the authority's own number, or zero "
            .. "for the asset itself. Parts that share a copy share it on the replica too.",
    },
    {
        Name = "MaterialCloneValues",
        Id = 10,
        Encoding = "MaterialValues",
        Source = "Component",
        Component = "parts",
        Doc = "What that copy changed. **Carried with each part that wears it** rather than as an object "
            .. "of its own, so a copy is never named before it has arrived; zero when the part wears none, "
            .. "so taking one off and putting it back is a change the diff sees.",
    },
    {
        Name = "MaterialCloneColorMap",
        Id = 11,
        Encoding = "NameAtom",
        Source = "Component",
        Component = "parts",
        Doc = "A map the copy changed, by URN; the three below are the others.",
    },
    {
        Name = "MaterialCloneNormalMap",
        Id = 12,
        Encoding = "NameAtom",
        Source = "Component",
        Component = "parts",
        Doc = "The copy's normal map, when it changed it.",
    },
    {
        Name = "MaterialCloneMetallicRoughnessMap",
        Id = 13,
        Encoding = "NameAtom",
        Source = "Component",
        Component = "parts",
        Doc = "The copy's metallic-roughness map, when it changed it.",
    },
    {
        Name = "MaterialCloneEmissiveMap",
        Id = 14,
        Encoding = "NameAtom",
        Source = "Component",
        Component = "parts",
        Doc = "The copy's emissive map, when it changed it.",
    },
}

--- **Retired**: a part has no colour or transparency of its own since ADR 0090;
--- the two are its material's parameters and travel in `MaterialParameters`.
--- The ids stay claimed for ever.
local BasePartRetired: { Schema.RetiredField } = {
    {
        Name = "Color",
        Id = 3,
        Since = "2.0.0",
        Reason = "a part's colour is its material's parameter (ADR 0090); see MaterialParameters",
    },
    {
        Name = "Transparency",
        Id = 4,
        Since = "2.0.0",
        Reason = "a part's see-through is its material's parameter (ADR 0090); see MaterialParameters",
    },
}

local CharacterBodyFields: { Schema.Field } = {
    {
        Name = "VerticalVelocity",
        Id = 1,
        Encoding = "F32",
        Source = "Component",
        Component = "characterBodies",
        Doc = "**Sent because a position alone cannot be interpolated**, and vertical because that is "
            .. "the axis a controller integrates itself: a `CharacterVirtual` is not a body the "
            .. "solver knows about, so gravity accumulates here. Two positions a tick apart say where "
            .. "a body was; this says where it is going, which is what lets a replica draw a smooth "
            .. "frame between two snapshots instead of stepping.",
    },
    {
        Name = "Grounded",
        Id = 2,
        Encoding = "Bool",
        Source = "Component",
        Component = "characterBodies",
        Doc = "Drives what a game shows -- a jump, a fall, a landing -- and is the authority's answer "
            .. "rather than something a replica can derive: whether a character is on the ground "
            .. "depends on collision the replica does not solve.",
    },
    {
        Name = "State",
        Id = 3,
        Encoding = "I32",
        Source = "Component",
        Component = "characterBodies",
        Doc = "`Enum.CharacterState`, raw, exactly as the component stores it. It is what an animation "
            .. "picks a clip from, and a replica that guessed it from velocity would guess wrong at "
            .. "every transition -- which is where a player looks.",
    },
}

--- **`moveDirection` is deliberately absent**, and it is worth saying why here
--- rather than in `Excluded`: it is a COMMAND, cleared by the mirror once
--- consumed, and it travels the other way. A client says what it did on the
--- `Intent` channel; an authority pushing a move direction back would be an
--- authority pressing a player's keys.

local ModelFields: { Schema.Field } = {
    {
        Name = "Scale",
        Id = 1,
        Encoding = "F32",
        Source = "Component",
        Component = "models",
        Doc = "One number that moves every descendant, so sending it beats sending the descendants it " .. "moved.",
    },
}

local BloomEffectFields: { Schema.Field } = {
    {
        Name = "Enabled",
        Id = 1,
        Encoding = "Bool",
        Source = "Component",
        Component = "postEffects",
        Doc = "An effect switched off from a script is off on every screen that sees the world.",
    },
    {
        Name = "Intensity",
        Id = 2,
        Encoding = "F32",
        Source = "Component",
        Component = "bloomEffects",
        Doc = "How strong the glow is.",
    },
    {
        Name = "Size",
        Id = 3,
        Encoding = "F32",
        Source = "Component",
        Component = "bloomEffects",
        Doc = "How far it reaches.",
    },
    {
        Name = "Threshold",
        Id = 4,
        Encoding = "F32",
        Source = "Component",
        Component = "bloomEffects",
        Doc = "How bright a pixel must be to glow.",
    },
}

local ColorCorrectionEffectFields: { Schema.Field } = {
    {
        Name = "Enabled",
        Id = 1,
        Encoding = "Bool",
        Source = "Component",
        Component = "postEffects",
        Doc = "An effect switched off from a script is off on every screen that sees the world.",
    },
    {
        Name = "Brightness",
        Id = 2,
        Encoding = "F32",
        Source = "Component",
        Component = "colorCorrectionEffects",
        Doc = "A grade is how the world looks, so every replica wears it.",
    },
    {
        Name = "Contrast",
        Id = 3,
        Encoding = "F32",
        Source = "Component",
        Component = "colorCorrectionEffects",
        Doc = "The same.",
    },
    {
        Name = "Saturation",
        Id = 4,
        Encoding = "F32",
        Source = "Component",
        Component = "colorCorrectionEffects",
        Doc = "The same -- a flash to grey when a player is hit is a grade a script sets.",
    },
    {
        Name = "TintColor",
        Id = 5,
        Encoding = "Color3",
        Source = "Component",
        Component = "colorCorrectionEffects",
        Doc = "The same.",
    },
}

local BlurEffectFields: { Schema.Field } = {
    {
        Name = "Enabled",
        Id = 1,
        Encoding = "Bool",
        Source = "Component",
        Component = "postEffects",
        Doc = "An effect switched off from a script is off on every screen that sees the world.",
    },
    {
        Name = "Size",
        Id = 2,
        Encoding = "F32",
        Source = "Component",
        Component = "blurEffects",
        Doc = "How far the world picture is softened.",
    },
}

local DepthOfFieldEffectFields: { Schema.Field } = {
    {
        Name = "Enabled",
        Id = 1,
        Encoding = "Bool",
        Source = "Component",
        Component = "postEffects",
        Doc = "An effect switched off from a script is off on every screen that sees the world.",
    },
    {
        Name = "FocusDistance",
        Id = 2,
        Encoding = "F32",
        Source = "Component",
        Component = "depthOfFieldEffects",
        Doc = "Where the world's lens is focused.",
    },
    {
        Name = "InFocusRadius",
        Id = 3,
        Encoding = "F32",
        Source = "Component",
        Component = "depthOfFieldEffects",
        Doc = "How deep the sharp band is.",
    },
    {
        Name = "NearIntensity",
        Id = 4,
        Encoding = "F32",
        Source = "Component",
        Component = "depthOfFieldEffects",
        Doc = "How soft the near side is.",
    },
    {
        Name = "FarIntensity",
        Id = 5,
        Encoding = "F32",
        Source = "Component",
        Component = "depthOfFieldEffects",
        Doc = "How soft the far side is.",
    },
}

local SunRaysEffectFields: { Schema.Field } = {
    {
        Name = "Enabled",
        Id = 1,
        Encoding = "Bool",
        Source = "Component",
        Component = "postEffects",
        Doc = "An effect switched off from a script is off on every screen that sees the world.",
    },
    {
        Name = "Intensity",
        Id = 2,
        Encoding = "F32",
        Source = "Component",
        Component = "sunRaysEffects",
        Doc = "How bright the shafts are.",
    },
    {
        Name = "Spread",
        Id = 3,
        Encoding = "F32",
        Source = "Component",
        Component = "sunRaysEffects",
        Doc = "How far they reach.",
    },
}

local AtmosphereFields: { Schema.Field } = {
    {
        Name = "Density",
        Id = 1,
        Encoding = "F32",
        Source = "Component",
        Component = "atmospheres",
        Doc = "The air is the world's weather: a fog rolling in on the authority rolls in everywhere.",
    },
    {
        Name = "Offset",
        Id = 2,
        Encoding = "F32",
        Source = "Component",
        Component = "atmospheres",
        Doc = "Where it is thickest.",
    },
    {
        Name = "Color",
        Id = 3,
        Encoding = "Color3",
        Source = "Component",
        Component = "atmospheres",
        Doc = "Its colour.",
    },
    {
        Name = "Decay",
        Id = 4,
        Encoding = "F32",
        Source = "Component",
        Component = "atmospheres",
        Doc = "How it thins with height.",
    },
    {
        Name = "Glare",
        Id = 5,
        Encoding = "F32",
        Source = "Component",
        Component = "atmospheres",
        Doc = "Its glow towards the sun.",
    },
    {
        Name = "Haze",
        Id = 6,
        Encoding = "F32",
        Source = "Component",
        Component = "atmospheres",
        Doc = "Its thickness at the horizon.",
    },
}

local SkyFields: { Schema.Field } = {
    {
        Name = "SkyboxBack",
        Id = 1,
        Encoding = "NameAtom",
        Source = "Component",
        Component = "skies",
        Doc = "The pictures, by content URN through the snapshot's string table; the images ship with the game.",
    },
    {
        Name = "SkyboxDown",
        Id = 2,
        Encoding = "NameAtom",
        Source = "Component",
        Component = "skies",
        Doc = "The same.",
    },
    {
        Name = "SkyboxFront",
        Id = 3,
        Encoding = "NameAtom",
        Source = "Component",
        Component = "skies",
        Doc = "The same.",
    },
    {
        Name = "SkyboxLeft",
        Id = 4,
        Encoding = "NameAtom",
        Source = "Component",
        Component = "skies",
        Doc = "The same.",
    },
    {
        Name = "SkyboxRight",
        Id = 5,
        Encoding = "NameAtom",
        Source = "Component",
        Component = "skies",
        Doc = "The same.",
    },
    {
        Name = "SkyboxUp",
        Id = 6,
        Encoding = "NameAtom",
        Source = "Component",
        Component = "skies",
        Doc = "The same.",
    },
    {
        Name = "SkyboxOrientation",
        Id = 7,
        Encoding = "Vector3",
        Source = "Component",
        Component = "skies",
        Doc = "How the pictures are turned.",
    },
    {
        Name = "SunTexture",
        Id = 8,
        Encoding = "NameAtom",
        Source = "Component",
        Component = "skies",
        Doc = "The sun's picture.",
    },
    {
        Name = "MoonTexture",
        Id = 9,
        Encoding = "NameAtom",
        Source = "Component",
        Component = "skies",
        Doc = "The moon's picture.",
    },
    {
        Name = "SunAngularSize",
        Id = 10,
        Encoding = "F32",
        Source = "Component",
        Component = "skies",
        Doc = "How large the sun looks.",
    },
    {
        Name = "MoonAngularSize",
        Id = 11,
        Encoding = "F32",
        Source = "Component",
        Component = "skies",
        Doc = "How large the moon looks.",
    },
    {
        Name = "StarCount",
        Id = 12,
        Encoding = "F32",
        Source = "Component",
        Component = "skies",
        Doc = "How many stars.",
    },
    {
        Name = "CelestialBodiesShown",
        Id = 13,
        Encoding = "Bool",
        Source = "Component",
        Component = "skies",
        Doc = "Whether the sun, moon and stars are drawn.",
    },
    {
        Name = "CloudCover",
        Id = 14,
        Encoding = "F32",
        Source = "Component",
        Component = "skies",
        Doc = "How much of the sky is cloud -- weather, and the world's.",
    },
    {
        Name = "CloudDensity",
        Id = 15,
        Encoding = "F32",
        Source = "Component",
        Component = "skies",
        Doc = "How thick the clouds are.",
    },
    {
        Name = "CloudColor",
        Id = 16,
        Encoding = "Color3",
        Source = "Component",
        Component = "skies",
        Doc = "Their colour.",
    },
}

local NoFields: { Schema.Field } = {}

local TeamFields: { Schema.Field } = {
    {
        Name = "Color",
        Id = 1,
        Encoding = "Color3",
        Source = "Component",
        Component = "teams",
        Doc = "What a side looks like on every machine: a name tag painted on one replica and not another "
            .. "is two games.",
    },
    {
        Name = "AutoAssign",
        Id = 2,
        Encoding = "Bool",
        Source = "Component",
        Component = "teams",
        Doc = "Only the authority assigns, but a replica's scripts may show which sides are open.",
    },
}

local LightingFields: { Schema.Field } = {
    {
        Name = "ClockTime",
        Id = 1,
        Encoding = "F32",
        Source = "Component",
        Component = "lighting",
        Doc = "Night on the authority is night on every replica: the sun is where the game says it is.",
    },
    {
        Name = "GeographicLatitude",
        Id = 2,
        Encoding = "F32",
        Source = "Component",
        Component = "lighting",
        Doc = "Where the sun's path runs, which with the clock is where every shadow falls.",
    },
    {
        Name = "Ambient",
        Id = 3,
        Encoding = "Color3",
        Source = "Component",
        Component = "lighting",
        Doc = "The light in the shadows, which a game changes to say it is getting dark.",
    },
    {
        Name = "Brightness",
        Id = 4,
        Encoding = "F32",
        Source = "Component",
        Component = "lighting",
        Doc = "How strong the sun is.",
    },
    {
        Name = "FogColor",
        Id = 5,
        Encoding = "Color3",
        Source = "Component",
        Component = "lighting",
        Doc = "What the distance fades into.",
    },
    {
        Name = "FogStart",
        Id = 6,
        Encoding = "F32",
        Source = "Component",
        Component = "lighting",
        Doc = "Where it starts to fade.",
    },
    {
        Name = "FogEnd",
        Id = 7,
        Encoding = "F32",
        Source = "Component",
        Component = "lighting",
        Doc = "Where it is gone -- a storm rolling in is this number coming closer.",
    },
    {
        Name = "ExposureCompensation",
        Id = 8,
        Encoding = "F32",
        Source = "Component",
        Component = "lighting",
        Doc = "A flash, a blinding moment, a cave entrance a game brightens.",
    },
    {
        Name = "EnvironmentDiffuseScale",
        Id = 9,
        Encoding = "F32",
        Source = "Component",
        Component = "lighting",
        Doc = "How much the sky lights the world (ADR 0096).",
    },
    {
        Name = "EnvironmentSpecularScale",
        Id = 10,
        Encoding = "F32",
        Source = "Component",
        Component = "lighting",
        Doc = "How much it is reflected.",
    },
    {
        Name = "ShadowSoftness",
        Id = 11,
        Encoding = "F32",
        Source = "Component",
        Component = "lighting",
        Doc = "How soft the sun's shadows are.",
    },
    {
        Name = "GlobalShadows",
        Id = 12,
        Encoding = "Bool",
        Source = "Component",
        Component = "lighting",
        Doc = "Whether the sun casts at all.",
    },
    {
        Name = "AutoExposure",
        Id = 13,
        Encoding = "Bool",
        Source = "Component",
        Component = "lighting",
        Doc = "Whether the world's exposure follows the frame.",
    },
}

local DecalFields: { Schema.Field } = {
    {
        Name = "CFrame",
        Id = 1,
        Encoding = "CFrameD",
        Source = "Component",
        Component = "decals",
        Doc = "Where the mark is -- relative to the part it is on, so a mark on a moving crate moves with it.",
    },
    {
        Name = "Size",
        Id = 2,
        Encoding = "Vector3",
        Source = "Component",
        Component = "decals",
        Doc = "How much of the world the mark covers.",
    },
    {
        Name = "Texture",
        Id = 3,
        Encoding = "NameAtom",
        Source = "Component",
        Component = "decals",
        Doc = "The image, by content URN, through the snapshot's string table as every name travels.",
    },
    {
        Name = "Color",
        Id = 4,
        Encoding = "Color3",
        Source = "Component",
        Component = "decals",
        Doc = "What the image is multiplied by.",
    },
    {
        Name = "Transparency",
        Id = 5,
        Encoding = "F32",
        Source = "Component",
        Component = "decals",
        Doc = "A mark fading out is a game saying something.",
    },
}

local ParticleEmitterFields: { Schema.Field } = {
    {
        Name = "Enabled",
        Id = 1,
        Encoding = "Bool",
        Source = "Component",
        Component = "particleEmitters",
        Doc = "A stream a replica keeps drawing after the authority stopped it is smoke from a fire somebody put out.",
    },
    {
        Name = "Rate",
        Id = 2,
        Encoding = "F32",
        Source = "Component",
        Component = "particleEmitters",
        Doc = "How dense the stream is, which a game changes to say how much is burning.",
    },
    {
        Name = "Lifetime",
        Id = 3,
        Encoding = "F32",
        Source = "Component",
        Component = "particleEmitters",
        Doc = "How far a stream reaches, with Speed.",
    },
    {
        Name = "Speed",
        Id = 4,
        Encoding = "F32",
        Source = "Component",
        Component = "particleEmitters",
        Doc = "How far a stream reaches, with Lifetime.",
    },
    {
        Name = "SpreadAngle",
        Id = 5,
        Encoding = "F32",
        Source = "Component",
        Component = "particleEmitters",
        Doc = "A jet and a fountain are one number apart.",
    },
    {
        Name = "Acceleration",
        Id = 6,
        Encoding = "Vector3",
        Source = "Component",
        Component = "particleEmitters",
        Doc = "Whether smoke rises or sparks fall.",
    },
    {
        Name = "Drag",
        Id = 7,
        Encoding = "F32",
        Source = "Component",
        Component = "particleEmitters",
        Doc = "How soon a puff stops.",
    },
    {
        Name = "Color",
        Id = 8,
        Encoding = "Color3",
        Source = "Component",
        Component = "particleEmitters",
        Doc = "What it looks like, which is the whole of what it is.",
    },
    {
        Name = "ColorEnd",
        Id = 9,
        Encoding = "Color3",
        Source = "Component",
        Component = "particleEmitters",
        Doc = "What it fades to.",
    },
    {
        Name = "Size",
        Id = 10,
        Encoding = "F32",
        Source = "Component",
        Component = "particleEmitters",
        Doc = "How big a particle is born.",
    },
    {
        Name = "SizeEnd",
        Id = 11,
        Encoding = "F32",
        Source = "Component",
        Component = "particleEmitters",
        Doc = "How big it dies.",
    },
    {
        Name = "Transparency",
        Id = 12,
        Encoding = "F32",
        Source = "Component",
        Component = "particleEmitters",
        Doc = "How solid it is born.",
    },
    {
        Name = "TransparencyEnd",
        Id = 13,
        Encoding = "F32",
        Source = "Component",
        Component = "particleEmitters",
        Doc = "How solid it dies.",
    },
    {
        Name = "LightEmission",
        Id = 14,
        Encoding = "F32",
        Source = "Component",
        Component = "particleEmitters",
        Doc = "Whether it glows or blends.",
    },
    {
        Name = "Brightness",
        Id = 15,
        Encoding = "F32",
        Source = "Component",
        Component = "particleEmitters",
        Doc = "How hard it glows.",
    },
    {
        Name = "Shape",
        Id = 16,
        Encoding = "I32",
        Source = "Component",
        Component = "particleEmitters",
        Doc = "What one particle looks like.",
    },
    {
        Name = "Emitted",
        Id = 17,
        Encoding = "U32",
        Source = "Component",
        Component = "particleEmitters",
        Doc = "**How bursts reach a replica**: a running total of what `Emit` asked for, so a replica spawns the difference it has not seen -- the low 32 bits, because a difference is all it ever takes, and it wraps safely.",
    },
}

--- A sprite and a body on the 2D plane (the 2D layer, ADR 0088). Every field is
--- in its component, which the 2D mirror writes quietly as the 3D one writes a
--- part's.
local Part2DFields: { Schema.Field } = {
    {
        Name = "Position",
        Id = 1,
        Encoding = "Vector3",
        Source = "Component",
        Component = "parts2d",
        Doc = "Where it is, on the plane. **A `Vector2` travels as a `Vector3` with a zero z** (ADR 0088): "
            .. "twelve bytes the codec already has, rather than an encoding of its own for four fewer.",
    },
    {
        Name = "Rotation",
        Id = 2,
        Encoding = "F32",
        Source = "Component",
        Component = "parts2d",
        Doc = "Degrees, as authored. A turned crate on the authority is a turned crate everywhere.",
    },
    {
        Name = "Size",
        Id = 3,
        Encoding = "Vector3",
        Source = "Component",
        Component = "parts2d",
        Doc = "The picture's size and the collider's, for the reason a `BasePart`'s travels.",
    },
    {
        Name = "Velocity",
        Id = 4,
        Encoding = "Vector3",
        Source = "Component",
        Component = "parts2d",
        Doc = "Sent because a script on the replica reads it -- a run animation picks its frame from it -- "
            .. "and because it is what a later version extrapolates with.",
    },
    {
        Name = "AngularVelocity",
        Id = 5,
        Encoding = "F32",
        Source = "Component",
        Component = "parts2d",
        Doc = "The same argument as `Velocity`.",
    },
    {
        Name = "Color",
        Id = 6,
        Encoding = "Color3",
        Source = "Component",
        Component = "parts2d",
        Doc = "Games say things with colour; the same argument as a `BasePart`'s.",
    },
    {
        Name = "Transparency",
        Id = 7,
        Encoding = "F32",
        Source = "Component",
        Component = "parts2d",
        Doc = "A blinking hurt sprite is a gameplay fact.",
    },
    {
        Name = "Anchored",
        Id = 8,
        Encoding = "Bool",
        Source = "Component",
        Component = "parts2d",
        Doc = "Decides whether the replica's body is kinematic-driven or ground.",
    },
    {
        Name = "CanCollide",
        Id = 9,
        Encoding = "Bool",
        Source = "Component",
        Component = "parts2d",
        Doc = "A door that opened on the authority must be open on the replica.",
    },
    {
        Name = "Sensor",
        Id = 10,
        Encoding = "Bool",
        Source = "Component",
        Component = "parts2d",
        Doc = "Whether it pushes: a replica's own character must pass through a coin.",
    },
    {
        Name = "Shape",
        Id = 11,
        Encoding = "I32",
        Source = "Component",
        Component = "parts2d",
        Doc = "`Enum.Shape2D`, raw: what it is drawn and collides as.",
    },
    {
        Name = "ZIndex",
        Id = 12,
        Encoding = "I32",
        Source = "Component",
        Component = "parts2d",
        Doc = "Which sprite is in front; a game changes it to say something.",
    },
    {
        Name = "FlipX",
        Id = 13,
        Encoding = "Bool",
        Source = "Component",
        Component = "parts2d",
        Doc = "**The facing of a sprite is animation state**, and a replica that did not receive "
            .. "it would show every other player running backwards half the time.",
    },
    {
        Name = "FlipY",
        Id = 14,
        Encoding = "Bool",
        Source = "Component",
        Component = "parts2d",
        Doc = "The same as `FlipX`.",
    },
    {
        Name = "Image",
        Id = 15,
        Encoding = "NameAtom",
        Source = "Component",
        Component = "parts2d",
        Doc = "The picture, by URN, through the snapshot's string table as a `Decal`'s is.",
    },
    {
        Name = "ImageRectOffset",
        Id = 16,
        Encoding = "Vector3",
        Source = "Component",
        Component = "parts2d",
        Doc = "**The frame of a sprite sheet**: a walk cycle is this changing, so it is the other half "
            .. "of a replicated sprite's animation. A `Vector2` as a `Vector3`, as `Position` is.",
    },
    {
        Name = "ImageRectSize",
        Id = 17,
        Encoding = "Vector3",
        Source = "Component",
        Component = "parts2d",
        Doc = "The frame's size, beside its offset.",
    },
    {
        Name = "Filter",
        Id = 18,
        Encoding = "I32",
        Source = "Component",
        Component = "parts2d",
        Doc = "`Enum.TextureFilter`, raw: pixel art stays crisp on every screen.",
    },
}

--- A tilemap on the 2D plane (ADR 0103, protocol 15). Its properties are fields
--- like any other class's; its cells are not a property and travel in
--- `TilemapBlocks`, a block at a time.
local Tilemap2DFields: { Schema.Field } = {
    {
        Name = "Position",
        Id = 1,
        Encoding = "Vector3",
        Source = "Component",
        Component = "tilemaps2d",
        Doc = "Where cell (0, 0) is, on the plane. A `Vector2` as a `Vector3`, as a `Part2D`'s is.",
    },
    {
        Name = "CellSize",
        Id = 2,
        Encoding = "F32",
        Source = "Component",
        Component = "tilemaps2d",
        Doc = "How big a cell is, in metres: the ground a replica stands on is this size.",
    },
    {
        Name = "Tileset",
        Id = 3,
        Encoding = "NameAtom",
        Source = "Component",
        Component = "tilemaps2d",
        Doc = "The picture the tiles are cut from, by URN, through the string table.",
    },
    {
        Name = "TileSize",
        Id = 4,
        Encoding = "Vector3",
        Source = "Component",
        Component = "tilemaps2d",
        Doc = "One tile's size in the tileset, in pixels.",
    },
    {
        Name = "ZIndex",
        Id = 5,
        Encoding = "I32",
        Source = "Component",
        Component = "tilemaps2d",
        Doc = "Which of it and a sprite is in front.",
    },
    {
        Name = "Color",
        Id = 6,
        Encoding = "Color3",
        Source = "Component",
        Component = "tilemaps2d",
        Doc = "What the tiles are multiplied by.",
    },
    {
        Name = "Filter",
        Id = 7,
        Encoding = "I32",
        Source = "Component",
        Component = "tilemaps2d",
        Doc = "`Enum.TextureFilter`, raw.",
    },
    {
        Name = "Collides",
        Id = 8,
        Encoding = "Bool",
        Source = "Component",
        Component = "tilemaps2d",
        Doc = "Whether its solid tiles are ground: a replica's own character stands on them.",
    },
    {
        Name = "Friction",
        Id = 9,
        Encoding = "F32",
        Source = "Component",
        Component = "tilemaps2d",
        Doc = "How the ground grips, for the same reason.",
    },
}

local Classes: { Schema.Class } = {
    {
        Name = "BasePart",
        Doc = "The moving half of nearly every game, and therefore the field set that decides whether "
            .. "replication is affordable at all. `Part` and `MeshPart` inherit it.",
        Fields = BasePartFields,
        Retired = BasePartRetired,
    },
    {
        Name = "CharacterBody",
        Extends = "BasePart",
        Doc = "A player's body, and the one class where the authority's answer and the client's guess "
            .. "are most visibly different. **Carries `BasePart`'s six too**, by `Extends` -- the first "
            .. "version of this entry said so in its doc and not in its data, and a replica would have "
            .. "held a character that never moved.",
        Fields = CharacterBodyFields,
    },
    {
        Name = "Model",
        Doc = "Replicated for its scale alone: everything else about a model is its children, and "
            .. "they replicate as themselves.",
        Fields = ModelFields,
    },
    {
        Name = "Lighting",
        Service = true,
        -- ADR 0096: the world's look is instances under it, and they travel with
        -- it. A camera's do not -- `Camera` is excluded, and its subtree with it.
        Contents = true,
        Doc = "**The first service whose properties travel**: the time of day, the fog and the light. "
            .. "It is never spawned -- every world has one from boot, on both ends -- so it travels under "
            .. "a fixed id and a replica writes its own.",
        Fields = LightingFields,
    },
    {
        Name = "Decal",
        Doc = "A scorch mark, a footprint, a poster (F2). Replicated since the snapshot's string table "
            .. "carries every name-shaped field rather than `Name` alone, which is what its image URN "
            .. "needed.",
        Fields = DecalFields,
    },
    {
        Name = "ParticleEmitter",
        Doc = "Sparks, smoke and magic (F2). Its particles are a picture and never travel -- each "
            .. "machine simulates its own -- but how an emitter is set up is what a game changes to say "
            .. "something, and a burst a script asked for on the authority has to happen on a replica too.",
        Fields = ParticleEmitterFields,
    },
    {
        Name = "Folder",
        Doc = "**Replicated with no fields of its own, and that is the point.** A folder is "
            .. "structure: a `Parent` reference to one has to resolve on the replica, so the folder "
            .. "has to be there. Its `Name` and `Parent` come from the common set.",
        Fields = NoFields,
    },
    {
        Name = "RemoteEvent",
        Doc = "Replicated so a message has something both ends agree it is for (ADR 0077): its "
            .. "network id is the address a `RemoteToAuthority` or `RemoteToReplica` names. No fields "
            .. "of its own; a message is not state.",
        Fields = NoFields,
    },
    {
        Name = "ReplicatedStorage",
        Service = true,
        Contents = true,
        Doc = "A service with no fields whose contents travel (ADR 0080): every instance under it "
            .. "reaches every replica, whatever its position, parented to the replica's own "
            .. "`ReplicatedStorage`.",
        Fields = NoFields,
    },
    {
        Name = "RemoteFunction",
        Doc = "Replicated for the reason `RemoteEvent` is (ADR 0079): a question and its answer both "
            .. "name it. Its handler is a function on the authority and never travels.",
        Fields = NoFields,
    },
    {
        Name = "Part2D",
        Doc = "The 2D layer's moving half (ADR 0088, protocol 11): where a sprite is, how it is turned "
            .. "and which frame of its sheet it shows. A replica drives it kinematically, as it drives a "
            .. "`BasePart`, and draws a remote one between snapshots, its rotation the short way round "
            .. "(ADR 0103, protocol 15).",
        Fields = Part2DFields,
    },
    {
        Name = "Tilemap2D",
        Doc = "A level on the plane (ADR 0103, protocol 15): its properties as fields, and its cells in "
            .. "`TilemapBlocks` -- every block after the spawn, then each block an edit changed. What a "
            .. "wall broken or a floor built on the authority is on every replica.",
        Fields = Tilemap2DFields,
    },
    {
        Name = "BloomEffect",
        Doc = "The world's look under `Lighting` (ADR 0096, protocol 13): the authority's is every replica's. A camera's stays on its own screen.",
        Fields = BloomEffectFields,
    },
    {
        Name = "ColorCorrectionEffect",
        Doc = "The world's look under `Lighting` (ADR 0096, protocol 13): the authority's is every replica's. A camera's stays on its own screen.",
        Fields = ColorCorrectionEffectFields,
    },
    {
        Name = "BlurEffect",
        Doc = "The world's look under `Lighting` (ADR 0096, protocol 13): the authority's is every replica's. A camera's stays on its own screen.",
        Fields = BlurEffectFields,
    },
    {
        Name = "DepthOfFieldEffect",
        Doc = "The world's look under `Lighting` (ADR 0096, protocol 13): the authority's is every replica's. A camera's stays on its own screen.",
        Fields = DepthOfFieldEffectFields,
    },
    {
        Name = "SunRaysEffect",
        Doc = "The world's look under `Lighting` (ADR 0096, protocol 13): the authority's is every replica's. A camera's stays on its own screen.",
        Fields = SunRaysEffectFields,
    },
    {
        Name = "Atmosphere",
        Doc = "The world's look under `Lighting` (ADR 0096, protocol 13): the authority's is every replica's. A camera's stays on its own screen.",
        Fields = AtmosphereFields,
    },
    {
        Name = "Sky",
        Doc = "The world's look under `Lighting` (ADR 0096, protocol 13): the authority's is every replica's. A camera's stays on its own screen.",
        Fields = SkyFields,
    },
    {
        Name = "TeamService",
        Service = true,
        Contents = true,
        Doc = "Where the sides live (ADR 0099, protocol 14): every `Team` under it reaches every "
            .. "replica, whatever its distance, as `ReplicatedStorage`'s contents do.",
        Fields = NoFields,
    },
    {
        Name = "Team",
        Doc = "A side (ADR 0099, protocol 14). Who is on it is not a field of the team: it is the "
            .. "roster's third column, beside each player's character.",
        Fields = TeamFields,
    },
}

--- Recorded by name, because a class nobody decided about and a class
--- deliberately left out look identical in a schema.
local Excluded: { Schema.Excluded } = {
    {
        Name = "Terrain",
        Reason = "Its bulk is not a property and a field is megabytes. A world's ground arrives with "
            .. "the world -- from the scene, or later from a streamed cell -- and a sculpt is rare, "
            .. "authored, and not a per-tick fact. ADR 0069 decides this by name so the next person "
            .. "does not have to work out whether it was considered.",
    },
    {
        Name = "Player",
        Reason = "Lives under `NetworkService`, which is not replicated, and each end makes its own: an "
            .. "authority one per peer, a replica one for itself and one for each player the `Players` "
            .. "roster names. What a replica knows of a player -- their number, their character and "
            .. "their team -- is the roster, not a snapshot of the instance.",
    },
    {
        Name = "DataModel",
        Reason = "The root. Every world has exactly one and it is created at boot on both sides, so "
            .. "there is nothing to spawn and nothing to reference by id.",
    },

    {
        Name = "Attachment",
        Reason = "A joint frame. The replica's bodies are kinematic-driven from the authority's "
            .. "transforms and solve nothing, so the frames that produced those transforms are work "
            .. "it would do twice and disagree about.",
    },
    {
        Name = "Bone",
        Reason = "The same argument as `Attachment`, plus one: a bone's world transform is derived "
            .. "from an animated pose, and the pose is derived from state that does replicate.",
    },
    {
        Name = "Weld",
        Reason = "A welded part's transform is resolved from its anchor every tick. The authority "
            .. "sends the resolved transform; sending the weld as well would give the replica two "
            .. "answers.",
    },
    {
        Name = "WeldConstraint",
        Reason = "The same as `Weld`.",
    },
    {
        Name = "BallSocketConstraint",
        Reason = "The replica does not solve constraints -- its bodies are kinematic. What it needs "
            .. "is where the limbs ended up, which is `BasePart.CFrame`.",
    },
    {
        Name = "HingeConstraint",
        Reason = "The same as `BallSocketConstraint`.",
    },
    {
        Name = "FixedConstraint",
        Reason = "The same as `BallSocketConstraint`.",
    },
    {
        Name = "Ragdoll",
        Reason = "A ragdoll is parts and constraints, and the parts replicate. The ragdoll instance "
            .. "is the recipe rather than the result.",
    },
    {
        Name = "Constraint2D",
        Reason = "The same as `BallSocketConstraint`, on the plane (ADR 0102): a replica's sprites are "
            .. "moved to where the authority's joints put them, and `Part2D.Position` is what arrives. "
            .. "The one entry covers its three classes.",
    },
    {
        Name = "SpriteAnimator",
        Reason = "What it does is write its sprite's `ImageRectOffset`, and that property replicates "
            .. "(ADR 0102). Sending the animator too would play the sheet twice on a replica.",
    },

    {
        Name = "Camera",
        Reason = "A replica's view is its own. Replicating it would take the camera away from the "
            .. "player, which is the one thing a client must decide.",
    },
    {
        Name = "Script",
        Reason = "Source code is not world state. A replica runs the game it was shipped, and a "
            .. "server that could push source to a client would be a server that can run code on it.",
    },
    {
        Name = "ModuleScript",
        Reason = "The same as `Script`.",
    },
    {
        Name = "Sound",
        Reason = "Playback position is local and drifts by design. What replicates is the world event "
            .. "that starts a sound, not the sound.",
    },
    {
        Name = "AudioGroup",
        Reason = "A mixer bus. What a player hears at what volume is theirs.",
    },
    {
        Name = "PointLight",
        Reason = "Lighting is drawn from world state a replica already has. A light that moved "
            .. "because its parent moved needs no message of its own.",
    },
    {
        Name = "SpotLight",
        Reason = "The same as `PointLight`.",
    },

    -- ADR 0096: the abstract base of the five effects, which travel by name.
    {
        Name = "PostEffect",
        Reason = "Abstract. Its five classes are replicated by name.",
    },
    {
        Name = "Material",
        Reason = "A material is an asset reference, and assets arrive with the build rather than over " .. "the wire.",
    },

    {
        Name = "InputAction",
        Reason = "Input is the client's to send as intent and never the authority's to push back. "
            .. "ADR 0041 already routes it through one dispatch; replicating it would give a server a "
            .. "second way to press a player's keys.",
    },
    {
        Name = "InputBinding",
        Reason = "A rebinding is a preference on one machine. The same as `InputAction`.",
    },
    {
        Name = "InputContext",
        Reason = "Which bindings are live is local UI state. The same as `InputAction`.",
    },

    {
        Name = "ScreenGui",
        Reason = "Screen-space UI is drawn from local state by local scripts. What a player sees on "
            .. "their own HUD is not a fact the authority owns.",
    },
    {
        Name = "BillboardGui",
        Reason = "World-space UI is built by each peer's own scripts, as the screen's is: everything under "
            .. "it is a UI class the wire does not carry, so sending the root alone would send an empty "
            .. "board. A name tag over another player's character is made on each client from what IS "
            .. "replicated -- the character and its name.",
    },
    { Name = "SurfaceGui", Reason = "World-space UI. The same as `BillboardGui`." },
    {
        Name = "NavigationArea",
        Reason = "What it says is a question only the authority's navigation asks (ADR 0098): a path is "
            .. "searched where the game's scripts run, and a replica that wants one asks with a remote. "
            .. "The part it labels is replicated; the label is not something a replica draws.",
    },
    { Name = "NavigationLink", Reason = "A way across for the authority's paths. The same as `NavigationArea`." },
    {
        Name = "NavigationAgent",
        Reason = "It walks its part on the authority's tick, and the part's `CFrame` is what arrives -- "
            .. "interpolated like any other part's. Sending the agent too would send a second account of "
            .. "one motion.",
    },
    { Name = "Frame", Reason = "Screen-space UI. The same as `ScreenGui`." },
    { Name = "TextLabel", Reason = "Screen-space UI. The same as `ScreenGui`." },
    { Name = "TextButton", Reason = "Screen-space UI. The same as `ScreenGui`." },
    { Name = "TextInput", Reason = "Screen-space UI. The same as `ScreenGui`." },
    { Name = "ImageLabel", Reason = "Screen-space UI. The same as `ScreenGui`." },
    { Name = "ImageButton", Reason = "Screen-space UI. The same as `ScreenGui`." },
    { Name = "ScrollFrame", Reason = "Screen-space UI. The same as `ScreenGui`." },
    { Name = "UICorner", Reason = "Screen-space UI. The same as `ScreenGui`." },
    { Name = "UIListLayout", Reason = "Screen-space UI. The same as `ScreenGui`." },
    { Name = "UIPadding", Reason = "Screen-space UI. The same as `ScreenGui`." },

    {
        Name = "AnimationPlayer",
        Reason = "**The first candidate for protocol 2, and out of N1 on purpose.** What an animation "
            .. "needs is which clip, at what time, at what weight -- three fields whose meaning is a "
            .. "blend tree the wire would have to describe. N1 replicates where bodies are, which is "
            .. "what a player sees; making the limbs move the same way needs a design of its own and "
            .. "would double this milestone.",
    },
}

--- **One reason, written once.** Thirteen copies of a sentence is a schema
--- nobody reads, and a schema nobody reads is one nobody notices a fourteenth
--- entry being wrong in.
local ExcludedByTag: { Schema.ExcludedTag } = {
    {
        Tag = "Service",
        Reason = "A service is a singleton every world creates at boot, on both sides, so there is "
            .. "nothing to spawn, despawn or reference by network id. Whether a service's PROPERTIES "
            .. "are world state is a separate, per-service question: a service whose properties travel is "
            .. "listed under `Classes` with `Service = true` (`Lighting` is), and every other one "
            .. "falls here.",
    },
}

--- **Numbers are permanent for the reason field ids are.** Channel 3 was
--- reserved so the numbering could not shift when network ownership arrived,
--- and it has (ADR 0099). `RemoteEvent` arrived without it: its messages ride
--- the Control channel, after the spawns they may name (ADR 0077).
local Channels: { Schema.Channel } = {
    {
        Name = "Control",
        Id = 0,
        Delivery = "Reliable",
        Doc = "Handshake, spawn, despawn, and the baseline acknowledgement. Everything whose loss "
            .. "cannot be recovered by the next tick's snapshot.",
    },
    {
        Name = "State",
        Id = 1,
        Delivery = "UnreliableSequenced",
        Doc = "The per-tick diff. Unreliable because a dropped snapshot is superseded by the next "
            .. "one, and sequenced because an old one arriving late is worse than none. **This is the "
            .. "channel `flagsFor`'s missing `ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT` would have "
            .. "broken**, silently, by putting a large snapshot on the reliable-fragmented path -- "
            .. "head-of-line blocking, which is exactly what the mode exists to avoid.",
    },
    {
        Name = "Intent",
        Id = 2,
        Delivery = "UnreliableSequenced",
        Doc = "What a client did, never what happened. The client says it moved and fired; the "
            .. "authority decides whether it moved and whether it hit.",
    },
    {
        Name = "Ownership",
        Id = 3,
        Delivery = "UnreliableSequenced",
        Doc = "Where an owner's parts are, from the owner (ADR 0099, protocol 14). Reserved as channel 3 "
            .. "since protocol 1 so its arrival shifted nothing. Unreliable and sequenced for the reason "
            .. "the State channel is: a lost state is superseded by the next tick's, and an old one "
            .. "arriving late would move a part backwards.",
    },
}

local Messages: { Schema.Message } = {
    {
        Name = "Hello",
        Id = 1,
        Channel = "Control",
        Direction = "ToAuthority",
        Layout = {
            {
                Name = "protocol",
                Type = "u32",
                Doc = "The protocol version the replica speaks. Any other than the authority's is refused.",
            },
            {
                Name = "tokenHigh",
                Type = "u64",
                Doc = "The player token the authority gave on an earlier join, or zero the first time.",
            },
            { Name = "tokenLow", Type = "u64" },
        },
        Doc = "A replica announcing itself: protocol version, then the player token the authority "
            .. "gave it last time, or zeros the first time. **Version first, so a mismatch is refused "
            .. "before a single field is parsed** rather than after a decoder has already read a field "
            .. "id it does not have. The token is how a player who drops and comes back is the same "
            .. "player (ADR 0085).",
    },
    {
        Name = "Welcome",
        Id = 2,
        Channel = "Control",
        Direction = "ToReplica",
        Layout = {
            {
                Name = "protocol",
                Type = "u32",
                Doc = "The authority's version; a replica refuses a Welcome in another.",
            },
            { Name = "userId", Type = "u32", Doc = "The replica's own player number, 2 and up." },
            { Name = "tick", Type = "u64", Doc = "The authority's tick when it welcomed." },
            { Name = "tokenHigh", Type = "u64", Doc = "The high half of the player token (ADR 0085)." },
            { Name = "tokenLow", Type = "u64", Doc = "The low half." },
        },
        Doc = "Accepted: the peer's own `Player` id, the authority's tick, and the player token to "
            .. "send back on a reconnect -- the same id again (ADR 0085). A refusal is a disconnect "
            .. "with a keyed reason rather than a silence.",
    },
    {
        Name = "Spawn",
        Id = 3,
        Channel = "Control",
        Direction = "ToReplica",
        Layout = {
            { Name = "count", Type = "u32" },
            {
                Name = "instances",
                Type = "repeat",
                Count = "count",
                Doc = "In ascending network id. A service is never spawned.",
                Each = {
                    { Name = "netId", Type = "u32" },
                    {
                        Name = "className",
                        Type = "text",
                        Doc = "Created by name; a class the replica does not have is skipped.",
                    },
                },
            },
        },
        Doc = "An instance entering interest: its network id and the class to create. Reliable, "
            .. "because an instance that is never created is a replica missing part of the world for "
            .. "good. **Its fields ride the snapshot**, as a whole record against a baseline that "
            .. "does not have it -- one decoding path for new and changed rather than two.",
    },
    {
        Name = "Despawn",
        Id = 4,
        Channel = "Control",
        Direction = "ToReplica",
        Layout = {
            { Name = "destroyedCount", Type = "u32" },
            {
                Name = "destroyed",
                Type = "repeat",
                Count = "destroyedCount",
                Doc = "Instances the authority destroyed.",
                Each = {
                    { Name = "netId", Type = "u32" },
                },
            },
            { Name = "streamedOutCount", Type = "u32" },
            {
                Name = "streamedOut",
                Type = "repeat",
                Count = "streamedOutCount",
                Doc = "Instances that left this replica's interest and still exist on the authority.",
                Each = {
                    { Name = "netId", Type = "u32" },
                },
            },
        },
        Doc = "Instances a replica no longer has, in two lists: those the authority destroyed, then "
            .. "those that only left this peer's interest (protocol 6). **Leaving interest is the same "
            .. "event as a chunk being evicted**: one a script holds becomes a husk, reparented to nil, "
            .. "and is reported through the same `InstanceStreamedOut` a script already handles -- a "
            .. "second spelling of one thing would mean every game handles both.",
    },
    {
        Name = "Snapshot",
        Id = 5,
        Channel = "State",
        Direction = "ToReplica",
        Layout = {
            {
                Name = "tick",
                Type = "u64",
                Doc = "The authority tick this is the world at. Older than one applied: dropped.",
            },
            {
                Name = "baseTick",
                Type = "u64",
                Doc = "The acknowledged tick this is a diff against, or 0: every record is then whole.",
            },
            { Name = "checksum", Type = "u64", Doc = 'Over the replica\'s state once applied; see "The checksum".' },
            {
                Name = "intentTick",
                Type = "u64",
                Doc = "The newest of this replica's `Intent` ticks the authority has applied.",
            },
            { Name = "atomCount", Type = "u16" },
            {
                Name = "atoms",
                Type = "repeat",
                Count = "atomCount",
                Doc = "Every name the records mention, in ascending number.",
                Each = {
                    { Name = "atom", Type = "u32", Doc = "The authority's number for a name." },
                    { Name = "text", Type = "text" },
                },
            },
            { Name = "recordCount", Type = "u32" },
            {
                Name = "records",
                Type = "repeat",
                Count = "recordCount",
                Doc = "In ascending network id; an instance with nothing changed has none.",
                Each = {
                    { Name = "netId", Type = "u32" },
                    { Name = "schema", Type = "u8", Doc = 'The class\'s index in "Replicated classes".' },
                    {
                        Name = "flags",
                        Type = "u8",
                        Doc = "Bit 0: a whole record, every field in order. Otherwise only what changed.",
                    },
                    { Name = "fieldCount", Type = "u16" },
                    {
                        Name = "fields",
                        Type = "repeat",
                        Count = "fieldCount",
                        Each = {
                            { Name = "wireId", Type = "u16", Doc = 'See "Wire ids".' },
                            { Name = "value", Type = "value", Doc = "In the encoding of the field the wire id names." },
                        },
                    },
                },
            },
        },
        Doc = "The tick's diff against the peer's last acknowledged baseline, with a checksum over "
            .. "what was applied. **The checksum catches apply bugs and nothing else** -- interest "
            .. "gives a replica a strict subset of the world, so its hash is not the authority's and "
            .. "comparing them would be comparing two different worlds.",
    },
    {
        Name = "Ack",
        Id = 6,
        Channel = "Control",
        Direction = "ToAuthority",
        Layout = {
            {
                Name = "tick",
                Type = "u64",
                Doc = "The snapshot tick the replica now holds, and may be diffed against.",
            },
        },
        Doc = "Which baseline the replica now holds. Reliable, because a lost acknowledgement makes "
            .. "the authority diff against a baseline the replica has moved past.",
    },
    {
        Name = "Intent",
        Id = 7,
        Channel = "Intent",
        Direction = "ToAuthority",
        Layout = {
            { Name = "tick", Type = "u64", Doc = "The replica's tick. Not newer than the last one applied: dropped." },
            { Name = "count", Type = "u16" },
            {
                Name = "intents",
                Type = "repeat",
                Count = "count",
                Each = {
                    { Name = "action", Type = "text", Doc = "The input action's name." },
                    {
                        Name = "type",
                        Type = "u8",
                        Doc = "0 Bool, 1 Direction1D, 2 Direction2D, 3 Direction3D, 4 ViewportPosition.",
                    },
                    { Name = "x", Type = "f32" },
                    { Name = "y", Type = "f32" },
                    { Name = "z", Type = "f32" },
                    { Name = "pressed", Type = "u8" },
                },
            },
        },
        Doc = "One tick of a client's input, as actions rather than as results. The client says what "
            .. "it did; a client that could assert what happened is a client that always hits.",
    },
    {
        Name = "Players",
        Id = 8,
        Channel = "Control",
        Direction = "ToReplica",
        Layout = {
            { Name = "count", Type = "u32" },
            {
                Name = "players",
                Type = "repeat",
                Count = "count",
                Doc = "In join order.",
                Each = {
                    { Name = "userId", Type = "u32" },
                    { Name = "character", Type = "u32", Doc = "The network id of their `Character`, or 0." },
                    { Name = "team", Type = "u32", Doc = "The network id of their `Team`, or 0." },
                },
            },
        },
        Doc = "Who is taking part: every player's number, their character's network id and their "
            .. "team's (protocol 14), zero for none, sent whole whenever any of it changes and "
            .. "once on welcome. Whole rather than as joins and leaves, because a roster is a few "
            .. "numbers and a list a replica can only ever replace cannot drift from the authority's.",
    },
    {
        Name = "RemoteToAuthority",
        Id = 9,
        Channel = "Control",
        Direction = "ToAuthority",
        Layout = {
            { Name = "remote", Type = "u32", Doc = "The network id of the `RemoteEvent` or `RemoteFunction`." },
            {
                Name = "call",
                Type = "u32",
                Doc = "0 for an event; otherwise the call a `RemoteFunction` answer belongs to.",
            },
            {
                Name = "flags",
                Type = "u8",
                Doc = "Bit 0: an answer. Bit 1: the answer is an error. Always 0 toward the authority.",
            },
            { Name = "refCount", Type = "u16" },
            {
                Name = "refs",
                Type = "repeat",
                Count = "refCount",
                Doc = "The instances the arguments name, by position.",
                Each = {
                    { Name = "netId", Type = "u32", Doc = "0 for an instance the sender has no id for." },
                },
            },
            { Name = "size", Type = "u32", Doc = "The byte length of what follows, at most 65552." },
            { Name = "arguments", Type = "arguments", Doc = 'See "Arguments".' },
        },
        Doc = "`RemoteEvent:FireServer` (protocol 7): the event's network id, a call number and a "
            .. "flags byte (protocol 8), the network ids of the instances the arguments name, and the "
            .. "arguments as the script module encoded them. A call number that is not zero is "
            .. "`RemoteFunction:InvokeServerAsync` asking, and names a `RemoteFunction`; zero names a "
            .. "`RemoteEvent`. The sender is the connection's player, never anything the message says.",
    },
    {
        Name = "RemoteToReplica",
        Id = 10,
        Channel = "Control",
        Direction = "ToReplica",
        Layout = {
            { Name = "remote", Type = "u32", Doc = "The network id of the `RemoteEvent` or `RemoteFunction`." },
            {
                Name = "call",
                Type = "u32",
                Doc = "0 for an event; otherwise the call a `RemoteFunction` answer belongs to.",
            },
            {
                Name = "flags",
                Type = "u8",
                Doc = "Bit 0: an answer to `call`. Bit 1: the answer is an error, one string argument.",
            },
            { Name = "refCount", Type = "u16" },
            {
                Name = "refs",
                Type = "repeat",
                Count = "refCount",
                Doc = "The instances the arguments name, by position.",
                Each = {
                    { Name = "netId", Type = "u32", Doc = "0 for an instance the sender has no id for." },
                },
            },
            { Name = "size", Type = "u32", Doc = "The byte length of what follows, at most 65552." },
            { Name = "arguments", Type = "arguments", Doc = 'See "Arguments".' },
        },
        Doc = "`RemoteEvent:FireClient` and `FireAllClients`, laid out as `RemoteToAuthority` is. On "
            .. "the Control channel, after the spawns of the same send, so an event created and fired "
            .. "in one tick is never named before it exists. With the reply flag set it is a "
            .. "`RemoteFunction`'s answer to the call number it carries, and with the failed flag the "
            .. "answer is the error the handler raised.",
    },
    {
        Name = "Ownership",
        Id = 11,
        Channel = "Control",
        Direction = "ToReplica",
        Layout = {
            { Name = "count", Type = "u32" },
            {
                Name = "parts",
                Type = "repeat",
                Count = "count",
                Doc = "Every part this replica owns, in ascending network id.",
                Each = {
                    { Name = "netId", Type = "u32" },
                },
            },
        },
        Doc = "The network ids of every part this replica owns (ADR 0099, protocol 14), sent whole "
            .. "whenever the list changes. Whole, as the roster is: a list the replica can only replace "
            .. "cannot drift from the authority's. Reliable and after the spawns, so it never names a "
            .. "part the replica has not been sent.",
    },
    {
        Name = "OwnedState",
        Id = 12,
        Channel = "Ownership",
        Direction = "ToAuthority",
        Layout = {
            { Name = "tick", Type = "u64", Doc = "The owner's tick. Not newer than the last one taken: dropped." },
            { Name = "count", Type = "u16" },
            {
                Name = "parts",
                Type = "repeat",
                Count = "count",
                Each = {
                    { Name = "netId", Type = "u32", Doc = "A part the sender owns; any other is skipped." },
                    { Name = "cframe", Type = "CFrameD" },
                    { Name = "linearVelocity", Type = "Vector3", Doc = "Metres per second." },
                    { Name = "angularVelocity", Type = "Vector3", Doc = "Radians per second." },
                },
            },
        },
        Doc = "One tick of the parts a replica owns: for each, its network id, its transform and its "
            .. "linear and angular velocity, as the owner's solver left them. The authority takes a state "
            .. "only for a part the sender owns at the moment it arrives, and moves the part there.",
    },
    {
        Name = "TilemapBlocks",
        Id = 13,
        Channel = "Control",
        Direction = "ToReplica",
        Layout = {
            { Name = "netId", Type = "u32", Doc = "The `Tilemap2D`. One the replica does not have is skipped." },
            { Name = "count", Type = "u16", Doc = "At most 64 blocks a message; a larger map takes several." },
            {
                Name = "blocks",
                Type = "repeat",
                Count = "count",
                Each = {
                    {
                        Name = "x",
                        Type = "u32",
                        Doc = "The block's column, two's complement: a block may be left of the origin.",
                    },
                    { Name = "y", Type = "u32", Doc = "Its row, the same way." },
                    {
                        Name = "cellCount",
                        Type = "u16",
                        Doc = "Always 256, 16 by 16; a block of any other size is refused.",
                    },
                    {
                        Name = "cells",
                        Type = "repeat",
                        Count = "cellCount",
                        Doc = "Row after row from the block's bottom left.",
                        Each = {
                            { Name = "tile", Type = "u16", Doc = "The tile, 0 for empty." },
                        },
                    },
                },
            },
        },
        Doc = "A tilemap's cells, a block of 16 by 16 at a time (ADR 0103, protocol 15). Every block it "
            .. "holds after its spawn, then every block an edit changed, added or emptied. **A block of all "
            .. "zeros is an empty one**, and the replica drops it. Whole blocks, so a block received twice is "
            .. "simply the truth; reliable and after the spawns, so it never names a tilemap the replica has "
            .. "not been sent.",
    },
}

local Definition: Schema.Definition = {
    Common = Common,
    Classes = Classes,
    Excluded = Excluded,
    ExcludedByTag = ExcludedByTag,
    Channels = Channels,
    Messages = Messages,
}

return Definition
