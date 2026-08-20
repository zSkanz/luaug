"""Generates examples/02-meshes/content/models/scene.gltf.

Checked in as the .gltf it produces rather than run at build time: a text glTF
diffs line by line, and a generator in the build would make the example's own
content depend on Python. This script lives beside it so the next person can see
how the numbers were arrived at rather than guessing.
"""
import base64
import json
import struct

# A unit cube, 24 vertices so each face has its own normal, 36 indices. Split
# into two primitives -- top and sides+bottom -- so the file exercises the
# importer's multi-primitive, multi-material path, which is what a real
# exporter's output looks like.
FACES = [
    # (normal, four corners counter-clockwise seen from outside)
    ((0, 1, 0), [(-1, 1, -1), (-1, 1, 1), (1, 1, 1), (1, 1, -1)]),      # top
    ((0, -1, 0), [(-1, -1, 1), (-1, -1, -1), (1, -1, -1), (1, -1, 1)]),  # bottom
    ((0, 0, 1), [(-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)]),       # +Z
    ((0, 0, -1), [(1, -1, -1), (-1, -1, -1), (-1, 1, -1), (1, 1, -1)]),  # -Z
    ((1, 0, 0), [(1, -1, 1), (1, -1, -1), (1, 1, -1), (1, 1, 1)]),       # +X
    ((-1, 0, 0), [(-1, -1, -1), (-1, -1, 1), (-1, 1, 1), (-1, 1, -1)]),  # -X
]

positions, normals, uvs = [], [], []
top_indices, rest_indices = [], []

for index, (normal, corners) in enumerate(FACES):
    base = len(positions)
    for corner in corners:
        positions.append(tuple(c * 0.5 for c in corner))
        normals.append(normal)
    uvs.extend([(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)])
    quad = [base, base + 1, base + 2, base, base + 2, base + 3]
    (top_indices if index == 0 else rest_indices).extend(quad)

buffer = b""
views = []


def add_view(payload, target):
    global buffer
    while len(buffer) % 4:
        buffer += b"\0"
    offset = len(buffer)
    buffer += payload
    views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(payload), "target": target})
    return len(views) - 1


position_view = add_view(b"".join(struct.pack("<3f", *p) for p in positions), 34962)
normal_view = add_view(b"".join(struct.pack("<3f", *n) for n in normals), 34962)
uv_view = add_view(b"".join(struct.pack("<2f", *t) for t in uvs), 34962)
top_view = add_view(b"".join(struct.pack("<H", i) for i in top_indices), 34963)
rest_view = add_view(b"".join(struct.pack("<H", i) for i in rest_indices), 34963)

accessors = [
    # POSITION needs min/max: fastgltf's validator requires them, and it is
    # right to -- a bounding box the file does not state is a bounding box every
    # reader has to compute.
    {"bufferView": position_view, "componentType": 5126, "count": len(positions), "type": "VEC3",
     "min": [-0.5, -0.5, -0.5], "max": [0.5, 0.5, 0.5]},
    {"bufferView": normal_view, "componentType": 5126, "count": len(normals), "type": "VEC3"},
    {"bufferView": uv_view, "componentType": 5126, "count": len(uvs), "type": "VEC2"},
    {"bufferView": top_view, "componentType": 5123, "count": len(top_indices), "type": "SCALAR"},
    {"bufferView": rest_view, "componentType": 5123, "count": len(rest_indices), "type": "SCALAR"},
]

# Four materials sweeping the two axes PBR is actually about: metalness and
# roughness. A scene where everything is the same dielectric proves the shader
# ran, not that it is right.
materials = [
    {"name": "PolishedCopper", "pbrMetallicRoughness": {
        "baseColorFactor": [0.95, 0.64, 0.54, 1.0], "metallicFactor": 1.0, "roughnessFactor": 0.15}},
    {"name": "BrushedSteel", "pbrMetallicRoughness": {
        "baseColorFactor": [0.56, 0.57, 0.58, 1.0], "metallicFactor": 1.0, "roughnessFactor": 0.55}},
    {"name": "MattePaint", "pbrMetallicRoughness": {
        "baseColorFactor": [0.18, 0.45, 0.72, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.85}},
    {"name": "GlossyPaint", "pbrMetallicRoughness": {
        "baseColorFactor": [0.75, 0.25, 0.22, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.2},
     "emissiveFactor": [0.08, 0.0, 0.0]},
]

# One mesh per material pair rather than one shared mesh, so each node can wear
# a different material. All of them share the same accessors -- the geometry is
# uploaded once and the primitives differ only in which material they name.
#
# **The ground is a dielectric on purpose.** The first version of this scene put
# a metal on every up-facing surface, and this renderer has no IBL (M4 brief,
# NOT-in-scope 6) -- a metal lit only by punctual lights reflects nothing but a
# tiny specular lobe, so the correct image was a black floor. That looked exactly
# like a broken sun.
def cube(name, top_material, side_material):
    return {
        "name": name,
        "primitives": [
            {"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "material": top_material},
            {"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 4, "material": side_material},
        ],
    }


meshes = [
    cube("Ground", 2, 2),
    cube("Copper", 0, 0),
    cube("Steel", 1, 1),
    cube("Glossy", 3, 3),
]

# The scene: a wide flat slab to catch shadows, and four cubes above it at
# different heights so the shadow map has something to say.
nodes = [
    {"name": "Ground", "mesh": 0, "scale": [16.0, 0.5, 16.0], "translation": [0.0, -0.25, 0.0]},
    {"name": "CubeCopper", "mesh": 1, "translation": [-2.2, 1.0, 0.0], "scale": [1.4, 1.4, 1.4]},
    {"name": "CubeSteel", "mesh": 2, "translation": [0.6, 0.7, -1.8], "scale": [1.0, 1.0, 1.0],
     "rotation": [0.0, 0.3826834, 0.0, 0.9238795]},
    {"name": "CubeGlossy", "mesh": 3, "translation": [2.4, 1.6, 1.2], "scale": [0.8, 2.4, 0.8]},
]

document = {
    "asset": {"version": "2.0", "generator": "LuauG examples/02-meshes scene generator"},
    "scene": 0,
    "scenes": [{"name": "Scene", "nodes": [0, 1, 2, 3]}],
    "nodes": nodes,
    "meshes": meshes,
    "materials": materials,
    "accessors": accessors,
    "bufferViews": views,
    "buffers": [{
        "byteLength": len(buffer),
        "uri": "data:application/octet-stream;base64," + base64.b64encode(buffer).decode("ascii"),
    }],
}

import io
import sys

with io.open(sys.argv[1], "w", encoding="utf-8", newline="\n") as handle:
    json.dump(document, handle, indent=2)
    handle.write("\n")
print("wrote", sys.argv[1], len(buffer), "bytes of geometry")
