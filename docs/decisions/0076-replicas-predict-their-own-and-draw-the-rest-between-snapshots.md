# 0076 — Replicas predict their own character, draw the rest between snapshots, and are sent what is near

- Status: accepted
- Date: 2026-09-23
- Milestone: N1 follow-up (the gaps the N1 brief named)
- Decided by: the agent, under the owner's order of work of 2026-09-23 and the
  standing instruction of 2026-08-26 to take the repository's decisions on
  their behalf.
- Builds on: ADR 0069 (replication reads state and diffs it), ADR 0074 (Jolt
  cross-platform determinism)

## Context

N1 shipped a server-authoritative LAN session with three gaps named in its
own brief. Each is a thing a person playing notices:

- **No prediction.** A replica's own racer moved when the authority's snapshot
  said so, a round trip after the key.
- **No interpolation.** Every remote part stepped at the snapshot rate, thirty
  hertz at the default rate, because each snapshot was written the moment it
  arrived.
- **No interest management.** Every replica was sent the whole workspace, and
  the engine had no idea which part belonged to which player.

## Decision

1. **`Player.Character`** names the part that is a player in the world. The
   authority's game sets it. The roster message carries each player's
   character as a NetId (protocol version 4), and every replica resolves it to
   its own copy. It is the one piece of ownership the engine knows about, and
   the other three decisions are built on it.
2. **Prediction, as Source and Unreal do it for their own pawn.**
   - The replica's scripts move its own character at once, from its own input.
     In the example, the same `drive` function runs on the authority from the
     intent and on the replica from the key.
   - After each tick the replica records where it predicted its character to
     be, keyed by the intent tick it sent.
   - Each snapshot carries the last intent the authority applied for that
     peer. The replica compares the authority's transform with its own
     prediction at that intent.
   - Past a centimetre, it shifts both its current transform and every later
     prediction by the difference, rotation included.
   - The own character's replicated transform and motion state are never
     overwritten. A snapshot corrects them.
3. ~~**Correction by the error, not by re-simulation.**~~ **Amended
   2026-09-23 (the owner's mandate, S3): correction by re-simulation**, as
   Unreal's character movement does it with its saved moves.
   - The reason given against it was that replaying would need the physics
     state at the answered intent, which the backend does not keep. For a
     character controller it needs much less: where the character was, its
     vertical velocity and whether it stood on ground. The authority sends all
     three (`CFrame`, `VerticalVelocity`, `Grounded`).
   - Each tick's prediction now keeps the command the physics step consumed:
     direction, jump, walk and jump speed, and the step's length.
   - A correction puts the character where the authority said and steps it
     through the unanswered commands with the same function the simulation
     steps it with (`PhysicsSync::stepController`), in the world as it now
     is. A prediction that walked through a wall the authority's did not
     comes out at the wall.
   - The interface is `scene::ICharacterReplay`, implemented by `PhysicsSync`
     and handed to the replication module by `app`, the shape `AnimationHost`
     set. The rotation stays the scripts' and is corrected by the turn, as
     before; a controller does not turn.
   - Shifting by the error remains the fallback, where a tick of the history
     has no command.
   - **Two defects it found on the way:**
     - D178: a controller put somewhere new kept its old contacts, so its
       first step away from a wall was blocked.
     - D179: the own character was compared only when the authority's
       transform changed. An authority holding it still at a wall never
       corrected a prediction walking through it.
4. **Everyone else is drawn between two snapshots** (Valve's entity
   interpolation).
   - The replica keeps each remote part's last few snapshot transforms by
     server tick.
   - It estimates the server's clock and draws each part four ticks behind it,
     lerping between the two samples on either side.
   - Four ticks is two snapshot intervals at the default rate: the smallest
     delay that nearly always has a sample on each side of the moment drawn.
   - `Config::interpolationDelayTicks` sets it. Zero applies each snapshot as
     it arrives, which is what the pixel-for-pixel `replica_seam` gate uses.
5. **On a replica, only its own player's `CharacterBody` is simulated.**
   Before, every character integrated gravity and movement locally, and the
   snapshots fought it every tick. A character somebody else plays now follows
   its snapshots and is not written back.
6. **Interest is measured from each peer's character**, and it reuses the
   streaming radius and hysteresis, as the plan said it would.
   - A part is in interest inside `StreamingService.LoadRadius`, and leaves it
     only past a quarter more.
   - Everything under an in-range part comes with it.
   - The ancestors of anything sent come, so that it can be parented.
   - A container with no part anywhere in or around it always comes.
   - A peer whose player has no character is sent everything, which is what
     every session did before.
7. **The diff, the baseline and the checksum are per peer.** The authority
   remembers which ids each peer held at each of its last 64 sends. It diffs
   against the global state at the acked tick, cut to what that peer then
   held, and sends whole anything that is re-entering interest, since the
   replica scrubbed it when it left. The checksum is over the peer's subset,
   which is what ADR 0069 decision 8 already said a replica's hash is.

## And two things that were not on the wire (protocol version 5)

8. **Every name-shaped field goes through the snapshot's string table**, not
   `Name` alone. That is what a `Decal`'s image URN needed, so decals
   replicate.
9. **A service whose properties are world state is a wire class with
   `Service = true`.**
   - It travels under a fixed id, far above any instance's, and it is never
     spawned or despawned and is always in interest.
   - The replica writes the fields to its own service of that class, and never
     touches the service's name or parent.
   - `Lighting` is the first, carrying the clock, the latitude, the light, the
     fog and the exposure. Night on the authority is night on every replica.

## Not decided here

- ~~**Leaving interest still destroys the replica's copy**~~ -- built the same
  day (protocol 6). A despawn now carries two lists, destroyed and streamed
  out, since only the authority knows which is which. A streamed-out copy
  that a script holds becomes a husk, reparented to nil, and fires
  `InstanceStreamedOut`, as ADR 0069 decision 6 says. Building it found D160:
  the chunk streamer's half of the same contract had never been wired.
- **The vertical axis:** the radius is a sphere around the character, so this
  does not have the column problem the plan warned about. Chunked interest is
  still not built.
- **Rollback and lag compensation** for hits.
- **What an interest radius costs per peer** in memory: one id list per send.
  There is no knob.

## Evidence

`engine/replication/tests/session_tests.cpp`:

- **Ownership:** each machine's `Player.Character` is its own copy of the part
  the authority named.
- **Interest:** a part far from the character is not sent, and one walking in
  arrives. One just past the radius stays until the hysteresis lets it go, and
  one coming back is re-sent whole. A straddling model and a far part attached
  to a near one behave as described above. There are no checksum failures.
- **Prediction:** over a four-tick round trip, the replica's own racer leads
  the authority's and needs no correction. The authority's teleport is
  corrected on the replica.
- **Interpolation:** a part moving a metre a tick, snapshotted every other
  tick, moves exactly a metre a tick on the replica, a few ticks behind.

The determinism traces moved at tick 0, and only because `Player` gained a
property the world hash reads.
