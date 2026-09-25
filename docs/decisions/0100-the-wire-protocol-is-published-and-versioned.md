# 0100 — The wire protocol is published and versioned

- Status: accepted
- Date: 2026-09-25
- Decided by: the agent, under the owner's mandate of 2026-09-24 ("do all of
  it", the depth the phases deferred) and their word of 2026-09-25 to build it
- Amends: ADR 0069, which declined "a public protocol commitment". The format
  it designed is unchanged; what changes is that it is written down and
  promised.

## Context

The wire has had a schema since ADR 0069, and every decision in it is argued in
`api/wire/state.wire.luau`. What it never had is a description somebody outside
the engine could build a peer from: the message layouts lived only in
`session.cpp`, and the only statement about compatibility was a version number
bumped by hand. A dedicated server written in another language, a bot, a
recorder or a proxy all need the bytes; a game shipping updates needs to know
which builds can talk to each other.

## How others do it

- **Minecraft (Java)**: a protocol number per release, exact match required,
  and the format documented by the community rather than the vendor.
- **Valve's Source engine**: a network protocol version per build; mismatched
  clients are refused.
- **Quake 3 / id Tech**: a protocol number and a published source; exact match.
- **Enet-based and bespoke engines** (Godot's high-level multiplayer, Unity's
  Netcode): an internal format with no third-party promise.

Exact matching on one number is the common rule where a format is promised at
all; negotiation appears only in protocols meant for mixed fleets, which a game
server and its clients are not.

## Decision

1. **The specification is generated from the schema.** `gen_wire.luau` writes
   `docs/protocol/wire.md` beside the C++ table: transport and channels,
   primitives, every encoding with its size and layout, identity rules, the
   handshake, every message's layout part by part, wire ids, the checksum, the
   argument encoding, every replicated class's fields, and every excluded class
   with its reason. The gate regenerates both and fails on any difference.
2. **Message layouts are schema data.** Each message carries a `Layout` of
   named, typed parts; `wirecheck` refuses a message without one, a type the
   specification does not define, or a repeat whose count is not written
   before it. The encodings are a table in the schema with their sizes, and a
   test holds the engine's `wireBytes` to those sizes.
3. **Compatibility is one number, matched exactly.** `ProtocolVersion` is sent
   first in `Hello`; any other is a disconnect. There is no negotiation.
4. **The number changes whenever the bytes do** -- a field, a message, a
   layout, an encoding -- and only then. An engine release that leaves the wire
   alone keeps it; a release that moves it says so in the CHANGELOG.
5. **Numbers are permanent.** Field ids, message ids, channel numbers and
   encoding values are never reused, in any version; a removed field is
   retired and stays claimed, as ADR 0069 already required of fields.
6. **The specification of a released version is the page at that release's
   tag.** Nothing else is promised: no support window for old versions, and no
   stability of what is not on the page (timings, interest radius, resend
   policy).
7. **The wire is little-endian by assertion**, not by coincidence: the field
   encoder copies host bytes, and a big-endian target now fails to compile
   rather than speaking another protocol.

## Consequences

- A peer in another language can be written from one page, and that page
  cannot drift from the engine: a schema change that is not regenerated fails
  the gate, and so does a hand edit.
- The gate now also checks `wire_schema.gen.h` for freshness, which it never
  did.
- Layouts are stated twice -- in the schema and in `session.cpp` -- and nothing
  but review and the session tests keeps them together. Generating the reader
  and writer from the layouts would close that, and is not done here: it
  touches every message path for no change in behaviour.

## Alternatives considered

- **A hand-written specification.** Rejected: it is the document that is right
  on the day it is written.
- **Version negotiation, or a minimum-compatible version.** Rejected for now:
  a game's server and clients ship together, and a negotiated protocol is two
  code paths through every message to test.
