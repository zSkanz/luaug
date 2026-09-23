# 19-terrain-test

The controlled cases of the owner's terrain report, side by side: flat ground,
a slope, a hill, a ball added, a ball taken away, a tunnel, a cave, a vertical
wall, an overhang, and a ball across a chunk corner. Each stands on its own
block of ground, so its walls and its bottom show as well as its top.

```
run.bat
```

- **The camera tours every case** from above, from the side and from below,
  and loops.
- **To look at the mesh rather than the picture**, set `ShowMesh = true` in
  `src/scripts/init.luau`, or open the project in the editor and turn on
  `Terrain wireframe` and `Terrain normals`. A triangle is green when it faces
  the way its normals do and red when it does not; a yellow line runs along
  each vertex normal, which is what the shader lights by.
- **What the tests prove about the same cases** is in
  `engine/asset/tests/terrain_mesh_validation_tests.cpp`: no degenerate or
  duplicated triangle, no open or over-used edge, one winding everywhere, and
  every triangle facing the way its normals do.
