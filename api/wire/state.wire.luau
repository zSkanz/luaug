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
        Name = "Color",
        Id = 3,
        Encoding = "Color3",
        Source = "Component",
        Component = "parts",
        Doc = "Games signal state with colour -- a captured point, a damaged crate -- so it is "
            .. "gameplay rather than decoration.",
    },
    {
        Name = "Transparency",
        Id = 4,
        Encoding = "F32",
        Source = "Component",
        Component = "parts",
        Doc = "The same argument as `Color`: it is how a game says a thing is a ghost, is picked up, "
            .. "or is about to vanish.",
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

local NoFields: { Schema.Field } = {}

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

local Classes: { Schema.Class } = {
    {
        Name = "BasePart",
        Doc = "The moving half of nearly every game, and therefore the field set that decides whether "
            .. "replication is affordable at all. `Part` and `MeshPart` inherit it.",
        Fields = BasePartFields,
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
            .. "authority one per peer, a replica one for itself. A replica does not yet see the OTHER "
            .. "players -- a player list is its own message, owed after N1's first playable build.",
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

--- **Numbers are permanent for the reason field ids are**, and channel 3 is
--- reserved rather than left free so the numbering cannot shift when
--- `NetworkOwnership` or `Team` arrives. `RemoteEvent` arrived without it: its
--- messages ride the Control channel, after the spawns they may name (ADR 0077).
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
        Name = "Reserved3",
        Id = 3,
        Delivery = "Reliable",
        Doc = "Claimed and unused. `NetworkOwnership` and `Team` are deliberately not built, and a "
            .. "channel number that shifted when one of them arrived would break every build that had "
            .. "already shipped. `RemoteEvent` does not use it: a message on its own channel could "
            .. "arrive before the spawn of the event it names.",
    },
}

local Messages: { Schema.Message } = {
    {
        Name = "Hello",
        Id = 1,
        Channel = "Control",
        Direction = "ToAuthority",
        Doc = "A replica announcing itself: protocol version and nothing else. **Version first, so a "
            .. "mismatch is refused before a single field is parsed** rather than after a decoder has "
            .. "already read a field id it does not have.",
    },
    {
        Name = "Welcome",
        Id = 2,
        Channel = "Control",
        Direction = "ToReplica",
        Doc = "Accepted: the peer's own `Player` id and the authority's tick. A refusal is a "
            .. "disconnect with a keyed reason rather than a silence.",
    },
    {
        Name = "Spawn",
        Id = 3,
        Channel = "Control",
        Direction = "ToReplica",
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
        Doc = "Which baseline the replica now holds. Reliable, because a lost acknowledgement makes "
            .. "the authority diff against a baseline the replica has moved past.",
    },
    {
        Name = "Intent",
        Id = 7,
        Channel = "Intent",
        Direction = "ToAuthority",
        Doc = "One tick of a client's input, as actions rather than as results. The client says what "
            .. "it did; a client that could assert what happened is a client that always hits.",
    },
    {
        Name = "Players",
        Id = 8,
        Channel = "Control",
        Direction = "ToReplica",
        Doc = "Who is taking part: every player's number, sent whole whenever the list changes and "
            .. "once on welcome. Whole rather than as joins and leaves, because a roster is a few "
            .. "numbers and a list a replica can only ever replace cannot drift from the authority's.",
    },
    {
        Name = "RemoteToAuthority",
        Id = 9,
        Channel = "Control",
        Direction = "ToAuthority",
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
        Doc = "`RemoteEvent:FireClient` and `FireAllClients`, laid out as `RemoteToAuthority` is. On "
            .. "the Control channel, after the spawns of the same send, so an event created and fired "
            .. "in one tick is never named before it exists. With the reply flag set it is a "
            .. "`RemoteFunction`'s answer to the call number it carries, and with the failed flag the "
            .. "answer is the error the handler raised.",
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
