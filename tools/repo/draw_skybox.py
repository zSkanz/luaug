"""Draws the sample skybox ADR 0096's examples and captures use.

    python tools/repo/draw_skybox.py <folder> [--size 1024]

Six PNGs -- back, down, front, left, right, up -- of one sky computed from
direction alone, so the faces meet without a seam: a late-afternoon gradient,
a ring of distant mountains fading into the haze, thin streaks of high cloud,
and dark ground below the horizon. **No sun is painted in** (ADR 0096): the
engine draws the sun where the clock puts it, and a painted one would disagree.

Drawn rather than photographed so it carries no licence but the repository's
own (R6).

The faces are laid out as `render::skyFaceOf` reads them: seen from inside and
upright, `front` towards -Z, `back` towards +Z, `right` towards +X, `left`
towards -X; `up`'s bottom edge and `down`'s top edge both meet `front`.
"""

import argparse
import math
from pathlib import Path

from PIL import Image

HORIZON = (0.93, 0.84, 0.74)
ZENITH = (0.22, 0.40, 0.76)
MOUNTAIN_NEAR = (0.30, 0.31, 0.36)
MOUNTAIN_FAR = (0.55, 0.58, 0.66)
GROUND = (0.26, 0.24, 0.20)


def lerp(a, b, t):
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def clamp(value, low=0.0, high=1.0):
    return low if value < low else high if value > high else value


def ridge(azimuth, octave):
    """A ring of mountains: their height above the horizon, in radians."""
    if octave == 0:
        return (0.075 + 0.035 * math.sin(3 * azimuth + 0.4) + 0.018 * math.sin(7 * azimuth + 1.9)
                + 0.009 * math.sin(19 * azimuth + 0.3) + 0.004 * math.sin(41 * azimuth + 2.2))
    return (0.028 + 0.016 * math.sin(5 * azimuth + 2.6) + 0.010 * math.sin(11 * azimuth + 0.8)
            + 0.005 * math.sin(29 * azimuth + 1.4))


def colour(x, y, z):
    length = math.sqrt(x * x + y * y + z * z)
    x, y, z = x / length, y / length, z / length
    elevation = math.asin(clamp(y, -1.0, 1.0))
    azimuth = math.atan2(x, -z)

    if elevation < 0.0:
        # Ground, darker straight down, and the haze thickest at the horizon.
        return lerp(GROUND, lerp(HORIZON, MOUNTAIN_FAR, 0.5), clamp(1.0 + elevation * 6.0) ** 3)

    sky = lerp(HORIZON, ZENITH, math.sqrt(clamp(elevation / (math.pi / 2))))

    # Streaks of high cloud, following the wind across the sky.
    band = math.sin(azimuth * 2.0 + elevation * 9.0) * math.sin(azimuth * 5.0 - elevation * 4.0 + 1.3)
    wisp = clamp((band - 0.35) * 2.2) * clamp(elevation * 6.0) * clamp(1.4 - elevation * 1.8)
    sky = lerp(sky, (0.97, 0.95, 0.93), wisp * 0.55)

    # Two rings of mountains: the far one paler, the near one darker.
    far = ridge(azimuth, 1)
    near = ridge(azimuth, 0)
    if elevation < near * 0.72:
        return lerp(MOUNTAIN_NEAR, HORIZON, 0.25 + 0.35 * (elevation / max(near, 1e-4)))
    if elevation < far + 0.02:
        return lerp(MOUNTAIN_FAR, HORIZON, 0.35 + 0.4 * (elevation / (far + 0.02)))
    return sky


def direction(face, u, v):
    a = 2.0 * u - 1.0
    b = 1.0 - 2.0 * v
    if face == "front":
        return a, b, -1.0
    if face == "back":
        return -a, b, 1.0
    if face == "right":
        return 1.0, b, a
    if face == "left":
        return -1.0, b, -a
    if face == "up":
        return a, 1.0, 1.0 - 2.0 * v
    return a, -1.0, 2.0 * v - 1.0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folder", type=Path)
    parser.add_argument("--size", type=int, default=1024)
    arguments = parser.parse_args()
    arguments.folder.mkdir(parents=True, exist_ok=True)
    size = arguments.size
    for face in ("back", "down", "front", "left", "right", "up"):
        pixels = bytearray(size * size * 3)
        at = 0
        for row in range(size):
            v = (row + 0.5) / size
            for column in range(size):
                r, g, b = colour(*direction(face, (column + 0.5) / size, v))
                pixels[at] = int(clamp(r) * 255 + 0.5)
                pixels[at + 1] = int(clamp(g) * 255 + 0.5)
                pixels[at + 2] = int(clamp(b) * 255 + 0.5)
                at += 3
        Image.frombytes("RGB", (size, size), bytes(pixels)).save(arguments.folder / f"{face}.png", optimize=True)
        print(f"draw_skybox: wrote {arguments.folder / (face + '.png')}")


if __name__ == "__main__":
    main()
