# M7 Kickoff — Scaling the World: Asset Pipeline, Async IO, Streaming, Floating Origin

- Started: 2026-08-21
- Roadmap section: docs/roadmap.md#m7--scaling-the-world-asset-pipeline-async-io-streaming-floating-origin-l

## Goal (restated)

Every world this engine has run so far fits in memory, loads at boot, and sits
near the origin. `examples/04-obby` is a game, and it is a game the size of a
room: its content is built by the script that runs it, its meshes are read from
`.gltf` files parsed at startup, and the furthest thing from `(0, 0, 0)` is
maybe eighty metres away. Nothing about that scales, and the v1 definition of
done is a *large open world*.

This milestone builds the substrate that makes a world bigger than memory
possible, and it has four halves that only look separate:

- **Content stops being source files.** An offline pipeline turns authored
  glTF, images and `.prefab.luau` trees into an engine format — meshopt-encoded
  vertex/index streams with a LOD chain, transcodable textures, compiled
  prefabs — packed content-addressed into `.lpack` files with a manifest. Same
  input, same hashes, every time, on every machine: that is what makes CI
  caching possible and it is what makes "did the content change" a question
  with an answer.
- **Loading stops blocking.** A work-stealing job pool and real async IO, so a
  chunk arriving is a read that was issued three seconds ago, decoded on a
  worker, and materialized inside a per-frame time budget — rather than a
  hitch. R10 governs every line of it: the deterministic commit rule (per-job
  buffers, barrier, stable merge) is not advice here, it is what keeps a replay
  a replay once work is spread across cores.
- **Coordinates stop being f32 in disguise.** The tree has stored `CFrameD`
  since M2 and the renderer has extracted camera-relative since M4, but
  `toJoltPosition` still casts f64 straight to f32, so the physics world *is*
  the absolute world and precision dies at a few tens of kilometres. A
  per-World floating origin closes that, and the gate is a hash: an object at
  coordinate 1e7 must behave exactly as it does at the origin.
- **The world stops being resident.** A uniform chunk grid, a
  `StreamingManager` that scores by distance to focus with hysteresis, a budget
  denominated in *milliseconds* rather than in chunks, and eviction that
  honours §4 reparent-to-nil husk semantics. `StreamingService` is the Luau
  face of it.

And the through-line, which is a generalisation of M6 Finding 11: **this
milestone failure mode is invisible in a screenshot.** A world that streams
correctly and a world that pre-loads everything look identical for the first
minute; the difference is a hitch histogram, a memory ceiling and what happens
at minute five. So the gate is the deliverable *plus instrumentation*, and
`examples/05-streaming` ships a chunk-state overlay and a memory graph because
that is the only way a person can see whether it works.

## Scope checklist (from roadmap)

- [x] **Offline pipeline via Lute (`luaug build-assets`)**: glTF to engine mesh
      format, basis_universal/KTX2 textures, meshopt LODs, content-addressed
      pack + manifest, deterministic outputs (same input, same hashes; enables
      CI caching)
- [x] **assimp as the offline-CLI-only importer** for exotic formats,
      normalizing to glTF 2.0 — never linked into the runtime
- [x] **Engine job system + async IO**
- [x] **64-bit world coordinates + floating origin** (render-relative
      translation; origin/rebase is per-World state, never a global — ADR 0014)
      — enforced by tests
- [x] **Chunked world format + StreamingService semantics** (load/unload radius
      around foci, priority queue, budgeted per-frame materialization)
- [x] **Basic LOD switching**
- [x] **Low-level net primitives**: GameNetworkingSockets + ENet behind the
      transport interface, exposed as the minimal socket/HTTP surface via
      `@std/net` (loopback echo example only — replication is post-v1)
- [x] **Recast/Detour**: vendored, seam defined, **no integration** (explicit
      non-goal, ADR 0022)
- [x] **The default typeface ships here**: Inter, OFL 1.1, vendored with its
      licence in `THIRD_PARTY_NOTICES.md` (R6). `TextLabel.Font` stops being
      `Inert` and the marker goes with it
- [x] **`CharacterBody:Jump()` stops refusing in mid-air** (human decision,
      2026-08-21), with the doc sentence about calling it every frame, and the
      three call-site migrations
- [x] **Deliverable:** `examples/05-streaming` — a procedurally generated large
      world (no giant binary assets in the repo), fly-cam, ImGui chunk-state
      overlay, memory graph

### Carried into scope because the pipeline is what they were waiting for

