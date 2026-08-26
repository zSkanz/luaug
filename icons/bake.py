"""Bake the icon masters into the runtime theme.

Reads the white-on-black masters under art/editor-icons/ and writes the default
theme under icons/default/ — alpha PNGs at 256, normalised against the keyline,
plus theme.json.

    python bake.py
"""

import json
import re
from pathlib import Path
from PIL import Image

# Repo-relative, because an absolute path is a script that runs on one machine.
ART = Path(__file__).resolve().parent.parent / "art" / "editor-icons"
OUT = Path(__file__).resolve().parent / "default"

CANVAS = 256

# The keyline boxes, as fractions of the canvas. From art/editor-icons/README.md.
KEYLINE = {
    "Square": (768 / 1024, 768 / 1024),
    "Circle": (854 / 1024, 854 / 1024),
    "Tall": (683 / 1024, 854 / 1024),
    "Wide": (854 / 1024, 683 / 1024),
}


def keylines_from(doc):
    """The keyline each icon is specified against, read from the brief itself.

    Parsed rather than copied so the bake cannot drift from the spec: a row that
    changes keyline changes the output on the next run without anybody editing
    two files.
    """
    out = {}
    for line in doc.read_text(encoding="utf-8").splitlines():
        if not line.startswith("| `"):
            continue
        name = re.search(r"`([A-Za-z]+)\.png`", line)
        band = re.match(r"\|\s*`[A-Za-z]+\.png`\s*\|\s*([A-Za-z]+)\s*\|", line)
        if name and band and band.group(1) in KEYLINE:
            out[name.group(1)] = band.group(1)
    return out


def bake(src: Path, keyline: str) -> Image.Image:
    """One master to one runtime icon: levels, trim, fit the keyline, alpha."""
    im = Image.open(src).convert("L")

    # Levels first. Clamps JPEG ringing and any grey haze to the ends while
    # leaving real edge antialiasing in the middle — measured on a Bing JPEG at
    # 0.56% midtone, which this brings to 0.42% against a PNG's 0.38%.
    lut = [0 if v < 30 else (255 if v > 225 else int((v - 30) * 255 / 195)) for v in range(256)]
    im = im.point(lut)

    box = im.point(lambda v: 255 if v > 128 else 0).getbbox()
    shape = im.crop(box) if box else im

    # Fit the shape inside its keyline, preserving proportion. This is the step
    # that makes a folder and a cube look the same SIZE rather than the same
    # number of pixels, and it is why an under-filled master is not a defect.
    kw, kh = KEYLINE[keyline]
    max_w, max_h = CANVAS * kw, CANVAS * kh
    scale = min(max_w / shape.width, max_h / shape.height)
    fitted = shape.resize((max(1, round(shape.width * scale)), max(1, round(shape.height * scale))), Image.LANCZOS)

    # The mask IS the alpha. The colour is white everywhere and the editor
    # multiplies it by a theme colour at draw time, which is what lets one file
    # serve both the light and the dark panel.
    alpha = Image.new("L", (CANVAS, CANVAS), 0)
    alpha.paste(fitted, ((CANVAS - fitted.width) // 2, (CANVAS - fitted.height) // 2))
    out = Image.new("RGBA", (CANVAS, CANVAS), (255, 255, 255, 0))
    out.putalpha(alpha)
    return out


def main():
    class_bands = keylines_from(ART / "README.md")
    action_bands = keylines_from(ART / "ACTIONS.md")
    content_bands = keylines_from(ART / "CONTENT.md")

    # Where each logical id's pixels come from. An id with no file of its own
    # points at another id's file — five of them do, and that is deliberate: two
    # names for one drawing beats two drawings nobody can tell apart.
    ALIAS = {
        "content.Folder": "class/Folder.png",
        "content.Texture": "class/ImageLabel.png",
        "content.Chunk": "class/StreamingService.png",
        "content.Mesh": "class/MeshPart.png",
        "action.List": "class/UIListLayout.png",
    }

    sources = []
    for name, band in sorted(class_bands.items()):
        sources.append(("class", name, band, ART / "src" / f"{name}.png"))
    for name, band in sorted(action_bands.items()):
        if f"action.{name}" in ALIAS:
            continue
        src = ART / "actions" / f"{name}.png"
        if not src.exists():
            src = ART / "temporary" / f"{name}.png"
        sources.append(("action", name, band, src))
    for name, band in sorted(content_bands.items()):
        if f"content.{name}" in ALIAS:
            continue
        sources.append(("content", name, band, ART / "content" / f"{name}.png"))

    icons, missing = {}, []
    for group, name, band, src in sources:
        rel = f"{group}/{name}.png"
        if not src.exists():
            missing.append(f"{group}.{name}")
            continue
        (OUT / group).mkdir(parents=True, exist_ok=True)
        bake(src, band).save(OUT / rel)
        icons[f"{group}.{name}"] = rel

    for key, target in ALIAS.items():
        if (OUT / target).exists():
            icons[key] = target
        else:
            missing.append(key)

    theme = {
        "id": "default",
        "name": "LuauG Default",
        "version": 1,
        "size": CANVAS,
        "tintable": True,
        "fallback": "class.Instance",
        "icons": dict(sorted(icons.items())),
    }
    (OUT / "theme.json").write_text(json.dumps(theme, indent=2) + "\n", encoding="utf-8")

    print(f"baked {len(icons)} ids into {OUT}")
    print(f"  {len([k for k in icons if k.startswith('class.')])} class"
          f"  {len([k for k in icons if k.startswith('action.')])} action"
          f"  {len([k for k in icons if k.startswith('content.')])} content"
          f"  ({len(ALIAS)} of them aliases)")
    if missing:
        print(f"\nnot drawn yet ({len(missing)}): {' '.join(sorted(missing))}")


main()
