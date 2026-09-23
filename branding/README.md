# LuauG branding ? Orbit G

The refreshed engine identity, created 2026-09-23 alongside the Orbit editor
icons. An open circular G with a detached satellite replaces the earlier
crescent mark. Rounded geometry, mint accents and Inter typography connect
it to the editor icon family.

- [Identity overview](../art/branding/orbit/brand-board.png)
- [Visual gallery](../art/branding/orbit/index.html)
- [Exact design prompt and style rules](../art/branding/orbit/STYLE.md)

## Assets

| File | Purpose |
|---|---|
| `luaug-mark.svg` | Primary teal symbol, editable vector |
| `luaug-mark-{light,dark,white,mono}.svg` | Shared geometry in surface-specific or single-ink colors |
| `luaug-mark-512.png` | Primary transparent 512-pixel symbol |
| `luaug-mark-{light,dark,white,mono}-512.png` | Matching raster variants |
| `luaug-logo-512.png` | Transparent wordmark on a square canvas |
| `luaug-lockup-horizontal.png` | Default light-surface lockup, 1400 by 360 |
| `luaug-lockup-horizontal-{light,dark}.png` | Surface-specific horizontal lockups |
| `luaug-lockup-stacked.png` | Default light-surface lockup, 800 by 800 |
| `luaug-lockup-stacked-{light,dark}.png` | Surface-specific stacked lockups |
| `luaug-social-card.png` | 1280 by 640 repository/link preview artwork |
| `icon/luaug-{16,24,32,48,64,128,256}.png` | Application icon at native OS sizes |
| `icon/luaug.ico` | Seven PNG-compressed Windows icon entries |

Use the light variant on light backgrounds, the dark variant on dark
backgrounds. Use the standalone symbol for small application icons, and a
lockup wherever the full name should be visible. Never squeeze the wordmark
into a 16-pixel square.

## Source and reproduction

```sh
python tools/repo/draw_branding.py
```

The generator is the editable source of truth. It exports SVG geometry and
renders PNGs from the same coordinates. It requires Pillow, already used by
the icon art tools, and the vendored Inter font; no engine dependency is added.

The wordmark is exactly **LuauG**, set in Inter at weight 650, optical size 32,
tracking -2.5% em. Its letters share one weight and one color. Lockups are
exported as PNGs so they remain exact without installing the font. The symbol
is an authored SVG arc, bar and circle; it is not an autotraced bitmap.

## Windows resource contract

`luaug.rc` embeds `icon/luaug.ico` into the host and platform test executable.
Every ICO entry must contain PNG data, including the smallest sizes: the
engine decodes the resource using `stb_image`, without a DIB decoder. The
generator assembles the ICO directory explicitly to preserve this contract.
A game built with `luaug build` can replace this fallback with its own icon.

Rebuild the executable to embed the new resource. Regenerating artwork does
not modify an installed executable or publish the social card to GitHub.

## History

Earlier crescent candidates, review notes and derived images under
`art/branding/` are historical. The active specification is
[Orbit G](../art/branding/orbit/STYLE.md), which records the prompt, geometry,
palette and maintenance workflow.