The five remaining `Inert` markers name this milestone in their own docs
(`tools/repo/inertcheck.luau` is what keeps them honest), and four of the five
are `Inert` for exactly one reason: there was no way to hand the engine a file.

- [x] `ImageLabel.Image` / `ImageButton.Image` — a real texture, so the flat
      tint becomes a picture
- [x] `ImageLabel.ScaleType` and `SliceCenter` — meaningless until there is an
      image to scale or slice
- [x] `ScrollFrame.ScrollBarThickness` — the one that is *not* an asset
      question; it is a bar nobody drew
- [x] `MeshPart.CollisionFidelity` — a hull needs mesh geometry the physics
      mirror cannot see, and the asset system is what gives it eyes

## NOT in scope

Named so that "it does not do that" is a decision on the record rather than a
discovery. Fifteen items.

1. **No replication, no `NetworkService`, no Remotes.** ADR 0012. `@std/net`
   is sockets and HTTP; `ITransport` is a seam with an implementation and no
   game-facing caller.
2. **No navmesh anything.** Recast/Detour is vendored and `engine/nav` is a
   header. No baking, no queries, no `NavigationService` (ADR 0022, R15).
3. **No whole-world save format.** Prefabs plus chunk payloads plus spawner
   code cover v1 (api-design.md §2.6); scene serialization stays deferred.
4. **No GPU-driven rendering.** Meshlet data is *emitted* by the importer from
   day one because ADR 0010 says so; nothing consumes it this milestone and
   the RHI capability flag stays unqueried.
5. **No HLOD imposters.** architecture.md §10 describes a merged-imposter mesh
   per chunk. v1 LOD switching is per-mesh chains; chunk-level HLOD is named
   here and not built, because a chunk that drops to nothing beyond the target
   radius is honest and an imposter is a renderer feature.
6. **No streaming of scripts or audio.** Chunks carry instance trees, meshes
   and textures. `ModuleSource` and `SoundAsset` stay boot-loaded.
7. **No texture compression on the GPU side beyond what basis transcodes to.**
   No runtime BC7 encoding, no virtual texturing.
8. **No shadows, lights or reflections work.** That is M7.5, deliberately after
   this, and this milestone must not quietly start it (R18 names the reference
   comparison; M7.5 owns it).
9. **No multi-threaded Luau.** `synchronize`/`desynchronize` remain reserved
   names. The job pool is for engine work; the game VM stays serial (§5).
10. **No actor VMs.** The `ThreadSafety` annotations continue to be carried and
    not enforced.
11. **No mobile, no editor, no 2D.** R15.
12. **No cross-platform determinism promise.** ADR 0025 level B is unchanged by
    this milestone, and wiring the job pool must not weaken it — see Decision 4.
13. **No hot reload of packed content.** `onAssetInvalidated` exists for the
    dev-mode content directory. A `.lpack` is immutable by construction;
    swapping one at runtime is M8 `luaug build` problem.
14. **No `PauseOutsideLoadedArea` physics integration beyond the documented
    contract.** The min-radius ring is guaranteed resident before the focus may
    advance into it; that is a streaming pause, not a rollback.
15. **No shipping profile.** It still does not configure, it still needs a
    bytecode-loading path, and it is still M8.

## Gate checklist (verbatim from roadmap)

- [x] scripted 5-minute fly-through: peak memory under the declared ceiling
- [x] zero frame hitches >33 ms attributable to streaming (frame-time histogram
      asserted)
- [x] float-precision test: object behavior at coordinate 1e7 identical to
      origin (hash comparison)
- [x] asset build determinism check in CI
- [x] loopback socket echo test
- [x] pak round-trip fuzz test (truncated/corrupt pak, structured error, no
      crash)

## The decisions this brief makes

Written before the work so that a reviewer can disagree with a decision rather
than with a diff.

### 1. The offline pipeline is a native tool the Luau CLI drives

The roadmap says "offline pipeline via Lute (`luaug build-assets`)". Lute is a
Luau runtime; it cannot run a mesh simplifier or a texture encoder, and it must
not — meshoptimizer and basis_universal are C++ libraries this repository
already vendors or has a row for. So `luaug build-assets` is a
`tools/cli/commands/build-assets.luau` that discovers inputs, decides what is
stale, and drives **`assetc`**, a native host tool built beside `tools/imgcmp`.

