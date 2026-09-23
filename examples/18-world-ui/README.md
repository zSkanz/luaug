# 18-world-ui

UI in the world: a `SurfaceGui` scoreboard on a wall and `BillboardGui` name
tags over spinning crates.

```
run.bat
```

- **The scoreboard** is a `SurfaceGui` on the wall's front face at fifty pixels
  to the metre, with rich text in its labels and a rounded panel behind them.
  Its `Brightness` is above one, so it glows a little, the way a screen does.
- **The name tags** are sized in pixels, so each one is as big on the screen
  near or far. The health bars are two `Frame`s.
- **The sign** by the post is sized in metres, so it shrinks with distance
  like the post it stands on.
- **Everything is in the world**: the crate in front of the wall hides the
  scoreboard behind it, and the tags are drawn back to front.
