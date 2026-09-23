# 0085 — A player is the same player after a reconnect

- Status: accepted
- Date: 2026-09-23
- Decided by: the agent, under the owner's mandate of 2026-09-23 ("multiplayer:
  fix what you listed, make it perfect"), which named `Player` identity across
  a reconnect among the gaps.
- Builds on: ADR 0069 (replication reads state and diffs it), ADR 0070 (one
  binary, four postures), ADR 0076 (prediction, interpolation, interest)
- Protocol: version 10

## Context

A peer is one connection. The transport numbers each connection afresh and
never reuses a number, and the authority gave each welcomed peer the next
`UserId`. So a player whose link dropped and who came back was a new player:
a new `UserId`, a new `Player`, and whatever a game kept under the old number
was somebody else's.

A game keys everything a player owns on that number: the score, the
inventory, the seat at the table, the saved data. Losing it on a Wi-Fi blip is
the defect a player notices first. The replica also did nothing when its link
went: it sat disconnected until somebody restarted it.

Every mature multiplayer engine separates the connection from the player:
- One keeps a departed player's state inactive for a while, and hands it back
  when the same unique id connects again.
- Others key the player on an account the platform vouches for.

This engine has no accounts. The identity has to come from the session itself.

## Decision

1. **A player token is who a player is.**
   - The authority makes one at a player's first welcome: 128 bits from the
     operating system's entropy.
   - The `Welcome` message carries it, and the replica presents it in every
     later `Hello`.
   - The authority maps each token to the `UserId` it first gave, for as long
     as it runs. A token it knows is welcomed as that `UserId` again.
   - A token it does not know, or none, is a new player with a new token. That
     covers a first join, and a token from an authority that has since
     restarted.
2. **The `Player` instance is new; the identity is not.**
   - The old one left with the connection. `PlayerRemoving` fired, and a game
     saved what it wanted to save.
   - The new one fires `PlayerAdded` under the same `UserId` and name.
   - This is what a script already handles for any player joining. There is
     nothing new to learn.
3. **A second connection for a player already present replaces the first.**
   A link that died without saying so stays open on the authority until it
   times out. The player on a new link is the real one, so the old link is
   dropped and its `Player` removed. There are never two copies of one
   `UserId`.
4. **A replica whose connection went dials again, by itself.**
   - It waits one second, then two, four, and so on to thirty-two.
   - The wait is counted in ticks, not milliseconds (R10), so it is a
     function of the simulation.
   - Until the authority answers, the replica's world stands still rather than
     vanishing.
5. **A rejoin starts the replicated world again.**
   - The new connection's baseline on the authority is empty, so everything is
     sent afresh.
   - What the replica held from the old connection leaves exactly as a chunk
     streaming out does. It becomes a husk if a script still holds it
     (`InstanceStreamedOut`), and is destroyed otherwise. Nothing stale
     survives that no snapshot would ever update.
   - Reusing the old copies would need a barrier the protocol does not have:
     "every spawn for this connection has arrived". Their states travel on
     different channels, whose relative order is not guaranteed.
6. **The token is a bearer claim, and it is exactly as strong as the link.** An
   ENet session is a LAN or a trusted link (ADR 0012): nothing on it is
   encrypted or authenticated. A token makes a `UserId` unguessable by another
   player; it does not make it proof of anything to an attacker who can read
   the traffic. That waits for the transport that ADR 0012 names for the open
   internet.

## Consequences

- **Protocol version 10.** A version-9 peer is refused by the version check,
  as every protocol change is.
- The authority keeps one small entry per player it has ever welcomed, for
  its lifetime: a token and a number. It is not per connection, so reconnects
  do not grow it.
- `ReplicaSession::setPlayerToken` lets an identity outlive the process that
  received it. A game that wants "the same player tomorrow" can save the token
  and present it. Nothing does so by default, because a token on disk is a
  credential on disk.
- Tests over the memory transport cover all of it:
  - a player who drops and comes back gets the same `UserId`, and the world
    once;
  - a stale connection is let go;
  - a replica redials by itself and is taken back by a restarted server.
