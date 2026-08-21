# 0042 — A vendored row may narrow to the upstream paths the build uses

- Status: accepted
- Date: 2026-08-21
- Amends: ADR 0021 (the "exact upstream content" clause, narrowed rather than
  replaced). Extends ADR 0032's reasoning about binaries in history.

## Context

ADR 0021 says a vendored tree is "exact upstream content at the pinned commit
with no `.git` directory". That has held for eleven dependencies and it is the
right default: it makes a vendored file diffable against upstream, it makes a
pin mean something, and it removes every argument about which files matter.

M7 vendored `basis_universal` at `v2_50` and the rule met a tree it was not
written for. Upstream at that commit is **302 MB**, of which about **275 MB** is
content this repository never compiles:

| Path | Size | What it is |
|---|---|---|
| `webgl/` | 87 MB | Browser demos, with their own prebuilt `.wasm` and `.js` |
| `test_files/` | 80 MB | Reference images and encoded textures for upstream's own tests |
| `python/` | 58 MB | Python bindings and built wheels |
| `bin/` | 49 MB | **Prebuilt executables and libraries** |
| `shader_deblocking_*` | 13 MB | Two sample applications |
| `contrib/` | 4.8 MB | Third-party integrations |

The parts that get compiled — `transcoder/`, `encoder/`, `zstd/` — are 12 MB.

Two things make this different in kind rather than in degree. The first is that
git history is forever: 302 MB per pin bump is a cost every future clone,
every CI checkout and every container build pays, and it is paid to carry test
images. The second is that `bin/` is 49 MB of **opaque prebuilt binaries**, and
ADR 0032 exists precisely because "50 MiB of opaque binaries would land in
history per version" was judged unacceptable for DXC. Applying one rule would
have violated the reasoning of the other.

## Decision

A manifest row may carry an optional `include` list of upstream paths. When it
is present, `tools/repo/vendor.luau` passes those paths to git as **pathspecs on
the checkout itself** rather than deleting files afterwards.

The distinction matters and is the whole reason this is acceptable:

- What lands is still **byte-exact upstream content at the pinned commit**. Git
  selects it; nothing is rewritten, filtered or regenerated. A reviewer can
  still diff any vendored file against upstream and get silence.
- What is given up is only **"the whole tree"**. The row says exactly which
  paths it kept, so the narrowing is a line in the manifest a human can read
  rather than a property of a tool nobody runs.

`include` is absent from every other row and stays that way. It is for a tree
whose non-compiled payload dwarfs its source, and the row must say why in its
own note.

**What this does not do:** it does not permit editing a vendored file (R13 is
untouched), it does not permit vendoring an unpinned commit (R5 is untouched),
and it does not permit dropping a licence or notice file — the four licence and
notice files are in `basis_universal`'s include list for that reason, and a row
that omitted them would be a licence violation rather than a tidy-up.

## Consequences

`third_party/basis_universal` is 13 MB instead of 302 MB. The mechanism is four
lines in the vendor tool and one optional field in the manifest schema.

The cost is honest and worth stating: a narrowed row can no longer be verified
by "check out upstream and diff the directory", because the directory listings
differ by construction. Verifying one means diffing the paths the row names,
which is what the `include` list is for.

There is one judgement here a human may want to remake, and it is recorded in
`PROGRESS.md`: whether narrowing should exist at all, or whether a dependency
this large should instead be an ADR 0032 fetched artifact — which it cannot be
today, because ADR 0032 rule 4 says anything the engine LINKS is vendored as
source, and the transcoder is linked into the runtime.
