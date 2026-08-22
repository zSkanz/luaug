# 06-scene — a world that came from a file

Every example before this one builds its world in code: `init.luau` runs at boot
and calls `Instance.new` until there is something to look at. **This one does
not.** Its world is `content/scenes/main.scene.json`, and `src/scripts/init.luau` is only what
the world *does* — one handler that turns whatever is wearing the `Spin` tag.

That split is **ADR 0047**, taken at the visual editor's first review and in the
reviewer's own words: *the way I want is how the big engines do it, Unity,
Unreal*. The authored world is data, scripts are behaviour, and the editor is
what authors the data.

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

## Running it without the editor

```
run.bat
run.bat --headless --frames=30 --exit --screenshot=out.png
```

The same world either way: the host loads the scene whether or not anybody is
editing it.

## What this example is deliberately not

It is not pretty and it is not a game. It is the smallest world that proves a
file can hold one — six instances, one of them the camera. The flagship
(`examples/10-open-world`) still builds its world in code and still works; ADR
0047 does not deprecate that, and `Instance.new` at runtime stays first-class
the way it is in Unity. What moved is where the world a project *starts* with is
written down.

## One rough edge, stated rather than discovered

The script's file-scope `print` reports **zero** tagged parts, because at boot
the scene has not been loaded yet — `WorldHost::boot` starts the scripts, and
the scene is applied after it. The `Heartbeat` handler sees all three, which is
why the blocks turn. ADR 0047's lifecycle is load-then-start and this engine's
is start-then-load; closing that gap is what the milestone after this one owes.