That split is not a compromise, it is where the two halves belong: the Luau side
owns the project model (`luaug.toml`, the content directory, incremental
decisions, the console output) and the native side owns the codecs. It is the
same shape `shadercross` already has for shaders, which is the precedent in the
tree rather than an invention.

**Determinism is a property of `assetc` and it is designed in, not tested for
afterwards**: inputs sorted by path before processing, every encoder parameter
pinned in the tool rather than defaulted, no timestamps or paths in output
bytes, and single-threaded encoding unless a parallel encode is proven to
produce identical bytes. The CI gate builds the same content twice and diffs
the manifest.

### 2. `ContentHash` is BLAKE3-128, and BLAKE3 is vendored here

architecture.md §2 has said `ContentHash` is BLAKE3-128 since planning, the
manifest row has existed since planning, and nothing has needed it until now.
Vendored at the milestone that first needs it — the xxhash, meshoptimizer, Jolt
and miniaudio rule.

**Not xxh3**, which is already in the tree and would save a dependency: xxh3 is
a *checksum* and content addressing is a *name*. Two distinct assets colliding
under a 64-bit non-cryptographic hash is a corrupted build that looks like a
cache hit, and the difference between "unlikely" and "cryptographically hard"
is exactly what a content-addressed store is buying. `WorldHash` stays xxh3
because that is a checksum and is compared against itself.

### 3. Textures are basis_universal writing KTX2; `ktx` stays un-vendored

ADR 0010 names "basis_universal/KTX2" and the manifest carries two rows —
`basis_universal` and `ktx` (KTX-Software). The basis_universal encoder writes
`.ktx2` and its transcoder reads it, so the pairing partner is not needed to
produce or consume the format we use. KTX-Software is a large tree with its own
dependency set (astc-encoder, zstd, its own build system) bought for a container
both halves of which basis already implements.

So this milestone vendors `basis_universal` and leaves the `ktx` row unpinned,
which is a state the manifest already supports and the vendor tool already
skips. **Removing the row is not the agent call** (§10, and ADR 0040
precedent): it stays, with an ADR recording why nothing pins it, and a human
decides whether a row nothing will ever vendor should exist.

Encoder settings are pinned to **UASTC and ETC1S only** — the research report
own warning is that XUASTC was not standardized as of early 2026, and a
non-portable texture format is the last thing an asset pipeline should reach
for.

### 4. The job pool ships; Jolt use of it is decided by the determinism gate

`engine/jobs` at L1 is new: a work-stealing pool, `parallelFor`, and the
**deterministic commit rule** as the module central contract — sim-visible work
writes per-job buffers, hits a barrier, and merges in index order before any
world mutation. Job domains are classified (*sim-visible*, *render*,
*asset/IO*, *tooling*) and the classification is in the API, not in a comment,
because "which worker finished first" becoming observable is the one bug this
module can cause that no test looks for by accident.

`joltAdapter()` is the interesting one. Upstream documents contact callbacks,
the active-body list and query hit order as non-deterministic under a
multi-threaded job system (`third_party/jolt/Docs/Architecture.md:804-807`).
M5 wrote the stabilizing sorts precisely so this milestone could wire the pool;
whether it *stays* wired is a measurement, not a preference. **The rule for
this milestone: the adapter is built, and Jolt runs on it only if the
determinism gate stays green and the physics benchmarks improve.** If either
fails, the adapter ships unused with the finding written down, because the
milestone that discovers a thousand recorded traces are worthless is not one
anybody wants.

### 5. The floating origin is per-World state and physics is what it fixes

The tree has stored `CFrameD` since M2. The renderer has extracted
camera-relative since M4 (`RenderCamera::origin`), which is *finer* than an
origin rebase and already correct. The gap is one function:
`jolt_physics.cpp` `toJoltPosition` casts f64 to f32 with nothing subtracted,
so the solver world is the absolute world and a body at 1e7 has metre-scale
quantization before it has moved.

So: a per-World `OriginRebase` holding the current f64 origin; every
scene-to-physics write goes through it; every physics-to-scene read undoes it;
the origin shifts when the primary focus passes 4 km from it, at a FrameStart
safe point, in one budgeted pass that teleports resident bodies preserving
velocity. `DebugDraw::rebaseTo` already exists and gets pointed at the same
value.

**It is World-scoped and never a global**, which ADR 0014 says twice, and the
test that proves it is two worlds with different origins stepping in the same
process.

### 6. `.lpack` is a mount, and a corrupt one is an error rather than a crash

