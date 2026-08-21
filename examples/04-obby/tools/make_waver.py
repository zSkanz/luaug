"""Generates examples/04-obby/content/models/waver.gltf.

Checked in as the .gltf it produces rather than run at build time, for the same
reason `examples/02-meshes/tools/make_scene.py` is: a text glTF diffs line by
line, and a generator in the build would make an example's content depend on
Python. This script lives beside it so the next person can see how the numbers
were arrived at rather than guessing.

**The waver is the obby's skinned-animation demonstrator**: a bar welded 3.4 m
above the character, playing a looping wave, so the animation can be seen riding
the character through every contact. Two joints and five rows of vertices, with
the rows between the ends ramping from one joint to the other.

WHAT CHANGED AT M7.5, and both were reported by a human looking at it:

  * **The bend collapsed the bar into a wedge.** Three rows put a ninety-degree
    rotation across a single fifty-fifty vertex row, and linear blend skinning
    pinches wherever a large rotation crosses a blended vertex -- the "candy
    wrapper". What was left projected as a triangle pointing down. Five rows and
    a forty-degree wave spread the rotation until the blend holds its width.
  * **It was a plane, and a plane has one side.** Six vertices, four triangles,
    every one of them at z = 0. The pipeline culls back faces, so from behind the
    character the waver was not dim or thin -- it was GONE, and from in front the
    bend folded its silhouette into shapes nobody could name. A bar with
    thickness has the same outline from every angle and cannot vanish.
  * **It declared no material at all.** glTF says a primitive without one uses
    the default material, and that default is `metallicFactor` 1.0 with
    `roughnessFactor` 1.0 -- a perfectly rough metal, which renders as a dark
    grey slab. The engine was right and the asset was under-specified. It has a
    material now.

The joints, their inverse binds and the animation's single rotation channel are
M6's. What changed inside them is the vertex rows the weights are spread over and
the size of the rotation -- the rig is the same rig.
"""
import base64
import json
import struct

# The bar: a metre wide, two tall, and now twelve centimetres thick. The first
# two are what the obby was built around and are unchanged; the third is the fix.
HALF_WIDTH = 0.5
HEIGHT = 2.0
HALF_DEPTH = 0.06

# FIVE rows, where M6 had three, and that is the fix for the shape rather than
# for the colour.
#
# **Linear blend skinning collapses towards the axis wherever a large rotation
# crosses a blended vertex** -- the "candy wrapper", and it is a property of the
# blend rather than a bug in it: averaging two rotation matrices does not give a
# rotation, it gives something shorter. Three rows put the entire ninety-degree
# bend across ONE fifty-fifty row, so the middle of the bar pinched to nothing
# and what was left projected as a triangle pointing down. That is what a human
# reported seeing above the character.
#
# Two changes together fix it and neither alone is enough: the rotation is spread
# over four blended rows instead of one, and it is smaller (see BEND_ROTATIONS).
ROWS = (0.0, 0.5, 1.0, 1.5, 2.0)

# Skin joint 0 is the node named `Tip` and joint 1 is `Root` (see `skins` below).
# The bottom is pinned to the root, the top rides the tip, and the rows between
# them ramp -- which is the whole content of the rig.
ROW_WEIGHTS = (
    ((1, 0, 0, 0), (1.0, 0.0, 0.0, 0.0)),
    ((1, 0, 0, 0), (0.75, 0.25, 0.0, 0.0)),
    ((1, 0, 0, 0), (0.5, 0.5, 0.0, 0.0)),
    ((1, 0, 0, 0), (0.25, 0.75, 0.0, 0.0)),
    ((0, 0, 0, 0), (1.0, 0.0, 0.0, 0.0)),
)


def _side_face(normal, corner_at):
    """One of the four sides, as a strip of two vertices per row.

    `corner_at(row_index, side)` returns the position; `side` is -1 or +1 along
    whichever axis the face runs. Winding is fixed up against the normal below
    rather than reasoned about per face, because four faces of hand-written
    index order is four chances to get one backwards.
    """
    positions = []
    rows = []
    for row in range(len(ROWS)):
        first = len(positions)
        positions.append(corner_at(row, -1))
        positions.append(corner_at(row, 1))
        rows.append((first, first + 1, row))

    triangles = []
    for lower, upper in zip(rows, rows[1:]):
        a, b, _ = lower
        c, d, _ = upper
        triangles.append((a, b, c))
        triangles.append((c, b, d))
    return positions, triangles, [row for _, _, row in rows for _ in range(2)], normal


