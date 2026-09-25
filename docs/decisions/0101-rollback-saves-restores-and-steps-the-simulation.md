# 0101 — Rollback saves, restores and steps the simulation; the game keeps its own state

- Status: accepted
- Date: 2026-09-25
- Decided by: the agent, under the owner's mandate of 2026-09-24 ("do all of
  it", the depth the phases deferred) and their word of 2026-09-25 to build it
- Implements: ADR 0016's physics `saveState`/`restoreState` seam, declared at
  v1 and left refusing. Builds on ADR 0083 (level-C determinism) and ADR 0076
  (the character replay).

## Context

The mandate asks for rollback: the world rewound to a tick and re-simulated
with corrected input. ADR 0016 laid the foundations and said plainly what they
were not -- full rollback would also have to restore the Luau VM, its
coroutines and the task scheduler, and nothing in the engine can do that. A
running VM's heap has no snapshot, and pretending otherwise would be a rollback
that silently keeps half the game's state from the future.

## How others do it

- **GGPO** (and every fighting game built on it): the library owns the input
  exchange and the decision to roll back; **the game** provides `save_game_state`,
  `load_game_state` and `advance_frame` callbacks, and owns what its state is.
- **Photon Quantum**: all game state lives in a deterministic ECS the engine can
  copy; systems are pure functions over it.
- **Unity Netcode for Entities / Unreal's Network Prediction plugin**: the
  engine snapshots a declared set of simulated components and re-runs the
  simulation systems over it; code outside that set is not rolled back.

The common shape: the engine can copy and re-step its simulation exactly, and
what is outside the engine's copy is the game's to save beside it.

## Decision

1. **The solver's whole state is saved and restored** (the Jolt backend now
   implements the seam): every body's transform, velocity and sleep, the
   contact cache and constraint warm starts, each character controller's own
   state, and the contact pairs the next step's touch diff compares against.
   A restore goes into the same bodies or not at all: the blob opens with which
   body and character slots are alive at which generation, and the origin.
2. **The mirror adds the instances' side**: for every instance with a body or a
   character, its `CFrame`, velocities, pending impulse and sleep, and a
   character's ground, state, command and vertical velocity. Refused -- false,
   nothing changed -- when the set of simulated instances differs.
3. **Scripts get three calls on `RunService`**: `SaveSimulation()` returns a
   `buffer`; `RestoreSimulation(buffer)` answers whether it was put back;
   `StepSimulation()` steps the 3D simulation one fixed tick now, without
   running a script and without firing `Touched` or `TouchEnded`, since a tick
   stepped again already fired them.
4. **The game keeps its own state.** What a rollback game's scripts hold --
   scores, cooldowns, input history -- is saved and restored by them beside the
   buffer, as a GGPO game does. The engine does not snapshot the VM.
5. **Not included**: which instances exist, any property other than those
   above, the 2D layer (Box2D has no state snapshot), audio, animation and UI.
   A game that creates or destroys simulated parts inside its rollback window
   rolls back to before the change by rebuilding, or does not roll back
   across it.

## Consequences

- A fighting game, a racing game with lockstep inputs, or an authority that
  re-simulates a late input can be written in Luau today, and the re-simulation
  is bit-exact: a test saves, steps forty ticks, restores, steps the same
  forty with the same inputs, and compares every body bit for bit, with a
  walking character among them.
- A saved state is a few kilobytes for a small scene and grows with bodies and
  contacts; keeping one per tick for a short window is the intended use, not
  keeping minutes of them.
- The replica's own-character prediction (ADR 0076) is unchanged. It could now
  roll back the whole local simulation instead of replaying one character; that
  is a separate decision with its own tests.

## Alternatives considered

- **Snapshot everything, VM included.** Not possible with Luau's VM, and a
  partial VM snapshot is worse than none.
- **Restore the whole `World` snapshot the editor uses.** Rejected: it replaces
  instances under a VM that still holds them, and it restores properties the
  game changed on purpose -- rollback is about motion, not about undo.
- **A rollback service that owns the input exchange** (GGPO's half). Deferred:
  games differ in what their input is, and the three calls are what every such
  library is built from.
