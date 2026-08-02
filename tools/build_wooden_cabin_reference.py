#!/usr/bin/env python3
"""Build the reference-locked wooden cabin PCG graph from manifest-backed primitives."""

from __future__ import annotations

import json
from pathlib import Path


OUT = Path("Unity/Assets/PICGGenerator/Scenes/PcgReview_wooden-cabin-reference/wooden-cabin-reference.pcg")


def make_node(nodes, node_id, node_type, x, y, **data):
    data = {"__nodeTitle": data.pop("title", node_id), **data}
    nodes.append({"id": node_id, "type": node_type, "position": {"x": x, "y": y}, "data": data})
    return node_id


def wire(edges, edge_id, source, target, source_handle="out", target_handle="in"):
    edges.append({"id": edge_id, "source": source, "target": target, "sourceHandle": source_handle, "targetHandle": target_handle})


def vec(v):
    return list(v)


def box_chain(nodes, edges, p, lane, row, name, dims, pos, material, rotation=(0, 0, 0), bevel=0.025, uv_axis="y"):
    x = lane * 640
    y = row * 160 + lane * 200
    box = make_node(nodes, f"{p}_box", "CreateBoxMesh", x, y, title=f"{name} Box", width=dims[0], height=dims[1], depth=dims[2])
    sub = make_node(nodes, f"{p}_sub", "SubdivideMesh", x, y + 160, title=f"{name} Subdivide", levels=1, method="simple")
    bev = make_node(nodes, f"{p}_bevel", "BevelMesh", x, y + 320, title=f"{name} Bevel", method="edge", amount=bevel, segments=2, angleLimit=40)
    uv = make_node(nodes, f"{p}_uv", "UVTexture", x, y + 480, title=f"{name} UV", projection="planar", axis=uv_axis, scaleU=0.5, scaleV=0.5, offsetU=0.0, offsetV=0.0)
    mat = make_node(nodes, f"{p}_mat", "AssignMaterial", x + 320, y + 480, title=f"{name} Material", group="", materialName=material)
    place = make_node(nodes, f"{p}_place", "TransformMesh", x + 320, y + 640, title=f"{name} Place", translate=vec(pos), rotation=vec(rotation), scale=[1, 1, 1])
    wire(edges, f"{p}_e_sub", box, sub)
    wire(edges, f"{p}_e_bevel", sub, bev)
    wire(edges, f"{p}_e_uv", bev, uv)
    wire(edges, f"{p}_e_mat", uv, mat)
    wire(edges, f"{p}_e_place", mat, place)
    return place


def cylinder_chain(nodes, edges, p, lane, row, name, dims, pos, material, bevel=0.012):
    x = lane * 640
    y = row * 160 + lane * 200
    src = make_node(nodes, f"{p}_cylinder", "CreateCylinderMesh", x, y, title=f"{name} Cylinder", radius=dims[0], height=dims[1], radialSegments=12, heightSegments=1, capTop=True, capBottom=True)
    bev = make_node(nodes, f"{p}_bevel", "BevelMesh", x, y + 160, title=f"{name} Bevel", method="edge", amount=bevel, segments=2, angleLimit=40)
    uv = make_node(nodes, f"{p}_uv", "UVTexture", x, y + 320, title=f"{name} UV", projection="cylindrical", axis="y", scaleU=0.5, scaleV=0.5, offsetU=0.0, offsetV=0.0)
    mat = make_node(nodes, f"{p}_mat", "AssignMaterial", x + 320, y + 320, title=f"{name} Material", group="", materialName=material)
    place = make_node(nodes, f"{p}_place", "TransformMesh", x + 320, y + 480, title=f"{name} Place", translate=vec(pos), rotation=[0, 0, 0], scale=[1, 1, 1])
    wire(edges, f"{p}_e_bevel", src, bev)
    wire(edges, f"{p}_e_uv", bev, uv)
    wire(edges, f"{p}_e_mat", uv, mat)
    wire(edges, f"{p}_e_place", mat, place)
    return place