def _cap(normal, corners, row):
    positions = list(corners)
    triangles = [(0, 1, 2), (0, 2, 3)]
    return positions, triangles, [row] * 4, normal


def _faces():
    top = HEIGHT
    faces = []

    faces.append(_side_face((0.0, 0.0, 1.0),
                            lambda row, side: (side * HALF_WIDTH, ROWS[row], HALF_DEPTH)))
    faces.append(_side_face((0.0, 0.0, -1.0),
                            lambda row, side: (side * HALF_WIDTH, ROWS[row], -HALF_DEPTH)))
    faces.append(_side_face((1.0, 0.0, 0.0),
                            lambda row, side: (HALF_WIDTH, ROWS[row], side * HALF_DEPTH)))
    faces.append(_side_face((-1.0, 0.0, 0.0),
                            lambda row, side: (-HALF_WIDTH, ROWS[row], side * HALF_DEPTH)))

    faces.append(_cap((0.0, 1.0, 0.0), [
        (-HALF_WIDTH, top, -HALF_DEPTH), (-HALF_WIDTH, top, HALF_DEPTH),
        (HALF_WIDTH, top, HALF_DEPTH), (HALF_WIDTH, top, -HALF_DEPTH),
    ], len(ROWS) - 1))
    faces.append(_cap((0.0, -1.0, 0.0), [
        (-HALF_WIDTH, 0.0, HALF_DEPTH), (-HALF_WIDTH, 0.0, -HALF_DEPTH),
        (HALF_WIDTH, 0.0, -HALF_DEPTH), (HALF_WIDTH, 0.0, HALF_DEPTH),
    ], 0))
    return faces


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _build():
    positions, normals, joints, weights, indices = [], [], [], [], []
    for face_positions, face_triangles, face_rows, normal in _faces():
        base = len(positions)
        for local, position in enumerate(face_positions):
            positions.append(position)
            normals.append(normal)
            row_joints, row_weights = ROW_WEIGHTS[face_rows[local]]
            joints.append(row_joints)
            weights.append(row_weights)
        for triangle in face_triangles:
            a, b, c = (face_positions[i] for i in triangle)
            edge0 = tuple(b[i] - a[i] for i in range(3))
            edge1 = tuple(c[i] - a[i] for i in range(3))
            facing = _cross(edge0, edge1)
            # Counter-clockwise seen from outside is what the engine's
            # `FrontFace::CounterClockwise` with back-face culling wants. Checked
            # rather than written out: the sign is the one thing here that is
            # invisible until something disappears.
            if sum(facing[i] * normal[i] for i in range(3)) < 0.0:
                triangle = (triangle[0], triangle[2], triangle[1])
            indices.extend(base + i for i in triangle)
    return positions, normals, joints, weights, indices


# The rig, M6's: the root at the origin and the tip a metre above it.
INVERSE_BIND = [
    [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, -1.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0],
]
# A WAVE rather than a single fold, and forty degrees rather than ninety.
#
# Ninety degrees is what made the collapse above unmissable; forty is enough to
# read as motion at the size the bar is drawn and small enough that the blend
# holds its width. Four keyframes instead of two make it swing both ways, which
# is what "waver" was always supposed to mean.
#
# sin(20 degrees) = 0.342020, cos(20 degrees) = 0.939693, and a quaternion about
# Z is (0, 0, sin(half), cos(half)).
BEND_TIMES = [0.0, 0.25, 0.5, 0.75, 1.0]
BEND_ROTATIONS = [
    [0.0, 0.0, 0.0, 1.0],
    [0.0, 0.0, 0.342020, 0.939693],
    [0.0, 0.0, 0.0, 1.0],
    [0.0, 0.0, -0.342020, 0.939693],
    [0.0, 0.0, 0.0, 1.0],
]


