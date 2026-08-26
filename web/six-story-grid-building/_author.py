#!/usr/bin/env python3
"""Fill the authoring plan and emit six-story-grid-building.pcg."""
from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent
PLAN = ROOT / "six-story-grid-building-plan.json"
PCG = ROOT / "six-story-grid-building.pcg"


def fill_plan() -> None:
    plan = json.loads(PLAN.read_text(encoding="utf-8"))
    plan["objectClass"] = {
        "primaryType": "office_building",
        "primaryDomain": "building",
        "notes": "Six-story rectangular grid building; dark 5 m podium, 5 typical 4 m floors, 4-bay front.",
    }
    plan["observation"]["layers"] = {
        "identification": "Modern six-story rectangular office/retail building, primaryDomain=building.",
        "formSilhouette": "Axis-aligned box 24 m wide x 21 m high x 16 m deep; flat roof; small roof bulkhead.",
        "macroMesoMicro": "Macro: podium, upper mass, roof, penthouse. Meso: 4-bay storefront, 4x5 upper window grid, side window column, pilasters, floor bands. Micro: 3-pane mullions, transom rail, entrance doors, panel seams.",
        "spatialRelationships": "Upper mass sits on podium at y=5 m. Windows overlay front +Z face. Side windows on ±X. Penthouse near rear of roof. Entrance occupies third bay from left.",
        "materialsSurface": "Matte concrete/stone podium and upper panels; dark metal window frames; tinted glass; low-gloss roof bulkhead.",
        "colorFinish": "Podium charcoal #3a3d42 roughness 0.85; upper panels #d8d6d0 roughness 0.72; frames #1a1c1e metallic 0.55 roughness 0.35; glass #4a5863 roughness 0.08.",
        "identityFeatures": "4 equal 6 m bays; ground openings 5 m tall; typical floors 4 m; five vertical pilasters; six-window side stack; roof stair/elevator bulkhead.",
        "uncertainty": "Interior core/stairs not modeled as rooms. Top-view pillar count (~7) deferred to front 4-bay owner. Trees in perspective omitted (not in ortho set).",
    }
    plan["observation"]["shapeAnalysis"] = (
        "Rectangular prisms throughout. Front is 4-bay grid. Ground unique (dark, 5 m). "
        "Upper five floors share one window prototype. Side is mostly solid with one window column. "
        "Roof is flat plus a small bulkhead. No sweep/revolve bodies."
    )
    plan["observation"]["viewObservations"] = {
        "front": {
            "silhouette": "24 m x 21 m rectangle; dark podium 5 m; light 16 m upper frame; 4 storefront bays; 4x5 window grid.",
            "landmarks": [
                {"name": "podium-cornice", "uv": [0.5, 0.76], "world": [0, 5.0, 8.0]},
                {"name": "roof-line", "uv": [0.5, 0.08], "world": [0, 21.0, 8.0]},
                {"name": "left-bay-center", "uv": [0.2, 0.45], "world": [-9.0, 13.0, 8.0]},
                {"name": "entrance-bay", "uv": [0.62, 0.82], "world": [3.0, 2.5, 8.0]},
            ],
            "visibleComponents": ["ground", "upper", "storefronts", "upper_windows", "pilasters", "entrance"],
            "occlusionNotes": "Side depth not measurable here.",
            "confidence": 0.95,
        },
        "side": {
            "silhouette": "16 m x 21 m rectangle; dark 5 m base; one 6-window column; five balcony nibs on front edge; roof bulkhead.",
            "landmarks": [
                {"name": "front-edge", "uv": [0.88, 0.5], "world": [12.0, 10.5, 8.0]},
                {"name": "rear-edge", "uv": [0.12, 0.5], "world": [12.0, 10.5, -8.0]},
                {"name": "side-window-stack", "uv": [0.5, 0.45], "world": [12.0, 10.5, 0.0]},
                {"name": "roof-bulkhead", "uv": [0.5, 0.06], "world": [2.0, 22.4, -4.0]},
            ],
            "visibleComponents": ["ground", "upper", "side_windows", "roof", "penthouse", "front_balcony_nibs"],
            "occlusionNotes": "Front 4-bay width not measurable here.",
            "confidence": 0.92,
        },
        "top": {
            "silhouette": "24 m x 16 m rectangle; perimeter columns; recessed front entry; rear-center service core.",
            "landmarks": [
                {"name": "front-wall", "uv": [0.5, 0.88], "world": [0.0, 0.0, 8.0]},
                {"name": "rear-wall", "uv": [0.5, 0.12], "world": [0.0, 0.0, -8.0]},
                {"name": "service-core", "uv": [0.52, 0.22], "world": [1.5, 21.0, -4.0]},
                {"name": "entry-recess", "uv": [0.5, 0.9], "world": [0.0, 0.0, 8.0]},
            ],
            "visibleComponents": ["footprint", "penthouse", "perimeter_structure"],
            "occlusionNotes": "Heights taken from front/side; interior rooms not built.",
            "confidence": 0.9,
        },
    }
    for item in plan["observation"]["crossViewConstraints"]:
        if item["dimension"] == "width":
            item.update({"value": 24.0, "driver": "ground_box.width", "status": "measured", "tolerance": 0.05})
        elif item["dimension"] == "height":
            item.update({"value": 21.0, "driver": "upper_box.height+ground_box.height", "status": "measured", "tolerance": 0.05})
        elif item["dimension"] == "depth":
            item.update({"value": 16.0, "driver": "ground_box.depth", "status": "measured", "tolerance": 0.05})
    plan["observation"]["conflictResolutions"] = [
        "Front 4-bay grid owns facade spacing; top-view ~7 perimeter ticks are interior/structure and not extra front bays.",
        "Perspective places side windows near the front corner; side elevation centers the column on the 16 m face — side elevation owns Z.",
    ]
    plan["visualTokens"] = {
        "namedDimensions": ["width 24 m", "depth 16 m", "height 21 m", "ground 5 m", "typical floor 4 m", "bay 6 m"],
        "proportions": ["ground 5/21 of height", "four equal bays", "upper frame 16 m on podium"],
        "materialPalette": [
            "podium charcoal #3a3d42 roughness 0.85",
            "upper panel #d8d6d0 roughness 0.72",
            "frame #1a1c1e metallic 0.55 roughness 0.35",
            "glass #4a5863 roughness 0.08",
        ],
        "note": "Metres from drawing ticks (mm labels /1000).",
    }
    plan["qualityContract"]["definitionOfDone"] = [
        "Front/side/top overall box is 24 x 21 x 16 m within 0.05 m",
        "Ground storey is dark and 5 m; five typical floors are 4 m with a 4x5 window grid",
        "Third bay from the left has a distinct entrance; windows do not occupy that door AABB",
        "Right side shows one 6-window column; roof has a rear-center bulkhead",
        "Per-part BevelMesh before MergeMesh; named materials on podium/upper/frame/glass/roof",
    ]
    plan["qualityContract"]["minimumMacroParts"] = 6
    plan["qualityContract"]["minimumMesoParts"] = 10
    plan["detailInventory"] = {
        "targetMinDetails": 12,
        "details": [
            {"id": "podium-height", "kind": "panel", "region": "ground", "mapsTo": {"nodeId": "gnd_box", "property": "height"}, "confidence": 0.98},
            {"id": "upper-mass", "kind": "panel", "region": "floors 2-6", "mapsTo": {"nodeId": "upr_box", "property": "height"}, "confidence": 0.98},
            {"id": "bay-spacing", "kind": "array", "region": "front", "mapsTo": {"nodeId": "front_win_copy_x", "property": "translateX"}, "confidence": 0.95},
            {"id": "typical-floor", "kind": "array", "region": "upper grid", "mapsTo": {"nodeId": "front_win_copy_y", "property": "translateY"}, "confidence": 0.95},
            {"id": "storefront-bays", "kind": "array", "region": "ground front", "mapsTo": {"nodeId": "shop_copy_x", "property": "count"}, "confidence": 0.95},
            {"id": "entrance-bay", "kind": "hole", "region": "third bay", "mapsTo": {"nodeId": "entrance", "property": "subgraphId"}, "confidence": 0.9},
            {"id": "window-mullions", "kind": "linework", "region": "upper cells", "mapsTo": {"nodeId": "uw_mul_l_box", "property": "width"}, "confidence": 0.85},
            {"id": "window-transom", "kind": "trim", "region": "upper cells", "mapsTo": {"nodeId": "uw_rail_box", "property": "height"}, "confidence": 0.85},
            {"id": "pilasters", "kind": "array", "region": "front frame", "mapsTo": {"nodeId": "st_pil_copy", "property": "count"}, "confidence": 0.9},
            {"id": "floor-bands", "kind": "array", "region": "front frame", "mapsTo": {"nodeId": "st_band_copy", "property": "count"}, "confidence": 0.9},
            {"id": "side-window-column", "kind": "array", "region": "+X facade", "mapsTo": {"nodeId": "side_r_up_copy", "property": "count"}, "confidence": 0.9},
            {"id": "roof-bulkhead", "kind": "panel", "region": "roof rear", "mapsTo": {"nodeId": "ph_box", "property": "width"}, "confidence": 0.88},
        ],
    }
    plan["modules"] = [
        {"id": "ground", "name": "Podium", "subgraph": True, "parts": ["dark 24x5x16 mass"]},
        {"id": "upper", "name": "Upper mass", "subgraph": True, "parts": ["light 24x16x16 mass"]},
        {"id": "roof", "name": "Roof", "subgraph": True, "parts": ["slab", "penthouse"]},
        {"id": "structure", "name": "Front frame", "subgraph": True, "parts": ["pilasters", "floor bands"]},
        {"id": "upper_window", "name": "Upper window unit", "subgraph": True, "reused": True, "parts": ["frame", "glass", "mullions", "transom"]},
        {"id": "storefront", "name": "Storefront unit", "subgraph": True, "parts": ["frame", "glass"]},
        {"id": "side_window", "name": "Side window unit", "subgraph": True, "parts": ["frame", "glass"]},
        {"id": "entrance", "name": "Entrance", "subgraph": True, "parts": ["door pair"]},
    ]
    plan["componentHypotheses"] = [
        {"component": "podium", "chosenNodeType": "CreateBoxMesh", "manifestEvidence": "schema/node-manifest.json CreateBoxMesh width/height/depth metres, centered origin", "crossViewEvidence": "front+side 5 m dark base; top 24x16 footprint", "dimensionDrivers": ["gnd_box.width=24", "gnd_box.height=5", "gnd_box.depth=16"]},
        {"component": "upper-mass", "chosenNodeType": "CreateBoxMesh", "manifestEvidence": "CreateBoxMesh rectangular prism", "crossViewEvidence": "front+side 16 m light body on podium", "dimensionDrivers": ["upr_box.height=16", "upr_place.translate.y=13"]},
        {"component": "roof-slab", "chosenNodeType": "CreateBoxMesh", "manifestEvidence": "CreateBoxMesh thin prism", "crossViewEvidence": "front/side roof line at 21 m; top full footprint", "dimensionDrivers": ["rf_slab_box.height=0.28", "rf_slab_place.translate.y=21.14"]},
        {"component": "penthouse", "chosenNodeType": "CreateBoxMesh", "manifestEvidence": "CreateBoxMesh", "crossViewEvidence": "side bulkhead; top rear-center core", "dimensionDrivers": ["ph_box.width=6", "ph_place.translate"]},
        {"component": "upper-window", "chosenNodeType": "CreateBoxMesh", "manifestEvidence": "CreateBoxMesh + CopyMesh linear", "crossViewEvidence": "front 4x5 grid; bay 6 m; floor 4 m", "dimensionDrivers": ["front_win_copy_x.translateX=6", "front_win_copy_y.translateY=4"]},
        {"component": "storefront", "chosenNodeType": "CreateBoxMesh", "manifestEvidence": "CreateBoxMesh + CopyMesh", "crossViewEvidence": "front four ground openings", "dimensionDrivers": ["shop_copy_x.count=4"]},
        {"component": "side-window", "chosenNodeType": "CreateBoxMesh", "manifestEvidence": "CreateBoxMesh + CopyMesh + TransformMesh rotateY", "crossViewEvidence": "side six-window column on 16 m face", "dimensionDrivers": ["side_r_up_copy.count=5"]},
        {"component": "pilasters", "chosenNodeType": "CopyMesh", "manifestEvidence": "CopyMesh linear count/translateX", "crossViewEvidence": "front five verticals at bay lines", "dimensionDrivers": ["st_pil_copy.count=5", "st_pil_copy.translateX=6"]},
    ]
    plan["localRuleHits"] = [
        "pcg/graph-contract",
        "pcg/triview",
        "pcg/assembly-bevel",
        "pcg/building",
        "pit-pcg-assign-material-binding",
    ]
    plan["unknownsToResolve"] = [
        "Exact mullion thickness",
        "Interior stair/elevator geometry",
        "Balcony glass railing thickness",
    ]
    plan["assetSpec"] = {
        "assetId": "six-story-grid-building",
        "intent": "Reference-matched six-story grid office building for web PCG review and glTF export.",
        "references": ["ref_six-story-grid-building_front.png", "ref_six-story-grid-building_side.png", "ref_six-story-grid-building_top.png"],
        "scale": {"width": 24.0, "height": 21.0, "depth": 16.0, "unit": "m"},
        "modules": ["ground", "upper", "roof", "structure", "upper_window", "storefront", "side_window", "entrance"],
        "geometryDoD": plan["qualityContract"]["definitionOfDone"],
        "materialSlots": [
            {"name": "bldg_podium", "role": "ground stone", "shader": "pcg.standard-pbr", "finish": "matte charcoal"},
            {"name": "bldg_upper", "role": "upper panels", "shader": "pcg.standard-pbr", "finish": "matte light concrete"},
            {"name": "bldg_frame", "role": "window frames", "shader": "pcg.standard-pbr", "finish": "dark metal"},
            {"name": "bldg_glass", "role": "glazing", "shader": "pcg.standard-pbr", "finish": "tinted glass"},
            {"name": "bldg_roof", "role": "roof bulkhead", "shader": "pcg.standard-pbr", "finish": "matte light"},
        ],
        "variation": {"BayCount": [2, 8], "TypicalFloorCount": [1, 8]},
        "outputs": {"pcg": str(PCG), "review": "/review?graph=web/six-story-grid-building/six-story-grid-building.pcg"},
        "acceptance": {"worstRequiredView": 0.9, "frontAxis": "+z"},
    }
    PLAN.write_text(json.dumps(plan, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def node(nid, ntype, x, y, title, **data):
    payload = {"__nodeTitle": title, **data}
    return {"id": nid, "type": ntype, "position": {"x": x, "y": y}, "data": payload}


def edge(eid, src, tgt, sh="out", th="in"):
    return {"id": eid, "source": src, "target": tgt, "sourceHandle": sh, "targetHandle": th}


def solid_part(prefix, title, w, h, d, tx, ty, tz, mat_id, mat_name, *, x0=0, bevel=0.06, uv_axis="y", segments=2):
    """Box → subdiv → bevel → uv → assign → place. Returns nodes, edges, out id."""
    box, sd, bv, uv, am, pl = (
        f"{prefix}_box",
        f"{prefix}_sd",
        f"{prefix}_bv",
        f"{prefix}_uv",
        f"{prefix}_am",
        f"{prefix}_pl",
    )
    nodes = [
        node(box, "CreateBoxMesh", x0, 0, f"{title} Box", width=w, height=h, depth=d),
        node(sd, "SubdivideMesh", x0, 160, f"{title} Subdiv", levels=1, method="simple"),
        node(bv, "BevelMesh", x0, 320, f"{title} Bevel", method="edge", amount=bevel, segments=segments, angleLimit=40),
        node(uv, "UVTexture", x0, 480, f"{title} UV", projection="planar", axis=uv_axis, scaleU=1.0, scaleV=1.0, offsetU=0.0, offsetV=0.0),
        node(am, "AssignMaterial", x0, 640, f"{title} Mat", group="", materialName=mat_name),
        node(pl, "TransformMesh", x0, 800, f"{title} Place", translate=[tx, ty, tz], rotation=[0, 0, 0], scale=[1, 1, 1]),
    ]
    edges = [
        edge(f"e_{prefix}_sd", box, sd),
        edge(f"e_{prefix}_bv", sd, bv),
        edge(f"e_{prefix}_uv", bv, uv),
        edge(f"e_{prefix}_am", uv, am),
        edge(f"e_{prefix}_pl", am, pl),
        edge(f"e_{prefix}_mat", mat_id, am, "out", "material"),
    ]
    return nodes, edges, pl


def material(nid, x, y, title, name, color, roughness, metallic=0.0):
    return node(
        nid,
        "Material",
        x,
        y,
        title,
        materialName=name,
        shaderId="pcg.standard-pbr",
        baseColor=color,
        metallic=metallic,
        roughness=roughness,
    )


def subgraph(sid, name, nodes, edges, out_port="mesh"):
    return {
        "id": sid,
        "name": name,
        "inputs": [],
        "outputs": [{"id": out_port, "name": "Mesh", "pinType": "SpatialMesh"}],
        "nodes": nodes,
        "edges": edges,
    }


def build_graph() -> dict:
    subgraphs = []

    # --- ground ---
    g_nodes = [material("gnd_mat", 520, 640, "Podium Material", "bldg_podium", "#3a3d42", 0.85)]
    n, e, out = solid_part("gnd", "Podium", 24.0, 5.0, 16.0, 0.0, 2.5, 0.0, "gnd_mat", "bldg_podium", bevel=0.12, uv_axis="z")
    g_nodes += n
    g_nodes.append(node("gnd_out", "SubgraphOutput", 0, 960, "Podium Out"))
    e.append(edge("e_gnd_out", out, "gnd_out", "out", "mesh"))
    subgraphs.append(subgraph("ground", "Podium", g_nodes, e))

    # --- upper ---
    u_nodes = [material("upr_mat", 520, 640, "Upper Material", "bldg_upper", "#d8d6d0", 0.72)]
    n, e, out = solid_part("upr", "Upper", 24.0, 16.0, 16.0, 0.0, 13.0, 0.0, "upr_mat", "bldg_upper", bevel=0.14, uv_axis="z")
    u_nodes += n
    u_nodes.append(node("upr_out", "SubgraphOutput", 0, 960, "Upper Out"))
    e.append(edge("e_upr_out", out, "upr_out", "out", "mesh"))
    subgraphs.append(subgraph("upper", "Upper Mass", u_nodes, e))

    # --- roof: slab + penthouse ---
    r_nodes = [material("rf_mat", 840, 640, "Roof Material", "bldg_roof", "#cfcbc4", 0.7)]
    n1, e1, o1 = solid_part("rf_slab", "Roof Slab", 24.2, 0.28, 16.2, 0.0, 21.14, 0.0, "rf_mat", "bldg_roof", x0=0, bevel=0.04, uv_axis="y")
    n2, e2, o2 = solid_part("ph", "Penthouse", 6.0, 2.4, 4.2, 0.0, 22.4, -4.2, "rf_mat", "bldg_roof", x0=320, bevel=0.08, uv_axis="z")
    r_nodes += n1 + n2
    r_nodes.append(node("rf_merge", "MergeMesh", 160, 960, "Roof Merge"))
    r_nodes.append(node("rf_out", "SubgraphOutput", 160, 1120, "Roof Out"))
    re = e1 + e2 + [
        edge("e_rf_m1", o1, "rf_merge"),
        edge("e_rf_m2", o2, "rf_merge"),
        edge("e_rf_out", "rf_merge", "rf_out", "out", "mesh"),
    ]
    subgraphs.append(subgraph("roof", "Roof", r_nodes, re))

    # --- structure: pilasters + bands ---
    s_nodes = [material("st_mat", 840, 640, "Frame Structure Mat", "bldg_upper", "#d8d6d0", 0.72)]
    n1, e1, o1 = solid_part("st_pil", "Pilaster", 0.55, 16.0, 0.42, -12.0, 13.0, 8.18, "st_mat", "bldg_upper", x0=0, bevel=0.04, uv_axis="z")
    s_nodes += n1
    s_nodes.append(node("st_pil_copy", "CopyMesh", 0, 960, "Pilaster Copy", mode="linear", count=5, axis="x", translateX=6.0, translateY=0.0, translateZ=0.0))
    n2, e2, o2 = solid_part("st_band", "Floor Band", 24.1, 0.32, 0.4, 0.0, 5.0, 8.18, "st_mat", "bldg_upper", x0=320, bevel=0.03, uv_axis="x")
    s_nodes += n2
    s_nodes.append(node("st_band_copy", "CopyMesh", 320, 960, "Band Copy", mode="linear", count=5, axis="y", translateX=0.0, translateY=4.0, translateZ=0.0))
    s_nodes.append(node("st_merge", "MergeMesh", 160, 1120, "Structure Merge"))
    s_nodes.append(node("st_out", "SubgraphOutput", 160, 1280, "Structure Out"))
    se = e1 + e2 + [
        edge("e_st_pc", o1, "st_pil_copy"),
        edge("e_st_bc", o2, "st_band_copy"),
        edge("e_st_m1", "st_pil_copy", "st_merge"),
        edge("e_st_m2", "st_band_copy", "st_merge"),
        edge("e_st_out", "st_merge", "st_out", "out", "mesh"),
    ]
    subgraphs.append(subgraph("structure", "Front Frame", s_nodes, se))

    # --- upper window unit at origin ---
    w_nodes = [
        material("uw_frm_mat", 200, 640, "Win Frame Shader", "bldg_frame", "#1a1c1e", 0.35, 0.55),
        material("uw_gl_mat", 840, 640, "Win Glass Shader", "bldg_glass", "#4a5863", 0.08, 0.05),
    ]
    n_fr, e_fr, o_fr = solid_part("uw_frm", "Win Frame", 5.35, 3.45, 0.16, 0.0, 0.0, 0.0, "uw_frm_mat", "bldg_frame", x0=0, bevel=0.025, uv_axis="z")
    n_gl, e_gl, o_gl = solid_part("uw_gl", "Win Glass", 5.05, 3.12, 0.05, 0.0, 0.05, 0.06, "uw_gl_mat", "bldg_glass", x0=320, bevel=0.008, uv_axis="z")
    n_ml, e_ml, o_ml = solid_part("uw_mul_l", "Mullion L", 0.07, 3.0, 0.08, -0.85, 0.05, 0.07, "uw_frm_mat", "bldg_frame", x0=640, bevel=0.008, uv_axis="z")
    n_mr, e_mr, o_mr = solid_part("uw_mul_r", "Mullion R", 0.07, 3.0, 0.08, 0.85, 0.05, 0.07, "uw_frm_mat", "bldg_frame", x0=960, bevel=0.008, uv_axis="z")
    n_rl, e_rl, o_rl = solid_part("uw_rail", "Win Rail", 5.0, 0.07, 0.08, 0.0, -0.72, 0.07, "uw_frm_mat", "bldg_frame", x0=1280, bevel=0.008, uv_axis="z")
    w_nodes += n_fr + n_gl + n_ml + n_mr + n_rl
    w_nodes.append(node("uw_merge", "MergeMesh", 640, 960, "Window Merge"))
    w_nodes.append(node("uw_out", "SubgraphOutput", 640, 1120, "Window Out"))
    we = e_fr + e_gl + e_ml + e_mr + e_rl + [
        edge("e_uw_m1", o_fr, "uw_merge"),
        edge("e_uw_m2", o_gl, "uw_merge"),
        edge("e_uw_m3", o_ml, "uw_merge"),
        edge("e_uw_m4", o_mr, "uw_merge"),
        edge("e_uw_m5", o_rl, "uw_merge"),
        edge("e_uw_out", "uw_merge", "uw_out", "out", "mesh"),
    ]
    subgraphs.append(subgraph("upper_window", "Upper Window", w_nodes, we))

    # --- storefront ---
    sh_nodes = [
        material("sh_frm_mat", 200, 640, "Shop Frame Shader", "bldg_frame", "#1a1c1e", 0.35, 0.55),
        material("sh_gl_mat", 840, 640, "Shop Glass Shader", "bldg_glass", "#3e4a52", 0.08, 0.05),
    ]
    n_fr, e_fr, o_fr = solid_part("sh_frm", "Shop Frame", 5.5, 4.55, 0.18, 0.0, 0.0, 0.0, "sh_frm_mat", "bldg_frame", x0=0, bevel=0.03, uv_axis="z")
    n_gl, e_gl, o_gl = solid_part("sh_gl", "Shop Glass", 5.15, 4.2, 0.05, 0.0, 0.05, 0.07, "sh_gl_mat", "bldg_glass", x0=320, bevel=0.008, uv_axis="z")
    sh_nodes += n_fr + n_gl
    sh_nodes.append(node("sh_merge", "MergeMesh", 160, 960, "Shop Merge"))
    sh_nodes.append(node("sh_out", "SubgraphOutput", 160, 1120, "Shop Out"))
    she = e_fr + e_gl + [
        edge("e_sh_m1", o_fr, "sh_merge"),
        edge("e_sh_m2", o_gl, "sh_merge"),
        edge("e_sh_out", "sh_merge", "sh_out", "out", "mesh"),
    ]
    subgraphs.append(subgraph("storefront", "Storefront", sh_nodes, she))

    # --- side window (smaller) ---
    sw_nodes = [
        material("sw_frm_mat", 200, 640, "Side Frame Shader", "bldg_frame", "#1a1c1e", 0.35, 0.55),
        material("sw_gl_mat", 840, 640, "Side Glass Shader", "bldg_glass", "#4a5863", 0.08, 0.05),
    ]
    n_fr, e_fr, o_fr = solid_part("sw_frm", "Side Frame", 1.85, 2.25, 0.14, 0.0, 0.0, 0.0, "sw_frm_mat", "bldg_frame", x0=0, bevel=0.02, uv_axis="z")
    n_gl, e_gl, o_gl = solid_part("sw_gl", "Side Glass", 1.6, 2.0, 0.04, 0.0, 0.02, 0.05, "sw_gl_mat", "bldg_glass", x0=320, bevel=0.006, uv_axis="z")
    sw_nodes += n_fr + n_gl
    sw_nodes.append(node("sw_merge", "MergeMesh", 160, 960, "Side Win Merge"))
    sw_nodes.append(node("sw_out", "SubgraphOutput", 160, 1120, "Side Win Out"))
    swe = e_fr + e_gl + [
        edge("e_sw_m1", o_fr, "sw_merge"),
        edge("e_sw_m2", o_gl, "sw_merge"),
        edge("e_sw_out", "sw_merge", "sw_out", "out", "mesh"),
    ]
    subgraphs.append(subgraph("side_window", "Side Window", sw_nodes, swe))

    # --- entrance doors ---
    en_nodes = [material("en_mat", 520, 640, "Door Material", "bldg_frame", "#1a1c1e", 0.4, 0.45)]
    n1, e1, o1 = solid_part("en_l", "Door L", 0.85, 2.35, 0.08, -0.48, 0.0, 0.0, "en_mat", "bldg_frame", x0=0, bevel=0.02, uv_axis="z")
    n2, e2, o2 = solid_part("en_r", "Door R", 0.85, 2.35, 0.08, 0.48, 0.0, 0.0, "en_mat", "bldg_frame", x0=320, bevel=0.02, uv_axis="z")
    en_nodes += n1 + n2
    en_nodes.append(node("en_merge", "MergeMesh", 160, 960, "Door Merge"))
    en_nodes.append(node("en_out", "SubgraphOutput", 160, 1120, "Door Out"))
    ene = e1 + e2 + [
        edge("e_en_m1", o1, "en_merge"),
        edge("e_en_m2", o2, "en_merge"),
        edge("e_en_out", "en_merge", "en_out", "out", "mesh"),
    ]
    subgraphs.append(subgraph("entrance", "Entrance", en_nodes, ene))

    # --- root ---
    def sg(nid, x, y, title, sid):
        return node(nid, "Subgraph", x, y, title, subgraphId=sid)

    nodes = [
        sg("ground", 0, 0, "Podium", "ground"),
        sg("upper", 320, 0, "Upper Mass", "upper"),
        sg("roof", 640, 0, "Roof", "roof"),
        sg("structure", 960, 0, "Front Frame", "structure"),
        sg("win_unit", 1280, 0, "Upper Window", "upper_window"),
        sg("shop_unit", 1600, 0, "Storefront", "storefront"),
        sg("side_unit_r", 1920, 0, "Side Window R", "side_window"),
        sg("side_unit_l", 2240, 0, "Side Window L", "side_window"),
        sg("back_unit", 2560, 0, "Back Window", "upper_window"),
        sg("entrance", 2880, 0, "Entrance", "entrance"),
        node("win_place", "TransformMesh", 1280, 160, "Front Win Place", translate=[-9.0, 7.0, 8.12], rotation=[0, 0, 0], scale=[1, 1, 1]),
        node("front_win_copy_x", "CopyMesh", 1280, 320, "Front Win Bays", mode="linear", count=4, axis="x", translateX=6.0, translateY=0.0, translateZ=0.0),
        node("front_win_copy_y", "CopyMesh", 1280, 480, "Front Win Floors", mode="linear", count=4, axis="y", translateX=0.0, translateY=4.0, translateZ=0.0),
        node("shop_cut", "CreateBoxMesh", 320, 160, "Shop Cutter", width=5.2, height=4.4, depth=1.2),
        node("shop_cut_place", "TransformMesh", 320, 320, "Shop Cutter Place", translate=[-9.0, 2.55, 7.55], rotation=[0, 0, 0], scale=[1, 1, 1]),
        node("shop_cut_copy", "CopyMesh", 320, 480, "Shop Cutter Bays", mode="linear", count=4, axis="x", translateX=6.0, translateY=0.0, translateZ=0.0),
        node("shop_bool", "BooleanMesh", 0, 480, "Podium Openings", operation="subtract"),
        node("shop_place", "TransformMesh", 1600, 160, "Shop Place", translate=[-9.0, 2.55, 7.35], rotation=[0, 0, 0], scale=[1, 1, 1]),
        node("shop_copy_x", "CopyMesh", 1600, 320, "Shop Bays", mode="linear", count=4, axis="x", translateX=6.0, translateY=0.0, translateZ=0.0),
        node("en_place", "TransformMesh", 2880, 160, "Entrance Place", translate=[-3.0, 1.25, 8.22], rotation=[0, 0, 0], scale=[1, 1, 1]),
        node("en_copy", "CopyMesh", 2880, 320, "Entrance Bays", mode="linear", count=2, axis="x", translateX=6.0, translateY=0.0, translateZ=0.0),
        node("side_r_place", "TransformMesh", 1920, 160, "Side R Place", translate=[12.12, 2.5, 0.0], rotation=[0, 90, 0], scale=[1, 1, 1]),
        node("side_r_up_src", "TransformMesh", 1920, 320, "Side R Upper Src", translate=[0.0, 4.5, 0.0], rotation=[0, 0, 0], scale=[1, 1, 1]),
        node("side_r_up_copy", "CopyMesh", 1920, 480, "Side R Upper Copy", mode="linear", count=4, axis="y", translateX=0.0, translateY=4.0, translateZ=0.0),
        node("side_l_place", "TransformMesh", 2240, 160, "Side L Place", translate=[-12.12, 2.5, 0.0], rotation=[0, -90, 0], scale=[1, 1, 1]),
        node("side_l_up_src", "TransformMesh", 2240, 320, "Side L Upper Src", translate=[0.0, 4.5, 0.0], rotation=[0, 0, 0], scale=[1, 1, 1]),
        node("side_l_up_copy", "CopyMesh", 2240, 480, "Side L Upper Copy", mode="linear", count=4, axis="y", translateX=0.0, translateY=4.0, translateZ=0.0),
        node("back_place", "TransformMesh", 2560, 160, "Back Win Place", translate=[-9.0, 7.0, -8.12], rotation=[0, 180, 0], scale=[1, 1, 1]),
        node("back_copy_x", "CopyMesh", 2560, 320, "Back Win Bays", mode="linear", count=4, axis="x", translateX=6.0, translateY=0.0, translateZ=0.0),
        node("back_copy_y", "CopyMesh", 2560, 480, "Back Win Floors", mode="linear", count=4, axis="y", translateX=0.0, translateY=4.0, translateZ=0.0),
        node("bldg_merge", "MergeMesh", 960, 800, "Building Merge"),
        node("out", "Output", 960, 960, "Output", label="Mesh"),
    ]
    edges = [
        edge("e_win_pl", "win_unit", "win_place", "mesh", "in"),
        edge("e_win_cx", "win_place", "front_win_copy_x"),
        edge("e_win_cy", "front_win_copy_x", "front_win_copy_y"),
        edge("e_shop_pl", "shop_unit", "shop_place", "mesh", "in"),
        edge("e_shop_cx", "shop_place", "shop_copy_x"),
        edge("e_cut_pl", "shop_cut", "shop_cut_place"),
        edge("e_cut_cx", "shop_cut_place", "shop_cut_copy"),
        edge("e_bool_a", "ground", "shop_bool", "mesh", "a"),
        edge("e_bool_b", "shop_cut_copy", "shop_bool", "out", "b"),
        edge("e_en_pl", "entrance", "en_place", "mesh", "in"),
        edge("e_en_cp", "en_place", "en_copy"),
        edge("e_sr_pl", "side_unit_r", "side_r_place", "mesh", "in"),
        edge("e_sr_us", "side_r_place", "side_r_up_src"),
        edge("e_sr_uc", "side_r_up_src", "side_r_up_copy"),
        edge("e_sl_pl", "side_unit_l", "side_l_place", "mesh", "in"),
        edge("e_sl_us", "side_l_place", "side_l_up_src"),
        edge("e_sl_uc", "side_l_up_src", "side_l_up_copy"),
        edge("e_bk_pl", "back_unit", "back_place", "mesh", "in"),
        edge("e_bk_cx", "back_place", "back_copy_x"),
        edge("e_bk_cy", "back_copy_x", "back_copy_y"),
        edge("e_m_gnd", "shop_bool", "bldg_merge"),
        edge("e_m_upr", "upper", "bldg_merge", "mesh", "in"),
        edge("e_m_rf", "roof", "bldg_merge", "mesh", "in"),
        edge("e_m_st", "structure", "bldg_merge", "mesh", "in"),
        edge("e_m_win", "front_win_copy_y", "bldg_merge"),
        edge("e_m_shop", "shop_copy_x", "bldg_merge"),
        edge("e_m_en", "en_copy", "bldg_merge"),
        edge("e_m_srg", "side_r_place", "bldg_merge"),
        edge("e_m_sru", "side_r_up_copy", "bldg_merge"),
        edge("e_m_slg", "side_l_place", "bldg_merge"),
        edge("e_m_slu", "side_l_up_copy", "bldg_merge"),
        edge("e_m_bk", "back_copy_y", "bldg_merge"),
        edge("e_out", "bldg_merge", "out"),
    ]
    parameters = [
        {
            "id": "p_bay_count",
            "name": "BayCount",
            "type": "integer",
            "default": 4,
            "exposed": True,
            "targetNode": "front_win_copy_x",
            "targetProperty": "count",
            "hasRange": True,
            "min": 2,
            "max": 8,
        },
        {
            "id": "p_floor_count",
            "name": "TypicalFloorCount",
            "type": "integer",
            "default": 4,
            "exposed": True,
            "targetNode": "front_win_copy_y",
            "targetProperty": "count",
            "hasRange": True,
            "min": 1,
            "max": 8,
        },
        {
            "id": "p_bay_spacing",
            "name": "BaySpacing",
            "type": "number",
            "default": 6.0,
            "exposed": True,
            "targetNode": "front_win_copy_x",
            "targetProperty": "translateX",
            "hasRange": True,
            "min": 4.0,
            "max": 8.0,
        },
    ]
    return {"version": "1.0", "nodes": nodes, "edges": edges, "parameters": parameters, "subgraphs": subgraphs}


def main() -> None:
    fill_plan()
    graph = build_graph()
    PCG.write_text(json.dumps(graph, indent=2) + "\n", encoding="utf-8")
    print(PLAN)
    print(PCG)


if __name__ == "__main__":
    main()