def sweep_chain(nodes, edges, p, lane, row, name, profile_points, path_points, material, pos=(0, 0, 0), rotation=(0, 0, 0), bevel=0.018):
    x = lane * 640
    y = row * 160 + lane * 200
    profile = make_node(nodes, f"{p}_profile", "CreateSpline", x, y, title=f"{name} Profile", mode="polyline", closed=True, subdivisions=1, controlPoints=json.dumps(profile_points, separators=(",", ":")), editPlane="none")
    path = make_node(nodes, f"{p}_path", "CreateSpline", x, y + 160, title=f"{name} Path", mode="polyline", closed=False, subdivisions=1, controlPoints=json.dumps(path_points, separators=(",", ":")), editPlane="none")
    sx = x + 320
    sweep = make_node(nodes, f"{p}_sweep", "SweepAlongSpline", sx, y + 160, title=f"{name} Sweep", surfaceShape="crossSection", profilePlane="xy", sampleSpacing=0.08, capStart=True, capEnd=True, upX=0, upY=1, upZ=0, twist=0, scaleStart=1.0, scaleEnd=1.0, shadeMode="auto", cuspAngle=30)
    uv = make_node(nodes, f"{p}_uv", "UVTexture", sx, y + 320, title=f"{name} UV", projection="planar", axis="z", scaleU=0.5, scaleV=0.5, offsetU=0.0, offsetV=0.0)
    mat = make_node(nodes, f"{p}_mat", "AssignMaterial", sx, y + 480, title=f"{name} Material", group="", materialName=material)
    bev = make_node(nodes, f"{p}_bevel", "BevelMesh", sx, y + 640, title=f"{name} Bevel", method="edge", amount=bevel, segments=2, angleLimit=40)
    place = make_node(nodes, f"{p}_place", "TransformMesh", sx, y + 800, title=f"{name} Place", translate=vec(pos), rotation=vec(rotation), scale=[1, 1, 1])
    wire(edges, f"{p}_e_path", path, sweep, target_handle="backbone")
    wire(edges, f"{p}_e_profile", profile, sweep, target_handle="profile")
    wire(edges, f"{p}_e_uv", sweep, uv)
    wire(edges, f"{p}_e_bevel", uv, bev)
    wire(edges, f"{p}_e_mat", bev, mat)
    wire(edges, f"{p}_e_place", mat, place)
    return place


def merge_output(nodes, edges, part_ids, name, y):
    merge_y = max((n["position"]["y"] for n in nodes), default=y) + 320
    merge = make_node(nodes, f"{name.lower()}_merge", "MergeMesh", -640, merge_y, title=f"{name} Assembly")
    for i, part in enumerate(part_ids):
        wire(edges, f"{name.lower()}_e_{i}", part, merge, target_handle="in")
    out = make_node(nodes, f"{name.lower()}_out", "SubgraphOutput", -640, merge_y + 160, title=f"{name} Out")
    wire(edges, f"{name.lower()}_e_out", merge, out, target_handle="mesh")
    return out


def new_subgraph(sid, name):
    return {"id": sid, "name": name, "inputs": [], "outputs": [{"id": "mesh", "name": "Mesh", "pinType": "Mesh"}], "nodes": [], "edges": []}


def build_foundation():
    sg = new_subgraph("foundation", "Foundation")
    parts = [
        box_chain(sg["nodes"], sg["edges"], "foundation_body", 0, 0, "Stone Foundation", (6.25, 0.5, 7.75), (0, 0.25, 0), "cabin_stone", bevel=0.05, uv_axis="y"),
        box_chain(sg["nodes"], sg["edges"], "foundation_front_cap", 1, 0, "Front Stone Cap", (6.35, 0.12, 0.12), (0, 0.56, -3.85), "cabin_stone", bevel=0.02, uv_axis="z"),
        box_chain(sg["nodes"], sg["edges"], "foundation_back_cap", 2, 0, "Rear Stone Cap", (6.35, 0.12, 0.12), (0, 0.56, 3.85), "cabin_stone", bevel=0.02, uv_axis="z"),
    ]
    merge_output(sg["nodes"], sg["edges"], parts, "Foundation", 720)
    return sg


