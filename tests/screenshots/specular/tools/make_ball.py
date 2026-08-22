"""Generates tests/screenshots/specular/content/models/ball.gltf.

Checked in as the .gltf it produces rather than run at build time, for the same
reason `examples/04-obby/tools/make_waver.py` is: a text glTF diffs line by line,
and a generator in the build would make test content depend on Python.

**A polished metal sphere is the smallest thing that can show specular
aliasing.** The normal turns through a full hemisphere across the silhouette, so
at any size the outer ring of pixels covers a large slice of directions -- and a
GGX lobe at roughness 0.1 is narrower than that slice long before the sphere
gets small. That mismatch is the whole phenomenon: the highlight has nowhere
stable to sit, and a camera moving a fraction of a pixel makes it crawl.

A UV sphere, and the ring/segment counts are chosen so that geometry is NOT the
thing under test: at 32 by 48 the facets are far below a pixel at every distance
this scene places a ball at, so a shimmer in the render is the shading's and not
the tessellation's.
"""
import base64
import json
import math
import struct

RADIUS = 0.35
RINGS = 32
SEGMENTS = 48

# Polished, and metal. A dielectric would put most of its response in a diffuse
# lobe that does not alias, which would make the scene prove nothing.
#
# 0.045 is `LuaugMinRoughness` -- the SMOOTHEST material this engine will shade,
# because `makeSurface` clamps there and has since M4, whose comment says why:
# "below roughly 0.045 GGX highlights start aliasing into single-pixel
# fireflies". So this is the worst case the engine can actually be asked to
# draw, which is the only roughness worth testing an antialiasing measure at.
ROUGHNESS = 0.045
METALLIC = 1.0


def _build():
    positions, normals, indices = [], [], []
    for ring in range(RINGS + 1):
        # Poles included: a ring at v = 0 and one at v = 1 collapse to a point,
        # which costs a strip of degenerate triangles and buys a seamless cap.
        theta = math.pi * ring / RINGS
        for segment in range(SEGMENTS + 1):
            phi = 2.0 * math.pi * segment / SEGMENTS
            n = (math.sin(theta) * math.cos(phi), math.cos(theta), math.sin(theta) * math.sin(phi))
            normals.append(n)
            positions.append(tuple(c * RADIUS for c in n))

    stride = SEGMENTS + 1
    for ring in range(RINGS):
        for segment in range(SEGMENTS):
            a = ring * stride + segment
            b = a + stride
            # Counter-clockwise seen from outside, which is what the engine's
            # `FrontFace::CounterClockwise` with back-face culling wants.
            indices.extend((a, b, a + 1))
            indices.extend((a + 1, b, b + 1))
    return positions, normals, indices


def main():
    positions, normals, indices = _build()

    blob = bytearray()
    views = []

    def view(payload, target):
        while len(blob) % 4 != 0:
            blob.append(0)
        offset = len(blob)
        blob.extend(payload)
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(payload), "target": target})
        return len(views) - 1

    position_view = view(b"".join(struct.pack("<3f", *v) for v in positions), 34962)
    normal_view = view(b"".join(struct.pack("<3f", *v) for v in normals), 34962)
    index_view = view(b"".join(struct.pack("<I", i) for i in indices), 34963)

    lo = [min(v[i] for v in positions) for i in range(3)]
    hi = [max(v[i] for v in positions) for i in range(3)]

    document = {
        "asset": {"version": "2.0", "generator": "luaug tests/screenshots/specular/tools/make_ball.py"},
        "scenes": [{"name": "Ball", "nodes": [0]}],
        "nodes": [{"name": "Ball", "mesh": 0}],
        "materials": [{
            "name": "PolishedMetal",
            "pbrMetallicRoughness": {
                "baseColorFactor": [0.94, 0.90, 0.84, 1.0],
                "metallicFactor": METALLIC,
                "roughnessFactor": ROUGHNESS,
            },
        }],
        "meshes": [{
            "name": "Ball",
            "primitives": [{
                "attributes": {"POSITION": 0, "NORMAL": 1},
                "indices": 2,
                "material": 0,
            }],
        }],
        "accessors": [
            {"componentType": 5126, "count": len(positions), "type": "VEC3",
             "min": lo, "max": hi, "bufferView": position_view},
            {"componentType": 5126, "count": len(normals), "type": "VEC3", "bufferView": normal_view},
            {"componentType": 5125, "count": len(indices), "type": "SCALAR", "bufferView": index_view},
        ],
        "bufferViews": views,
        "buffers": [{
            "byteLength": len(blob),
            "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(blob)).decode("ascii"),
        }],
    }

    with open("tests/screenshots/specular/content/models/ball.gltf", "w", newline="\n") as handle:
        json.dump(document, handle, indent=1)
        handle.write("\n")
    print(f"ball.gltf: {len(positions)} vertices, {len(indices) // 3} triangles")


if __name__ == "__main__":
    main()
