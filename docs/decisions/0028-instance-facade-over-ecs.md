# 0028 — Instance facade over a hand-rolled deterministic ECS

- Status: accepted
- Date: 2026-08-19

## Context
User decision #2: Roblox-style Instance tree as the public API, data-oriented
internals for open-world scale. EnTT was considered for the ECS.

## Decision
The scene is a **hand-rolled sparse-set ECS** (`luaug::scene::World`), not
EnTT: (a) guaranteed deterministic iteration (dense pools in insertion order,
compaction only at safe points); (b) trivially snapshottable POD SoA pools
(rollback foundations, ADR 0016); (c) chunk-scoped entity lifetime (bulk
unload); (d) no template-metaprogramming compile costs for an autonomous
implementer. Entities are `InstanceId{index, generation}` slotmap handles. The
**Instance facade** (properties, hierarchy, signals, attributes, tags) is
driven by **generated reflection** from the API definition IDL (single source
of truth: C++ descriptors, Luau defs, docs, thread-safety annotations, i18n
error keys). Property writes validate → write component → enqueue deferred
change events; bulk engine-side mutation uses quiet writes + subscription
bitmasks so 10k moving parts cost nothing when nobody listens (benchmarked
with a CI threshold from M2).

## Consequences
Roblox-familiar authoring over cache-friendly storage; the facade's overhead
is engineered against, measured, and gated — not hoped away.
