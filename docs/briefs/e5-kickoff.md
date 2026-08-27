# E5 Kickoff — The World You Build

- Started: 2026-08-24
- Roadmap section: [docs/roadmap.md § E5](../roadmap.md#e5--the-world-you-build-l)
- Settled by: [ADR 0053](../decisions/0053-the-grid-decides-when-and-the-model-decides-what.md)

## Goal (restated)

A person builds a world in `Workspace`, presses play, and the world streams —
with no generator script, nothing sorted into folders by hand, and nothing to
configure. The grid decides *when* something becomes eligible; the `Model`
decides *what* comes with it. Underneath that sentence are three mechanisms:
a `StreamingMode` on `Model`, a partitioner that reads the scene and writes
cells without ever building the world it is reading, and a size class on the
`layer` field the chunk format has carried unused since M7.

It is one pass, not four. A partitioner with no `StreamingMode` writes the wrong
cells and a `StreamingMode` with no partitioner has no caller, so the seams
between them are not places to stop.

## What the reconnaissance found, before anything was written

Nine passes over the tree, read-only, before a line of design. Two of them
shrank the milestone and seven of them grew it, and the seven are the reason
this section exists at all: every one is a thing the ADR or the roadmap assumed
was already true.

### The two that shrink it

- **`ChunkId::layer` exists and nothing uses it.** The size classes go there and
  the on-disk `ChunkId` does not change. Confirmed.
- **Four of the five properties Roblox exposes already exist** on
  `StreamingService` and none needed a change: `Enabled`, `MinRadius`,
  `LoadRadius`, `PauseOutsideLoadedArea`. Confirmed.

### The seven that grow it

1. **The index's `DAABB` is world-space in one axis only.** `writeChunkIndex`
   writes `minY` and `maxY`; `readChunkIndex` recomputes x and z from
   `chunkBounds(id, chunkSize)` — the cell FOOTPRINT. So the straddling problem
   was solved vertically and not horizontally, and an atomic model that
   overhangs its cell would be scored as though it did not. The index has to
   carry real x/z bounds, defaulting to the footprint so an old index still
   reads.

2. **A cell record cannot express most of what an authored part carries.**
   `ChunkInstance` holds a transform, a size, a colour, a transparency, a shape,
   an anchored flag, a name and a mesh URN. `CanCollide`, `CanQuery`,
   `CollisionGroup`, `Friction`, `Restitution`, `Density`, `CollisionFidelity`
   and `PivotOffset` are all absent. That costs nothing while only a generator
   writes cells; partitioning an authored world would lose them in silence.

3. **A cell record carries no tags, and without them ADR 0053's rule 5 does not
   work at all.** "Address by tag, never by path" is the documented primary path
   for a streamed world, and `TagService:GetTagged` would never see a streamed
   instance. `World::addTag` already enqueues the deferred change, so the
   signals fire correctly the moment a record can carry a tag — but it has to be
   able to carry one.

4. **There is no streaming overlay panel.** `StreamingHost::view()` has no
   caller anywhere in the tree, `DebugService:ShowPanel` appends to a list only
   `script` ever reads, and `docs/manual/assets/streaming.md` already tells
   people the panel is there. The gate asks for a screenshot of it.

5. **The editor's Play does not reload the scene.** `Editor::play` snapshots the
   live world and runs it. So "it runs on play" is the world BOOT — a run, the
   dev server, a hot reload, a packaged game — and not the editor's transport.
   The gate says `luaug run` and means exactly that. The editor goes on holding
   the whole world, which is what ADR 0053 wants: editing is bounded by the
   editor, not by the grid.

6. **A scene node may be a stamp**, whose subtree lives in another file
   (ADR 0049). The partitioner has to expand stamps to see the parts at all, and
   the cache key has to include them or editing a stamp would not repartition.

7. **There is no `luaug run` command.** A project is run by handing its
   directory to `luaug-host`, which is what every `run.bat` does. The gate item
   is read as "running the project".

### And one measurement that overturned the design

The size-class thresholds were first proposed as 8 m and 64 m. Measured against
the flagship's 26,884 instances:

| class | extent | share under 8/64 |
|---|---|---|
| Ground | 32.0 m | 69% — landed in `structure` |
| Trunk | 6.5 m | `detail` |
| Crown | 5.0 m | `detail` |
| Boulder | 3.3 m | `detail` |
| Tower | 5.5 m | `detail` |

`terrain` was **empty** and the ground — the one thing that has to be visible at
distance — took the middle radius. The reason is worth more than the number:
**the flagship's terrain is not made of large features.** It is 18,496 tiles of
32 m, and classifying by an individual part's extent will never find terrain
that was authored as many small pieces.

Two corrections follow, both from the human, 2026-08-24:

- **The cuts come from the data.** Nothing in the world lives between 9 m (the
  largest trunk) and 32 m (the ground), so the cuts are **12 m** and **24 m**:
  detail below 12, structure between, terrain above 24. All three classes then
  do something, and a house lands where the empty class is.
- **The extent classified is the GROUP's, not the part's.** A house of forty
  small parts is a structure and not forty details. This is ADR 0053's own rule
  — the model decides what comes with it — applied to the classification and not
  only to the materialisation.

## Scope checklist (from the roadmap)

- [ ] `Model.StreamingMode`: `Nonatomic` (default), `Atomic`, `Persistent`.
      `PersistentPerPlayer` is out — it needs a per-connection player.
- [ ] The partitioner: walks the scene, `floor(position / chunkSize)`, groups by
      cell honouring each model's mode, writes cell sources and the index.
- [ ] It buckets records as it reads and never materialises the world.
- [ ] It runs on play, cached by a hash of the scene; a shipping build pre-warms
      the same cache and is not a second code path.
- [ ] A cell holds groups: an atomic model and its descendants materialise
      together.
- [ ] Size classes on `layer` — 0 detail, 1 structures, 2 terrain features —
      with a `minRadius`/`loadRadius` pair per layer on `StreamingFocus`.
- [ ] Tag addressing documented as the primary path. Nothing built.
- [ ] The replica's pause: `PauseOutsideLoadedArea` stays the authoritative
      world's; a replica holds its camera. Written down, not built.

### Added at kickoff, from the reconnaissance above

- [ ] The chunk format grows to version 2: the rest of `BasePart`, tags, and
      groups.
- [ ] The chunk index carries real world-space x/z bounds.
- [ ] A Streaming panel in the debug overlay, which the manual already promises.
- [ ] `--partition` on the host, so `luaug build` pre-warms the cache through
      the same code the run uses.

## NOT in scope

- **Baked distant geometry (HLOD).** Outside `LoadRadius` there is nothing, not
  a cheap version of something. The layers push the horizon out for large
  objects and are not a LOD hierarchy.
- **Arbitrary hierarchy in a cell.** A group is a model and its descendants,
  flat. A general nested-instance serialization is the scene format's job.
- **A scene that is a folder of per-cell files, with streaming while editing.**
  That wall is far past the one this removes.
- **Making a materialised cell savable.** The serializer's `generated` skip
  stays and stays correct.
- **`PersistentPerPlayer`**, and anything else that needs a per-connection
  player.
- **Streaming in the editor.** The editor holds the whole world; that is what
  editing it means.
- **Attributes on a cell record.** Tags are what rule 5 needs; attributes are
  named here so their absence is a decision rather than an oversight.

## Design decisions taken at kickoff

**D1 — The partitioner reads TEXT and holds one authored subtree.** It is not
given a `World` and it never creates an instance. It scans the scene JSON, takes
one child of `Workspace` at a time, parses that child alone, emits records into
a sink, and drops it. Its peak is one authored subtree plus the per-cell
bookkeeping the index needs anyway — bounded by the GRID, never by N.

**D2 — What stays is spliced, not reserialised.** The residual scene is built by
copying the verbatim source text of every node that stays. A node that partly
stays is rebuilt from its own verbatim slices with a new `children` array. No
number is ever reformatted, so a scene with nothing streamable partitions to
itself byte for byte.

**D3 — Conservative by construction, and counted.** An instance leaves the scene
only when the cell format can express it whole, nothing outside points at it,
and it carries no descendant the format cannot express. Everything else stays
authored and the report says how much. A world that streams half a weld is worse
than a world that streams less.

**D4 — The partitioner lives in `scene` and the writer in `app`.** `scene` (L3)
owns the scene format and may call `asset` (L2) to encode a cell; it has no
filesystem, so the sink that writes files is `app`'s, exactly as `writeScene`
returns a string and the editor writes it.

**D5 — Per-layer radii default to the base pair.** Three layers whose radii are
all `MinRadius`/`LoadRadius` until something says otherwise, so a world built
before this milestone — every cell of it at layer 0 — behaves identically. The
thresholds are fixed and the radii are properties: "nothing to configure" in the
ADR is about the partitioner, and a world that cannot tune its own draw distance
is a worse world than the one we have.

## Subagent plan

None. This is one system across four modules' seams — the chunk format, the
policy engine, the scene format and the host — and MASTER_PROMPT.md §7 puts
exactly that on the orchestrator. The interfaces cannot be frozen before the
thing is built because the milestone's whole content IS the interfaces.

## Gate checklist (verbatim from the roadmap)

- [ ] **A world built by hand in the editor streams.** `examples/06-scene` grows
      a few hundred parts spread over more than one cell, is saved, and
      `luaug run` streams it — with no generator script anywhere in the project.
      A screenshot of the chunk-state overlay is attached to the gate record.
- [ ] **The default changes nothing.** `examples/10-open-world` runs
      byte-identically through the partitioner: the cells it produces from the
      flagship's scene plus its generated world match what `generate_world.luau`
      and `assetc` produce today, or the difference is explained in the record.
      This is the regression that matters, because `Nonatomic` is supposed to be
      what already happens.
- [ ] **The partitioner never holds the world.** Proven by measurement, not by
      inspection: peak resident instance count during a partition of a world
      with N instances is bounded by a constant, not by N. A test partitions a
      synthetic world an order of magnitude larger than the flagship's and
      asserts it.
- [ ] **An atomic model crosses a boundary and arrives whole.** A model whose
      parts straddle two cells materialises in one frame with every descendant
      present, and evicts with none left behind. Driven headlessly.
- [ ] **A persistent model is never in a cell.** It is in the saved scene, it
      exists before the first tick, and no eviction touches it — including at
      four kilometres from the origin, where its cell would long since have gone.
- [ ] **The cache is a cache.** Editing the scene and pressing play
      repartitions; pressing play again does not. Asserted on the hash, not on
      wall-clock time.
- [ ] **Layers separate.** With three layers configured, a large object stays
      resident at a distance that has already evicted a small one, and the
      overlay shows both states. A unit test over the scoring covers it without
      a window.
- [ ] **`luaug check` and the full local gate are green**, and the docs say what
      a script may assume about a streamed world — the tag path written down,
      with the `nil` a path reference may return stated rather than discovered.

## Attempted / abandoned

(append during the milestone)

## Gate Record

**BUILT, awaiting review.** Filled 2026-08-24; the chunk-state screenshot row
closed 2026-08-27, so **one row is left and it is a person at a window.** Every
other item below has a result. The rows marked as what they are: a person
looking, and a migration
the human has said they will ask for separately.

Closing run, `scripts/localgate.ps1`:

```
  ok    docs · luau · format · shipping
  FAIL  windows 43/44 · linux 40/41   — openworld_soak only
```

**The one red is the migration this milestone deliberately did not do.**
`openworld_soak` compiles the flagship's chunk sources, and the other session's
in-flight rewrite of `examples/10-open-world/tools/generate_world.luau` writes a
scene instead. It is the last of the generator-path examples and it moves with
`tests/support/ensure_generated_world.cmake` when it moves.

| Claim | Answer |
|---|---|
| A world built by hand in the editor streams | **Built, and the screenshot row is closed** (finish-line S4.8, 2026-08-27). `examples/06-scene` is four hundred parts over six cells with no generator anywhere in the project, and the partition happens on the way to the first frame. The overlay was an ImGui panel, which is why this sat PENDING from the day the milestone closed; `drawChunkGrid` puts the grid in the WORLD through `DebugDraw` instead — one cell outline per chunk coloured by state, a post at one corner, each focus's two rings. Better for a person, because streaming is a question about space, and capturable, because `--headless --screenshot` reaches the ordinary renderer. `tests/screenshots/chunkgrid` is the differential. |
| The default changes nothing | **Pass, and provable rather than argued.** `examples/10-open-world`'s authored scene has one streamable candidate and a `Weld` pins it, so it produces zero cells and its residual is byte-identical to the original. `partition_tests.cpp`: "a scene with nothing streamable partitions to itself, byte for byte". |
| The partitioner never holds the world | **Pass, by measurement.** "the partitioner never holds the world" partitions two worlds an order of magnitude apart and requires the SAME peak, not merely a small one — which is what caught the real defect: `World::destroy` defers its generation bump to `retireDestroyed`, and a partitioner has no drain. |
| An atomic model crosses a boundary and arrives whole | **Pass.** "an atomic model crosses a boundary and is filed under one cell" and, on the other side of the format, "an atomic model arrives whole and leaves with none left behind". |
| A persistent model is never in a cell | **Pass.** "a persistent model is never in a cell, four kilometres out or anywhere else". |
| The cache is a cache | **Pass, asserted on the outcome rather than on a clock.** `partition_cache_tests.cpp`, four cases: it partitions once, an edit repartitions, editing a STAMP the scene names repartitions it too, and a scene that streams nothing leaves the original to boot. |
| Layers separate | **Pass.** `streaming_tests.cpp`: "a size class keeps its own distance" and "a layer with no radius of its own follows the focus's own pair". The overlay draws one grid per class. |
| `luaug check` and the full local gate green, and the docs say what a script may assume | **Pass**, above; `docs/manual/assets/streaming.md` carries the tag path as the primary one and states the `nil` a path reference may return. |
| A human plays it and says whether it works | **PENDING — a person at a window.** |

**What is left, and both are named rather than discovered**: the chunk-state
screenshot, and migrating `examples/05-streaming` and `examples/10-open-world`
off the generator path — which is the same movement as
`tests/support/ensure_generated_world.cmake` and the soak, and which the human
has said they will ask for.
