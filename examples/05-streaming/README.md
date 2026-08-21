# `examples/05-streaming` — the M7 deliverable

A world larger than the engine keeps resident, flown over by a camera that is
the streaming focus. Nothing in the script builds the world: it is on disk as
chunks, and what the script does is say where to look and how far to keep
loaded.

**The point of this example is what you cannot see.** A world that streams
correctly and one that pre-loaded everything look identical for the first
minute; the difference is the memory ceiling and what happens at minute five.
That is why the overlay is not decoration here — the chunk-state panel and the
memory graph are the only way a person can tell which of the two they are
looking at.

Run it:

```
examples\05-streaming\run.bat                    # windowed
examples\05-streaming\run.bat --headless --frames=600 --exit --screenshot=out.png
```

## The world is generated, not committed

`tools/generate_world.luau` writes the chunk SOURCES and `luaug build-assets`
compiles them into `.lchunk` payloads. `run.bat` does both when the output is
missing, so a fresh clone works without reading this first.

What is committed is the forty-line generator. The roadmap's own deliverable
line asks for "no giant binary assets in the repo", and a megabyte and a half of
generated JSON is the same thing wearing a text extension.

The generator is deterministic — every value comes from a hash of the cell's own
coordinates — so the world is the same on every machine and, more to the point,
a chunk looks the same however many times it streams in and out. A chunk whose
contents changed on reload would make eviction visible, which is exactly what
streaming exists to hide.

## What to watch

- **The chunk panel.** Cells go `unloaded → loading → decoded → resident` and
  back. The flight is a circuit rather than a straight line precisely so that
  chunks are left behind and returned to: eviction and re-entry are the halves a
  straight line never exercises.
- **Frame time.** The materialisation budget is two milliseconds and it is
  denominated in TIME rather than in a count of chunks, because a chunk's cost
  varies with what is in it. The gate is stated the same way: zero hitches over
  33 ms attributable to streaming.
- **Memory.** It should reach a plateau and stay there. A number that climbs is
  eviction not giving the bytes back, which is the failure the ceiling exists to
  catch.
