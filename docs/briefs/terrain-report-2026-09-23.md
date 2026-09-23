# The terrain report, answered (2026-09-23)

The owner sent a technical report on the terrain, written as a second opinion
from watching a video of the editor. It said itself that every point was a
hypothesis to check against the code, not a finding. This file checks each one
and gives the verdict and the evidence:

- **Refuted** means the code does not do what the hypothesis feared, and a test
  says so.
- **Confirmed** means it did, and the fix is named.
- **Partly** means both, and says where.

The video predates the voxel rewrite's later fixes: the edge slab, the sky
rays, the brush tools. Some of what it showed was already gone before this
check began.

## The instruments

- **`engine/asset/tests/terrain_mesh_validation_tests.cpp`** meshes the report's
  own controlled cases whole, region by region as the renderer and the colliders
  cut them, and checks the union as one surface. The cases are flat ground, a
  slope, a hill, a ball added, a ball taken away, a tunnel, a cave, a vertical
  wall, an overhang and a ball across a chunk corner. The checks:
  - no degenerate triangle;
  - no duplicate;
  - no open edge and no edge used three times;
  - every shared edge traversed once each way;
  - every triangle facing the way its vertex normals do;
  - every normal of unit length;
  - a bound on how thin a triangle may be.
- **Debug views**:
  - **Terrain wireframe**: triangles drawn green, or red where one faces the
    wrong way.
  - **Terrain normals**: a yellow line along each vertex normal.
  - Both are in the editor's viewport settings, and both are shown by
    `DebugService:ShowPanel("Terrain")` in a game.
- **`examples/19-terrain-test`**: every case on its own block of ground, toured
  from above, the side and below, with the mesh one flag away.

## Verdicts, in the report's order

| # | Hypothesis | Verdict | Evidence and fix |
|---|---|---|---|
| 2 | Surface extraction produces an inconsistent surface: holes, duplicate faces, degenerate triangles, internal surfaces | **Refuted** | All eleven cases are closed, with no duplicate and no degenerate triangle. Internal surfaces exist only where there is air inside ground (a cave), which is correct. |
| 3 | Some triangles are wound backwards | **Refuted** | The mesher does not take winding from a table. Each triangle is compared with the field's own gradient and turned to agree. The validation test finds every shared edge traversed once each way, and no triangle facing away from its normals. The wireframe view draws such a triangle red, and none is. |
| 4 | Triangulation shows through the shading | **Partly** | The normals are smooth (see 5), so shading does not follow triangles. What could show was the quad split. It always took the same diagonal, which made long thin pairs along sharp rims. **Fixed**: quads split along the shorter diagonal. |
| 5 | Face normals instead of smooth vertex normals | **Refuted** | Vertex normals are the gradient of the occupancy field by central differences, not an average of faces, so they are smooth across triangles by construction. |
| 6 | Normals disagree across chunk boundaries | **Refuted** | Two neighbouring regions compute the shared ring of cells from the same samples. The vertices and normals on a boundary are therefore the same numbers on both sides. `terrain_mesher_tests.cpp` asserts no crack across a boundary, and the chunk-corner case here asserts the whole surface is closed. |
| 7 | Cracks and seams, especially after edits | **Refuted for one level of detail, covered at the transition** | At full detail the surface is closed (above). Between two levels of detail a crack of up to a coarse cell exists by the nature of level of detail. It is covered by skirts hung behind every region boundary. |
| 8 | Stretched triangles | **Confirmed, fixed** | Measured as the longest side over the height to it. Before the fix the worst case reached 6. After the shorter-diagonal split, every ordinary case is under 3.6. Two special shapes still make a few thin triangles, both named in the test with the reason. One is a ball floated in a hole, where the union's crease crowds vertices: 2 in 12,292. The other is a surface passing exactly through voxel centres: 24 in 12,316. Both are closed and correctly wound, and shaded by the gradient. |
| 9 | Z-fighting from duplicate or coplanar surfaces | **Refuted** | There are no duplicate triangles. A region owns each quad exactly once, so two regions never both emit one. |
| 10 | Hidden faces inside solid ground | **Refuted** | A surface is emitted only where occupancy crosses one half. A node meshes only the chunk layers that can hold a crossing (`activeRuns`), never the rock between. |
| 11 | Marching Cubes table errors | **Not applicable** | The mesher is surface nets, one vertex per cell and a quad per crossed edge. There is no case table to have an entry wrong, and no ambiguous cases to resolve. |
| 12 | Digs look rounded, "melted" | **By design, stated** | Occupancy ramps across four voxels (`RampVoxels`, ADR 0082). It was measured, because a one-voxel ramp turned every hillside into a staircase. A ball also stays round rather than blocky. |
| 13 | Caves go abruptly to black | **Confirmed, fixed** | Two causes. The sky term marked every point below any ground as under a roof, which stood dark stripes down walls; it now marches rays (ADR 0082, amended). And `Lighting.Ambient` was multiplied by the sky term, so it was zero inside a cave: the one place a stand-in for bounced light exists for. **Fixed** by ADR 0084: `Ambient` lights enclosed spaces, `OutdoorAmbient` lights open ones, and a surface takes a blend by how much sky it sees. A cave is dim, not black. |
| 13b | (The owner's second picture) the shadow under the overhang's ball looks wrong | **Confirmed, fixed** | Not the shadow map: the teeth stayed with the sun's shadows off. The per-vertex sky term weighed only the upper sky and gave any surface facing down a fixed half, so the rim under the ball alternated between the two rules from vertex to vertex, and the triangles between drew teeth. **Fixed**: rays over the whole sphere, every direction weighed by how squarely the surface faces it (ADR 0082, amended), so the value turns smoothly as the normal does. |
| 14 | Lighting is only `max(dot(N, L), 0)`, no ambient or skylight | **Refuted** | Lighting is the sun with cascaded shadows, clustered local lights, image-based irradiance and reflection from the sky, screen-space ambient occlusion and a flat ambient. What was wrong was 13: the ambient zeroed in caves. |
| 15 | Geometry bugs mistaken for lighting bugs | **Heeded** | The geometry was proved first (above). Only then was the lighting changed, and only where the geometry was right and the light was wrong. |
| 16 | Material shading is basic | **Deferred** | Out of this report's scope until geometry and light were trustworthy, as the report itself ordered. The terrain has per-vertex materials, triplanar detail and a slope rule today. |
| 17 | Debug views | **Built** | Wireframe coloured by winding, and vertex normals. The validation test covers the rest of the list in numbers rather than pictures: degenerate, duplicate, backface, open edges. |
| 18 | A test scene of controlled cases | **Built** | `examples/19-terrain-test`. |

## What the report got right that nothing else had said

Item 13, and item 15's warning with it. The black caves were real, and the
picture made them look like a geometry problem: black regions and vanishing
surfaces. It was a lighting rule. The report said to prove the geometry before
touching the shader, and doing it in that order is what showed which one was
wrong.