The pack is: a fixed header with magic, format version and a TOC offset; a TOC
of `(ContentHash, offset, storedSize, originalSize, codec)` sorted by hash; then
blobs. Sorted so lookup is a binary search on a read-once TOC, and so the file
itself is deterministic.

Every field is validated before it is used: the gate is a fuzz test that
truncates and corrupts real packs and requires a structured error and no crash,
so **every offset is checked against the file length before it is followed** and
that is a rule the reader is written under rather than a bug it is patched for.
Errors are i18n keys (R3) — `asset.err.pack_truncated`, `asset.err.pack_magic`,
`asset.err.pack_hash_mismatch` — because a person hitting a bad pack deserves to
know which of those it was.

### 7. Streaming budget is time, and the score is distance with hysteresis

The roadmap performance note is explicit and the design follows it literally:
the per-frame materialization budget is milliseconds, not a chunk count, and the
gate measures the same thing it budgets. Materialization runs until the budget
is spent and resumes next frame, so a chunk full of instances costs several
frames rather than one hitch.

Chunk scoring: distance from the nearest focus, `MinRadius` (must-have,
integrity-guaranteed) and `LoadRadius` (best-effort), with **separate load and
evict thresholds** so a focus oscillating on a boundary does not thrash. Loads
are issued in score order through the async IO queue with the score as the IO
priority.

Eviction honours §4: an instance a script still references becomes a husk
reparented to nil, `StreamingMode.Persistent` is immune, `Atomic` means a model
streams as one unit or not at all.

### 8. The streamed world is generated, and generating it is the first caller

`examples/05-streaming` must be a large world with no giant binary assets in the
repository. So the example ships a **generator**: a Luau tool that writes a
world description, which `luaug build-assets` compiles into a real `.lpack` with
real chunks. That is not a shortcut around the pipeline — it is the first honest
caller of the pipeline, and it means the example content is regenerable,
diffable as source, and small in git.

### 9. `@std/net` on the engine own sockets; ENet is the transport; GNS is not vendored

Three separate things wear the word "networking" in the roadmap bullet and they
have different answers.

- **`@std/net`** — the game-facing surface — is HTTP client, WS client, HTTP/WS
  server and raw TCP/UDP. `engine/net` already has TCP and a WebSocket client
  (ADR 0035 dev-server path). This milestone widens that to listeners and UDP,
  and binds it into the VM with the §7 permission model. **Zero new
  dependencies**, and the loopback echo gate lands on it.
- **`ITransport`** — the future replication seam — is a header, and it gets one
  implementation so the seam is proven rather than asserted. **ENet** is that
  implementation: MIT, tiny, no transitive dependencies.
- **GameNetworkingSockets** is not vendored this milestone. **Answered by the
  human on 2026-08-21: agreed, without reservation** — what v1 needs is the
  seam, not the implementation behind it, and the row keeps `TBD` with the
  reasoning written into it the way the Jolt row did until M5. Its dependency set is *OpenSSL or libsodium,
  plus protobuf* (`docs/research/ecosystem-2026.md:210`) — none of which has a
  manifest row, all of which would be new vendored dependencies (R5, R6, §10)
  bought for a seam with no v1 caller. ADR 0040 settled this shape one milestone
  ago: a dependency nobody calls still enters every build, every notices file
  and every future reader half hour. **The row stays, unpinned, carrying the
  reason** — a decision deferred with a reason on it is a different thing from a
  decision forgotten.

### 10. Inter ships as a vendored font file, and the glyph cache widens

M6 built the glyph store as a cache keyed by face, size and codepoint precisely
so this would be a widening. What lands: the Inter TTF under `third_party`, its
OFL 1.1 licence in `THIRD_PARTY_NOTICES.md`, a `FontFace` the store can hold
more than one of, and `TextLabel.Font` reading a `Content` URI with Inter as the
default. The `Inert` marker goes with it, and the ASCII stb face stays as the
fallback for a `Content` that fails to load — because a label that draws nothing
is worse than a label that draws plainly.

### 11. `CharacterBody:Jump()` applies at the next tick, always

The human decision is the mechanism, not the policy: `Jump()` applies
`JumpSpeed` whether or not `Grounded`, and `if character.Grounded then` in the
game reproduces the old behaviour in one line. **What stays engine-side is the
tick** — the impulse is applied at the next simulation tick and never
immediately, or a replay diverges (R10). The property doc gains the sentence
that calling it every frame is flying, and the three call sites migrate.

## Build order

Sequenced so that each phase has a caller in the next one. The substrate is
orchestrator work; the leaves fan out.

