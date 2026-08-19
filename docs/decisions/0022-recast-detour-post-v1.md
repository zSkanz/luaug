# 0022 — Recast/Detour integration deferred post-v1 (seam only)

- Status: accepted
- Date: 2026-08-19

## Context
Recast & Detour (zlib, org fork) is the uncontested navigation choice. But the
v1 flagship demo (streamed open world + third-person character) does not need
navmesh, and integrating it (async tile builds per streamed region, agent
queries) is real scope.

## Decision
**Vendor Recast/Detour and define the `nav` module seam** (Recast builds on
the jobs pool, Detour queries on the sim thread), but ship **no navmesh
integration and no NavigationService in v1**. Navigation is the first post-v1
item, together with the 2D layer. Confirmed by the user (planning decision
#16).

## Consequences
A leaner, honest v1; the seam prevents the deferral from becoming a redesign.
The agent may not "helpfully" integrate it early (scope rule R15).
