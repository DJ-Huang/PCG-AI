#!/usr/bin/env python3
"""Generate ghost-protocol-glock.pcg from Glock18 traced profile stations."""
import json
from pathlib import Path

ROW = 160
COL = 320
SPINE = 200

# Traced cross-section stations (X, list of (y,z)) — Glock-18 adapter, not generic pistol
SHELL_STATIONS = [
    (-0.034, [(0.002, -0.013), (0.012, -0.014), (0.042, -0.013), (0.085, -0.011),
              (0.088, 0.011), (0.042, 0.013), (0.012, 0.013), (0.002, 0.012)]),
    (-0.014, [(0.052, -0.014), (0.068, -0.013), (0.092, -0.011), (0.095, 0.010),
              (0.068, 0.014), (0.052, 0.013)]),
    (0.028, [(0.052, -0.011), (0.078, -0.010), (0.102, -0.009), (0.118, 0.009),
             (0.078, 0.012), (0.052, 0.011)]),
    (0.078, [(0.102, -0.016), (0.118, -0.016), (0.133, -0.015), (0.133, 0.015),
             (0.118, 0.016), (0.102, 0.016)]),
    (0.132, [(0.112, -0.014), (0.130, -0.014), (0.136, -0.012), (0.138, 0.004),
             (0.135, 0.016), (0.118, 0.016), (0.112, 0.014)]),
    (0.148, [(0.114, -0.012), (0.128, -0.011), (0.132, -0.008), (0.132, 0.008),
             (0.128, 0.011), (0.114, 0.010)]),
]

# Dust cover slimmer inner stations (receiver vs dust cover variable section)
DUST_STATIONS = [
    (-0.014, [(0.055, -0.010), (0.078, -0.009), (0.095, 0.008), (0.078, 0.010), (0.055, 0.009)]),
    (0.028, [(0.055, -0.009), (0.078, -0.008), (0.102, -0.007), (0.118, 0.007),
             (0.078, 0.009), (0.055, 0.008)]),
    (0.078, [(0.102, -0.012), (0.118, -0.012), (0.128, -0.011), (0.128, 0.011),
             (0.118, 0.012), (0.102, 0.012)]),
]

GRIP_STATIONS = [
    (-0.034, [(0.002, -0.012), (0.012, -0.013), (0.042, -0.012), (0.085, -0.010),
              (0.088, 0.010), (0.042, 0.012), (0.012, 0.012), (0.002, 0.011)]),
    (-0.022, [(0.012, -0.012), (0.042, -0.013), (0.068, -0.012), (0.070, 0.010),
              (0.042, 0.012), (0.012, 0.011)]),
    (-0.010, [(0.042, -0.013), (0.068, -0.012), (0.088, -0.010), (0.088, 0.010),
              (0.068, 0.012), (0.042, 0.012)]),
]


def cp_json(x: float, y: float, z: float) -> dict:
    return {"x": x, "y": y, "z": z}


def profile_spline_node(pid: str, title: str, x: float, yz_pts: list, lane_x: int, row_y: int):
    pts = [cp_json(x, y, z) for y, z in yz_pts]
    return {
        "id": pid,
        "type": "CreateSpline",
        "position": {"x": lane_x, "y": row_y},
        "data": {
            "__nodeTitle": title,
            "mode": "catmullRom",
            "closed": True,
            "subdivisions": 10,
            "startX": x,
            "startY": 0,
            "startZ": 0,
            "endX": x,
            "endY": 0,
            "endZ": 0,
            "controlPoints": json.dumps(pts),
            "editPlane": "yz",
        },
    }


def node(id_, typ, title, lane_x, row_y, data=None):
    d = {"__nodeTitle": title}
    if data:
        d.update(data)
    return {"id": id_, "type": typ, "position": {"x": lane_x, "y": row_y}, "data": d}


def edge(id_, src, tgt, sh="out", th="in"):
    return {"id": id_, "source": src, "target": tgt, "sourceHandle": sh, "targetHandle": th}