def build_walls():
    sg = new_subgraph("walls", "Walls")
    parts = [
        box_chain(sg["nodes"], sg["edges"], "wall_left", 0, 0, "Left Wall", (0.22, 2.4, 7.5), (-2.89, 1.7, 0), "cabin_wood", bevel=0.025, uv_axis="z"),
        box_chain(sg["nodes"], sg["edges"], "wall_right", 1, 0, "Right Wall", (0.22, 2.4, 7.5), (2.89, 1.7, 0), "cabin_wood", bevel=0.025, uv_axis="z"),
        box_chain(sg["nodes"], sg["edges"], "wall_rear", 2, 0, "Rear Wall", (5.55, 2.4, 0.22), (0, 1.7, 3.64), "cabin_wood", bevel=0.025, uv_axis="x"),
        box_chain(sg["nodes"], sg["edges"], "wall_front_left", 3, 0, "Front Wall Left", (2.05, 2.4, 0.22), (-1.98, 1.7, -3.64), "cabin_wood", bevel=0.025, uv_axis="x"),
        box_chain(sg["nodes"], sg["edges"], "wall_front_right", 4, 0, "Front Wall Right", (2.05, 2.4, 0.22), (1.98, 1.7, -3.64), "cabin_wood", bevel=0.025, uv_axis="x"),
        box_chain(sg["nodes"], sg["edges"], "wall_front_header", 5, 0, "Front Door Header", (1.75, 0.42, 0.22), (0, 2.69, -3.64), "cabin_wood", bevel=0.025, uv_axis="x"),
    ]
    merge_output(sg["nodes"], sg["edges"], parts, "Walls", 1120)
    return sg


def build_roof():
    sg = new_subgraph("roof", "Roof")
    parts = [
        box_chain(sg["nodes"], sg["edges"], "roof_left_plane", 0, 0, "Left Roof Plane", (3.75, 0.16, 8.15), (-1.55, 3.95, 0), "cabin_roof", rotation=(0, 0, 35.5), bevel=0.02, uv_axis="z"),
        box_chain(sg["nodes"], sg["edges"], "roof_right_plane", 1, 0, "Right Roof Plane", (3.75, 0.16, 8.15), (1.55, 3.95, 0), "cabin_roof", rotation=(0, 0, -35.5), bevel=0.02, uv_axis="z"),
        sweep_chain(sg["nodes"], sg["edges"], "front_gable", 2, 0, "Front Gable", [{"x":-2.95,"y":2.88,"z":0},{"x":0,"y":4.98,"z":0},{"x":2.95,"y":2.88,"z":0}], [{"x":0,"y":0,"z":-3.76},{"x":0,"y":0,"z":-3.58}], "cabin_wood", bevel=0.015),
        sweep_chain(sg["nodes"], sg["edges"], "rear_gable", 3, 0, "Rear Gable", [{"x":-2.95,"y":2.88,"z":0},{"x":0,"y":4.98,"z":0},{"x":2.95,"y":2.88,"z":0}], [{"x":0,"y":0,"z":3.58},{"x":0,"y":0,"z":3.76}], "cabin_wood", bevel=0.015),
        box_chain(sg["nodes"], sg["edges"], "front_fascia_left", 4, 0, "Front Left Fascia", (3.62, 0.18, 0.22), (-1.54, 3.95, -4.13), "cabin_door", rotation=(0, 0, 35.5), bevel=0.015, uv_axis="x"),
        box_chain(sg["nodes"], sg["edges"], "front_fascia_right", 5, 0, "Front Right Fascia", (3.62, 0.18, 0.22), (1.54, 3.95, -4.13), "cabin_door", rotation=(0, 0, -35.5), bevel=0.015, uv_axis="x"),
        box_chain(sg["nodes"], sg["edges"], "rear_fascia_left", 0, 10, "Rear Left Fascia", (3.62, 0.18, 0.22), (-1.54, 3.95, 4.13), "cabin_door", rotation=(0, 0, 35.5), bevel=0.015, uv_axis="x"),
        box_chain(sg["nodes"], sg["edges"], "rear_fascia_right", 1, 10, "Rear Right Fascia", (3.62, 0.18, 0.22), (1.54, 3.95, 4.13), "cabin_door", rotation=(0, 0, -35.5), bevel=0.015, uv_axis="x"),
        box_chain(sg["nodes"], sg["edges"], "chimney_body", 2, 10, "Stone Chimney", (0.62, 1.25, 0.62), (1.15, 4.72, 1.15), "cabin_stone", bevel=0.025, uv_axis="y"),
        box_chain(sg["nodes"], sg["edges"], "chimney_cap", 3, 10, "Chimney Cap", (0.78, 0.14, 0.78), (1.15, 5.40, 1.15), "cabin_stone", bevel=0.02, uv_axis="y"),
    ]
    merge_output(sg["nodes"], sg["edges"], parts, "Roof", 1840)
    return sg


