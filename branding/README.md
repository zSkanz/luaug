# branding/ — the LuauG logo and icon

Public-facing branding is a human decision (`MASTER_PROMPT.md` §10), which is
why these live at the top level rather than buried under `docs/`.

| File | What it is |
|---|---|
| `luaug-logotipo-original.svg` | The artwork as delivered, on its green plate. Source of truth; the two below are derived from it. |
| `luaug-logo.svg` | The wordmark, transparent, cropped to its own bounds. |
| `luaug-mark.svg` | The `</>` symbol alone, square. This is the icon. |
| `luaug-logo-512.png` | Raster wordmark for README and docs. |
| `icon/luaug-{16..256}.png` | The symbol at each size an OS asks for. |
| `icon/luaug.ico` | The same set as one Windows icon resource. |

## Why the icon is the symbol and not the wordmark

The wordmark is 3.2:1. Rendered into the square an OS icon actually gets, it is
a smear at 16 px and marginal at 32 px — the `</>` above the letters disappears
first, which is the part that says what this is. The symbol reads at every size
down to 16 px. So the wordmark is for READMEs and docs pages, and the mark is
for windows, taskbars and installers.

## Removing the green was not deleting one shape

The original is a traced bitmap, not drawn vectors: 31 paths, ~30 near-identical
colours, and coordinates carrying eleven decimal places. The green plate is one
path — but the counters of the letters (the enclosed space inside `a`, inside
`u`) were traced as their own green shapes, because in a bitmap that space *is*
background. Deleting only the plate leaves a green block sitting inside the `a`.

So every green-family fill is removed, classified by `g > r and g > b` with
near-white excluded, and the blues and whites are kept byte-for-byte from the
original. **If the logo is ever re-traced, this has to be redone** — a fresh
trace will bring its own fringe colours.

## Where these get used

Nothing consumes them yet. Wiring them is **M8's application-identity scope**
(see `docs/roadmap.md` § M8), and it is scheduled there rather than done early
for a reason: the load-bearing half is not the engine's own icon, it is that a
game built with `luaug build` carries *its* icon and not ours. That needs
`luaug.toml` to have an `icon` key and `luaug build` to exist, and both are M8.

The cheap half — the dev host's own window wearing the mark — can land any time
`engine/app` is open anyway. It is `SDL_SetWindowIcon` over `stb_image` on the
embedded 64 px PNG, and it is worth doing before the first docs screenshots are
taken, because a default SDL icon in a README reads as unfinished.

## Regenerating the rasters

The PNGs and the `.ico` were produced from `luaug-mark.svg` and
`luaug-logo.svg` by flattening the Bézier segments and filling them, then
downsampling with a Lanczos filter, and they are checked in rather than built.

That is deliberate: rasterising SVG at build time needs a rasteriser, and this
repository pins every tool it depends on (R5) — adding one for six icons would
be a dependency ADR for an artifact that changes when the logo changes, which is
approximately never. The same trade `ADR 0032` makes for DXC: commit the
artifact, write down where it came from.
