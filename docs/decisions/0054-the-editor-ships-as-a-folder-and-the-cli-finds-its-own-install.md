# 0054 — The editor ships as a folder, and the CLI finds its own installation

- Status: accepted
- Date: 2026-08-24
- Extends: 0045, 0046

## Context

[ADR 0046](0046-the-editor-is-a-mode-of-the-engine-binary.md) settled what the
editor *is* and named, in its own consequences, the one thing it deliberately
did not settle:

> the editor being a product that a person downloads, rather than a thing you
> get by building the repository, is a distribution question this ADR does not
> answer and phase 1 must not answer by accident.

Four milestones later the editor is an editor — the loop, the manipulators, the
content browser, stamps, and a world that partitions itself — and the only way
to obtain it is still to clone a private repository, install a Developer Shell,
run a bootstrap, and compile Jolt. `docs/manual/get-started/install.md` opens
with the sentence "LuauG is built from source", and that sentence is the whole
of the distribution story.

**The repository already believes it has an installed shape, and it has never
had one.** That is the measurement this decision starts from, and all three
findings are in files nobody had reason to doubt:

- `tools/cli/project.luau`'s engine search ends at
  `joined(path.format(process.cwd()), name)` under the comment *"beside this
  CLI, which is the shape an installed release has"*. It is not beside the CLI.
  `process.cwd()` is the directory the person is standing in — which for
  `luaug edit ./mygame` is wherever they keep their projects. An installed CLI
  finds its own engine only when somebody happens to be standing inside the
  installation.
- `tools/cli/commands/version.luau` reads the `project(LuauG VERSION ...)`
  declaration out of `CMakeLists.txt` three directories above itself, which is
  right and is [ADR 0031](0031-build-provenance-header.md)'s rule — and an
  installation is not a source tree, so `luaug --version` in one has nothing to
  read.
- `tools/cli/commands/new.luau`'s `cliRoot()` is the one that is already
  correct: it walks up from its own file looking for `templates/starter` rather
  than counting directories, which is what D045 cost to learn.

So the same question has been answered three times in one CLI, two of the
answers are wrong, and neither is wrong in a way that anything here could
notice — because nothing here has ever run outside the repository.

**Three shapes for the distribution itself.**

**(a) An installer.** An MSI or an NSIS script: start-menu entries, a PATH edit,
an uninstaller. It is what a person expects from a desktop application, and it
is a new vendored dependency with a licence question (R5, R6) plus a signing
story this project does not have, bought before anybody has downloaded the thing
once.

**(b) A package manager.** rokit already installs this project's own toolchain,
and winget or Scoop would put `luaug` on a PATH. Both are *publishing* channels
rather than artifacts: each needs a manifest pointing at a released archive that
must exist first, and rokit distributes single executables while this is a tree
of a host binary, its content directory and an interpreter. The archive is the
prerequisite either way.

**(c) An archive of a folder.** Exactly the shape
[ADR 0045](0045-a-packaged-game-is-a-folder-that-ships-source.md) already chose
for a packaged game, for the same reasons and with the same one convention
underneath it: the host resolves its content directory beside its own executable
(`engine/platform/src/platform.cpp` derives `contentDir` from
`SDL_GetBasePath`), so a folder is a working installation and a copy of it is
another one.

## Decision

**The editor ships as an archive of a folder, built from an `editor` build
profile, and every path the CLI needs is resolved from its own installation
root.**

Four parts, and the last of them is the one that had already been half-written:

- **An `editor` profile** beside `debug`, `dev`, `profile`, `player` and
  `shipping`: Release, the debug UI on, the Luau compiler on, the C++ test suite
  off. It is not a feature difference from `dev` — an editor *is* a dev build,
  which 0046 says in as many words — it is the difference between a build tree
  and an artifact, and it exists so that the thing people download is a
  configuration a gate compiles. A profile nothing builds is a profile nobody
  knows is broken (D056, D057), so `scripts/gates/shipping-build.sh` builds this
  one too.
- **`tools/repo/package.luau`** writes the folder, and `scripts/package.ps1`
  compresses it. It packages for the platform it is running on and cannot do
  otherwise — the binaries it copies are named the way that platform names them
  — which is why it needs none of the refusal `luaug build` carries. Windows is
  the only tier this phase ships, and that is the release's decision rather than
  the packager's.
- **The attributions the archive carries are the ones this repository already
  generates.** `THIRD_PARTY_NOTICES.md` is written from
  `third_party/manifest.json` by `vendor.luau notices` and audited by
  `licensecheck.luau`; the packager copies it with `LICENSE` and `NOTICE` and
  refuses when one is missing. A second attribution list written for the archive
  would be a second thing to keep true.
- **`installRoot()` is the one answer to "where am I installed"**, resolved the
  way `new.luau` already resolves it — walk up from this file until the
  directory holds the layout, rather than counting. `findEngine`, `findTool`,
  the template, the generated definitions and `--version` all ask it.

**What the package contains** is what the four commands a person will type need:
`luaug new`, `luaug edit`, `luaug dev` and `luaug build`. The host, its content
directory, the CLI and the pinned Lute that runs it, the project template, the
generated type definitions, `assetc` and `iconpatch`, the licences, and a
version stamp standing in for the `CMakeLists.txt` that is not there.

**What it deliberately does not contain is `luau-lsp` and `stylua`.** They are
what `luaug check` and `luaug fmt` shell out to, both already probe for them and
name what is missing, and shipping them is a redistribution question — StyLua is
MPL-2.0, which is not on R6's list — bought for two commands that already fail
politely. `rokit install` is one line in the manual and remains the answer.

**And it does not contain a project browser.** An editor opened with no project
would need one; `luaug new` followed by `luaug edit` is the documented path, and
inventing a start screen at the packaging milestone would be inventing it
without having watched anybody need it.

## Consequences

**The installed shape becomes testable, and it was not before.** The resolution
takes its candidate roots as an argument, so the order is a pure function the
CLI's own tests drive — the seam `edit.engineArguments` already establishes. The
three ways a host is found (an explicit environment variable, the build tree,
the installation) stop being a comment about what happens and become a list a
test asserts on.

**`luaug --version` gains a second source and keeps one authority.** The
declaration in `CMakeLists.txt` is still the truth in a source tree; the stamp in
an installation is written *from* that declaration by the packager, at the moment
it copies the binary that was built from it. That is a derivation rather than a
second copy — ADR 0031's distinction — and the file names where it came from.

**A packaged editor and a packaged game share the layout and not the code.**
`luaug build` writes a game; this writes the tools. They agree that the product
is a folder and that the host finds its content beside itself, and beyond that
they are two programs — merging them would be merging what a player needs with
what an author needs, which are different sets that happen to overlap.

**What this does not decide.** Signing, an update channel, a Linux or macOS
package, and whether the repository is public — the last of which is the
human's, is in the ledger, and decides whether anybody can download this at all.
The archive is what those questions are about; it is not any of them.
