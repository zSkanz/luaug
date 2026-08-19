# 0009 — miniaudio; the audio module itself is the swappable seam

- Status: accepted
- Date: 2026-08-19

## Context
Audio needs all-platform output (WASAPI, CoreAudio incl. iOS, AAudio/OpenSL,
ALSA/Pulse), decoding, spatialization — permissively licensed. miniaudio
(0.11.25, MIT-0/Unlicense, single file) covers all of it. OpenAL Soft is LGPL
(conflicts with static-link-only platforms). FMOD/Wwise are commercial
middleware that pro teams expect as options later. An external review proposed
a formal `IAudioBackend` virtual interface now.

## Decision
**miniaudio** is the v1 implementation. **The `audio` module boundary itself is
the swappable seam** — selected at build time like every other backend
(ADR 0023). No `IAudioBackend` virtual layer in v1: there is exactly one
implementation, and FMOD/Wwise are middlewares with their own graphs — the
correct future boundary is an alternative implementation of the module behind
the same engine-facing calls, not a "mixer backend" abstraction. Invariant: the
public Luau API (Sound, AudioGroup, AudioService) never leaks miniaudio (or any
backend) concepts.

## Consequences
Zero speculative indirection on the audio hot path. FMOD/Wwise arrive post-v1
as module alternatives with their own ADR; if a genuine common interface
emerges then, it is extracted from two real implementations rather than
guessed from one.