def build_porch():
    sg = new_subgraph("porch", "Front Porch")
    parts = [
        box_chain(sg["nodes"], sg["edges"], "porch_deck", 0, 0, "Porch Deck", (5.7, 0.16, 1.35), (0, 0.7, -4.35), "cabin_wood", bevel=0.025, uv_axis="z"),
        box_chain(sg["nodes"], sg["edges"], "porch_roof", 1, 0, "Porch Roof", (6.05, 0.14, 1.40), (0, 3.08, -4.38), "cabin_roof", rotation=(-10.0, 0, 0), bevel=0.02, uv_axis="z"),
        box_chain(sg["nodes"], sg["edges"], "stair_step_1", 2, 0, "Stair Step 1", (1.55, 0.18, 0.34), (0, 0.09, -4.97), "cabin_door", bevel=0.025, uv_axis="z"),
        box_chain(sg["nodes"], sg["edges"], "stair_step_2", 3, 0, "Stair Step 2", (1.55, 0.18, 0.34), (0, 0.29, -4.64), "cabin_door", bevel=0.025, uv_axis="z"),
        box_chain(sg["nodes"], sg["edges"], "stair_step_3", 4, 0, "Stair Step 3", (1.55, 0.18, 0.34), (0, 0.49, -4.31), "cabin_door", bevel=0.025, uv_axis="z"),
        box_chain(sg["nodes"], sg["edges"], "porch_post_left", 5, 0, "Porch Post Left", (0.16, 2.20, 0.16), (-2.55, 1.88, -4.22), "cabin_wood", bevel=0.02, uv_axis="y"),
        box_chain(sg["nodes"], sg["edges"], "porch_post_right", 0, 10, "Porch Post Right", (0.16, 2.20, 0.16), (2.55, 1.88, -4.22), "cabin_wood", bevel=0.02, uv_axis="y"),
        box_chain(sg["nodes"], sg["edges"], "porch_side_post", 1, 10, "Porch Side Post", (0.16, 2.20, 0.16), (2.55, 1.88, -3.55), "cabin_wood", bevel=0.02, uv_axis="y"),
        box_chain(sg["nodes"], sg["edges"], "porch_side_rail", 2, 10, "Porch Side Return Rail", (0.12, 0.12, 0.78), (2.55, 1.30, -3.86), "cabin_wood", bevel=0.015, uv_axis="z"),
        box_chain(sg["nodes"], sg["edges"], "porch_rail_center_left", 3, 10, "Porch Stair Rail Post Left", (0.14, 0.62, 0.14), (-0.48, 1.09, -4.22), "cabin_wood", bevel=0.015, uv_axis="y"),
        box_chain(sg["nodes"], sg["edges"], "porch_rail_center_right", 4, 10, "Porch Stair Rail Post Right", (0.14, 0.62, 0.14), (0.48, 1.09, -4.22), "cabin_wood", bevel=0.015, uv_axis="y"),
        box_chain(sg["nodes"], sg["edges"], "porch_rail_left", 5, 10, "Porch Rail Left", (2.15, 0.12, 0.12), (-1.45, 1.30, -4.22), "cabin_wood", bevel=0.015, uv_axis="x"),
        box_chain(sg["nodes"], sg["edges"], "porch_rail_right", 6, 10, "Porch Rail Right", (2.15, 0.12, 0.12), (1.45, 1.30, -4.22), "cabin_wood", bevel=0.015, uv_axis="x"),
        cylinder_chain(sg["nodes"], sg["edges"], "front_lantern_body", 7, 10, "Front Lantern", (0.07, 0.26), (0.82, 2.12, -3.91), "cabin_door", bevel=0.01),
        box_chain(sg["nodes"], sg["edges"], "front_lantern_cap", 8, 10, "Front Lantern Cap", (0.16, 0.05, 0.16), (0.82, 2.29, -3.91), "cabin_door", bevel=0.01, uv_axis="y"),
    ]
    bal_proto = box_chain(sg["nodes"], sg["edges"], "porch_baluster_proto", 9, 10, "Baluster Prototype", (0.08, 0.56, 0.08), (0, 0, 0), "cabin_wood", bevel=0.012, uv_axis="y")

    def baluster_row(row_id, lane, start_x, end_x):
        path = make_node(
            sg["nodes"],
            f"porch_baluster_{row_id}_path",
            "CreateSpline",
            lane * 640,
            10 * 160,
            title=f"Baluster {row_id.title()} Row Path",
            mode="line",
            closed=False,
            subdivisions=1,
            startX=start_x,
            startY=1.04,
            startZ=-4.22,
            endX=end_x,
            endY=1.04,
            endZ=-4.22,
            controlPoints=json.dumps(
                [
                    {"x": start_x, "y": 1.04, "z": -4.22},
                    {"x": end_x, "y": 1.04, "z": -4.22},
                ],
                separators=(",", ":"),
            ),
            editPlane="none",
        )
        inst = make_node(
            sg["nodes"],
            f"porch_baluster_{row_id}",
            "InstanceAlongSpline",
            lane * 640 + 320,
            10 * 160,
            title=f"Baluster {row_id.title()}",
            spacing=0.42,
            offset=0.0,
            includeEnd=True,
            alignToTangent=True,
            scale=1.0,
        )
        wire(sg["edges"], f"porch_baluster_{row_id}_path_e", path, inst, target_handle="spline")
        wire(sg["edges"], f"porch_baluster_{row_id}_proto_e", bal_proto, inst, target_handle="mesh")
        bal_uv = make_node(
            sg["nodes"],
            f"porch_baluster_{row_id}_uv",
            "UVTexture",
            lane * 640 + 320,
            10 * 160 + 1200,
            title=f"Baluster {row_id.title()} UV",
            projection="planar",
            axis="y",
            scaleU=0.5,
            scaleV=0.5,
            offsetU=0.0,
            offsetV=0.0,
        )
        bal_mat = make_node(
            sg["nodes"],
            f"porch_baluster_{row_id}_mat",
            "AssignMaterial",
            lane * 640 + 640,
            10 * 160 + 1200,
            title=f"Baluster {row_id.title()} Material",
            group="",
            materialName="cabin_wood",
        )
        wire(sg["edges"], f"porch_baluster_{row_id}_uv_e", inst, bal_uv)
        wire(sg["edges"], f"porch_baluster_{row_id}_mat_e", bal_uv, bal_mat)
        return bal_mat

    parts.append(baluster_row("left", 10, -2.4, -0.48))
    parts.append(baluster_row("right", 14, 0.48, 2.4))
    merge_output(sg["nodes"], sg["edges"], parts, "Porch", 2080)
    return sg