def build_profile_chain(stations, prefix, lane_x, loft_id, mat_name=None, scale=1.0):
    nodes, edges = [], []
    profile_ids = []
    for i, (x, yz) in enumerate(stations):
        pid = f"{prefix}_profile_{i}"
        profile_ids.append(pid)
        nodes.append(profile_spline_node(pid, f"{prefix} Profile {i}", x, yz, lane_x + i * 0, i * ROW))
    loft_row = len(stations) * ROW
    nodes.append(node(loft_id, "LoftMesh", f"{prefix} Loft", lane_x, loft_row, {
        "columns": 24, "sortAxis": "x", "closedProfile": True,
        "capStart": False, "capEnd": False, "autoAlign": True, "shadeMode": "auto",
    }))
    for i, pid in enumerate(profile_ids):
        edges.append(edge(f"e_{prefix}_p{i}_loft", pid, loft_id, "out", "profiles"))
    bevel_row = loft_row + ROW
    bevel_id = f"{prefix}_bevel"
    nodes.append(node(bevel_id, "BevelMesh", f"{prefix} Bevel", lane_x, bevel_row, {
        "amount": 0.003, "segments": 2,
    }))
    edges.append(edge(f"e_{prefix}_loft_bv", loft_id, bevel_id))
    out_id = bevel_id
    next_row = bevel_row + ROW
    if mat_name:
        mat_id = f"{prefix}_mat"
        nodes.append(node(mat_id, "AssignMaterial", f"{prefix} Mat", lane_x, next_row, {
            "materialName": mat_name,
        }))
        edges.append(edge(f"e_{prefix}_bv_mat", bevel_id, mat_id))
        out_id = mat_id
        next_row += ROW
    return nodes, edges, out_id, next_row


