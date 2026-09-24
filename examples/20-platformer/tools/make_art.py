"""The platformer's pixel art, drawn by code so the example carries no asset
with a licence to track: a tileset, a two-frame hero and a coin.

    python examples/20-platformer/tools/make_art.py

Writes content/sprites/*.png beside this folder. Standard library only.
"""
import pathlib
import struct
import zlib

HERE = pathlib.Path(__file__).resolve().parent
OUT = HERE.parent / "content" / "sprites"


def png(path, width, height, pixel):
    rows = b"".join(b"\x00" + b"".join(bytes(pixel(x, y)) for x in range(width)) for y in range(height))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows, 9)) +
                     chunk(b"IEND", b""))


def speckle(x, y, seed):
    # A fixed hash, so the art is the same bytes every run.
    return ((x * 73856093) ^ (y * 19349663) ^ (seed * 83492791)) & 0xFF


# Four 16x16 tiles in a row: 1 grass over dirt, 2 dirt, 3 brick, 4 stone.
def tiles(x, y):
    tile, lx, ly = x // 16, x % 16, y % 16
    if tile == 0 and ly < 4:
        blade = ly == 3 and speckle(lx, 0, 1) % 3 == 0
        return (58, 140, 52, 255) if blade else (86, 176, 70, 255) if ly > 0 else (120, 206, 92, 255)
    if tile in (0, 1):
        dark = speckle(lx, ly, 2) % 7 == 0
        return (104, 70, 44, 255) if dark else (140, 96, 60, 255)
    if tile == 2:
        mortar = ly % 8 == 7 or (lx + (8 if ly >= 8 else 0)) % 16 == 15
        return (70, 58, 52, 255) if mortar else (176, 82, 60, 255)
    edge = lx in (0, 15) or ly in (0, 15)
    shade = speckle(lx, ly, 4) % 20
    return (84, 88, 100, 255) if edge else (128 - shade, 132 - shade, 144 - shade, 255)


# The hero: two 16x16 frames side by side, standing and mid-stride.
def hero(x, y):
    frame, lx = x // 16, x % 16
    body = 4 <= lx <= 11 and 3 <= y <= 11
    head = 5 <= lx <= 10 and 1 <= y <= 5
    if frame == 0:
        legs = y >= 12 and lx in (5, 6, 9, 10)
    else:
        legs = (y >= 12 and lx in (3, 4, 11, 12)) or (y == 12 and lx in (5, 10))
    if y == 3 and lx in (7, 10):
        return (24, 26, 40, 255)
    if head and y <= 2:
        return (220, 60, 56, 255)
    if head:
        return (246, 204, 160, 255)
    if body:
        return (54, 108, 214, 255)
    if legs:
        return (40, 44, 64, 255)
    return (0, 0, 0, 0)


def coin(x, y):
    dx, dy = x - 7.5, y - 7.5
    d = dx * dx + dy * dy
    if d > 49:
        return (0, 0, 0, 0)
    if d > 36:
        return (190, 130, 20, 255)
    if 2 <= x <= 5 and 3 <= y <= 6:
        return (255, 244, 180, 255)
    return (250, 200, 40, 255)


def flag(x, y):
    if x in (1, 2):
        return (200, 200, 210, 255)
    if y < 9 and x >= 3:
        check = ((x // 3) + (y // 3)) % 2
        return (240, 240, 240, 255) if check else (30, 30, 40, 255)
    return (0, 0, 0, 0)


OUT.mkdir(parents=True, exist_ok=True)
png(OUT / "tiles.png", 64, 16, tiles)
png(OUT / "hero.png", 32, 16, hero)
png(OUT / "coin.png", 16, 16, coin)
png(OUT / "flag.png", 16, 16, flag)
print("wrote", sorted(p.name for p in OUT.iterdir()))
