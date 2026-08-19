# 0026 — Child-name index supports duplicate sibling names

- Status: accepted
- Date: 2026-08-19

## Context
Roblox-like trees routinely contain siblings sharing a Name (three "Tree"
parts under one Folder). The originally sketched
`FlatMap<NameAtom, InstanceId>` per parent would clobber duplicates and break
`FindFirstChild`. Caught by external review — a real design bug.

## Decision
Per-parent index: **`FlatMap<NameAtom → first child with that name>` plus an
intrusive `nextSameName` chain on each instance.** `FindFirstChild` stays O(1)
and returns the first match in child (document) order — Roblox semantics;
rename/reparent is an O(chain) unlink/relink; overhead ≈ 8 bytes per instance;
child order remains deterministic (sibling insertion order).

## Consequences
Correct duplicate-name behavior with negligible cost. Conformance specs must
cover duplicate-name lookup, rename, and reparent ordering (M2 gate).
