--!strict
--- The wire definition, assembled (ADR 0069).
---
--- One module every generator and checker reads, for the reason `api/defs/init`
--- gives about itself: adding a replicated class is meant to be a visible edit in
--- a file people review, not a directory scan that picks up whatever is lying
--- around.

local Schema = require("@self/schema")
local State = require("@self/state.wire")

export type Definition = Schema.Definition
export type Class = Schema.Class
export type Field = Schema.Field
export type Channel = Schema.Channel
export type Message = Schema.Message

local Wire = {}

--- **The protocol version, and it is not the engine's.**
---
--- An engine release that changes nothing about the wire must not refuse a peer,
--- and a wire change inside one engine version must. Two numbers because they
--- answer two questions; tying them together would make every patch release a
--- protocol break.
---
--- Bumped by hand, in the commit that changes the wire, and `wirecheck` has
--- nothing to say about it because no tool can tell an intentional break from an
--- accidental one.
Wire.ProtocolVersion = 1

Wire.Definition = State
Wire.validate = Schema.validate

--- Every replicated class, by name.
function Wire.classes(): { Schema.Class }
    return State.Classes or {}
end

--- Every class deliberately not replicated, by name and reason.
function Wire.excluded(): { Schema.Excluded }
    return State.Excluded or {}
end

function Wire.channels(): { Schema.Channel }
    return State.Channels or {}
end

function Wire.messages(): { Schema.Message }
    return State.Messages or {}
end

return Wire