**Phase 0 — substrate**
1. `engine/jobs` (L1): pool, `parallelFor`, job domains, the deterministic
   commit rule, tests including a "does the merge order depend on completion
   order" case that fails if it does.
2. `platform::readFileAsync` over SDL_AsyncIO, with an IO priority queue.
3. `core::ContentHash` (BLAKE3-128); vendor blake3.

**Phase 1 — the offline pipeline**
4. `tools/assetc`: glTF to mesh format (meshopt vertex/index codecs, LOD chain,
   meshlets), images to KTX2/basis, `.prefab.luau` to `PrefabDef`, pack writer.
5. `luaug build-assets` driving it, with the determinism double-build check.
6. The `.lpack` reader in `engine/asset`, `mount()`, and the fuzz gate.

**Phase 2 — the runtime asset system**
7. `asset::load` with refcounted handles and states, async through jobs + IO,
   `AssetService` (`LoadModelAsync`, `PreloadAsync`, `Exists`).
8. The texture path to the GPU, so `ImageLabel.Image`, `ScaleType` and
   `SliceCenter` lose `Inert`.
9. Inter, so `TextLabel.Font` loses `Inert`.
10. LOD chain selection by projected screen error.
11. `MeshPart.CollisionFidelity` becomes a real hull; loses `Inert`.
12. `ScrollFrame.ScrollBarThickness` becomes a drawn bar; loses `Inert`.

**Phase 3 — coordinates**
13. Per-World `OriginRebase`, the physics rebase, the 1e7 hash test, the
    two-worlds test.

**Phase 4 — streaming**
14. Chunk format and chunk baking in `assetc`.
15. `asset::StreamingManager`: scoring, hysteresis, the time budget.
16. scene glue: materialization, eviction, husks, `Model.StreamingMode`.
17. `StreamingService` in the IDL and the VM.

**Phase 5 — net**
18. `@std/net` in the game VM with the §7 permission model; listeners and UDP
    in `engine/net`.
19. `ITransport` + `net_enet`; the GNS ADR.
20. The loopback echo example and its gate.

**Phase 6 — seams and the long tail**
21. Vendor recastnavigation; `engine/nav` header-only seam, no integration.
22. assimp as the exotic-format front end of `assetc`, built offline only.

**Phase 7 — the deliverable and the gate**
23. `examples/05-streaming`: the generator, the fly-cam, the chunk-state
    overlay, the memory graph.
24. `CharacterBody:Jump()` and its three migrations.
25. The full gate, the baselines, the Gate Record.

## Subagent plan

Orchestrator-only, because every one of these is a seam between two modules or
a determinism question: `engine/jobs` and its commit rule, the floating-origin
rebase, the streaming manager and its scene glue, the Jolt adapter decision,
and every gate run.

Fans out, once the interface exists and compiles:
- **The `assetc` codec front ends** — glTF to mesh, image to KTX2, prefab to
  `PrefabDef` — are three independent programs against one frozen output format.
- **The `.lpack` fuzz corpus and reader tests**, written against the format
  document alone.
- **Conformance specs** for `AssetService`, `StreamingService` and `@std/net`,
  written from `docs/api-design.md` alone, by an agent that has not read the
  implementation.
- **An adversarial reviewer** on the streaming diff and on the jobs diff,
  briefed to attack R10 first and R17 second.

## Attempted / abandoned

(append during the milestone)

## Findings

Things the documents assumed that reality corrected, in the order they cost
time.

1. **The fuzz gate found a hole in the format the first time it ran, and the
   answer was to change the format.** Four thousand random bit flips over a
   valid pack; 3,999 were refused and one was accepted. It was a bit in a TOC
   entry's `kind` field — a mesh becomes a prefab, every structural check still
   passes, and nothing else in the file says what that entry should have been.

   The general shape is worth more than the fix: **a content-addressed store
   protects its payload by construction and its metadata by nothing.** Blob
   bytes are covered by their own names and header fields by having to agree
   with each other; the table of contents had neither. Sixteen bytes of TOC hash
   in the header closes it, and it is checked on EVERY open rather than only a
   verifying one, because the TOC is where offsets come from.

   The second half is a test-design lesson. Adding the hash made the
   hand-written "entry points outside the pack" case stop testing what it was
   written for: it now failed at the hash check and never reached the bounds
   check, which is the one thing standing between a malicious pack and a read
   past the end of a buffer. That case now REPAIRS the TOC hash after corrupting
   the entry, so the bounds check is still covered by something.

