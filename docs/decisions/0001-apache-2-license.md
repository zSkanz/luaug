# 0001 — Apache-2.0, single license

- Status: accepted
- Date: 2026-08-19

## Context
LuauG is open source by user decision. An engine touches patent-adjacent
territory (rendering, streaming, codecs, physics), and studio/console adoption
raises due-diligence questions MIT does not answer. Relicensing later requires
every contributor's consent, so the choice had to be made before the first
commit. Dual MIT+Apache (Rust-style) was considered.

## Decision
License the project under **Apache-2.0, as a single license** (explicit patent
grant + defensive termination; precedent: O3DE). Ship `LICENSE`, `NOTICE`, and
a generated `THIRD_PARTY_NOTICES.md`. Contributions are accepted under DCO
sign-off (no CLA at this stage). Confirmed by the user (planning decision #15).

## Consequences
Every contributor grants an explicit patent license. Apache's GPLv2
incompatibility is irrelevant (the whole stack is permissive). All vendored and
future dependencies must remain permissive (MIT/BSD/zlib/Apache-2.0/PD); adding
anything else is an escalation item.
