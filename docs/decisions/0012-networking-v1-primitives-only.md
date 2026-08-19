# 0012 — v1 networking = low-level primitives only (GNS + ENet behind ITransport)

- Status: accepted
- Date: 2026-08-19

## Context
User decision #6: v1 is single-player, but games must be able to open a
socket/web server and connect to one (developers build their own backends; an
official multiplayer solution comes later). Roblox's 2026 server-authority +
rollback release shows where replication eventually goes; retrofitting is the
failure mode to avoid.

## Decision
v1 ships **low-level primitives only**, exposed through the Lute-compatible
`@std/net` surface (HTTP client always; HTTP/WS server and raw sockets in dev
mode, and behind `[permissions] net_serve` in shipped builds). Native
transports: **GameNetworkingSockets** (encryption, congestion control,
console/mobile record) with **ENet** as the LAN/fallback option, both behind
the `ITransport` interface — the seam future replication will use (QUIC/msquic
can slot in later). There is **no NetworkService, no Remotes, no replication
in v1**; those names are reserved. The blessed v1 pattern for client/server
projects is a sibling backend process running on Lute sharing `@shared` code.

## Consequences
Honest scope; portable backend code (runs verbatim under Lute); the simulation
core (ADR 0016) plus `ITransport` keep the path to official multiplayer open
without v1 paying for it.