2. **A bounds-checked reader is not a safe reader, because the allocation
   happens first.** Every offset and length in the mesh format was checked
   before it was followed, and the corruption case still threw `bad_alloc` on
   its first run: a flipped bit in a section's element COUNT reached
   `vector::resize` before anything compared it against the bytes the section
   actually had.

   The fix has two halves and the split is the interesting part. For a section
   of fixed-size records the check is exact arithmetic —
   `count <= length / recordBytes` — and every one of the nine such sections now
   carries it. For the two meshopt-COMPRESSED streams there is no such relation,
   because the encoded size is a function of the data rather than of the count;
   those are bounded by a stated engine ceiling instead (`MaxMeshVertices`,
   four million), which is honest about being a limit rather than a derivation.

   **A count is an input too**, and it is the input that is easiest to forget,
   because it does not look like a pointer.

3. **D018 reproduced during a routine gate run, and the hung process answered
   the question before a quarantine was needed.** §12 allows quarantining a test
   that flakes twice; this one hung a second time, and it turned out to be
   cheaper to look at it than to file it.

   The evidence was two commands. The process held exactly ONE socket —
   `127.0.0.1:65533` in `LISTEN`, nothing established — and two threads, both
   waiting. `tests/loopback_server.h` called `::accept` with no deadline, and
   `~LoopbackServer` joined that thread BEFORE closing the listener.

   **So the state a FAILING test leaves behind was being converted into a suite
   that hangs forever**, which is exactly why it presented as a flake: the
   assertion failure underneath was invisible. And the guard already existed one
   step later — `Connection` has had a ten-second read deadline since it was
   written, with a comment saying a suite that hangs tells you less than one
   that fails. The step before it never got the same treatment.

   The general shape: **a blocking call in test scaffolding needs a deadline for
   the same reason one in production code does, and the scaffolding is where
   nobody thinks to put one.** Fixed with a polled `select` against a deadline
   and a stop flag the destructor sets first; break-verified by reverting to the
   bare `accept`, at which point the new regression case hangs.

4. **meshoptimizer's index codec rotates triangles, and the round-trip test was
   wrong rather than the codec.** `(16, 15, 32)` comes back as `(15, 32, 16)`:
   the same triangle, wound the same way, starting on a different vertex. It is
   invisible to a renderer and invisible to the submesh ranges, and visible only
   to a test comparing index arrays element by element — which is what the first
   version of the case did, and it read as data corruption.

   Worth keeping because the reflex it corrects is a good one: "the bytes must
   come back identical" is right for the VERTEX stream, which is losslessly
   encoded and is compared with `memcmp` for exactly that reason, and wrong for
   the index stream, whose codec is defined to preserve triangles rather than
   arrays. **A round-trip test has to assert the invariant the codec promises,
   not the strongest invariant the author can imagine.**

5. **A symptom recorded four times as a "pattern to remember" was a defect
   nobody had looked for.** Twice in M6 and twice in M7 an access violation
   landed in code that had nothing wrong with it, immediately after a struct
   gained members, and every time `--clean-first` fixed it. It went into the
   findings as M6 Finding 17: *a stale object file after a struct changed size*.
   True, and not an explanation.

   The fifth time, somebody touched a header and read the output: `ninja: no work
   to do`. Ninja matches a prefix against `/showIncludes` to learn a file's
   header dependencies; CMake writes that prefix as UTF-8; a LOCALISED MSVC emits
   it in the console codepage. The two never matched, so **not one header
   dependency had ever been recorded** and every incremental Windows build in the
   project's history had been unreliable. `chcp 65001` fixes it, verified by
   touching the header and watching the object rebuild.

   The general shape: **a workaround that always works is indistinguishable from
   an explanation, and it stops the search.** "Remember to use `--clean-first`"
   is a sentence that costs one debugging session every time it is right. The
   test that found it took one minute and could have been run at any point in
   two milestones.

6. **The gate that measures the wrong thing passes for the wrong reason, and
   then fails for the wrong reason.** The soak gate's hitch check first read
   whole frames. It could not support the claim the roadmap asks for — "hitches
   attributable to streaming" — and on the Tier-2 container it went red on
   eighteen frames out of eighteen thousand that had nothing to do with
   streaming.

   Changing it to read the time spent inside the streaming pump did two things
   at once: it made the claim true, and it immediately found two real defects
   (D038, D039) that the whole-frame version had been averaging away. The
   roadmap had already said it — *budget and gate should measure the same thing*
   — and the sentence read like style advice until the gate was wrong in both
   directions on the same afternoon.

