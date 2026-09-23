# 17-cave

A cave dug into the side of a mountain: `Terrain` as a volume, not a height map.

```
run.bat
```

- **The mountain is cheap ground.** Balls added to the top of the plain only
  move its surface up, so it stays one height per column.
- **The tunnel is a chain of `FillBall(..., 0)`** from outside the rock to deep
  inside it: rock above, air in the middle, rock below. Those columns, and
  only those, become voxels. This is what the editor's dig brush does when
  it is aimed at a wall.
- **The chamber** at the end is two wider balls with a sand floor put back, and
  **a shaft** from the summit lets daylight in.
- **It is dark inside.** A cave's mesh records how much sky each point sees,
  and the sky's light is scaled by it, so the crystals and the torch are
  what light the chamber. The sun is kept out by its shadow map.

The camera flies from the plain through the mouth into the chamber and back,
so the whole shape can be seen without a mouse.
