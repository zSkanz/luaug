"""Reproduce the Orbit G identity using Pillow and the vendored Inter font.

    python tools/repo/draw_branding.py

The mark's vector and raster exports share geometry; ICO entries are all PNG.
See art/branding/orbit/STYLE.md for the exact design prompt and usage rules.
"""

import io
import math
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "branding"
ART = ROOT / "art/branding/orbit"
FONT = ROOT / "third_party/inter/docs/font-files/InterVariable.ttf"
INK = "#14252D"
PAPER = "#F4F8F9"
NIGHT = "#101C24"
MINT = "#55E0C5"
TEAL = "#07867C"
PRIMARY = "#17BDA6"
SIZES = (16, 24, 32, 48, 64, 128, 256)


def font(size, weight=650):
    face = ImageFont.truetype(str(FONT), size)
    face.set_variation_by_axes([32, weight])
    return face


def orbit_points():
    # Counterclockwise from the open shoulder, around the bowl, into the G bar.
    points = []
    for step in range(181):
        angle = math.radians(-48 - 312 * step / 180)
        points.append((32 + 20 * math.cos(angle), 32 + 20 * math.sin(angle)))
    points.append((34, 32))
    return points


def mark(size, color=PRIMARY):
    scale = max(4, math.ceil(1024 / size))
    edge = size * scale
    unit = edge / 64
    image = Image.new("RGBA", (edge, edge))
    draw = ImageDraw.Draw(image)
    points = [(x * unit, y * unit) for x, y in orbit_points()]
    draw.line(points, fill=color, width=round(9 * unit), joint="curve")
    for x, y in points:
        r = 4.5 * unit
        draw.ellipse((x-r, y-r, x+r, y+r), fill=color)
    return image.resize((size, size), Image.Resampling.LANCZOS)


def mark_svg(color):
    x, y = orbit_points()[0]
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64" '
            'width="512" height="512" role="img" aria-labelledby="title">\n'
            '<title id="title">LuauG — Orbit G</title>\n'
            f'<path d="M {x:.4f} {y:.4f} A 20 20 0 1 0 52 32 L 34 32" fill="none" stroke="{color}" '
            'stroke-width="9" stroke-linecap="round" stroke-linejoin="round"/>\n'
            '</svg>\n')


def app_icon(size):
    # A stable dark silhouette keeps the mint mark legible on any OS surface.
    edge = max(1024, size * 4)
    unit = edge / 64
    image = Image.new("RGBA", (edge, edge))
    ImageDraw.Draw(image).rounded_rectangle(
        (unit, unit, 63 * unit, 63 * unit), radius=14 * unit, fill=NIGHT)
    image.alpha_composite(mark(edge, MINT))
    return image.resize((size, size), Image.Resampling.LANCZOS)


def wordmark(height, color):
    # Render letter by letter to make the tracking reproducible without layout tools.
    face = font(height * 4)
    tracking = -0.025 * height * 4
    image = Image.new("RGBA", (height * 18, height * 6))
    draw = ImageDraw.Draw(image)
    x = height * 0.2
    for letter in "LuauG":
        draw.text((x, 0), letter, font=face, fill=color, stroke_width=0)
        x += face.getlength(letter) + tracking
    image = image.crop(image.getbbox())
    return image.resize((round(image.width * height / image.height), height), Image.Resampling.LANCZOS)