7. **Both of this milestone's new gates passed vacuously the first time.** The
   soak ran in 0.17 s over eleven instances because `--rhi=null` skipped the
   content mount; the determinism gate compared two files because `assetc`
   writes a pack and a manifest even for an empty input. Flat frame times over an
   empty world are exactly what a leak detector wants to see.

   Both now carry a declared minimum — `--soak-min-instances`, `MIN_FILES` — and
   the rule they encode is worth stating: **a gate that cannot fail on nothing is
   not a gate.** The failure mode is not a red build; it is a green one nobody
   questions.

8. **The engine's own timeline is not the wall clock, and a test that forgets
   that fails on a fast machine.** Three cases in this milestone waited on
   `task.wait` for something a SOCKET was doing. Headless runs the simulation as
   fast as the machine allows, so two seconds of sim time is milliseconds of real
   time — less than a worker thread needs to be scheduled at all.

   The fix is to spin on `os.clock` when the thing being waited for keeps
   wall-clock time, and it has a corollary that bit separately: a conformance run
   carries a hundred-thousand-FRAME budget, so a case that waits on the wall
   clock *spends* that budget and the suite is killed mid-run. The `@std/net`
   conformance spec is written so that every request fails during URL PARSING,
   before a socket exists — the park-and-resume path is identical and the socket
   is `luaug_net_tests`' job.

9. **`Inert` was five markers and one gap.** Four of the five named this
   milestone for the same reason — there was no way to hand the engine a file —
   and the fifth (`ScrollBarThickness`) was a bar nobody had drawn. But the UI
   could not sample a TEXTURE at all: `DrawQuad` had `uvMin`, `uvMax` and a
   `texture` index, and the vertex had no UV and the pipeline had no sampler. The
   fields were a promise, not a path.

   So the font and the images were one change and not two, and the ordering
   mattered: building the texture path for the typeface — which the human had
   decided independently — is what turned `Image`, `ScaleType` and `SliceCenter`
   from a milestone's work into a day's. **A cluster of deferred items with one
   name in their docs is usually one missing capability wearing five hats.**

10. **M6's seam held, and that is worth recording because it is rare.**
    `text.cpp` was written with the glyph key already face-size-codepoint and the
    store already a cache rather than a bake, on an argument stated at the time:
    *an atlas baked once at boot works exactly as long as there is one face at
    one size*. When the real face arrived the key did not change, `measureText`
    and `buildTextGeometry` kept their signatures, and what changed was what
    fills an entry.

    The cost of getting that right in M6 was a paragraph of reasoning and a
    slightly odd-looking cache key. The saving in M7 was not rewriting the text
    system. Seams pay when the shape of the future thing is guessed correctly,
    and the way to guess correctly is to name the property that will change —
    here, "there will be more than one face" — rather than the feature.

11. **A variable font has a default master, and stb_truetype does not know
    which.** Inter ships no static TTF at the pinned tag, and stb_truetype draws
    the outlines in `glyf` — which for a variable font is one particular master.
    If that master had been Thin, every label in the engine would have been
    hairline and the cause would have been invisible.

    It is Regular, and the way that is known is a test measuring the ink coverage
    of a capital H against its own bounding box: about two fifths for Regular,
    an eighth for Thin, two thirds for Black. **When a dependency's behaviour is
    a fact rather than a contract, assert the fact.**

## Gate Record

Filled at milestone end, before human review. Every number below was produced by
a command in this repository, and the command is named beside it.

### The roadmap's six

| Gate item | Where it runs | Result |
|---|---|---|
| Scripted 5-minute fly-through, peak memory under the declared ceiling | `ctest -R streaming_soak` | **25 MiB peak against a declared 192 MiB.** 17,939 frames — five minutes of sim in about nine seconds of wall clock, because the fixed 1/60 s headless step decouples the two |
| Zero frame hitches >33 ms attributable to streaming, frame-time histogram asserted | `ctest -R streaming_soak` | **Zero.** Worst pump inside streaming 1.9 ms; whole-frame median 0.44 ms, p99 0.83 ms, worst 3.8 ms. The histogram is in `streaming-soak.json` |
| Float precision: behaviour at 1e7 identical to the origin, hash comparison | `ctest -R determinism` | Identical hashes, with a differential proving the un-rebased case differs |
| Asset build determinism check in CI | `ctest -R asset_determinism` | **292 files byte-identical across two PROCESSES.** Break-verified both ways: an empty content tree is refused by the declared minimum |
| Loopback socket echo test | `ctest -R "^net$"` | Both directions, over a real ENet handshake, with the disconnect |
| Pak round-trip fuzz: truncated or corrupt pack gives a structured error and no crash | `ctest -R "^asset$"` | 4,000 bit flips, every one refused. The first run found a real hole and the format changed (Finding 1) |

