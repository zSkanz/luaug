# 0033 — A hand-written JSON reader in `core`, not a dependency

- Status: accepted
- Date: 2026-08-19
- Relates to: [ADR 0019](0019-i18n-day-one.md) (i18n catalogs),
  [ADR 0006](0006-hlsl-via-sdl-shadercross.md) (the shader pipeline)

## Context

`engine/core/src/i18n.cpp` carries a deliberately restricted JSON reader — an
object of string → (string | object of string), nothing else — and a note left
when it was written:

> When the engine needs general JSON it will be a deliberate, separate decision
> (and a dependency ADR), not an accident of this file growing.

That moment arrived with the shader pipeline. The build emits a shader manifest
and, per stage, a reflection sidecar produced by SDL_shadercross carrying the
sampler and uniform-buffer counts that `SDL_CreateGPUShader` demands and the
input locations pipeline creation demands. The manifest is ours to shape; the
reflection files are shadercross's, and they are nested JSON with arrays.

Three options were on the table.

**Avoid JSON at runtime.** Generate the shader table into a C++ header at
configure time, or invent a line-oriented format. It removes the parser, but it
does not remove the reflection files — those are not ours to reformat — and
hardcoding counts that only the shader source knows is the exact thing the
sidecars exist to prevent. It also leaves a generated manifest that nothing
reads, which is its own kind of lie.

**Vendor a JSON library.** A dependency, therefore R5/R6 and a human
escalation. It would also be the first dependency added for the engine's own
convenience rather than for a capability we cannot build.

**Generalize what already exists.**

## Decision

Grow the i18n reader into a general JSON reader in `core`, and reimplement the
catalog loader on top of it. **No dependency.**

The deciding argument is that this is *less* code than the status quo, not
more: the repository already maintains a JSON scanner with string escapes,
surrogate pairs and precise diagnostics. Generalizing it yields one parser with
one set of tests, where the alternatives yield two parsers, or one parser plus a
bespoke format, or one parser plus a vendored library.

Constraints the reader keeps from its restricted ancestor, because they are why
that one has never mis-parsed anything:

- **Diagnostics name the byte offset and what was expected.** A malformed
  manifest is a build-time or startup failure, and the message is the whole
  value of failing.
- **Bounded nesting.** A depth limit, so a hostile or corrupt file cannot
  recurse the parser into the stack guard.
- **No silent coercion.** Asking for a number and finding a string is an error,
  not a zero.
- **It parses; it does not serialize.** Nothing in the engine writes JSON —
  `tools/repo/vendor.luau` deliberately never writes `manifest.json`, and the
  generated files are written by CMake. A writer would be a surface with no
  caller.

## Consequences

One parser, in the module everything can see, with the tests that already exist
for the catalog half plus new ones for the general grammar. The i18n catalog
keeps its strict schema — the *grammar* widens, the *catalog's* validation does
not, so a catalog with an array in it still fails as loudly as before.

The cost is that this is now engine code we own and must keep correct, in a
category where subtle bugs are famous. It is bounded — a parser for a fixed
grammar is finished once it is finished — and it is bought against a dependency
we would also have had to keep pinned, audited and patched.

If a future need genuinely exceeds this (streaming multi-megabyte documents,
say), that is when a library becomes the right answer, and it gets its own ADR
with a real workload behind it rather than a guess.

## Addendum — 2026-08-20 (M3): the writer this said would have no caller

`json.h`'s header says a writer would be surface with no caller, and that was
true for as long as nothing in the engine produced bytes another program parses.
M3 gives it two callers: the dev control channel sends JSON messages over its
WebSocket (ADR 0035), and `luaug test` writes a machine-readable report the CLI
turns into TAP or JUnit — deliberately a file rather than the console, because
every line the engine prints is catalog-resolved and parsing it would break the
first time a locale is added.

`core::JsonWriter` is therefore added beside the reader, on the same terms: hand
written, in `core`, no dependency. It is a *writer* and not a DOM — nothing needs
to build a document, mutate it and then serialise it — which keeps field order
identical to call order and the output stable to compare without sorting (R10).