def lockup(mode="light", stacked=False):
    accent, text = (MINT, PAPER) if mode == "dark" else (TEAL, INK)
    if stacked:
        image = Image.new("RGBA", (800, 800))
        symbol = mark(460, accent)
        word = wordmark(116, text)
        image.alpha_composite(symbol, (170, 60))
        image.alpha_composite(word, ((800-word.width)//2, 564))
    else:
        image = Image.new("RGBA", (1400, 360))
        symbol = mark(300, accent)
        word = wordmark(190, text)
        image.alpha_composite(symbol, (20, 30))
        image.alpha_composite(word, (376, 85))
    return image


def ico(images):
    # stb_image consumes the resource payload directly: DIB entries are invalid here.
    payloads = []
    for image in images:
        stream = io.BytesIO()
        image.save(stream, format="PNG")
        payloads.append(stream.getvalue())
    offset = 6 + 16 * len(images)
    entries = []
    for image, payload in zip(images, payloads):
        size = image.width
        entries.append(struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0,
                                   1, 32, len(payload), offset))
        offset += len(payload)
    return struct.pack("<HHH", 0, 1, len(images)) + b"".join(entries + payloads)


def label(draw, xy, text, size=18, color=INK, weight=500):
    draw.text(xy, text, font=font(size, weight), fill=color)


def preview():
    board = Image.new("RGB", (1600, 1420), PAPER)
    d = ImageDraw.Draw(board)
    label(d, (70, 46), "LUAUG  /  ORBIT G", 18, TEAL, 650)
    label(d, (70, 82), "A new orbit.", 64, INK, 650)
    label(d, (70, 168), "Engine identity  /  September 2026", 20, "#61737B")
    d.rounded_rectangle((50, 230, 1550, 658), radius=24, fill=NIGHT)
    hero = lockup("dark").resize((1260, 324), Image.Resampling.LANCZOS)
    board.paste(hero, (170, 285), hero)
    label(d, (86, 606), "01  /  PRIMARY LOCKUP", 15, "#A0B5BE", 600)
    d.rounded_rectangle((50, 686, 778, 1106), radius=24, fill="white")
    d.rounded_rectangle((806, 686, 1550, 1106), radius=24, fill="#E1ECEB")
    symbol = mark(280, TEAL)
    board.paste(symbol, (100, 738), symbol)
    mono = mark(200, INK)
    board.paste(mono, (450, 778), mono)
    label(d, (86, 1050), "02  /  SYMBOL + ONE INK", 15, "#61737B", 600)
    compact = lockup("light", True).resize((400, 400), Image.Resampling.LANCZOS)
    board.paste(compact, (980, 685), compact)
    label(d, (842, 1050), "03  /  STACKED LOCKUP", 15, "#61737B", 600)
    label(d, (70, 1150), "Small by design.", 28, INK, 650)
    x = 70
    for size in (16, 24, 32, 48, 64):
        image = app_icon(size)
        board.paste(image, (x, 1235-size//2), image)
        label(d, (x, 1280), str(size), 14, "#61737B")
        x += 94
    for x, color, name in ((800, NIGHT, "GRAPHITE"),(980, TEAL, "DEEP TEAL"),(1160, MINT, "ORBIT MINT"),(1340, PAPER, "PAPER")):
        d.rounded_rectangle((x, 1160, x+130, 1250), radius=12, fill=color, outline="#D1DDDF")
        label(d, (x, 1272), name, 13, INK, 600)
        label(d, (x, 1296), color, 13, "#61737B")
    label(d, (70, 1370), "Rounded geometry. One name. Built for light and dark.", 16, "#61737B")
    board.save(ART / "brand-board.png")


def icon_preview():
    board = Image.new("RGB", (960, 440), PAPER)
    draw = ImageDraw.Draw(board)
    label(draw, (32, 20), "APPLICATION ICON / NATIVE SIZES", 20, TEAL, 650)
    for row, background, foreground in ((0, "#FFFFFF", INK), (1, "#202020", PAPER)):
        top = 70 + row * 180
        draw.rounded_rectangle((20, top, 940, top + 164), radius=16, fill=background)
        x = 55
        for size in (16, 24, 32, 48, 64, 128):
            icon = app_icon(size)
            board.paste(icon, (x, top + 70 - size // 2), icon)
            label(draw, (x, top + 137), str(size) + " px", 13, foreground)
            x += 145
    board.save(ART / "icon-review.png")


def social():
    image = Image.new("RGB", (1280, 640), NIGHT)
    d = ImageDraw.Draw(image)
    # Background geometry stays on the card, never in the application mark.
    for radius in (235, 325, 415):
        d.ellipse((1170-radius, 320-radius, 1170+radius, 320+radius), outline="#223841", width=2)
    symbol = mark(272, MINT)
    image.paste(symbol, (78, 140), symbol)
    word = wordmark(144, PAPER)
    image.paste(word, (398, 204), word)
    label(d, (408, 386), "The Luau game engine.", 30, "#A5BDC6", 450)
    label(d, (90, 558), "LUAUG", 16, MINT, 650)
    label(d, (984, 558), "CREATE. PLAY. ITERATE.", 13, "#A5BDC6", 550)
    image.save(OUT / "luaug-social-card.png")


def main():
    ART.mkdir(parents=True, exist_ok=True)
    (OUT / "icon").mkdir(exist_ok=True)
    for suffix, color in (("", PRIMARY),("-dark", MINT),("-light", TEAL),("-white", "#FFFFFF"),("-mono", INK)):
        (OUT / f"luaug-mark{suffix}.svg").write_text(mark_svg(color), encoding="utf-8")
        mark(512, color).save(OUT / f"luaug-mark{suffix}-512.png")
    for mode in ("light", "dark"):
        for stacked in (False, True):
            layout = "stacked" if stacked else "horizontal"
            image = lockup(mode, stacked)
            image.save(OUT / f"luaug-lockup-{layout}-{mode}.png")
            if mode == "light":
                image.save(OUT / f"luaug-lockup-{layout}.png")
    word = wordmark(160, PRIMARY)
    canvas = Image.new("RGBA", (512, 512))
    word = word.resize((472, round(word.height * 472 / word.width)), Image.Resampling.LANCZOS)
    canvas.alpha_composite(word, (20, (512-word.height)//2))
    canvas.save(OUT / "luaug-logo-512.png")
    icon_images = [app_icon(size) for size in SIZES]
    for size, image in zip(SIZES, icon_images):
        image.save(OUT / f"icon/luaug-{size}.png")
    (OUT / "icon/luaug.ico").write_bytes(ico(icon_images))
    app_icon(512).save(OUT / "luaug-app-icon-512.png")
    tile_svg = mark_svg(MINT).replace('<path', f'<rect x="1" y="1" width="62" height="62" rx="14" fill="{NIGHT}"/>\n<path')
    (OUT / "icon/luaug.svg").write_text(tile_svg, encoding="utf-8")
    preview()
    icon_preview()
    social()
    print("Orbit G: mark variants, lockups, wordmark, social card, seven PNG icon sizes and PNG-only ICO exported.")


if __name__ == "__main__":
    main()