### The gate's own honesty

Two of the six say something narrower than they first appear, and the narrowing
is written into the code rather than left for a reader to discover.

**"Attributable to streaming" is now measured, not assumed.** The hitch check
originally read whole frames, which cannot support that claim — and it went red
in the Tier-2 container on eighteen frames that had nothing to do with streaming.
The roadmap's own performance note says it plainly: *budget and gate should
measure the same thing*. The budget is two milliseconds of materialisation per
frame, so the check now reads the time a frame spent inside the streaming pump.
Whole frames are kept as a p99 backstop that tolerates a scheduler and does not
tolerate a machine that is uniformly slow.

**The soak runs on the null backend**, so a GPU-side leak is invisible to it.
That is a deliberate trade recorded in `tests/streamsoak/run_soak_gate.cmake`:
the gate cannot attribute a hitch to a cause, so a driver's scheduling on
whatever machine CI allocated would land in the same histogram as a slow chunk,
and the first flaky failure teaches everyone to ignore the gate. What was removed
is the renderer; what is measured — residency, decode, materialisation, eviction,
physics, the tick — is all still there.

Both gates carry a declared minimum (`--soak-min-instances`, `MIN_FILES`) because
each of them passed vacuously once. A gate that cannot fail on nothing is not a
gate.

### The full local gate

`scripts/localgate.ps1`, all five stages, on the commit this record is written
against:

```
  ok    docs     (6.2 s)
  ok    luau     (5.7 s)
  ok    format   (10.7 s)
  ok    windows  (47.2 s)
  ok    linux    (55.8 s)
```

39 ctest targets on Windows, 38 on Linux, 1,108 conformance cases, and the Luau
gate's own nine.

### Defects found and fixed during the milestone

Nine, and eight of them were found by instruments this milestone built. That
ratio is the milestone's real result: the gates were not written to pass.

| | What | Found by |
|---|---|---|
| D033 | The streamed world grew by a thousand instances every fifteen seconds and never shrank; 100 fps to 35 over five minutes | A human playing the deliverable |
| D034 | `--rhi=null` mounted no content: 289 chunks became eleven instances and the soak passed in 0.17 s | Wiring D033's gate |
| D035 | A refused connection waited out its whole timeout on Windows — present since M3 | `@std/net`'s first request |
| D036 | The frame loop's poller starved the network worker thread | `@std/net`'s first request |
| D037 | The job system deadlocked on shutdown and hung the local gate | The gate hanging, and a minidump |
| D038 | `SDL_LoadFileAsync` opens the file synchronously: 5–141 ms on the frame thread | The soak gate's first honest run |
| D039 | `ContentMounts::resolve` read every file in full to decide it existed, then read it again | The same measurement |
| D040 | **Header changes rebuilt nothing.** Every incremental Windows build had been silently reusing stale objects for the whole project | Someone finally touching a header and reading the output |
| — | M6 Finding 17, recorded four times as a pattern to remember, WAS D040 | — |

### What this milestone does not have

Stated here so a reviewer does not have to find it by looking.

- **No shipped example carries a mesh dense enough to have a LOD chain**, so the
  selector answers level zero everywhere and `MeshLodDraws` reads zero. The
  arithmetic is unit-proven and the plumbing has a differential; what is missing
  is content, and `MeshLodDraws` is the instrument that will show it the day
  there is some.
- **`@std/net` is client-side only.** `net.serve` does not exist and is reserved,
  with the reasoning at the top of `net_module.cpp`. `https` is refused rather
  than downgraded: this build vendors no TLS library.
- **`ITransport` has one implementation.** GameNetworkingSockets stays unvendored
  with a manifest row that says why.
- **Recast is vendored and has no build target at all**, which is the scope
  bullet. `engine/nav` is a header and a stub that proves the seam compiles.
- **The exotic importer does not import images.** A material's factors survive;
  its maps do not.
- **Skinned meshes take LOD zero only**, because joint weights are indexed by
  vertex and the simplifier drops vertices.
