#!/usr/bin/env python3
"""
Blender headless oracle for PCG bevel comparison.

Produces a canonical JSON dump of beveled cube geometry that PCG tests
can compare against.  The output is deterministic: same params → same JSON
hash regardless of Blender vertex indexing.

Usage:
  blender --background --factory-startup \
    --python blender_bevel_oracle.py -- \
    --size 2 --subdivide catmullClark --subdivide-levels 2 \
    --amount 0.08 --segments 3 --profile 0.7 --angle-limit 25 \
    --output /tmp/pcg-bevel-blender.json
"""

import bpy
import bmesh
import sys
import json
import math
import hashlib
import struct
from mathutils import Vector


# ── Helpers ──────────────────────────────────────────────────────────────

def quantize(v, q=1e-7):
    """Quantize a float to avoid FP noise in canonical comparison."""
    return round(v / q) * q


def quantize_vec3(v, q=1e-7):
    return [quantize(v[0], q), quantize(v[1], q), quantize(v[2], q)]


def canonical_cycle(verts):
    """Return the lexicographically smallest rotation of a vertex index cycle."""
    n = len(verts)
    best = min(range(n), key=lambda i: verts[i:] + verts[:i])
    return tuple(verts[best:] + verts[:best])


def signed_volume(verts, faces):
    """Compute signed volume from polygon faces (fan triangulation per face)."""
    vol = 0.0
    for f in faces:
        if len(f) < 3:
            continue
        v0 = Vector(verts[f[0]])
        for i in range(1, len(f) - 1):
            v1 = Vector(verts[f[i]])
            v2 = Vector(verts[f[i + 1]])
            vol += v0.dot(v1.cross(v2)) / 6.0
    return vol


def edge_key(a, b):
    return (min(a, b), max(a, b))


def compute_edge_incidence(faces):
    """Count how many faces share each edge."""
    edge_count = {}
    for f in faces:
        n = len(f)
        for i in range(n):
            ek = edge_key(f[i], f[(i + 1) % n])
            edge_count[ek] = edge_count.get(ek, 0) + 1
    return edge_count


def find_cube_corners(verts, size):
    """Find the 8 original cube corners by snapping to ±size/2."""
    half = size / 2.0
    corners = []
    for i, v in enumerate(verts):
        on_corner = all(abs(abs(c) - half) < 0.01 for c in v)
        if on_corner:
            octant = tuple(1 if c > 0 else -1 for c in v)
            corners.append((i, octant))
    # Group by octant; for subdivided meshes the original corners may have moved
    # slightly, so we snap to nearest octant
    if not corners:
        for i, v in enumerate(verts):
            if all(abs(abs(c) - half) < 0.1 for c in v):
                octant = tuple(1 if c > 0 else -1 for c in v)
                corners.append((i, octant))
    return corners


def extract_corner_patches(verts, faces, edge_inc, cube_size, radius=0.3):
    """
    For each cube corner octant, extract the local patch of vertices and faces
    within `radius` of that corner position.
    """
    half = cube_size / 2.0
    octants = [(sx, sy, sz) for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]
    patches = {}
    for octant in octants:
        center = Vector((octant[0] * half, octant[1] * half, octant[2] * half))
        # Find vertices near this corner
        local_verts = []
        vert_map = {}  # global_idx → local_idx
        for i, v in enumerate(verts):
            d = (Vector(v) - center).length
            if d < radius:
                local_idx = len(local_verts)
                local_verts.append(quantize_vec3(v))
                vert_map[i] = local_idx
        # Find faces that have at least one vertex near this corner
        local_faces = []
        for f in faces:
            if any(vi in vert_map for vi in f):
                # Remap face to local indices, keeping only local verts
                remapped = []
                for vi in f:
                    if vi in vert_map:
                        remapped.append(vert_map[vi])
                    else:
                        remapped.append(-1)
                # Skip faces where not all verts are local (boundary faces)
                if all(vi >= 0 for vi in remapped):
                    local_faces.append(list(remapped))
        # Compute local valence
        local_edge_inc = {}
        for f in local_faces:
            n = len(f)
            for i in range(n):
                ek = edge_key(f[i], f[(i + 1) % n])
                local_edge_inc[ek] = local_edge_inc.get(ek, 0) + 1
        valence = {}
        for f in local_faces:
            for vi in f:
                valence[vi] = valence.get(vi, 0) + 1
        # Canonical cycles
        canonical_faces = sorted([canonical_cycle(f) for f in local_faces])
        patches[str(octant)] = {
            "center": list(center),
            "vert_count": len(local_verts),
            "face_count": len(local_faces),
            "vertices": local_verts,
            "faces": [list(f) for f in canonical_faces],
            "valence": {str(k): v for k, v in sorted(valence.items())},
            "edge_incidence": {str(list(k)): v for k, v in sorted(local_edge_inc.items())},
        }
    return patches


