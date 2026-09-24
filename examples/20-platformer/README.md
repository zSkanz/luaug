# 20-platformer

A platformer on the 2D layer (phase 3): a level painted on a `Tilemap2D`, a
hero that runs and jumps on it, coins to collect and a flag at the end.

```
run.bat
```

A/D or the arrows run, Space jumps, and a gamepad works too.

- **The level is the ASCII map at the top of the script.** `#` is ground --
  grass where open sky is above it -- `B` brick, `S` stone, `o` a coin, `P` the
  start and `F` the flag. Change the text and run again, or open the project in
  the editor and paint with the Tiles tool.
- **The ground is one body per region**, the outline of its solid tiles, so the
  hero runs across a floor of forty tiles without catching on a seam.
- **The controller is where the feel is**: speed ramps rather than snaps, a
  jump still works a tenth of a second after running off a ledge (coyote time),
  a jump pressed just before landing is kept (a buffer), and letting go early
  cuts the rise short. Grounded is three short `Raycast2D`s under the feet, so
  standing on the lip of a ledge is still standing.
- **Coins and the flag are sensors**: they report the hero through `Touched`
  and push nothing.
- **The camera is orthographic** and eases ahead of the hero, stopping where the
  level stops.

The art is drawn by `tools/make_art.py`, so the example carries no asset with a
licence to track: `python examples/20-platformer/tools/make_art.py` rewrites it.