def main():
    positions, normals, joints, weights, indices = _build()

    blob = bytearray()
    views = []

    def view(payload, target=None):
        while len(blob) % 4 != 0:
            blob.append(0)
        offset = len(blob)
        blob.extend(payload)
        entry = {"buffer": 0, "byteOffset": offset, "byteLength": len(payload)}
        if target is not None:
            entry["target"] = target
        views.append(entry)
        return len(views) - 1

    position_view = view(b"".join(struct.pack("<3f", *v) for v in positions), 34962)
    normal_view = view(b"".join(struct.pack("<3f", *v) for v in normals), 34962)
    joint_view = view(b"".join(struct.pack("<4H", *v) for v in joints), 34962)
    weight_view = view(b"".join(struct.pack("<4f", *v) for v in weights), 34962)
    index_view = view(b"".join(struct.pack("<H", i) for i in indices), 34963)
    bind_view = view(b"".join(struct.pack("<16f", *m) for m in INVERSE_BIND))
    time_view = view(b"".join(struct.pack("<f", t) for t in BEND_TIMES))
    rotation_view = view(b"".join(struct.pack("<4f", *r) for r in BEND_ROTATIONS))

    lo = [min(v[i] for v in positions) for i in range(3)]
    hi = [max(v[i] for v in positions) for i in range(3)]

    document = {
        "asset": {"version": "2.0", "generator": "luaug examples/04-obby/tools/make_waver.py"},
        "scenes": [{"name": "Bar", "nodes": [0, 2]}],
        "nodes": [
            {"name": "Bar", "mesh": 0, "skin": 0},
            {"name": "Unused"},
            {"name": "Root", "children": [3], "translation": [0.0, 0.0, 0.0]},
            {"name": "Tip", "translation": [0.0, 1.0, 0.0]},
        ],
        "skins": [{"name": "Rig", "joints": [3, 2], "inverseBindMatrices": 5}],
        "animations": [{
            "name": "Bend",
            "samplers": [{"input": 6, "output": 7, "interpolation": "LINEAR"}],
            "channels": [{"sampler": 0, "target": {"node": 3, "path": "rotation"}}],
        }],
        # The material this file did not have. A dielectric rather than the
        # default metal, and rough rather than polished: it is meant to read as a
        # solid marker above the character, not as a mirror.
        "materials": [{
            "name": "WaverPaint",
            "pbrMetallicRoughness": {
                "baseColorFactor": [0.92, 0.45, 0.18, 1.0],
                "metallicFactor": 0.0,
                "roughnessFactor": 0.55,
            },
        }],
        "meshes": [{
            "name": "Bar",
            "primitives": [{
                "attributes": {"POSITION": 0, "NORMAL": 1, "JOINTS_0": 2, "WEIGHTS_0": 3},
                "indices": 4,
                "material": 0,
            }],
        }],
        "accessors": [
            {"componentType": 5126, "count": len(positions), "type": "VEC3",
             "min": lo, "max": hi, "bufferView": position_view},
            {"componentType": 5126, "count": len(normals), "type": "VEC3", "bufferView": normal_view},
            {"componentType": 5123, "count": len(joints), "type": "VEC4", "bufferView": joint_view},
            {"componentType": 5126, "count": len(weights), "type": "VEC4", "bufferView": weight_view},
            {"componentType": 5123, "count": len(indices), "type": "SCALAR", "bufferView": index_view},
            {"componentType": 5126, "count": len(INVERSE_BIND), "type": "MAT4", "bufferView": bind_view},
            {"componentType": 5126, "count": len(BEND_TIMES), "type": "SCALAR",
             "min": [min(BEND_TIMES)], "max": [max(BEND_TIMES)], "bufferView": time_view},
            {"componentType": 5126, "count": len(BEND_ROTATIONS), "type": "VEC4", "bufferView": rotation_view},
        ],
        "bufferViews": views,
        "buffers": [{
            "byteLength": len(blob),
            "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(blob)).decode("ascii"),
        }],
    }

    with open("examples/04-obby/content/models/waver.gltf", "w", newline="\n") as handle:
        json.dump(document, handle, indent=1)
        handle.write("\n")
    print(f"waver.gltf: {len(positions)} vertices, {len(indices) // 3} triangles")


if __name__ == "__main__":
    main()
