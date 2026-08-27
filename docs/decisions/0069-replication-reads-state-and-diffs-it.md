# 0069 — Replication reads state and diffs it, against a declared wire schema

- Status: accepted
- Date: 2026-08-27
- Milestone: N1 (post-v1 phase 4), part A
- Decided by: the agent, under the owner's standing instruction of 2026-08-26 to
  take the repository's decisions on their behalf. The **milestone** was the
  owner's own: *"terrain editor multiplayer voxels etc."*, 2026-08-27.
- Depends on: [ADR 0070](0070-a-port-is-opened-by-a-posture-and-never-by-a-script.md)

## Context

The engine is deterministic on a fixed tick, has a world hash, has an
`ITransport` seam with an ENet implementation, and has two `WorldHost`s able to
run side by side in one process. What it has never had is a reason for a second
world to agree with the first.

**The obvious delta source is `scene::ChangeQueue`, and it cannot work.** This
is the one paragraph worth putting in an ADR rather than a comment, because it
is the design somebody proposes every six months and the refutation is two
citations long:

- `PhysicsSync` writes transforms **straight into the component**, under a
  comment that names itself *the QUIET write*
  (`engine/scene/src/physics_sync.cpp:963-966`). It does not go through
  `setProperty` and it does not enqueue.
- `World::setProperty` enqueues **only when something is subscribed**
  (`engine/scene/src/world.cpp:626-634`).

So the most-replicated fact in any game — where things are — never reaches the
queue at all, and the second most-replicated facts reach it only by accident of
who happens to be listening. A replication layer built on the change queue would
work in a test scene and lose every moving object in a real one.

The second thing worth deciding before any code: **what a wire format is allowed
to be.** A hand-rolled serialiser drifts from the state it serialises, silently,
in the direction that matters — a field added to the world and forgotten on the
wire is a replica that is confidently wrong about the state deciding the next
tick.

## Decision

**1. Replication reads state and diffs it. It does not listen.**

Each send, the authority walks the instances in a peer's interest set, extracts
the fields the wire schema declares, and compares them with the last baseline
acknowledged by that peer. What differs is what is sent. This costs a walk the
change queue would have avoided, and it is the only design that cannot silently
miss a write — which is the property that matters, because the failure mode of
missing one is a replica that looks right and is not.

**2. The wire schema is declared, generated and checked — like the API is.**

`api/wire/*.wire.luau` declares every replicated field beside the class it
belongs to; `api/generator/gen_wire.luau` generates the C++; and
`tools/repo/wirecheck.luau` refuses a tree where the generated form and the
declaration disagree. This is the shape `api/defs/` already has, for the same
reason it has it: two hand-maintained lists of the same thing is one list plus a
bug waiting for someone to edit only one of them.

**Field ids are permanent.** A removed id goes to a `Retired` list with the
version it died in, and reuse is refused by the checker. An id that means one
thing to a server and another to a replica is not a bug anybody can debug from
the symptom.

**3. The correspondence gate, which is the part that is easy to skip.**

The wire schema's non-property entries and `world_hash.cpp`'s hand-written block
are two hand-maintained lists of the same simulation state, and a field in one
and not the other diverges a replica *in exactly the state that decides the next
tick*. A test compares the two lists by name and fails on a difference in either
direction. It is a list-versus-list assertion, which is cheap and is the only
thing that catches the class.

**4. The four postures are `NetworkService`'s to report and never to set**, per
ADR 0070. `Authority`, `Topology` and `ServerTick` are read-only and `HostFact`,
so they are out of the world hash — a fact about the machine, not the world.
Their defaults **are** the solo truth: authority true, topology solo, one
player. A script writes `if NetworkService.Authority then` and that branch is
present *and taken* when nothing is networked, which makes it a gameplay branch
("do I decide this") rather than a configuration branch ("am I networked").

**5. A replicated instance in a replica world is `driven`.**

Two lines at `physics_sync.cpp:212`. Its body becomes Kinematic, and `writeBack`
skips kinematic bodies (`:948-958`), so the solver neither fights the incoming
deltas nor overwrites them — with no branch in game script and no second
transform authority. This is the same mechanism a welded part already uses;
nothing new is invented for it.

**6. Losing interest is `InstanceStreamedOut`, verbatim.**

An instance a replica stops caring about is reparented to nil and reported
through the streaming husk contract (`streaming_glue.h:9-14,49-57`). A replica
losing interest and a chunk being evicted are the same event from a script's
point of view, and the API already has the word for it. Inventing a second one
would mean every game handles two spellings of one thing.

**7. `Terrain` is not replicated, and that is a decision rather than an
omission.** Its bulk is not a property, a field is megabytes, and a sculpt is
rare and authored. A world's terrain arrives with the world — from the scene, or
later from a streamed cell — and the wire schema records the exclusion by name so
that the next person to look does not have to work out whether it was considered.

**8. The replica's hash is not the server's and must not be compared to it.**
Interest management gives the replica a strict subset of the world, so the two
hashes differ by construction. The per-baseline checksum catches *apply* bugs,
which is what it can honestly catch. It cannot detect simulation divergence, and
a design that claimed otherwise would be claiming a guarantee it does not have.

## Consequences

**The acceptance test already exists and only has to be inverted.**
`runTwoWorldsGate` runs two `WorldHost`s and two Luau VMs in one process with a
pixel differential proving the two worlds render *different* images
(`engine/app/include/luaug/app/two_worlds.h:66`, whose own header names
networking as the second caller). Driving world B as world A's replica over a
loopback transport makes the acceptance test that they render the **same** one.
Headless, deterministic, no network involved, and the harness is built.

**Every determinism trace moves at tick zero, once.** `NetworkService` is
`Service`-tagged, so `registerServices` instantiates it at boot
(`services.cpp:1097-1133`), plus one `Player` in solo. `HostFact` on the three
properties is what stops it recurring on every later change to them.

**Two defects in code we already own have to be fixed before the design can
trust its own transport**, and both were found by reading rather than by
failing:

- `ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT` is never set in `flagsFor`
  (`engine/net/src/enet_transport.cpp:40-54`), so any payload over roughly one
  MTU sent as `UnreliableSequenced` takes the reliable-fragmented branch
  (`third_party/enet/peer.c:135-145`). That is head-of-line blocking, which is
  precisely what the mode exists to avoid, and no test catches it because the
  loopback case sends four bytes. It needs a 4 KB test.
- `ITransport` has never had a production caller, so N1 owes a deterministic
  loss-and-reorder decorator over its six virtuals, seeded from `core::Pcg32`
  and never from a clock. The transport tests say so themselves
  (`transport_tests.cpp:6-10`): a bug that only appears under loss or reordering
  is invisible today.

## Deliberately not in this decision

Rollback and prediction reconciliation (ADR 0025 records determinism level B
rather than enforcing it, and `CROSS_PLATFORM_DETERMINISTIC` is OFF), a public
protocol commitment, `RemoteEvent`, `NetworkOwnership` and `Team`. Channel 3 is
reserved and named in the wire schema so that the numbering cannot shift when
one of them arrives, and nothing more is promised.

## Alternatives considered

**Diff the change queue.** Refuted above, with citations, because it is the
proposal that keeps coming back.

**Replicate the world hash and resimulate.** Rejected: it requires
cross-platform bit-identical simulation, which ADR 0025 explicitly does not buy,
and a replica that must resimulate cannot have a subset of the world — which
kills interest management, which is the only thing that makes a large world
affordable.

**A hand-written serialiser per class.** Rejected for the reason `api/defs/`
exists: it is a second list of the same facts, and the two drift in the
direction nobody notices.
