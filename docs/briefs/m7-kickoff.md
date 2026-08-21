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
- [ ] **assimp as the offline-CLI-only importer** for exotic formats,
      normalizing to glTF 2.0 — never linked into the runtime
- [x] **Engine job system + async IO**
- [ ] **64-bit world coordinates + floating origin** (render-relative
      translation; origin/rebase is per-World state, never a global — ADR 0014)
      — enforced by tests
- [ ] **Chunked world format + StreamingService semantics** (load/unload radius
      around foci, priority queue, budgeted per-frame materialization)
- [ ] **Basic LOD switching**
- [ ] **Low-level net primitives**: GameNetworkingSockets + ENet behind the
      transport interface, exposed as the minimal socket/HTTP surface via
      `@std/net` (loopback echo example only — replication is post-v1)
- [ ] **Recast/Detour**: vendored, seam defined, **no integration** (explicit
      non-goal, ADR 0022)
- [ ] **The default typeface ships here**: Inter, OFL 1.1, vendored with its
      licence in `THIRD_PARTY_NOTICES.md` (R6). `TextLabel.Font` stops being
      `Inert` and the marker goes with it
- [ ] **`CharacterBody:Jump()` stops refusing in mid-air** (human decision,
      2026-08-21), with the doc sentence about calling it every frame, and the
      three call-site migrations
- [ ] **Deliverable:** `examples/05-streaming` — a procedurally generated large
      world (no giant binary assets in the repo), fly-cam, ImGui chunk-state
      overlay, memory graph

### Carried into scope because the pipeline is what they were waiting for

The five remaining `Inert` markers name this milestone in their own docs
(`tools/repo/inertcheck.luau` is what keeps them honest), and four of the five
are `Inert` for exactly one reason: there was no way to hand the engine a file.

- [ ] `ImageLabel.Image` / `ImageButton.Image` — a real texture, so the flat
      tint becomes a picture
- [ ] `ImageLabel.ScaleType` and `SliceCenter` — meaningless until there is an
      image to scale or slice
- [ ] `ScrollFrame.ScrollBarThickness` — the one that is *not* an asset
      question; it is a bar nobody drew
- [ ] `MeshPart.CollisionFidelity` — a hull needs mesh geometry the physics
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

- [ ] scripted 5-minute fly-through: peak memory under the declared ceiling
- [ ] zero frame hitches >33 ms attributable to streaming (frame-time histogram
      asserted)
- [ ] float-precision test: object behavior at coordinate 1e7 identical to
      origin (hash comparison)
- [ ] asset build determinism check in CI
- [ ] loopback socket echo test
- [ ] pak round-trip fuzz test (truncated/corrupt pak, structured error, no
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
- **GameNetworkingSockets** is not vendored this milestone, and this is the one
  decision here that needs a human. Its dependency set is *OpenSSL or libsodium,
  plus protobuf* (`docs/research/ecosystem-2026.md:210`) — none of which has a
  manifest row, all of which would be new vendored dependencies (R5, R6, §10)
  bought for a seam with no v1 caller. ADR 0040 settled this shape one milestone
  ago: a dependency nobody calls still enters every build, every notices file
  and every future reader half hour. **The row stays, unpinned, with the ADR
  recording why** — and the human answers, exactly as they did for Clay.

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

## Gate Record

(filled at milestone end, before human review)