def window_assembly(sg, prefix, lane, row, name, center, side=False, material="cabin_wood"):
    if side:
        outer_w, outer_h = 1.02, 1.36
        glass_dims = (0.05, 1.08, 0.82)
        frame_top_dims = (0.14, 0.12, outer_w)
        frame_side_dims = (0.14, outer_h, 0.12)
        frame_positions = [
            (center[0], center[1] + (outer_h - 0.12) * 0.5, center[2]),
            (center[0], center[1] - (outer_h - 0.12) * 0.5, center[2]),
            (center[0], center[1], center[2] - (outer_w - 0.12) * 0.5),
            (center[0], center[1], center[2] + (outer_w - 0.12) * 0.5),
        ]
        frame_dims = [frame_top_dims, frame_top_dims, frame_side_dims, frame_side_dims]
        mullion_v, mullion_h = (0.05, 1.08, 0.06), (0.05, 0.06, 0.82)
    else:
        outer_w, outer_h = 1.12, 1.36
        glass_dims = (0.84, 1.08, 0.05)
        frame_top_dims = (outer_w, 0.12, 0.14)
        frame_side_dims = (0.12, outer_h, 0.14)
        frame_positions = [
            (center[0], center[1] + (outer_h - 0.12) * 0.5, center[2]),
            (center[0], center[1] - (outer_h - 0.12) * 0.5, center[2]),
            (center[0] - (outer_w - 0.12) * 0.5, center[1], center[2]),
            (center[0] + (outer_w - 0.12) * 0.5, center[1], center[2]),
        ]
        frame_dims = [frame_top_dims, frame_top_dims, frame_side_dims, frame_side_dims]
        mullion_v, mullion_h = (0.06, 1.08, 0.05), (0.84, 0.06, 0.05)
    frame_parts = []
    for index, (dims, position) in enumerate(zip(frame_dims, frame_positions)):
        frame_parts.append(
            box_chain(
                sg["nodes"],
                sg["edges"],
                f"{prefix}_frame_{index}",
                lane + index,
                row,
                f"{name} Frame {index + 1}",
                dims,
                position,
                material,
                bevel=0.02,
                uv_axis="z" if not side else "x",
            )
        )
    glass_center = center
    glass = box_chain(sg["nodes"], sg["edges"], f"{prefix}_glass", lane + 4, row, f"{name} Glass", glass_dims, glass_center, "cabin_glass", bevel=0.008, uv_axis="z" if not side else "x")
    vert = box_chain(sg["nodes"], sg["edges"], f"{prefix}_mullion_v", lane + 5, row, f"{name} Vertical Mullion", mullion_v, glass_center, material, bevel=0.008, uv_axis="y")
    horiz = box_chain(sg["nodes"], sg["edges"], f"{prefix}_mullion_h", lane + 6, row, f"{name} Horizontal Mullion", mullion_h, glass_center, material, bevel=0.008, uv_axis="y")
    return frame_parts + [glass, vert, horiz]


