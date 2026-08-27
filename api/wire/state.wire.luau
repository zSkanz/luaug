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

local Classes: { Schema.Class } = {
    {
        Name = "BasePart",
        Doc = "The moving half of nearly every game, and therefore the field set that decides whether "
            .. "replication is affordable at all. `Part` and `MeshPart` inherit it.",
        Fields = BasePartFields,
    },
    {
        Name = "CharacterBody",
        Doc = "A player's body, and the one class where the authority's answer and the client's guess "
            .. "are most visibly different. Inherits `BasePart`'s six.",
        Fields = CharacterBodyFields,
    },
    {
        Name = "Model",
        Doc = "Replicated for its scale alone: everything else about a model is its children, and "
            .. "they replicate as themselves.",
        Fields = ModelFields,
    },
    {
        Name = "Folder",
        Doc = "**Replicated with no fields of its own, and that is the point.** A folder is "
            .. "structure: a `Parent` reference to one has to resolve on the replica, so the folder "
            .. "has to be there. Its `Name` and `Parent` come from the common set.",
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
            .. "are world state is a separate question and N1 does not answer it: "
            .. "`Lighting.ClockTime` is the real example, and it needs a per-service decision and a "
            .. "second delta path that is not the instance one. Getting instance replication right "
            .. "first is what makes that question answerable.",
    },
}

--- **Numbers are permanent for the reason field ids are**, and channel 3 is
--- reserved rather than left free so the numbering cannot shift when the first
--- of `RemoteEvent`, `NetworkOwnership` or `Team` arrives.
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
        Doc = "Claimed and unused. `RemoteEvent`, `NetworkOwnership` and `Team` are all deliberately "
            .. "out of N1, and a channel number that shifted when one of them arrived would break "
            .. "every build that had already shipped.",
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
        Doc = "An instance entering interest, with every field of its class at once. Reliable, "
            .. "because a diff against a baseline nobody has is a diff against nothing.",
    },
    {
        Name = "Despawn",
        Id = 4,
        Channel = "Control",
        Direction = "ToReplica",
        Doc = "An instance leaving interest. **The same event as a chunk being evicted**, reported "
            .. "through the same `InstanceStreamedOut` a script already handles -- a second spelling "
            .. "of one thing would mean every game handles both.",
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
