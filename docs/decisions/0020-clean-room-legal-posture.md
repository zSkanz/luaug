# 0020 — Clean-room legal posture toward Roblox

- Status: accepted
- Date: 2026-08-19

## Context
LuauG's audience is Roblox developers and its API is deliberately familiar.
Prior art (LunarEngine/Librebox) publicly adopted a "public API only" stance.
API concepts/idioms are not code; Luau itself is MIT open source.

## Decision
LuauG replicates **public API concepts and idioms only**. Bright lines: never
copy Roblox source code, assets, icons, or branding; never consume decompiled
or leaked material; conformance tests are written from LuauG's own
`docs/api-design.md`, never from Roblox behavior probing beyond public docs;
deliberate renames (`Signal` not RBXScriptSignal, `TagService`, `CharacterBody`,
`AudioService`) stand as evidence of independent design; the README carries a
non-affiliation disclaimer; "Roblox" appears only nominatively. Any doubt is a
stop-and-escalate item (`MASTER_PROMPT.md` §10). A legal review of the full
api-dump identifier list happens before public launch (M8).

## Consequences
The familiarity goal survives on safe ground; contributors and the autonomous
agent have unambiguous rules (R7).
