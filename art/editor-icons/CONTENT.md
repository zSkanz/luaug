# Content-browser icons

The **third** set, and it exists because the editor grew a content browser while
the other two were being drawn. `src/` is what a scene *contains*, `actions/` is
what a person *clicks*, and [`content/`](content/) is what a project's folder
*holds*.

These are not classes and not actions. They come from one enum in the engine —
`ContentKind` in `engine/app/include/luaug/app/content_tree.h` — and there are
exactly six of them, which is the whole set:

```cpp
enum class ContentKind { Folder, Scene, Mesh, Texture, Chunk, Other };
```

**Same style block, unedited.** Same keylines, same 85 / 35 / 120, same fill
rule, same no text. Deliver into `content/`.

## The set

| File | Keyline | Subject line |
|---|---|---|
| `Folder.png` | — | **Do not draw this one.** It is the same folder as `src/Folder.png` and it should be the same file. A browser folder and a tree `Folder` are the same thing to a person, and drawing two would be two files pretending to differ |
| `Scene.png` | Wide | a solid rectangle with a **clapperboard's hinged top bar** above it, set at a slight angle and separated from the body by a clear gap — the film-slate shape. A scene is the one thing in this browser you *open* rather than *reference*, so it earns the most distinct outline |
| `Mesh.png` | — | **Do not draw this one either — it is [`src/MeshPart.png`](src/MeshPart.png).** It was specified as the hexagon `Part` already is, so it came back as `Part`, as it had to. But the right alias is not `Part`: a `.glb` in the content folder is exactly what a `MeshPart` points at, and the triangle already means *imported geometry* where the hexagon means *primitive solid*. The subject line was wrong, not the drawing |
| `Texture.png` | Wide | a solid rectangle with a triangular mountain and a small circular sun cut out of it. **This is `src/ImageLabel.png`.** If it measures as the same icon, it is the same icon — say so and I will point both names at one file rather than have you differentiate them artificially |
| `Chunk.png` | Square | a three-by-three grid of solid squares with the four corner squares removed. **This is `src/StreamingService.png`** — a chunk file is exactly what that service streams. Same question, same answer |
| `Other.png` | Tall | a document sheet with its folded corner at the **top LEFT** and nothing on its face. **The mirrored fold is the point:** specified with the fold on the right it is `Script` minus the chevron, and they measured 13.5% apart — two icons separated by an *absence*, which is the weakest distinction there is. A notch on the other side is a real outline difference and reads at 16 px |
| `Audio.png` | Square | a waveform: five or six upright bars of differing heights, tallest in the middle, symmetric about the centre. **Not a speaker** — `AudioService`, `AudioGroup` and `Sound` already carry the speaker and the note between them. A waveform is what a FILE of sound looks like, which is what this names |
| `Font.png` | Square | a solid capital A filling the keyline, with its counter cut out. **The one stated exception to the no-letters rule** (see `README.md`): the rule exists because a letter is usually a label standing in for a picture nobody drew, and here the subject IS letters |

## Two drawings, four aliases

```
Folder   ->  src/Folder.png
Scene    ->  content/Scene.png          new
Mesh     ->  src/MeshPart.png
Texture  ->  src/ImageLabel.png
Chunk    ->  src/StreamingService.png
Other    ->  content/Other.png          new
```

## Half of this set may not need drawing at all

Four of the six point at drawings that already exist. That is not laziness, it
is the correct answer: **a chunk file and the streaming service are the same
concept seen from two places**, and giving them two slightly different icons
would be inventing a distinction the user does not have.

Draw `Scene` first — it is the only one that is certainly new, and it is the
most important, because opening a scene is the first thing anybody does in this
editor.

Then draw `Mesh` and `Other`, and measure all six against `src/` before drawing
anything else. If `Texture` and `Chunk` come back as duplicates of icons we
already have, delete them and tell me.
