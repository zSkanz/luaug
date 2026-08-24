# E4 — The Editor Ships — Kickoff Brief

**Milestone:** E4, post-v1 phase 1, size M.
**Opened:** 2026-08-24. **Written before any of it was built.**
**Authority:** [`docs/roadmap.md` § E4](../roadmap.md#e4--the-editor-ships-m) ·
[ADR 0054](../decisions/0054-the-editor-ships-as-a-folder-and-the-cli-finds-its-own-install.md)
· [ADR 0046](../decisions/0046-the-editor-is-a-mode-of-the-engine-binary.md) ·
[ADR 0045](../decisions/0045-a-packaged-game-is-a-folder-that-ships-source.md)

## The goal, in my own words

**Somebody who has never built this repository can download one file and make a
game with it — and the editor they get stays fast on the world they build.**

Those are the two halves the roadmap row names, and they are not two milestones
wearing one number. An editor nobody can obtain has no performance problem worth
fixing, and an editor that is obtainable and stalls on the first large world is
one nobody keeps. E4 is the milestone where the editor stops being a thing that
exists in a build tree.

The distribution half is the question ADR 0046 wrote down and refused to answer
in passing: *"the editor being a product that a person downloads, rather than a
thing you get by building the repository, is a distribution question this ADR
does not answer and phase 1 must not answer by accident."* It is answered here,
on purpose, in ADR 0054.

## Reconnaissance — six passes, and three of them found something

Run before writing the scope, and each one is a fact from a file rather than a
recollection.

**1. What does an installed CLI actually resolve?** `tools/cli/project.luau`
ends its engine search at `joined(path.format(process.cwd()), name)`, commented
*"beside this CLI, which is the shape an installed release has"*. It is not
beside the CLI — `process.cwd()` is wherever the person is standing.
`commands/version.luau` reads `CMakeLists.txt` three directories up, which an
installation does not have. `commands/new.luau` walks up looking for
`templates/starter` and is correct, and it is correct because D045 already cost
this exact lesson once. **Three answers to one question, two of them wrong, and
nothing here could have noticed** — nothing in this repository has ever run
outside it.

**2. What does the host need beside itself?** `engine/platform/src/platform.cpp`
derives `contentDir` from `SDL_GetBasePath`, so the answer is one directory:
`content/`, holding `fonts/`, `i18n/`, `icons/`, `runtime/` and `shaders/`. That
is what makes a folder a working installation, and it is the convention ADR 0045
already relies on for a packaged game.

**3. What else does the CLI reach for?** `templates/starter` and
`runtime/types/engine.d.luau` for `luaug new`; `assetc` for `luaug build-assets`
and `iconpatch` for `luaug build`, both through `findTool`, which has the same
`process.cwd()` ending; `luau-lsp` and `stylua` on `PATH` for `check` and `fmt`,
both already probed with a named failure.

**4. Which profile should the artifact be?** `cmake/luaug_options.cmake` gates
`LUAUG_DEBUG_UI` and `LUAUG_BUILD_TESTS` off for `shipping` and `player`, so
`dev` is the only profile that produces an editor at all — and `dev` is
RelWithDebInfo with a hundred test translation units beside it. The gap is not a
feature one. It is the gap between a build tree and an artifact.

**5. Where does an editor frame actually go?** `drawExplorer` calls
`collectTree`, which is a full preorder over every instance in the world, and
then walks its output a second time to compute visibility. The **drawing** is
already virtualised — `ImGuiListClipper`, at an exact row pitch, which D-numbered
work already paid for — so the panel looks fine and is charged, every frame, for
a world nobody can see. Nothing else in the editor's frame path is `O(world)`:
`ContentTree::refresh` runs on navigation, the property grid is `O(properties of
one instance)`, and picking is per click.

**6. What did the last milestone teach that applies here?** E5's peak
measurement failed on its first run and would have passed had the assertion been
"small" rather than "equal". The Explorer's claim has exactly that shape, so its
test is written the same way: **two worlds an order of magnitude apart must visit
the same number of instances.**

## Design decisions

**D1 — The product is a folder, and the archive is a compression of it.**
Not an installer, not a package-manager manifest. ADR 0054 records the three
shapes and why; the short version is that the other two are about an artifact
that has to exist first, and both cost a dependency this project would be adding
before anybody has downloaded the thing once.

**D2 — A new `editor` profile rather than shipping the `dev` build.**
The binary's features are identical, which is the argument *against* a profile,
and the argument for it wins on a different axis: the thing people download must
be a configuration that a gate compiles, and `dev` builds a hundred test
translation units on the way to producing it. D056 and D057 are both the same
story — a profile nothing builds is a profile nobody knows is broken — so this
one is built by `shipping-build.sh` from the day it exists.

**D3 — `installRoot()` is resolved by looking, not by counting.**
`new.luau` already does this and the comment above it says why: a count is a fact
about a directory layout that no check holds still. The other two call sites
adopt it, and the resolution order takes its roots as an argument so a test can
drive it — the seam `edit.engineArguments` established.

**D4 — The version stamp is a derivation, not a second copy.**
ADR 0031 forbids typing a version into a file. The packager reads the
`project(LuauG VERSION ...)` declaration and writes it into the folder at the
moment it copies the binary built from that declaration, and the stamp names its
source. In a source tree `CMakeLists.txt` is still the only authority.

**D5 — The attributions are the ones this repository already generates.**
The plan here said "generate a notices file from the manifest", and the seventh
reconnaissance pass — run while writing the packager — found that
`THIRD_PARTY_NOTICES.md` already is one: written by `vendor.luau notices` from
`third_party/manifest.json` and audited by `licensecheck.luau`. So the packager
copies it with `LICENSE` and `NOTICE` and refuses when one is missing. A second
attribution list would have been a second thing to keep true, which is the
failure the first one exists to prevent.

**D6 — The Explorer's walk and its visibility pass become one walk.**
The current pair is `O(world)` twice. The replacement is a preorder that asks the
caller, per candidate row, whether to skip it, show it, or show it and descend —
which is exactly the three answers the existing visibility pass computes, in the
one place that can act on them before paying for a subtree.

**D7 — The performance gate is counted work, not wall-clock time.**
`docs/perf-baselines.md`'s own methodology says a threshold on a busy machine is
a threshold that fails for the wrong reason, and E5 has the sharper version of
the argument. So the gate item is an equality over instances visited, which is a
property of the algorithm and reads the same on any machine; what a person sees
with the editor open is recorded in the gate record, where a number that depends
on the machine belongs.

## Scope checklist

- [x] `editor` build profile, its two presets, and the shipping gate building it
- [x] `tools/repo/package.luau` — the folder
- [x] `scripts/package.ps1` — the archive
- [x] The licence and notice files carried into the folder
- [x] `installRoot()`, and the four call sites that ask it
- [x] The version stamp, read when there is no `CMakeLists.txt`
- [x] CLI tests over the resolution order and over the stamp
- [x] `collectVisibleTree`, and the Explorer rewired onto it
- [x] The counted-work test: the same expansion over two worlds an order of
      magnitude apart visits the same number of instances
- [x] `docs/manual/get-started/install.md` — downloading comes first
- [x] `docs/perf-baselines.md` — an editor row
- [x] `PROGRESS.md`, and this brief's Gate Record

## NOT in scope

Imported from the roadmap section, and each one is a decision rather than an
omission:

- **An installer, code signing, an update channel.** Dependencies and decisions,
  all three about an archive that has to exist first.
- **A Linux or macOS package.** `luaug build` is Windows-only and says so; a
  packaging path no tier here can execute is one that ships broken.
- **A project browser or start screen.** `luaug new` then `luaug edit` is the
  path. Inventing a start screen at the packaging milestone is inventing it
  without having watched anybody need one.
- **Bundling `luau-lsp` and `stylua`.** A redistribution question — StyLua is
  MPL-2.0, outside R6's list — bought for two commands that already probe and
  name what is missing.
- **Making the repository public.** The human's, and in the ledger.

## Subagent plan

**None.** MASTER_PROMPT § 7 says not to fan out across two modules' seams, and
every item here is one: the CLI's resolution and the packager are the same
question asked from two sides, and the Explorer's walk is a change to a
tested `app` header with one call site. The milestone is small enough that
coordination would cost more than it saved.

## Gate (copied verbatim from `docs/roadmap.md` § E4)

- **A person who has never built this can use it.** The archive is unpacked on a
  machine with no repository, no `LUAUG_BUILD_ROOT` and no rokit, and
  `luaug --version`, `luaug new`, `luaug edit` and `luaug build` all work from
  it. The transcript goes in the gate record.
- **The installed resolution is asserted, not described.** A test drives the
  order — an explicit environment variable, then the build tree, then the
  installation — and a second one proves the installation is found when the
  working directory is somewhere else entirely, which is the case that has never
  worked.
- **`luaug --version` answers inside an installation**, with the number the
  binary in it was built from, and the stamp names where that number came from.
- **The `editor` profile is compiled and linked by the gate**, on the tier the
  shipping stage already runs.
- **The archive carries its attributions**, generated from the manifest, and the
  licence audit is still green.
- **The Explorer costs what is open, not what exists.** A world an order of
  magnitude larger than the flagship's, with the same subtrees expanded, visits
  the **same** number of instances — equal, not merely fewer. A test asserts the
  equality, for the reason E5's peak measurement asserted one: a bound that is
  merely small passes while the defect is still there.
- **The cost is recorded as work, not as a clock.** `docs/perf-baselines.md`
  gains an editor row giving the instances the Explorer visits per frame before
  and after, on worlds whose sizes are stated — a number that is the same on any
  machine, which is what the baselines methodology asks for and what a threshold
  on a busy machine cannot be. A wall-clock impression from a person with the
  editor open belongs in the gate record beside it, where a number that depends
  on the machine belongs.
- **`scripts/localgate.ps1` is green on every stage.**

## Gate Record

**BUILT, awaiting review, 2026-08-24.** Everything below is a result from this
machine; the two items a person has to answer are marked as such and are the
same limit E1 recorded.

Closing run, `scripts/localgate.ps1`:

```
  ok    docs (19.7 s)
  ok    luau (14.1 s)
  ok    format (20.8 s)
  FAIL  windows (62.8 s)     43 of 44 -- openworld_soak, below
  FAIL  linux (84.3 s)       40 of 41 -- the same one
  ok    shipping (104.5 s)   shipping, player AND editor
```

**The one red is not this milestone's and predates it.** `openworld_soak`
compiles the flagship's chunk sources, `tests/support/ensure_generated_world.cmake`
regenerates them when the directory is absent, and the other session's in-flight
rewrite of `examples/10-open-world/tools/generate_world.luau` now writes a scene
instead of those sources. It is the migration the human has already said they
would ask for; both files are theirs and uncommitted, and nothing in E4 touches
either. Every other test passes on both tiers.

| Claim | Answer |
|---|---|
| A person who has never built this can use it | **Pass, with one part a person owns.** `scripts/package.ps1` built `LuauG-1.0.0-win64` and `tests/installed` drove the packaged `luaug` from a scratch directory with an environment holding no `LUAUG_BUILD_ROOT` and no `LUAUG_HOST`: `--version` answered 1.0.0, `luaug new` scaffolded a project with its definitions, `luaug build` produced `installedtest.exe` through the packaged player host, and running that executable printed `LuauG 1.0.0 — engine initialized.` and `Hello from LuauG`. Break-verified. **`luaug edit` from the archive is the human's**: the shell cannot render headlessly. |
| The installed resolution is asserted, not described | **Pass.** `tools/cli/tests/install.test.luau`, seven cases: the order, the installation as the only answer when the environment is empty, the working directory absent from the list, the player host under `player/` and distinct from the editor, `LUAUG_HOST` never reaching a player search, a tool's two layouts, and this repository resolving as its own installation root. Break-verified. |
| `luaug --version` answers inside an installation | **Pass**, from the stamp, and the stamp names the declaration it was derived from. |
| The `editor` profile is compiled and linked by the gate | **Pass.** `shipping-build.sh` builds it beside `shipping` and `player` on Tier-2 (Clang, warnings as errors). |
| The archive carries its attributions | **Pass.** `LICENSE`, `NOTICE` and `THIRD_PARTY_NOTICES.md`, the last generated from the manifest; `licensecheck` green, 19 dependencies. |
| The Explorer costs what is open, not what exists | **Pass, as an equality.** 205 instances and 2,005 instances, same expansion: **5 visited in both**. Opening one branch of fifty costs exactly fifty more. A skipped subtree contributes neither a row nor a visit below it. Break-verified by making the walk descend unconditionally, which reports 205 and 2,005. |
| Cost recorded as work rather than as a clock | **Pass.** `docs/perf-baselines.md`, new section, with the numbers reproducible by running the suite rather than recorded off one afternoon. |
| `scripts/localgate.ps1` green on every stage | **Pass except the pre-existing red above.** |
| A human downloads it and says whether it works | **PENDING — a person at a machine.** Unzip `LuauG-1.0.0-win64.zip` where no build tree exists, `luaug new`, `luaug edit`. |

**And packaging found a defect a release had been carrying.** D086: `v1.0.0` was
tagged and published while the version declaration still read `0.0.1`, so the
released binary reported 0.0.1 — faithfully, through a derivation chain that was
working exactly as ADR 0031 designed it. What had no owner was the thing at the
top of the chain. Bumped, the api dump regenerated with it, and the packager now
refuses to write a folder whose name would disagree with the newest release tag.

## What E4 does not have

- **An installer, signing, or an update channel.** Named in scope as decisions
  about an archive that had to exist first. It exists now.
- **A Linux or macOS archive.** The packager itself is platform-agnostic — it
  packages for whatever it runs on — but `luaug build` is Windows-only, so a
  Linux folder would carry a tool that refuses. That is the gap, and it is
  `luaug build`'s rather than the packager's.
- **A project browser.** `luaug edit` with no project still needs a path.
- **`luaug check` and `luaug fmt` inside the archive.** Deliberate: a
  redistribution question bought for two commands that already name what is
  missing.
- **A gate that runs the packaging round trip on every push.** `tests/installed`
  runs where it matters — inside `scripts/package.ps1`, before the archive is
  compressed — rather than adding two Release profiles to a ninety-second gate.