def subgraph_shell():
    nodes, edges, out_id, _ = build_profile_chain(
        SHELL_STATIONS, "shell", SPINE, "shell_loft", "inner_core", scale=1.04)
    # Outer translucent shell — scaled profiles
    outer_stations = []
    for x, yz in SHELL_STATIONS:
        outer_stations.append((x, [(y, z * 1.06) for y, z in yz]))
    on, oe, outer_out, _ = build_profile_chain(
        outer_stations, "outer", SPINE + COL, "outer_loft", "ruby_shell", scale=1.06)
    nodes += on
    edges += oe
    merge = node("shell_merge", "MergeMesh", "Shell Merge", SPINE + COL // 2, len(SHELL_STATIONS) * ROW + ROW * 3)
    nodes.append(merge)
    edges.append(edge("e_shell_in", out_id, "shell_merge"))
    edges.append(edge("e_outer_in", outer_out, "shell_merge"))
    out = node("shell_out", "SubgraphOutput", "Shell Out", SPINE + COL // 2, len(SHELL_STATIONS) * ROW + ROW * 4)
    nodes.append(out)
    edges.append(edge("e_shell_out", "shell_merge", "shell_out", "out", "mesh"))
    return nodes, edges


def subgraph_frame():
    nodes, edges, out_id, _ = build_profile_chain(
        DUST_STATIONS, "dust", SPINE, "dust_loft", "inner_core")
    # Rail slots — 4 recessed boxes on dust cover
    slot_y = len(DUST_STATIONS) * ROW + ROW * 4
    slot_ids = []
    for i, sx in enumerate([0.015, 0.025, 0.035, 0.045]):
        sid = f"rail_slot_{i}"
        slot_ids.append(sid)
        nodes.append(node(sid, "CreateBoxMesh", f"Rail Slot {i}", SPINE + i * 80, slot_y, {
            "width": 0.008, "height": 0.003, "depth": 0.006,
        }))
        xf = f"rail_xf_{i}"
        nodes.append(node(xf, "TransformMesh", f"Rail Slot {i} Place", SPINE + i * 80, slot_y + ROW, {
            "translateX": sx, "translateY": 0.072, "translateZ": -0.012,
        }))
        edges.append(edge(f"e_rail_box_{i}", sid, xf))
    merge = node("frame_merge", "MergeMesh", "Frame Merge", SPINE, slot_y + ROW * 2)
    nodes.append(merge)
    edges.append(edge("e_frame_loft_m", out_id, "frame_merge"))
    for i, xf in enumerate([f"rail_xf_{i}" for i in range(4)]):
        edges.append(edge(f"e_rail_m_{i}", xf, "frame_merge"))
    out = node("frame_out", "SubgraphOutput", "Frame Out", SPINE, slot_y + ROW * 3)
    nodes.append(out)
    edges.append(edge("e_frame_out", "frame_merge", "frame_out", "out", "mesh"))
    return nodes, edges


def subgraph_grip():
    nodes, edges, out_id, _ = build_profile_chain(
        GRIP_STATIONS, "grip", SPINE, "grip_loft")
    rake = node("grip_rake", "TransformMesh", "Grip 22 Rake", SPINE, len(GRIP_STATIONS) * ROW + ROW * 3, {
        "translateX": -0.028, "translateY": 0.041, "translateZ": 0, "rotationZ": -22,
    })
    nodes.append(rake)
    edges.append(edge("e_grip_rake", out_id, "grip_rake"))
    # Grip stria ribs via instance along spline
    rib_path = node("grip_rib_path", "CreateSpline", "Grip Rib Path", SPINE + COL, 0, {
        "mode": "catmullRom", "closed": False, "subdivisions": 8,
        "startX": -0.028, "startY": 0.018, "startZ": 0.014,
        "endX": -0.032, "endY": 0.055, "endZ": 0.013,
        "controlPoints": json.dumps([
            cp_json(-0.028, 0.018, 0.014), cp_json(-0.030, 0.035, 0.014),
            cp_json(-0.032, 0.055, 0.013),
        ]),
    })
    rib_proto = node("grip_rib_proto", "CreateBoxMesh", "Grip Rib Proto", SPINE + COL, ROW, {
        "width": 0.0015, "height": 0.012, "depth": 0.002,
    })
    rib_inst = node("grip_ribs", "InstanceAlongSpline", "Grip Stria Ribs", SPINE + COL, ROW * 2, {
        "spacing": 0.004, "alignToTangent": True,
    })
    nodes += [rib_path, rib_proto, rib_inst]
    edges += [
        edge("e_rib_path", "grip_rib_path", "grip_ribs", "out", "spline"),
        edge("e_rib_proto", "grip_rib_proto", "grip_ribs", "out", "mesh"),
    ]
    merge = node("grip_merge", "MergeMesh", "Grip Merge", SPINE + COL // 2, ROW * 4)
    nodes.append(merge)
    edges.append(edge("e_grip_rake_m", "grip_rake", "grip_merge"))
    edges.append(edge("e_grip_ribs_m", "grip_ribs", "grip_merge"))
    mat = node("grip_mat", "AssignMaterial", "Grip Shell Mat", SPINE + COL // 2, ROW * 5, {
        "materialName": "ruby_shell",
    })
    nodes.append(mat)
    edges.append(edge("e_grip_mat", "grip_merge", "grip_mat"))
    out = node("grip_out", "SubgraphOutput", "Grip Out", SPINE + COL // 2, ROW * 6)
    nodes.append(out)
    edges.append(edge("e_grip_out", "grip_mat", "grip_out", "out", "mesh"))
    return nodes, edges


def subgraph_internals():
    nodes, edges = [], []
    lx = SPINE
    # Barrel revolve profile (chamber swell, hood, muzzle bore)
    barrel_profile = node("barrel_profile", "CreateSpline", "Barrel Profile", lx, 0, {
        "mode": "catmullRom", "closed": False, "subdivisions": 10,
        "startX": 0, "startY": 0.004, "startZ": 0,
        "endX": 0, "endY": 0.020, "endZ": 0,
        "controlPoints": json.dumps([
            cp_json(0, 0.004, 0), cp_json(0, 0.006, 0), cp_json(0, 0.008, 0.004),
            cp_json(0, 0.011, 0.008), cp_json(0, 0.009, 0.014), cp_json(0, 0.006, 0.018),
            cp_json(0, 0.004, 0.020),
        ]),
        "editPlane": "yz",
    })
    barrel_rev = node("barrel_rev", "RevolveMesh", "Barrel Revolve", lx, ROW, {
        "axis": "x", "segments": 20, "capStart": True, "capEnd": True,
    })
    barrel_xf = node("barrel_xf", "TransformMesh", "Barrel Place", lx, ROW * 2, {
        "translateX": 0.075, "translateY": 0.118, "translateZ": 0, "rotationZ": 90,
    })
    nodes += [barrel_profile, barrel_rev, barrel_xf]
    edges += [
        edge("e_bp_br", "barrel_profile", "barrel_rev", "out", "profile"),
        edge("e_br_bx", "barrel_rev", "barrel_xf"),
    ]
    # Recoil spring — spiral + sweep
    spring_sp = node("spring_spiral", "CreateSpiralSpline", "Spring Spiral", lx + COL, 0, {
        "radius": 0.0045, "pitch": 0.0025, "turns": 6, "pointsPerTurn": 12, "axis": "x",
    })
    spring_sw = node("spring_sweep", "SweepAlongSpline", "Spring Coil", lx + COL, ROW, {
        "surfaceShape": "circle", "radius": 0.0018, "columns": 6, "sampleSpacing": 0.15,
        "capStart": False, "capEnd": False, "shadeMode": "auto",
    })
    spring_xf = node("spring_xf", "TransformMesh", "Spring Place", lx + COL, ROW * 2, {
        "translateX": 0.04, "translateY": 0.115, "translateZ": 0,
    })
    nodes += [spring_sp, spring_sw, spring_xf]
    edges += [
        edge("e_sp_sw", "spring_spiral", "spring_sweep", "out", "backbone"),
        edge("e_sw_sx", "spring_sweep", "spring_xf"),
    ]
    # Magazine + feed lips
    mag = node("mag_box", "CreateBoxMesh", "Magazine Body", lx + COL * 2, 0, {
        "width": 0.022, "height": 0.078, "depth": 0.024,
    })
    mag_xf = node("mag_xf", "TransformMesh", "Mag Place", lx + COL * 2, ROW, {
        "translateX": -0.028, "translateY": 0.039, "translateZ": 0, "rotationZ": -22,
    })
    lip_l = node("mag_lip_l", "CreateBoxMesh", "Feed Lip L", lx + COL * 2, ROW * 2, {
        "width": 0.003, "height": 0.006, "depth": 0.008,
    })
    lip_l_xf = node("mag_lip_l_xf", "TransformMesh", "Lip L Place", lx + COL * 2, ROW * 3, {
        "translateX": -0.034, "translateY": 0.078, "translateZ": -0.006, "rotationZ": -22,
    })
    lip_r = node("mag_lip_r", "CreateBoxMesh", "Feed Lip R", lx + COL * 2 + 160, ROW * 2, {
        "width": 0.003, "height": 0.006, "depth": 0.008,
    })
    lip_r_xf = node("mag_lip_r_xf", "TransformMesh", "Lip R Place", lx + COL * 2 + 160, ROW * 3, {
        "translateX": -0.034, "translateY": 0.078, "translateZ": 0.006, "rotationZ": -22,
    })
    nodes += [mag, mag_xf, lip_l, lip_l_xf, lip_r, lip_r_xf]
    edges += [
        edge("e_mag_xf", "mag_box", "mag_xf"),
        edge("e_lip_l", "mag_lip_l", "mag_lip_l_xf"),
        edge("e_lip_r", "mag_lip_r", "mag_lip_r_xf"),
    ]
    # Striker
    striker = node("striker", "CreateBoxMesh", "Striker", lx, ROW * 3, {
        "width": 0.012, "height": 0.004, "depth": 0.006,
    })
    striker_xf = node("striker_xf", "TransformMesh", "Striker Place", lx, ROW * 4, {
        "translateX": -0.008, "translateY": 0.108, "translateZ": 0,
    })
    nodes += [striker, striker_xf]
    edges += [edge("e_striker", "striker", "striker_xf")]
    merge = node("int_merge", "MergeMesh", "Internals Merge", lx + COL, ROW * 5)
    nodes.append(merge)
    for src in ["barrel_xf", "spring_xf", "mag_xf", "mag_lip_l_xf", "mag_lip_r_xf", "striker_xf"]:
        edges.append(edge(f"e_int_{src}", src, "int_merge"))
    mat = node("int_mat", "AssignMaterial", "Internals Metal Mat", lx + COL, ROW * 6, {
        "materialName": "dark_metal",
    })
    nodes.append(mat)
    edges.append(edge("e_int_mat", "int_merge", "int_mat"))
    out = node("int_out", "SubgraphOutput", "Internals Out", lx + COL, ROW * 7)
    nodes.append(out)
    edges.append(edge("e_int_out", "int_mat", "int_out", "out", "mesh"))
    return nodes, edges


def subgraph_trigger_group():
    nodes, edges = [], []
    lx = SPINE
  # Trigger shoe (curved) — small sweep
    shoe_path = node("shoe_path", "CreateSpline", "Trigger Shoe Path", lx, 0, {
        "mode": "catmullRom", "closed": False, "subdivisions": 6,
        "startX": -0.008, "startY": 0.058, "startZ": 0.010,
        "endX": -0.002, "endY": 0.066, "endZ": 0.011,
        "controlPoints": json.dumps([
            cp_json(-0.008, 0.058, 0.010), cp_json(-0.004, 0.062, 0.012),
            cp_json(-0.002, 0.066, 0.011),
        ]),
    })
    shoe_sw = node("shoe_sweep", "SweepAlongSpline", "Trigger Shoe Sweep", lx, ROW, {
        "surfaceShape": "rectangle", "width": 0.005, "height": 0.014, "sampleSpacing": 0.1,
        "capStart": True, "capEnd": True, "shadeMode": "auto",
    })
    safety = node("safety_lever", "CreateBoxMesh", "Safety Lever", lx + COL, 0, {
        "width": 0.004, "height": 0.008, "depth": 0.003,
    })
    safety_xf = node("safety_xf", "TransformMesh", "Safety Place", lx + COL, ROW, {
        "translateX": -0.005, "translateY": 0.064, "translateZ": 0.014,
    })
    connector = node("connector_bar", "CreateBoxMesh", "Connector Bar", lx + COL, ROW * 2, {
        "width": 0.018, "height": 0.003, "depth": 0.004,
    })
    conn_xf = node("conn_xf", "TransformMesh", "Connector Place", lx + COL, ROW * 3, {
        "translateX": -0.006, "translateY": 0.070, "translateZ": 0,
    })
    nodes += [shoe_path, shoe_sw, safety, safety_xf, connector, conn_xf]
    edges += [
        edge("e_shoe_p", "shoe_path", "shoe_sweep", "out", "backbone"),
        edge("e_safety", "safety_lever", "safety_xf"),
        edge("e_conn", "connector_bar", "conn_xf"),
    ]
    merge = node("tg_merge", "MergeMesh", "Trigger Group Merge", lx + COL // 2, ROW * 4)
    nodes.append(merge)
    for src in ["shoe_sweep", "safety_xf", "conn_xf"]:
        edges.append(edge(f"e_tg_{src}", src, "tg_merge"))
    mat = node("tg_mat", "AssignMaterial", "Trigger Polymer Mat", lx + COL // 2, ROW * 5, {
        "materialName": "trigger_polymer",
    })
    nodes.append(mat)
    edges.append(edge("e_tg_mat", "tg_merge", "tg_mat"))
    out = node("tg_out", "SubgraphOutput", "Trigger Group Out", lx + COL // 2, ROW * 6)
    nodes.append(out)
    edges.append(edge("e_tg_out", "tg_mat", "tg_out", "out", "mesh"))
    return nodes, edges


def subgraph_slide():
    nodes, edges = [], []
    lx = SPINE
    # Slide loft (upper section only)
    slide_stations = [
        (0.078, [(0.102, -0.016), (0.118, -0.016), (0.133, -0.015), (0.133, 0.015),
                 (0.118, 0.016), (0.102, 0.016)]),
        (0.132, [(0.112, -0.014), (0.130, -0.014), (0.136, -0.012), (0.138, 0.004),
                 (0.135, 0.016), (0.118, 0.016), (0.112, 0.014)]),
        (0.148, [(0.114, -0.012), (0.128, -0.011), (0.132, -0.008), (0.132, 0.008),
                 (0.128, 0.011), (0.114, 0.010)]),
    ]
    pn, pe, loft_out, _ = build_profile_chain(slide_stations, "slide", lx, "slide_loft")
    nodes += pn
    edges += pe
    # Ejection port boolean cut
    port_cut = node("eject_cutter", "CreateBoxMesh", "Ejection Cutter", lx + COL, 0, {
        "width": 0.028, "height": 0.012, "depth": 0.018,
    })
    port_xf = node("eject_cutter_xf", "TransformMesh", "Ejection Cutter Place", lx + COL, ROW, {
        "translateX": 0.025, "translateY": 0.122, "translateZ": 0.01,
    })
    port_bool = node("eject_bool", "BooleanMesh", "Ejection Port Cut", lx + COL, ROW * 2, {
        "operation": "subtract",
    })
    nodes += [port_cut, port_xf, port_bool]
    edges += [
        edge("e_cut_xf", "eject_cutter", "eject_cutter_xf"),
        edge("e_loft_bool_a", loft_out, "eject_bool", "out", "a"),
        edge("e_loft_bool_b", "eject_cutter_xf", "eject_bool", "out", "b"),
    ]
    # Slide serrations — instance along rear edge
    serr_path = node("serr_path", "CreateSpline", "Serration Path", lx + COL * 2, 0, {
        "mode": "catmullRom", "closed": False, "subdivisions": 6,
        "startX": -0.018, "startY": 0.119, "startZ": -0.014,
        "endX": -0.006, "endY": 0.119, "endZ": -0.014,
        "controlPoints": json.dumps([
            cp_json(-0.018, 0.119, -0.014), cp_json(-0.012, 0.119, -0.014),
            cp_json(-0.006, 0.119, -0.014),
        ]),
    })
    serr_proto = node("serr_proto", "CreateBoxMesh", "Serration Proto", lx + COL * 2, ROW, {
        "width": 0.004, "height": 0.022, "depth": 0.002,
    })
    serr_inst = node("serrations", "InstanceAlongSpline", "Slide Serrations", lx + COL * 2, ROW * 2, {
        "spacing": 0.005, "alignToTangent": True,
    })
    nodes += [serr_path, serr_proto, serr_inst]
    edges += [
        edge("e_serr_p", "serr_path", "serrations", "out", "spline"),
        edge("e_serr_proto", "serr_proto", "serrations", "out", "mesh"),
    ]
    merge = node("slide_merge", "MergeMesh", "Slide Merge", lx + COL, ROW * 4)
    nodes.append(merge)
    edges.append(edge("e_port_m", "eject_bool", "slide_merge"))
    edges.append(edge("e_serr_m", "serrations", "slide_merge"))
    mat = node("slide_mat", "AssignMaterial", "Slide Mat", lx + COL, ROW * 5, {
        "materialName": "inner_core",
    })
    nodes.append(mat)
    edges.append(edge("e_slide_mat", "slide_merge", "slide_mat"))
    out = node("slide_out", "SubgraphOutput", "Slide Out", lx + COL, ROW * 6)
    nodes.append(out)
    edges.append(edge("e_slide_out", "slide_mat", "slide_out", "out", "mesh"))
    return nodes, edges


def subgraph_trigger_guard():
    nodes, edges = [], []
    lx = SPINE
    guard_path = node("guard_path", "CreateSpline", "Guard Bow Path", lx, 0, {
        "mode": "catmullRom", "closed": True, "subdivisions": 12,
        "startX": -0.016, "startY": 0.066, "startZ": 0,
        "endX": -0.016, "endY": 0.066, "endZ": 0,
        "controlPoints": json.dumps([
            cp_json(-0.016, 0.066, 0), cp_json(-0.012, 0.052, 0),
            cp_json(0.002, 0.052, 0), cp_json(0.006, 0.066, 0),
        ]),
    })
    guard_sw = node("guard_sweep", "SweepAlongSpline", "Guard Bow Sweep", lx, ROW, {
        "surfaceShape": "circle", "radius": 0.003, "columns": 8, "sampleSpacing": 0.12,
        "capStart": False, "capEnd": False, "shadeMode": "auto",
    })
    nodes += [guard_path, guard_sw]
    edges.append(edge("e_guard_p", "guard_path", "guard_sweep", "out", "backbone"))
    mat = node("guard_mat", "AssignMaterial", "Guard Mat", lx, ROW * 2, {
        "materialName": "ruby_shell",
    })
    nodes.append(mat)
    edges.append(edge("e_guard_mat", "guard_sweep", "guard_mat"))
    out = node("guard_out", "SubgraphOutput", "Guard Out", lx, ROW * 3)
    nodes.append(out)
    edges.append(edge("e_guard_out", "guard_mat", "guard_out", "out", "mesh"))
    return nodes, edges


def build():
    subgraphs = [
        {"id": "slide", "name": "Slide", "inputs": [], "outputs": [{"id": "mesh", "name": "Mesh", "pinType": "SpatialMesh"}],
         "nodes": subgraph_slide()[0], "edges": subgraph_slide()[1]},
        {"id": "frame_receiver", "name": "Frame Receiver", "inputs": [], "outputs": [{"id": "mesh", "name": "Mesh", "pinType": "SpatialMesh"}],
         "nodes": subgraph_frame()[0], "edges": subgraph_frame()[1]},
        {"id": "grip_assembly", "name": "Grip Assembly", "inputs": [], "outputs": [{"id": "mesh", "name": "Mesh", "pinType": "SpatialMesh"}],
         "nodes": subgraph_grip()[0], "edges": subgraph_grip()[1]},
        {"id": "internals", "name": "Internals", "inputs": [], "outputs": [{"id": "mesh", "name": "Mesh", "pinType": "SpatialMesh"}],
         "nodes": subgraph_internals()[0], "edges": subgraph_internals()[1]},
        {"id": "trigger_group", "name": "Trigger Group", "inputs": [], "outputs": [{"id": "mesh", "name": "Mesh", "pinType": "SpatialMesh"}],
         "nodes": subgraph_trigger_group()[0], "edges": subgraph_trigger_group()[1]},
        {"id": "trigger_guard", "name": "Trigger Guard", "inputs": [], "outputs": [{"id": "mesh", "name": "Mesh", "pinType": "SpatialMesh"}],
         "nodes": subgraph_trigger_guard()[0], "edges": subgraph_trigger_guard()[1]},
        {"id": "shell_outer", "name": "Shell Outer", "inputs": [], "outputs": [{"id": "mesh", "name": "Mesh", "pinType": "SpatialMesh"}],
         "nodes": subgraph_shell()[0], "edges": subgraph_shell()[1]},
    ]

    root_nodes = [
        node("sg_slide", "Subgraph", "Slide Module", SPINE, 0, {"subgraphId": "slide"}),
        node("sg_frame", "Subgraph", "Frame Module", SPINE + COL, 0, {"subgraphId": "frame_receiver"}),
        node("sg_grip", "Subgraph", "Grip Module", SPINE + COL * 2, 0, {"subgraphId": "grip_assembly"}),
        node("sg_internals", "Subgraph", "Internals Module", SPINE + COL * 3, 0, {"subgraphId": "internals"}),
        node("sg_trigger", "Subgraph", "Trigger Group Module", SPINE, ROW, {"subgraphId": "trigger_group"}),
        node("sg_guard", "Subgraph", "Trigger Guard Module", SPINE + COL, ROW, {"subgraphId": "trigger_guard"}),
        node("sg_shell", "Subgraph", "Shell Module", SPINE + COL * 2, ROW, {"subgraphId": "shell_outer"}),
        node("assembly_merge", "MergeMesh", "Assembly Merge", SPINE + COL, ROW * 2),
        node("side_tex", "ImageTexture", "Side Albedo Tex", SPINE - COL, ROW * 2, {
            "texture": "Assets/PcgPlugin/Examples/PCGDemo/ghost-protocol-glock/T_ghost-protocol-glock_side_albedo.png",
            "repeatX": 1.0, "repeatY": 1.0,
        }),
        node("project_tex", "ProjectTexture", "Project Side Finish", SPINE + COL, ROW * 3, {
            "direction": "z", "scaleU": 0.22, "scaleV": 0.14, "offsetU": -0.02, "offsetV": 0.0,
        }),
        node("out", "Output", "Output", SPINE + COL, ROW * 4, {"label": "Ghost Protocol Glock 18"}),
    ]
    root_edges = [
        edge("e_sg_slide", "sg_slide", "assembly_merge", "mesh", "in"),
        edge("e_sg_frame", "sg_frame", "assembly_merge", "mesh", "in"),
        edge("e_sg_grip", "sg_grip", "assembly_merge", "mesh", "in"),
        edge("e_sg_int", "sg_internals", "assembly_merge", "mesh", "in"),
        edge("e_sg_trig", "sg_trigger", "assembly_merge", "mesh", "in"),
        edge("e_sg_guard", "sg_guard", "assembly_merge", "mesh", "in"),
        edge("e_sg_shell", "sg_shell", "assembly_merge", "mesh", "in"),
        edge("e_tex_proj", "side_tex", "project_tex", "out", "texture"),
        edge("e_merge_proj", "assembly_merge", "project_tex"),
        edge("e_proj_out", "project_tex", "out"),
    ]

    doc = {
        "version": "1.0",
        "parameters": [],
        "nodes": root_nodes,
        "edges": root_edges,
        "subgraphs": subgraphs,
    }
    return doc


def main():
    out_paths = [
        Path(__file__).resolve().parent.parent / "examples" / "ghost-protocol-glock.pcg",
        Path(__file__).resolve().parent.parent / "Unity/Assets/PcgPlugin/Examples/PCGDemo/ghost-protocol-glock/ghost-protocol-glock.pcg",
    ]
    doc = build()
    text = json.dumps(doc, indent=2)
    for p in out_paths:
        p.write_text(text + "\n")
        print(f"Wrote {p} ({len(doc['nodes'])} root nodes, {len(doc['subgraphs'])} subgraphs)")


if __name__ == "__main__":
    main()