def build_openings():
    sg = new_subgraph("openings", "Doors and Windows")
    parts = []
    parts += [
        box_chain(sg["nodes"], sg["edges"], "front_door", 0, 0, "Front Door", (1.02, 2.1, 0.12), (0, 1.55, -3.81), "cabin_door", bevel=0.018, uv_axis="z"),
        box_chain(sg["nodes"], sg["edges"], "front_door_panel", 1, 0, "Door Panel", (0.78, 1.72, 0.045), (0, 1.52, -3.89), "cabin_door", bevel=0.01, uv_axis="z"),
        box_chain(sg["nodes"], sg["edges"], "front_door_panel_top", 2, 0, "Door Upper Panel", (0.3, 0.62, 0.05), (0, 1.95, -3.92), "cabin_door", bevel=0.01, uv_axis="z"),
        box_chain(sg["nodes"], sg["edges"], "front_door_panel_low", 3, 0, "Door Lower Panel", (0.48, 0.72, 0.05), (0, 1.08, -3.92), "cabin_door", bevel=0.01, uv_axis="z"),
    ]
    parts += window_assembly(sg, "front_window_left", 8, 0, "Front Window Left", (-1.55, 1.75, -3.83))
    parts += window_assembly(sg, "front_window_right", 16, 10, "Front Window Right", (1.55, 1.75, -3.83))
    parts += window_assembly(sg, "side_window_front", 24, 10, "Side Window Front", (3.02, 1.72, -1.55), side=True)
    parts += window_assembly(sg, "side_window_rear", 32, 20, "Side Window Rear", (3.02, 1.72, 1.35), side=True)
    parts += window_assembly(sg, "left_side_window", 40, 25, "Left Side Window", (-3.02, 1.72, 0.05), side=True)
    parts += window_assembly(sg, "rear_window", 48, 20, "Rear Window", (0.85, 1.65, 3.82))
    parts += window_assembly(sg, "attic_window_front", 56, 30, "Attic Window Front", (0, 4.08, -3.84))
    parts += window_assembly(sg, "attic_window_rear", 64, 30, "Attic Window Rear", (0, 4.08, 3.84))
    merge_output(sg["nodes"], sg["edges"], parts, "Openings", 5600)
    return sg