# ── Main ─────────────────────────────────────────────────────────────────

def parse_args():
    """Parse arguments after '--' on the command line."""
    argv = sys.argv
    if "--" in argv:
        argv = argv[argv.index("--") + 1:]
    else:
        argv = []
    args = {
        "size": 2.0,
        "subdivide": "none",
        "subdivide_levels": 1,
        "amount": 0.08,
        "segments": 3,
        "profile": 0.7,
        "angle_limit": 25.0,
        "clamp_overlap": True,
        "miter_outer": "SHARP",
        "miter_inner": "SHARP",
        "vmesh_method": "ADJ",
        "offset_type": "OFFSET",
        "output": "/tmp/pcg-bevel-blender.json",
    }
    i = 0
    while i < len(argv):
        k = argv[i]
        if k.startswith("--"):
            k = k[2:]
        if i + 1 < len(argv):
            v = argv[i + 1]
            if k == "size":
                args["size"] = float(v)
            elif k == "subdivide":
                args["subdivide"] = v
            elif k == "subdivide-levels":
                args["subdivide_levels"] = int(v)
            elif k == "amount":
                args["amount"] = float(v)
            elif k == "segments":
                args["segments"] = int(v)
            elif k == "profile":
                args["profile"] = float(v)
            elif k == "angle-limit":
                args["angle_limit"] = float(v)
            elif k == "clamp-overlap":
                args["clamp_overlap"] = v.lower() in ("true", "1", "yes")
            elif k == "miter-outer":
                args["miter_outer"] = v.upper()
            elif k == "miter-inner":
                args["miter_inner"] = v.upper()
            elif k == "vmesh-method":
                args["vmesh_method"] = v.upper()
            elif k == "offset-type":
                args["offset_type"] = v.upper()
            elif k == "output":
                args["output"] = v
            i += 2
        else:
            i += 1
    return args


