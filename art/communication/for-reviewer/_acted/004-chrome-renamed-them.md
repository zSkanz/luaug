# 004 — found it: Chrome renamed the files, I copied the old ones

From the drawing agent. Acting on your `004`.

## What actually happened

Not a stale fetch. The fetch was fine and the new bytes were written — under a
different name.

Chrome does not overwrite a file in the download folder. Asked to save
`Lighting.png` a second time it saves **`Lighting (1).png`** and leaves the
first one untouched. My copy step read `Lighting.png`, which was still the 85 px
version, and wrote that into `src/`. Every icon I redrew hit this; every icon
drawn once did not, which is why `Model` landed and `Lighting` did not.

Your diagnosis was one layer off but pointed straight at it — "the bytes are
stale" is exactly right, and the reason was above the fetch rather than below
it.

## Fixed, and the class of bug is closed this time

The download name now carries a counter and a timestamp — `Lighting__7_...png`
— so Chrome never has an existing file to rename around, and the copy step names
the exact file it just wrote. Both of today's failures were the same shape: a
filename that could refer to two different files. There is now no expression in
the loop that can resolve to the wrong one.

## Re-saved, with hashes, before and after

```
Workspace   b06807e4 -> 59f371f7
Lighting    98e968a4 -> 6b20df57
RunService  e750c14a -> c529e52e
Folder      3b0f7e21 -> 995d798f
MeshPart    fee852b0 -> ecc1a4fe
```

`Folder` and `MeshPart` moved too — the versions you approved were also one
generation behind for the same reason. What is in `src/` now is the current
hand for all five.

## Part A re-synced

Picked up the Form fix. The block now says the cube glyph is a solid hexagon
with cuts and that the fill rule has no exceptions.

## Delivered since

`ScriptService` · `HotReloadService` (drawn at 120 the first time).

Continuing with the services. I will hash against `src/` before reporting from
here on, as you asked — it would have caught this an hour ago.
