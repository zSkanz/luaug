# 0077 — Game messages cross the wire through a `RemoteEvent`

- Status: accepted
- Date: 2026-09-23
- Milestone: N2 (phase 4's second milestone, the first thing N1 left out by
  name)
- Decided by: the agent, under the owner's standing instruction of 2026-08-26
  to take the repository's decisions on their behalf.
- Builds on: ADR 0069 (replication reads state and diffs it), ADR 0070 (one
  binary, four postures), ADR 0076 (prediction, interpolation, interest)

## Context

N1 carries two things between machines: the world's state, from the authority,
and each player's input actions, as intent. That is enough to drive a racer and
nothing more. A game also needs to SAY things. A client needs to tell the
server "I bought the sword", "I sent this chat line" or "I pressed ready". The
server needs to tell one client "you won" and every client "the round starts".
None of these is a property of an instance, and none is an input action held
down.

Every engine with networking has this seam:

- Unreal has RPCs on actors: Server, Client and NetMulticast.
- Unity's Netcode has `[ServerRpc]` and `[ClientRpc]`.
- Godot has `@rpc`.
- The platform this engine's API is familiar from has a remote event object.

The wire schema reserved channel 3 for it in N1, "so the numbering cannot shift
when the first of `RemoteEvent`, `NetworkOwnership` or `Team` arrives".

## Decision

1. **A `RemoteEvent` is an instance, and it replicates.** Created on the
   authority under `Workspace`, it reaches every replica the way a `Folder`
   does: it has no position, so interest always sends it. A replica finds it by
   name, with `WaitForChild`. Its identity on the wire is its network id, the
   one both ends already agree on.
2. **Five members, familiar by design:**
   - `FireServer(...)` sends from a client to the authority.
   - `FireClient(player, ...)` sends from the authority to one player.
   - `FireAllClients(...)` sends from the authority to every player.
   - `ServerReceived(player, ...)` fires on the authority, naming who sent it.
   - `ClientReceived(...)` fires on a client.

   The rule ADR 0070 states is kept: **the client says what it did.** The
   authority learns the sender from the connection, never from the payload, so
   a client cannot speak for somebody else.
3. **One script runs solo, hosting and networked.** On an authority,
   `FireServer` is delivered to its own `ServerReceived`, from its local player.
   A host's `FireClient` to its own player and its `FireAllClients` reach its
   own `ClientReceived` too. A dedicated server has no local player, so there
   nothing is delivered locally. A replica's `FireClient` and `FireAllClients`
   are refused by name: only the authority speaks to clients.
4. **What travels is values, not code or references to memory.**
   - Allowed: nil, booleans, numbers, strings, vectors, instances, and tables
     of those, nested eight deep.
   - An instance travels as its network id and arrives as the receiver's own
     copy, or nil where the receiver does not have it: out of interest, or never
     replicated.
   - Anything else is refused at the call, by name: a function, a thread, a
     userdata that is not an instance, a cycle.
   - The script module encodes the values, because only it can read Luau
     values. The instance references ride beside the bytes, in a list the
     replication engine translates between local ids and network ids, so the
     session never parses a payload.
5. **Reliable, and on the Control channel, after the spawns of the same send.**
   A message is an action, and a lost "I bought the sword" is a bug. The channel
   is the one the spawns take, not the reserved channel 3. Reliable delivery is
   ordered within a channel and not across channels. A game that creates a
   `RemoteEvent` and fires it in the same tick would otherwise race its own
   spawn: the message would arrive naming an instance the replica had not been
   told about, and would be dropped. Channel 3 stays reserved. Protocol
   version 7 adds two messages, one for each direction.
6. **Delivered as deferred signals at the start of the tick** that follows its
   arrival, beside the input events: arrival is a network event and happens at
   a wall-clock moment, and the scheduler is where it becomes a tick (R8, R10).
7. **Bounded, because a client is not trusted.**
   - A message larger than 64 KiB is refused at the call.
   - An authority takes at most 256 messages a tick from one peer, drops the
     rest and counts them.
   - A message naming a network id that is not a `RemoteEvent` is dropped:
     a client cannot fire an event into anything else.

## Not decided here

- **Remote functions**, a call that waits for an answer. It is a request id and
  a reply over the same channel, and it waits for a game that needs it.
- **Unreliable messages**, for effects whose loss is harmless. Intent already
  covers the common case.
- **A replicated container that is not `Workspace`.** `RemoteEvent`s live under
  it for now, in a `Folder` if a game likes. A storage service that replicates
  and is not part of the world is a separate decision.

## Evidence

- `tests/conformance/world/remote.spec.luau`, played solo, where this machine is
  its own server and its own client:
  - every allowed value round-trips through the codec: strings, numbers,
    vectors, booleans, nested tables, and an instance that arrives as itself;
  - `FireServer` fires `ServerReceived` from the local player;
  - `FireClient` and `FireAllClients` reach `ClientReceived`;
  - each refused value is refused at the call: a function, a table that
    contains itself, a table key, 70,000 bytes, and a player that is not one.
- `engine/replication/tests/session_tests.cpp`:
  - a replica's message reaches the authority's inbox from that replica's own
    player, with an instance reference translated;
  - the authority's `FireClient` reaches that replica and no other;
  - a message naming a part is dropped;
  - a flood is cut at 256 a tick;
  - an event created and fired in the same tick arrives after its own spawn.
