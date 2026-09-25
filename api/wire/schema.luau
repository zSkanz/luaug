--!strict
--- The wire IDL's schema (ADR 0069).
---
--- `api/wire/*.wire.luau` are data files written against these types, exactly as
--- `api/defs/*.api.luau` are written against `api/schema.luau` — and for exactly
--- the same reason. Two hand-maintained lists of the same facts is one list plus
--- a bug waiting for somebody to edit only one of them, and the fact this one
--- describes is *what a replica is told*. A field the world has and the wire does
--- not is a replica that is confidently wrong about the state deciding the next
--- tick.
---
--- **Field ids are permanent.** That is the one rule this file exists to make
--- unbreakable. An id that means one thing to a server and another to a replica
--- produces a symptom nobody can debug: a position that is a colour, a health
--- that is a rotation. A removed field goes to `Retired` with the version it died
--- in, and `tools/repo/wirecheck.luau` refuses a tree that reuses one.

local Schema = {}

--- What a replicated field is read from.
---
--- **`Property` is the ordinary case and `Component` is the one that matters.**
--- The most-replicated fact in any game — where things are — is written into the
--- component directly by `PhysicsSync` under a comment naming itself the QUIET
--- write, and it never reaches `setProperty` at all (ADR 0069). A schema that
--- could only name properties would be a schema that could not replicate motion.
export type Source = "Property" | "Component"

--- How a field's bytes are laid down.
---
--- Deliberately small. Every entry is a fixed-width little-endian encoding with
--- no variable-length forms and no nesting: a wire format whose parser can
--- recurse is a wire format with a stack-depth question in it, and the answer to
--- that question is always found by somebody hostile rather than by a test.
---
--- `Vector3` and `CFrameD` are named rather than composed out of floats because
--- the quantisation decision belongs to the field's meaning: a position is f64
--- world coordinates (R9) and an orientation is not.
export type Encoding =
    "Bool"
    | "U8"
    | "U16"
    | "U32"
    | "I32"
    | "F32"
    | "F64"
    | "Vector3" -- three f32, which is what a `vector` is (R9)
    | "Position" -- three f64, because a world coordinate is f64 engine-side
    | "CFrameD" -- a position plus an orientation
    | "Color3"
    | "NameAtom" -- interned string, sent by id with the table replicated once
    | "InstanceRef" -- a network id, never a pointer and never a path
    -- ADR 0090: a part's overrides of its material, as one value -- which of the
    -- seven parameters are set, and their values.
    | "MaterialOverrides"
    -- ADR 0090: what a runtime clone of a material changed -- which fields are
    -- set, and the numbers, colours, alpha mode and sidedness. The four maps
    -- travel as names beside it.
    | "MaterialValues"

--- One encoding as the published protocol describes it (ADR 0100).
export type EncodingDesc = {
    Name: Encoding,
    --- Its size on the wire. Checked against the engine's `wireBytes` by a test,
    --- so the specification cannot say one size while the code writes another.
    Bytes: number,
    --- How the bytes are laid down, for somebody writing a peer.
    Layout: string,
}

--- **Every encoding, in its permanent numeric order**: the position is the
--- value of `generated::Encoding`, so an entry is only ever appended.
Schema.Encodings = {
    { Name = "Bool", Bytes = 1, Layout = "u8: 0 is false; anything else reads as true." },
    { Name = "U8", Bytes = 1, Layout = "u8." },
    { Name = "U16", Bytes = 2, Layout = "u16." },
    { Name = "U32", Bytes = 4, Layout = "u32." },
    { Name = "I32", Bytes = 4, Layout = "i32, two's complement." },
    { Name = "F32", Bytes = 4, Layout = "IEEE-754 binary32." },
    { Name = "F64", Bytes = 8, Layout = "IEEE-754 binary64." },
    { Name = "Vector3", Bytes = 12, Layout = "f32 x, y, z." },
    { Name = "Position", Bytes = 24, Layout = "f64 x, y, z: a world coordinate." },
    {
        Name = "CFrameD",
        Bytes = 60,
        Layout = "f64 position x, y, z; then the rotation as nine f32, column by column: the right vector "
            .. "(x, y, z), the up vector, then the back vector. The look direction is the back vector negated. "
            .. "Nothing is quantised.",
    },
    { Name = "Color3", Bytes = 12, Layout = "f32 r, g, b, linear in [0, 1] by convention and not clamped." },
    {
        Name = "NameAtom",
        Bytes = 4,
        Layout = "u32: an interned string, numbered in the AUTHORITY's table. The `Snapshot` that carries "
            .. "it also carries the number's text, and 0 is no name.",
    },
    { Name = "InstanceRef", Bytes = 4, Layout = "u32: a network id, 0 for none." },
    {
        Name = "MaterialOverrides",
        Bytes = 48,
        Layout = "u16 mask of the parameters set, two zero bytes, then eleven f32: colour r, g, b; "
            .. "transparency; emissive r, g, b; metalness; roughness; normal scale; alpha cutoff. Mask bit n is "
            .. "parameter n: Color 0, Transparency 1, ColorMap 2, NormalMap 3, MetallicRoughnessMap 4, "
            .. "Emissive 5, EmissiveMap 6, Metalness 7, Roughness 8, NormalScale 9, AlphaMode 10, "
            .. "AlphaCutoff 11, DoubleSided 12.",
    },
    {
        Name = "MaterialValues",
        Bytes = 52,
        Layout = "The 48 bytes of `MaterialOverrides`, then u8 alpha mode, u8 double-sided, and two zero "
            .. "bytes. All zero when the part wears no runtime clone.",
    },
} :: { EncodingDesc }