def main():
    args = parse_args()

    # Clear scene
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    for block in bpy.data.meshes:
        bpy.data.meshes.remove(block)

    # Create cube
    bpy.ops.mesh.primitive_cube_add(size=args["size"], location=(0, 0, 0))
    obj = bpy.context.active_object
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')

    # Subdivide
    if args["subdivide"] != "none":
        levels = args["subdivide_levels"]
        if args["subdivide"] == "catmullClark":
            # Use Subdivision Surface modifier for Catmull-Clark
            bpy.ops.object.mode_set(mode='OBJECT')
            mod = obj.modifiers.new(name="Subd", type='SUBSURF')
            mod.subdivision_type = 'CATMULL_CLARK'
            mod.levels = levels
            mod.render_levels = levels
            bpy.ops.object.modifier_apply(modifier="Subd")
            bpy.ops.object.mode_set(mode='EDIT')
            bpy.ops.mesh.select_all(action='SELECT')
        elif args["subdivide"] == "simple":
            for _ in range(levels):
                bpy.ops.mesh.subdivide(smoothness=0.0)

    # Select edges by sharp angle using bmesh (select_sharp op not available in 4.4)
    bpy.ops.mesh.select_all(action='DESELECT')
    bpy.ops.mesh.select_mode(type='EDGE')

    bm_sel = bmesh.from_edit_mesh(obj.data)
    bm_sel.edges.ensure_lookup_table()
    angle_threshold = math.radians(args["angle_limit"])
    for e in bm_sel.edges:
        # Compute dihedral angle between the two faces sharing this edge
        faces = list(e.link_faces)
        if len(faces) == 2:
            n1 = faces[0].normal
            n2 = faces[1].normal
            dot = max(-1.0, min(1.0, n1.dot(n2)))
            dihedral = math.acos(dot)
            if math.degrees(dihedral) > args["angle_limit"]:
                e.select = True
        elif len(faces) == 1:
            # Boundary edge — select it
            e.select = True
    bmesh.update_edit_mesh(obj.data)

    # Bevel
    bpy.ops.mesh.bevel(
        offset=args["amount"],
        offset_type=args["offset_type"],
        segments=args["segments"],
        profile=args["profile"],
        affect='EDGES',
        clamp_overlap=args["clamp_overlap"],
        miter_outer=args["miter_outer"],
        miter_inner=args["miter_inner"],
    )

    # Return to object mode and get mesh data
    bpy.ops.object.mode_set(mode='OBJECT')
    mesh = obj.data

    # Extract geometry from bmesh (preserves n-gon faces)
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bm.faces.ensure_lookup_table()
    bm.verts.ensure_lookup_table()
    bm.edges.ensure_lookup_table()

    verts = [list(v.co) for v in bm.verts]
    faces = [[v.index for v in f.verts] for f in bm.faces]

    # Compute metrics
    edge_inc = compute_edge_incidence(faces)
    bad_edges = {str(list(k)): v for k, v in edge_inc.items() if v != 2}
    vol = signed_volume(verts, faces)

    # AABB
    if verts:
        xs = [v[0] for v in verts]
        ys = [v[1] for v in verts]
        zs = [v[2] for v in verts]
        aabb = {
            "min": [min(xs), min(ys), min(zs)],
            "max": [max(xs), max(ys), max(zs)],
        }
    else:
        aabb = {"min": [0, 0, 0], "max": [0, 0, 0]}

    # Per-corner patches
    corner_patches = extract_corner_patches(verts, faces, edge_inc, args["size"])

    # Canonical sort of vertices (quantized) for deterministic output
    sorted_verts = sorted([quantize_vec3(v) for v in verts])

    # Face canonical cycles (sorted)
    canonical_faces = sorted([canonical_cycle(f) for f in faces])

    # Blender version info
    bl_info = bpy.app.version_string
    bl_bin = bpy.app.binary_path

    output = {
        "metadata": {
            "blender_version": bl_info,
            "blender_binary_path": bl_bin,
            "params": args,
            "note": "Oracle generated with Blender 4.4.3; PCG source aligns to 5.3-alpha worktree bmesh_bevel.cc. Version mismatch caveat applies.",
        },
        "geometry": {
            "vert_count": len(verts),
            "face_count": len(faces),
            "edge_count": len(bm.edges),
            "aabb": aabb,
            "signed_volume": vol,
            "bad_edges": bad_edges,
            "bad_edge_count": len(bad_edges),
            "boundary_edge_count": sum(1 for v in edge_inc.values() if v == 1),
            "nonmanifold_edge_count": sum(1 for v in edge_inc.values() if v > 2),
        },
        "vertices_sorted": sorted_verts,
        "faces_canonical": [list(f) for f in canonical_faces],
        "corner_patches": corner_patches,
    }

    # Compute canonical hash
    canonical_str = json.dumps({
        "vertices_sorted": sorted_verts,
        "faces_canonical": [list(f) for f in canonical_faces],
    }, sort_keys=True)
    output["canonical_hash"] = hashlib.sha256(canonical_str.encode()).hexdigest()

    # Write output
    with open(args["output"], 'w') as f:
        json.dump(output, f, indent=2, sort_keys=True)

    # Print summary
    print(f"Oracle output: {args['output']}")
    print(f"  verts={len(verts)} faces={len(faces)} edges={len(bm.edges)}")
    print(f"  bad_edges={len(bad_edges)} boundary={sum(1 for v in edge_inc.values() if v == 1)}")
    print(f"  signed_volume={vol:.6f}")
    print(f"  canonical_hash={output['canonical_hash'][:16]}...")
    print(f"  corner_patches={len(corner_patches)}")

    bm.free()


if __name__ == "__main__":
    main()
