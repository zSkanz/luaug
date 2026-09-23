# 0080 — `ReplicatedStorage` and `ServerStorage` hold what is not the world

- Status: accepted
- Date: 2026-09-23
- Milestone: N2 (phase 4), the second thing ADR 0077 left for later by name
- Decided by: the agent, under the owner's standing instruction of 2026-08-26
  and their word the same day to build what the engine still lacked.
- Builds on: ADR 0047 (the scene is the authored world), ADR 0069
  (replication), ADR 0076 (interest), ADR 0077 (remote events)

## Context

Everything a game authored lived in `Workspace`, and everything in
`Workspace` is the world: it is drawn, it collides, it is simulated, and a
replica is sent it by distance. A game also keeps things it does not show:

- a template it clones when an enemy spawns;
- a `RemoteEvent` that has no place in space;
- data only the server should hold.

With `Workspace` the only container, a template had to be hidden somewhere in
the world, where it still simulated, or be built by code. A remote had to sit
in a folder that interest management had to reason about.

The engine this API is familiar from answers with two services: one whose
contents replicate to every client, one that stays on the server. Its other
containers are for scripts, and here scripts are files mounted from `src/`
(ADR 0047), so they have nothing to answer.

## Decision

1. **Two services, created at boot like every other**: `ReplicatedStorage`
   and `ServerStorage`. Both are named as that platform names them. The
   service-name rule gains them as exceptions, beside `Workspace` and
   `Lighting`.
2. **Neither is the world.** Nothing under them is drawn, collides or moves,
   because only `Workspace`'s descendants ever were. A template comes into the
   world by being cloned into `Workspace`.
3. **Both are saved with the scene**, under an optional top-level `storage`
   key holding one node per service, in the scene format's own node shape.
   - The key is written only when a storage holds something, so every existing
     scene file is byte-for-byte what it was.
   - A reference names its root by its first path segment, so a part in the
     world can name a template in storage and the other way round.
   - A new scene empties them, as it empties `Workspace`.
   - The partitioner copies unknown top-level keys through, so streaming keeps
     them.
4. **`ReplicatedStorage`'s contents reach every replica, whatever their
   distance.**
   - The wire schema marks it `Contents`: a service whose children replicate.
   - Its children are captured beside `Workspace`'s, with the service's fixed
     network id as their parent.
   - They are pinned into every peer's interest.
   - A replica resolves services before instances in each state, so a child
     finds its parent in the same snapshot.
   - Protocol 9.
5. **`ServerStorage` is the authority's alone.** It is not in the wire schema,
   and a replica empties it on joining. The replica has the project's files
   anyway, so this is about the contract rather than secrecy: what a client
   script finds there is nothing.
6. **The scene's types include them.** `scene.d.luau` declares
   `game: DataModel & { ReplicatedStorage: ReplicatedStorage & {...} }` when a
   storage holds something, so `game.ReplicatedStorage.Sword` is typed.
7. **The Explorer takes a drop onto them.** A service may not be moved or
   deleted. The drop rule asked only that, so a drag onto `Workspace` itself
   was refused, and so was a drag onto a storage. A service that holds
   authored content is now a valid parent.

## Consequences

- Every world has two more instances from boot, which moved the determinism
  traces of `churn` and `example01` at tick 0, on both platforms. That is the
  named reason for re-recording them. Nothing after tick 0 changed its shape.
- A template no longer simulates while it waits.
- There is no `ServerScriptService`, `StarterPlayer` or `StarterGui`: scripts
  are files, and one script runs on every machine.

## Evidence

- `tests/conformance/world/storage.spec.luau`:
  - both exist under the data model;
  - a part kept there does not fall, and its clone in `Workspace` does;
  - a `RemoteEvent` kept there works.
- `engine/replication/tests/session_tests.cpp`:
  - a template 5 km from the player's character reaches the replica, parented
    to the replica's own `ReplicatedStorage`;
  - it stays while the character moves away, and goes when the authority
    destroys it;
  - joining clears the world's copy, the replicated storage's copy and all of
    `ServerStorage`.
- `engine/app/tests/editor_tests.cpp`:
  - a scene with empty storages is unchanged;
  - their contents and a reference into them round-trip;
  - a new scene empties them;
  - a drag onto `Workspace` or a storage moves the part, and the services stay
    put.
- `engine/app/tests/scene_definitions_tests.cpp`: storage contents are declared
  on `game`, and only when there are any.
