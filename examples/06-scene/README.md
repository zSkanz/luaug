# 06-scene — a world that came from a file, and came apart into cells

Every example before this one builds its world in code: `init.luau` runs at boot
and calls `Instance.new` until there is something to look at. **This one does
not.** Its world is `content/scenes/main.scene.json`, and `src/scripts/init.luau`
is only what the world *does*.

That split is **ADR 0047**, taken at the visual editor's first review and in the
reviewer's own words: *the way I want is how the big engines do it, Unity,
Unreal*. The authored world is data, scripts are behaviour, and the editor is
what authors the data.

## And it streams, with no generator anywhere in the project

The scene holds nearly four hundred parts across three quarters of a kilometre,
and **nothing sorted them into cells**. The engine partitions the scene on the
way to the first frame — cached by a hash of the file, so pressing play twice
costs the work once — and `luaug build` warms the same cache into the artifact.

That is **ADR 0053**: the grid decides *when* something becomes eligible and a
`Model` decides *what* comes with it. Two models in here say so out loud:

- **`Gatehouse`** is `Atomic`. Its piers sit either side of the `x = 256`
  boundary, so a per-part partition would file them in two cells and the gate
  would arrive in halves. It goes in one cell and materialises whole.
- **`Beacon`** is `Persistent`. It is four kilometres out, where its cell would
  long since have gone, and it is there before the first tick and never leaves.

The ground tiles are 64 m and land in the **terrain** class; the pillars are 2 m
and land in **detail**; the gatehouse is 20 m and lands in **structures**. The
script gives each class its own radius, which is what lets the ground stay
visible long after the pillars on it have gone.

**Press F3 while it runs** to see the streaming map: one grid per class, a
square per cell, green for resident.

## The loop this example exists to demonstrate

```
scripts\luaug.ps1 edit examples/06-scene
```

1. Find `scenes/main.scene.json` in the **content** panel and double-click it.
   The content directory is the asset manager and a scene is one of the assets
   in it, so this is where scenes are opened from.
2. Click a block in the viewport. The explorer highlights it and the properties
   panel fills.
3. Change something — a colour, a size, a position.
4. Press **play**. The world ticks and the tagged blocks turn.
5. Press **stop**. The world goes back to exactly where you pressed play, *with
   your change still in it*. That is what makes testing free.
6. Press **save**. The scene you have open is rewritten — that one, not a fixed
   name, which is the difference between a project with scenes and a project
   with a scene.
7. Close the editor and open it again. Your change is there.

Step 4 is the one that matters. A tool where testing your work costs you your
work is one nobody uses twice.

**The editor holds the whole world**, cells and all, and that is deliberate:
holding it is what editing it means. The grid is something a run applies, not
something you edit through.

## Running it without the editor

```
run.bat
run.bat --headless --frames=30 --exit --screenshot=out.png
```

The camera walks east and comes back, so cells arrive and leave while you watch.

## What a script may assume, which is the one thing streaming changes

`workspace.Pillar` is a path that is sometimes `nil`, because the thing it names
may not have arrived — and waiting for it does not help. `init.luau` asks
`TagService` instead, and connects `GetInstanceAddedSignal` to hear about what
arrives. That is the documented primary path for a streamed world, and the
`Landmark` line it prints is a cell coming into range.

## What this example is deliberately not

It is not pretty and it is not a game. It is the smallest world that proves a
file can hold one, and now the smallest one that proves a file can hold a world
too large to hold all at once.