--- One part of a message, in the order it is written (ADR 0100).
---
--- `Type` is a primitive (`u8`, `u16`, `u32`, `u64`, `f32`, `text`), an
--- `Encoding`'s name, `value` (a field's value in the encoding its wire id
--- names), `arguments` (a remote call's encoded arguments, specified on their
--- own), or `repeat` -- which writes `Each` as many times as the earlier part
--- `Count` names says.
export type Part = {
    Name: string,
    Type: string,
    Count: string?,
    Each: { Part }?,
    Doc: string?,
}

--- One replicated field.
export type Field = {
    Name: string,
    --- **Permanent.** Unique within its class and never reused, including after
    --- the field is retired.
    Id: number,
    Encoding: Encoding,
    Source: Source,
    --- For `Source = "Component"`: which component pool the value is read from,
    --- named as it appears in `LUAUG_SCENE_POOL_LIST`. Absent for properties,
    --- which are read through the generated accessor by name.
    Component: string?,
    --- Why this field is on the wire, in one sentence. Not decoration: the
    --- alternative to writing it down is a schema where nobody can tell an
    --- expensive field from a necessary one two years later.
    Doc: string,
}

--- A field that used to exist. Its id stays claimed for ever.
export type RetiredField = {
    Name: string,
    Id: number,
    --- The engine version the field was removed in, so a bug report naming an
    --- old build can be read.
    Since: string,
    Reason: string,
}

--- One replicated class.
export type Class = {
    Name: string,
    --- The replicated class this one's instances ALSO carry the fields of: a
    --- `CharacterBody` is a `BasePart`, and a replica that received its three
    --- fields and not the part's six would hold a character that never moves.
    --- One level, and it must be declared earlier in the list.
    Extends: string?,
    --- A service rather than an instance class: one per world, made at boot on
    --- both ends, so it is never spawned or despawned and is always in
    --- interest. Its properties are what travel.
    Service: boolean?,
    --- A service whose CHILDREN replicate too (ADR 0080): to every replica,
    --- whatever their position, because what is kept there is kept for
    --- everybody. Only a service can be one.
    Contents: boolean?,
    Fields: { Field },
    Retired: { RetiredField }?,
    Doc: string?,
}

--- A class that is deliberately NOT replicated.
---
--- **Recorded by name rather than by absence**, because the two look identical
--- in a schema and only one of them was thought about. `Terrain` is the example
--- this exists for: its bulk is not a property, a field is megabytes, and a
--- world's ground arrives with the world.
export type Excluded = {
    Name: string,
    Reason: string,
}

--- A whole tag excluded at once.
---
--- **For the case where the reason is the same sentence thirteen times.** Every
--- `Service` is excluded for one reason and writing it once is more honest than
--- writing it thirteen times: thirteen copies of a sentence is a schema nobody
--- reads, and a schema nobody reads is one nobody notices a fourteenth entry
--- being wrong in.
export type ExcludedTag = {
    Tag: string,
    Reason: string,
}

--- A protocol channel. Numbers are permanent for the same reason field ids are.
export type Channel = {
    Name: string,
    Id: number,
    --- ENet's delivery mode for this channel, as `net::Delivery` spells it.
    Delivery: "Reliable" | "UnreliableSequenced" | "Unreliable",
    Doc: string,
}

--- A message type on a channel.
export type Message = {
    Name: string,
    Id: number,
    Channel: string,
    --- Which direction it may travel. A message a replica may send to an
    --- authority and one an authority may send to a replica are different
    --- things, and a schema that did not say which would leave a server
    --- accepting whatever a client asserted.
    Direction: "ToAuthority" | "ToReplica",
    Doc: string,
    --- **Its bytes, after the type byte, as the published protocol states
    --- them** (ADR 0100).
    Layout: { Part },
}

export type Definition = {
    --- The fields every replicated instance carries whatever its class.
    ---
    --- **Not a `Class` entry for `Instance`**, and the distinction is
    --- load-bearing rather than tidy: every class extends `Instance`, so a row
    --- for it would make every class in the engine "decided by inheritance" and
    --- the one check this schema exists for would pass on an empty file.
    Common: { Field }?,
    Classes: { Class }?,
    Excluded: { Excluded }?,
    ExcludedByTag: { ExcludedTag }?,
    Channels: { Channel }?,
    Messages: { Message }?,
}