def build_details():
    sg = new_subgraph("details", "Siding and Trim")
    parts = [
        box_chain(sg["nodes"], sg["edges"], "corner_post_left", 0, 0, "Corner Post Left", (0.18, 2.65, 0.18), (-2.96, 1.85, -3.76), "cabin_door", bevel=0.018, uv_axis="y"),
        box_chain(sg["nodes"], sg["edges"], "corner_post_right", 1, 0, "Corner Post Right", (0.18, 2.65, 0.18), (2.96, 1.85, -3.76), "cabin_door", bevel=0.018, uv_axis="y"),
        box_chain(sg["nodes"], sg["edges"], "front_sill", 2, 0, "Front Sill", (5.9, 0.12, 0.16), (0, 0.78, -3.82), "cabin_door", bevel=0.015, uv_axis="x"),
        box_chain(sg["nodes"], sg["edges"], "rear_sill", 3, 0, "Rear Sill", (5.9, 0.12, 0.16), (0, 0.78, 3.82), "cabin_door", bevel=0.015, uv_axis="x"),
        box_chain(sg["nodes"], sg["edges"], "side_sill", 4, 0, "Side Sill", (0.16, 0.12, 7.35), (3.02, 0.78, 0), "cabin_door", bevel=0.015, uv_axis="z"),
        box_chain(sg["nodes"], sg["edges"], "front_gable_trim", 5, 0, "Front Gable Trim", (5.9, 0.1, 0.08), (0, 2.96, -3.86), "cabin_door", bevel=0.01, uv_axis="x"),
        box_chain(sg["nodes"], sg["edges"], "rear_gable_trim", 0, 10, "Rear Gable Trim", (5.9, 0.1, 0.08), (0, 2.96, 3.86), "cabin_door", bevel=0.01, uv_axis="x"),
    ]
    siding_y = [0.74, 1.10, 1.46, 1.82, 2.18, 2.54]
    detail_lane = 6
    for i, sy in enumerate(siding_y):
        parts.append(box_chain(sg["nodes"], sg["edges"], f"siding_front_{i}", detail_lane, 20 + i, f"Front Siding Board {i + 1}", (5.72, 0.035, 0.035), (0, sy, -3.79), "cabin_wood", bevel=0.006, uv_axis="x")); detail_lane += 1
        parts.append(box_chain(sg["nodes"], sg["edges"], f"siding_rear_{i}", detail_lane, 30 + i, f"Rear Siding Board {i + 1}", (5.72, 0.035, 0.035), (0, sy, 3.79), "cabin_wood", bevel=0.006, uv_axis="x")); detail_lane += 1
        parts.append(box_chain(sg["nodes"], sg["edges"], f"siding_left_{i}", detail_lane, 40 + i, f"Left Siding Board {i + 1}", (0.035, 0.035, 7.32), (-3.02, sy, 0), "cabin_wood", bevel=0.006, uv_axis="z")); detail_lane += 1
        parts.append(box_chain(sg["nodes"], sg["edges"], f"siding_right_{i}", detail_lane, 50 + i, f"Right Siding Board {i + 1}", (0.035, 0.035, 7.32), (3.02, sy, 0), "cabin_wood", bevel=0.006, uv_axis="z")); detail_lane += 1
    gable_widths = [5.15, 4.10, 2.85, 1.80]
    for i, (gy, gw) in enumerate(zip([3.15, 3.52, 3.89, 4.26], gable_widths)):
        parts.append(box_chain(sg["nodes"], sg["edges"], f"gable_siding_front_{i}", detail_lane, 70 + i, f"Front Gable Board {i + 1}", (gw, 0.035, 0.035), (0, gy, -3.87), "cabin_wood", bevel=0.006, uv_axis="x")); detail_lane += 1
        parts.append(box_chain(sg["nodes"], sg["edges"], f"gable_siding_rear_{i}", detail_lane, 80 + i, f"Rear Gable Board {i + 1}", (gw, 0.035, 0.035), (0, gy, 3.87), "cabin_wood", bevel=0.006, uv_axis="x")); detail_lane += 1
    merge_output(sg["nodes"], sg["edges"], parts, "Details", 1440)
    return sg


def build_graph():
    subgraphs = [build_foundation(), build_walls(), build_roof(), build_porch(), build_openings(), build_details()]
    nodes = []
    edges = []
    root_specs = [
        ("foundation", "Foundation", -800),
        ("walls", "Walls", -480),
        ("roof", "Roof", -160),
        ("porch", "Front Porch", 160),
        ("openings", "Doors and Windows", 480),
        ("details", "Siding and Trim", 800),
    ]
    for sid, title, x in root_specs:
        make_node(nodes, sid, "Subgraph", x, 0, title=title, subgraphId=sid)
    merge = make_node(nodes, "cabin_assembly", "MergeMesh", 0, 160, title="Cabin Assembly")
    for sid, _, _ in root_specs:
        wire(edges, f"{sid}_root_merge", sid, "cabin_assembly", source_handle="mesh", target_handle="in")
    out = make_node(nodes, "output", "Output", 0, 320, title="Output", label="Wooden Cabin")
    wire(edges, "assembly_output", merge, out)
    return {"version": "1.0", "nodes": nodes, "edges": edges, "parameters": [], "subgraphs": subgraphs}


def main():
    graph = build_graph()
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(graph, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"wrote {OUT} with {len(graph['nodes'])} root nodes, {len(graph['edges'])} root edges, {len(graph['subgraphs'])} subgraphs")
    for sg in graph["subgraphs"]:
        print(f"  {sg['id']}: {len(sg['nodes'])} nodes / {len(sg['edges'])} edges")


if __name__ == "__main__":
    main()