local Primitives: { [string]: boolean } = {
    u8 = true,
    u16 = true,
    u32 = true,
    u64 = true,
    f32 = true,
    text = true,
    value = true,
    arguments = true,
}

--- A layout names only types the specification defines, and a repeat names a
--- count written before it.
local function checkLayout(where: string, parts: { Part }, fail: (string) -> ())
    local seen: { [string]: boolean } = {}
    local encodings: { [string]: boolean } = {}
    for _, encoding in Schema.Encodings do
        encodings[encoding.Name] = true
    end
    for _, part in parts do
        if part.Type == "repeat" then
            if part.Count == nil or not seen[part.Count] then
                fail(`{where}.{part.Name}: a repeat counts by a part written before it`)
            end
            if part.Each == nil or #part.Each == 0 then
                fail(`{where}.{part.Name}: a repeat says what it repeats`)
            else
                checkLayout(`{where}.{part.Name}`, part.Each, fail)
            end
        elseif not Primitives[part.Type] and not encodings[part.Type] then
            fail(`{where}.{part.Name}: {part.Type} is not a type the protocol defines`)
        end
        seen[part.Name] = true
    end
end

--- What `wirecheck` enforces beyond what the type checker can.
---
--- Returns the problems, empty when there are none. A list rather than a throw,
--- so one run reports everything wrong rather than the first thing.
function Schema.validate(definition: Definition): { string }
    local problems: { string } = {}

    local function fail(message: string)
        table.insert(problems, message)
    end

    do
        local claimed: { [number]: string } = {}
        local common: { Field } = definition.Common or {}
        for _, field in common do
            if claimed[field.Id] ~= nil then
                fail(`Common.{field.Name}: id {field.Id} is already {claimed[field.Id]} -- ids are permanent`)
            end
            claimed[field.Id] = field.Name
            if field.Doc == "" then
                fail(`Common.{field.Name}: every field says why it is on the wire`)
            end
        end
    end

    local classes: { Class } = definition.Classes or {}
    local declared: { [string]: Class } = {}
    for _, class in classes do
        if class.Name == "" then
            fail("a class with no name")
            continue
        end
        if class.Extends ~= nil then
            local base = declared[class.Extends]
            if base == nil then
                fail(`{class.Name}: extends {class.Extends}, which is not a replicated class declared above it`)
            elseif base.Extends ~= nil then
                fail(`{class.Name}: extends {class.Extends}, which extends another -- one level only`)
            end
        end
        declared[class.Name] = class

        --- Ids claimed by this class, live and retired alike. **Both, in one
        --- table**, which is the whole point: a retired id is still claimed.
        local claimed: { [number]: string } = {}
        local names: { [string]: boolean } = {}

        local retiredList: { RetiredField } = class.Retired or {}
        for _, retired in retiredList do
            if claimed[retired.Id] ~= nil then
                fail(`{class.Name}: retired field id {retired.Id} is claimed twice`)
            end
            claimed[retired.Id] = `retired {retired.Name}`
            if retired.Since == "" then
                fail(`{class.Name}.{retired.Name}: a retired field must name the version it died in`)
            end
        end

        for _, field in class.Fields do
            if field.Id <= 0 then
                fail(`{class.Name}.{field.Name}: field ids start at 1`)
            end
            local existing = claimed[field.Id]
            if existing ~= nil then
                fail(`{class.Name}.{field.Name}: id {field.Id} is already {existing} -- ids are permanent`)
            end
            claimed[field.Id] = field.Name

            if names[field.Name] then
                fail(`{class.Name}.{field.Name}: declared twice`)
            end
            names[field.Name] = true

            if field.Source == "Component" and (field.Component == nil or field.Component == "") then
                fail(`{class.Name}.{field.Name}: a component field must name its pool`)
            end
            if field.Source == "Property" and field.Component ~= nil then
                fail(`{class.Name}.{field.Name}: a property field must not name a pool`)
            end
            if field.Doc == "" then
                fail(`{class.Name}.{field.Name}: every field says why it is on the wire`)
            end
        end
    end

    local channelIds: { [number]: string } = {}
    local channelNames: { [string]: boolean } = {}
    local channels: { Channel } = definition.Channels or {}
    for _, channel in channels do
        if channelIds[channel.Id] ~= nil then
            fail(`channel {channel.Name}: id {channel.Id} is already {channelIds[channel.Id]}`)
        end
        channelIds[channel.Id] = channel.Name
        channelNames[channel.Name] = true
    end

    local messageIds: { [number]: string } = {}
    local messages: { Message } = definition.Messages or {}
    for _, message in messages do
        if messageIds[message.Id] ~= nil then
            fail(`message {message.Name}: id {message.Id} is already {messageIds[message.Id]}`)
        end
        messageIds[message.Id] = message.Name
        if not channelNames[message.Channel] then
            fail(`message {message.Name}: channel {message.Channel} is not declared`)
        end
        if #message.Layout == 0 then
            fail(`message {message.Name}: a message states its layout (ADR 0100)`)
        end
        checkLayout(`message {message.Name}`, message.Layout, fail)
    end

    return problems
end

return Schema
