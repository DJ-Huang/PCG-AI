#!/usr/bin/env python3
"""Validate a PCG-AI .pcg graph against schema/node-manifest.json."""

from __future__ import annotations

import argparse
import json
import sys
import urllib.error
import urllib.request
from collections import Counter, defaultdict
from pathlib import Path


# Walk up from cwd to find the repo root (contains schema/node-manifest.json)
_REPO_ROOT = Path.cwd()
for _candidate in [Path.cwd(), *_REPO_ROOT.parents]:
    if (_candidate / "schema" / "node-manifest.json").is_file():
        _REPO_ROOT = _candidate
        break
REPO_ROOT = _REPO_ROOT
MANIFEST_PATH = REPO_ROOT / "schema" / "node-manifest.json"

ROW_STEP_Y = 160
HORIZONTAL_STEP_X = 200
# GraphView node pill ~61px + right-side title overlay up to 220px → need ≥320 same-row Δx
COL_STEP_X = 320
SAME_ROW_Y_TOL = ROW_STEP_Y * 0.5
MIN_SAME_ROW_DX = COL_STEP_X * 0.9  # allow tiny float/rounding slack

# Structural types (not in node-manifest); ports come from subgraph definitions.
STRUCTURAL_TYPES = frozenset({"Subgraph", "SubgraphInput", "SubgraphOutput", "SubgraphAsset"})
ROOT_SOFT_MODULARIZE = 40
ROOT_MUST_MODULARIZE = 80
# Executable nodes inside a definition (excludes interface nodes). Soft floor for "module".
MIN_MODULE_EXECUTABLE = 5


def load_manifest() -> dict[str, dict]:
    data = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    return {node["type"]: node for node in data["nodes"]}


def pin_maps(defn: dict) -> tuple[dict[str, str], dict[str, str]]:
    inputs = {p["id"]: p["pinType"] for p in defn.get("inputs", [])}
    outputs = {p["id"]: p["pinType"] for p in defn.get("outputs", [])}
    return inputs, outputs


def can_connect(src_type: str, tgt_type: str, src_handle: str, tgt_handle: str, manifest: dict) -> bool:
    # Structural Subgraph pins are validated against definition ports separately.
    if src_type in STRUCTURAL_TYPES or tgt_type in STRUCTURAL_TYPES:
        return True
    src_def = manifest.get(src_type)
    tgt_def = manifest.get(tgt_type)
    if not src_def or not tgt_def:
        return False
    _, src_out = pin_maps(src_def)
    tgt_in, _ = pin_maps(tgt_def)
    src_pin = src_out.get(src_handle or "out", "SpatialPoint")

    tgt_pin = tgt_in.get(tgt_handle or "in")
    if tgt_pin is None:
        # Check for variadic input — any handle is accepted if pinType matches
        for p in tgt_def.get("inputs", []):
            if p.get("variadic"):
                tgt_pin = p["pinType"]
                break
    if tgt_pin is None:
        tgt_pin = "SpatialPoint"
    if src_pin == tgt_pin or src_pin == "Any" or tgt_pin == "Any":
        return True
    spatial_geometry_family = {"SpatialGeometry", "SpatialMesh", "SpatialSpline"}
    return (
        src_pin == "SpatialGeometry" and tgt_pin in spatial_geometry_family
    ) or (
        tgt_pin == "SpatialGeometry" and src_pin in spatial_geometry_family
    )


def layout_style(nodes: list[dict]) -> str:
    if len(nodes) < 2:
        return "single"
    positioned = [
        n for n in nodes
        if isinstance(n.get("position"), dict)
        and "x" in n["position"]
        and "y" in n["position"]
    ]
    if len(positioned) < 2:
        return "single"
    xs = [n["position"]["x"] for n in positioned]
    ys = [n["position"]["y"] for n in positioned]
    x_range = max(xs) - min(xs)
    y_range = max(ys) - min(ys)
    # Multi-lane assemblies are wide but still top-down if they span multiple rows.
    if y_range >= ROW_STEP_Y * 1.5:
        return "vertical"
    if y_range >= ROW_STEP_Y * 0.5 and y_range >= x_range:
        return "vertical"
    if x_range >= HORIZONTAL_STEP_X * 0.5 and x_range > y_range:
        return "horizontal"
    return "mixed"


def _segments_cross(
    ax: float, ay: float, bx: float, by: float,
    cx: float, cy: float, dx: float, dy: float,
) -> bool:
    """Proper intersection of open segments AB and CD."""

    def orient(px, py, qx, qy, rx, ry):
        return (qy - py) * (rx - qx) - (qx - px) * (ry - qy)

    o1 = orient(ax, ay, bx, by, cx, cy)
    o2 = orient(ax, ay, bx, by, dx, dy)
    o3 = orient(cx, cy, dx, dy, ax, ay)
    o4 = orient(cx, cy, dx, dy, bx, by)
    return o1 * o2 < 0 and o3 * o4 < 0


def check_wire_crossings(nodes: list[dict], edges: list[dict], warnings: list[str]) -> None:
    """Flag layouts with many geometric edge crossings or long diagonals."""
    by_id = {n["id"]: n for n in nodes if "id" in n}
    # Final assembly merge fan-in is allowed to be wide; exclude those tips→merge edges.
    inbound_count: dict[str, int] = defaultdict(int)
    for edge in edges:
        inbound_count[edge.get("target", "")] += 1
    assembly_merges = {nid for nid, c in inbound_count.items() if c >= 4}

    segs: list[tuple[str, str, float, float, float, float]] = []
    long_diag = 0
    for edge in edges:
        s = by_id.get(edge.get("source", ""))
        t = by_id.get(edge.get("target", ""))
        if not s or not t:
            continue
        sp, tp = s.get("position"), t.get("position")
        if not isinstance(sp, dict) or not isinstance(tp, dict):
            continue
        if "x" not in sp or "y" not in sp or "x" not in tp or "y" not in tp:
            continue
        ax, ay, bx, by = sp["x"], sp["y"], tp["x"], tp["y"]
        segs.append((s["id"], t["id"], ax, ay, bx, by))
        if t["id"] in assembly_merges:
            continue
        # Long diagonal: spans ≥ 2 lane columns and ≥ 1 row (ignore final assembly fan-in)
        if abs(bx - ax) >= COL_STEP_X * 2 and abs(by - ay) >= ROW_STEP_Y * 0.5:
            long_diag += 1

    crosses = 0
    for i, (a_src, a_tgt, ax, ay, bx, by) in enumerate(segs):
        for b_src, b_tgt, cx, cy, dx, dy in segs[i + 1 :]:
            if {a_src, a_tgt} & {b_src, b_tgt}:
                continue
            if _segments_cross(ax, ay, bx, by, cx, cy, dx, dy):
                crosses += 1

    n_edges = max(len(segs), 1)
    if crosses >= max(8, n_edges // 4):
        warnings.append(
            f"wire spaghetti risk: ~{crosses} edge crossings "
            f"(prefer subsystem lanes so wires stay vertical; see pcg-graph-authoring layout)"
        )
    if long_diag >= max(6, n_edges // 5):
        warnings.append(
            f"wire spaghetti risk: {long_diag} long diagonal edges "
            f"(|Δx|≥{COL_STEP_X * 2}); keep parent/child in the same lane"
        )


def _node_position(node: dict) -> tuple[float, float] | None:
    position = node.get("position")
    if not isinstance(position, dict):
        return None
    try:
        return float(position["x"]), float(position["y"])
    except (KeyError, TypeError, ValueError):
        return None


def check_subgraph_lane_layout(
    nodes: list[dict],
    edges: list[dict],
    scope: str,
    warnings: list[str],
) -> None:
    """Check lane alignment and Merge centering inside every graph scope.

    ``layout_style`` only classifies the aggregate bounding box.  This check
    catches the more useful Subgraph-specific failures: independent fan-in
    branches that start on different rows and an assembly Merge that is far
    away from the center of its inputs.
    """

    by_id = {node["id"]: node for node in nodes if isinstance(node, dict) and "id" in node}
    inbound: dict[str, list[str]] = defaultdict(list)
    for edge in edges:
        if not isinstance(edge, dict):
            continue
        source = edge.get("source")
        target = edge.get("target")
        if source in by_id and target in by_id:
            inbound[target].append(source)

    root_cache: dict[str, set[str]] = {}

    def source_roots(node_id: str, visiting: set[str] | None = None) -> set[str]:
        if node_id in root_cache:
            return root_cache[node_id]
        visiting = set() if visiting is None else visiting
        if node_id in visiting:
            return set()
        visiting.add(node_id)
        predecessors = inbound.get(node_id, [])
        if not predecessors:
            roots = {node_id}
        else:
            roots = set()
            for predecessor in predecessors:
                roots.update(source_roots(predecessor, visiting))
        visiting.remove(node_id)
        root_cache[node_id] = roots
        return roots

    prefix = "" if scope == "root" else f"{scope} "
    for merge_id, predecessors in inbound.items():
        if len(predecessors) < 4:
            continue
        merge_position = _node_position(by_id[merge_id])
        predecessor_positions = [
            _node_position(by_id[pred_id])
            for pred_id in predecessors
            if _node_position(by_id[pred_id]) is not None
        ]
        if merge_position is None or len(predecessor_positions) < 4:
            continue

        fan_in_center_x = sum(position[0] for position in predecessor_positions) / len(
            predecessor_positions
        )
        fan_in_span_x = max(position[0] for position in predecessor_positions) - min(
            position[0] for position in predecessor_positions
        )
        center_tolerance = max(COL_STEP_X, fan_in_span_x * 0.25)
        if abs(merge_position[0] - fan_in_center_x) > center_tolerance:
            warnings.append(
                f"{prefix}assembly Merge {merge_id} is off-center: "
                f"x={merge_position[0]:.0f}, fan-in center={fan_in_center_x:.0f}; "
                "run scripts/layout_pcg.py for independent lane layout"
            )

        branch_start_y: list[float] = []
        for predecessor in predecessors:
            roots = source_roots(predecessor)
            if len(roots) != 1:
                continue
            root_id = next(iter(roots))
            root_position = _node_position(by_id.get(root_id, {}))
            if root_position is not None:
                branch_start_y.append(root_position[1])
        if len(branch_start_y) >= 4:
            start_row_span = max(branch_start_y) - min(branch_start_y)
            if start_row_span >= SAME_ROW_Y_TOL:
                warnings.append(
                    f"{prefix}assembly fan-in branches start on staggered rows "
                    f"(Δy={start_row_span:.0f}); align source lanes before MergeMesh"
                )


def node_title(node: dict) -> str:
    data = node.get("data") or {}
    raw = data.get("__nodeTitle")
    if isinstance(raw, str):
        return raw.strip()
    return ""


def check_same_row_spacing(nodes: list[dict], warnings: list[str]) -> None:
    """Flag siblings whose right-side titles will overlap the next pill."""
    positioned = [
        n for n in nodes
        if isinstance(n.get("position"), dict)
        and "x" in n["position"]
        and "y" in n["position"]
    ]
    for i, a in enumerate(positioned):
        ax, ay = a["position"]["x"], a["position"]["y"]
        for b in positioned[i + 1 :]:
            bx, by = b["position"]["x"], b["position"]["y"]
            if abs(ay - by) >= SAME_ROW_Y_TOL:
                continue
            dx = abs(ax - bx)
            if dx < 1e-3:
                warnings.append(
                    f"same-row overlap: {a.get('id')} and {b.get('id')} share position "
                    f"({ax}, {ay}); separate by ≥{COL_STEP_X} on X or different rows"
                )
            elif dx < MIN_SAME_ROW_DX:
                warnings.append(
                    f"same-row title overlap risk: {a.get('id')} ↔ {b.get('id')} "
                    f"|Δx|={dx:.0f} < {COL_STEP_X} (node titles sit right of pills; "
                    f"use COL_STEP_X={COL_STEP_X})"
                )


def check_display_titles(
    nodes: list[dict],
    manifest: dict[str, dict],
    warnings: list[str],
) -> None:
    titles: list[tuple[str, str]] = []
    by_type: dict[str, list[dict]] = defaultdict(list)

    for node in nodes:
        nid = node.get("id", "?")
        ntype = node.get("type", "")
        by_type[ntype].append(node)
        title = node_title(node)
        if title:
            titles.append((title, nid))

    title_counts = Counter(t for t, _ in titles)
    for title, count in title_counts.items():
        if count < 2:
            continue
        owners = [nid for t, nid in titles if t == title]
        warnings.append(
            f"duplicate __nodeTitle {title!r} on nodes: {', '.join(owners)}"
        )

    for ntype, group in by_type.items():
        if len(group) < 2 or not ntype:
            continue
        missing = [n.get("id", "?") for n in group if not node_title(n)]
        if not missing:
            continue
        default_name = (manifest.get(ntype) or {}).get("displayName") or ntype
        warnings.append(
            f"repeated type {ntype} ({len(group)} nodes) missing __nodeTitle on: "
            f"{', '.join(missing)}; the graph editor will show {default_name!r} for all of them"
        )


def _port_ids(ports: list) -> set[str]:
    return {p.get("id") for p in ports if isinstance(p, dict) and p.get("id")}


def validate_subgraph_defs(
    definitions: list,
    errors: list[str],
    warnings: list[str],
) -> dict[str, dict]:
    """Validate subgraph definitions; return id → definition map."""
    by_id: dict[str, dict] = {}
    if not isinstance(definitions, list):
        errors.append("subgraphs must be an array")
        return by_id

    for definition in definitions:
        if not isinstance(definition, dict):
            errors.append("subgraph definition must be an object")
            continue
        sid = definition.get("id")
        if not sid or not isinstance(sid, str):
            errors.append("subgraph definition missing string id")
            continue
        if sid in by_id:
            errors.append(f"duplicate subgraph id: {sid}")
            continue
        by_id[sid] = definition

        inputs = definition.get("inputs") or []
        outputs = definition.get("outputs") or []
        if not isinstance(inputs, list) or not isinstance(outputs, list):
            errors.append(f"subgraph {sid}: inputs/outputs must be arrays")
            continue
        in_ids = _port_ids(inputs)
        out_ids = _port_ids(outputs)
        if len(in_ids) != len(inputs):
            errors.append(f"subgraph {sid}: duplicate or missing input port ids")
        if len(out_ids) != len(outputs):
            errors.append(f"subgraph {sid}: duplicate or missing output port ids")

        nodes = definition.get("nodes") or []
        edges = definition.get("edges") or []
        if not isinstance(nodes, list) or not isinstance(edges, list):
            errors.append(f"subgraph {sid}: nodes/edges must be arrays")
            continue

        node_ids = [n.get("id") for n in nodes if isinstance(n, dict)]
        if len(set(node_ids)) != len(node_ids):
            errors.append(f"subgraph {sid}: duplicate node ids")

        executable = 0
        input_nodes = {
            n.get("id") for n in nodes
            if isinstance(n, dict) and n.get("type") == "SubgraphInput"
        }
        output_nodes = {
            n.get("id") for n in nodes
            if isinstance(n, dict) and n.get("type") == "SubgraphOutput"
        }
        for node in nodes:
            if not isinstance(node, dict):
                continue
            ntype = node.get("type")
            if ntype not in ("SubgraphInput", "SubgraphOutput"):
                executable += 1

        for edge in edges:
            if not isinstance(edge, dict):
                continue
            src, tgt = edge.get("source"), edge.get("target")
            if src in input_nodes:
                handle = edge.get("sourceHandle") or ""
                if handle not in in_ids:
                    errors.append(
                        f"subgraph {sid}: SubgraphInput edge sourceHandle {handle!r} "
                        f"not in inputs (runtime maps via handles)"
                    )
            if tgt in output_nodes:
                handle = edge.get("targetHandle") or ""
                if handle not in out_ids:
                    errors.append(
                        f"subgraph {sid}: SubgraphOutput edge targetHandle {handle!r} "
                        f"not in outputs (runtime maps via handles)"
                    )

        # Soft: tiny defs are usually "为封而封" unless they are pure I/O wrappers with reuse.
        if 0 < executable < MIN_MODULE_EXECUTABLE:
            warnings.append(
                f"subgraph {sid}: only {executable} executable node(s) "
                f"(<{MIN_MODULE_EXECUTABLE}); encapsulate complete modules, "
                f"not tiny stubs (see pcg-graph-authoring Subgraph modularization)"
            )

    return by_id


def check_subgraph_modularity(
    root_nodes: list[dict],
    definitions: dict[str, dict],
    warnings: list[str],
) -> None:
    """Soft heuristics: large flat roots should modularize; avoid fake mega-bags."""
    n_root = len(root_nodes)
    n_defs = len(definitions)
    instances = [n for n in root_nodes if n.get("type") == "Subgraph"]

    if n_root >= ROOT_MUST_MODULARIZE and n_defs == 0:
        warnings.append(
            f"root has {n_root} nodes (≥{ROOT_MUST_MODULARIZE}) with no subgraphs[]; "
            f"package complete modules (wheel/body/doors/…) — modules first, not count-first"
        )
    elif n_root >= ROOT_SOFT_MODULARIZE and n_defs == 0:
        warnings.append(
            f"root has {n_root} nodes (≥{ROOT_SOFT_MODULARIZE}); consider Subgraphs for "
            f"complete functional modules (skip if still a single clean spine)"
        )

    # One mega definition that holds almost all geometry while root is only Merge+Output.
    if n_defs == 1 and n_root <= 4 and instances:
        only = next(iter(definitions.values()))
        interior = [
            n for n in (only.get("nodes") or [])
            if isinstance(n, dict) and n.get("type") not in ("SubgraphInput", "SubgraphOutput")
        ]
        if len(interior) >= ROOT_SOFT_MODULARIZE:
            warnings.append(
                f"subgraph {only.get('id')}: looks like a mega-bag ({len(interior)} interior nodes) "
                f"with a tiny root — split into several complete modules, not one fake Subnet"
            )


def validate_node_list(
    nodes: list,
    edges: list,
    manifest: dict[str, dict],
    definitions: dict[str, dict],
    scope: str,
    errors: list[str],
    warnings: list[str],
    *,
    require_output: bool,
) -> None:
    if not isinstance(nodes, list) or not isinstance(edges, list):
        errors.append(f"{scope}: nodes/edges must be arrays")
        return

    by_id = {n["id"]: n for n in nodes if isinstance(n, dict) and "id" in n}
    if len(by_id) != len(nodes):
        errors.append(f"{scope}: duplicate node ids")

    if require_output and not any(n.get("type") == "Output" for n in nodes if isinstance(n, dict)):
        warnings.append(f"{scope}: no Output node (runnable graphs should end with Output)")

    for node in nodes:
        if not isinstance(node, dict):
            continue
        ntype = node.get("type")
        nid = node.get("id")
        if ntype in STRUCTURAL_TYPES:
            if ntype == "Subgraph":
                sgid = (node.get("data") or {}).get("subgraphId")
                if not sgid:
                    errors.append(f"{scope}: Subgraph {nid} missing data.subgraphId")
                elif sgid not in definitions:
                    errors.append(
                        f"{scope}: Subgraph {nid} references missing definition {sgid!r}"
                    )
            elif ntype in ("SubgraphInput", "SubgraphOutput") and scope == "root":
                errors.append(
                    f"root: {ntype} {nid} is only valid inside subgraphs[] definitions"
                )
        elif ntype not in manifest:
            errors.append(f"{scope}: unknown node type: {ntype} (id={nid})")
        if "position" not in node or "x" not in node["position"] or "y" not in node["position"]:
            errors.append(f"{scope}: node {nid} missing position.x/y")
        if "data" not in node:
            errors.append(f"{scope}: node {nid} missing data")

    for edge in edges:
        if not isinstance(edge, dict):
            continue
        for key in ("id", "source", "target"):
            if key not in edge:
                errors.append(f"{scope}: edge missing {key}: {edge}")
        src = by_id.get(edge.get("source", ""))
        tgt = by_id.get(edge.get("target", ""))
        if not src:
            errors.append(f"{scope}: edge {edge.get('id')}: unknown source {edge.get('source')}")
            continue
        if not tgt:
            errors.append(f"{scope}: edge {edge.get('id')}: unknown target {edge.get('target')}")
            continue
        sh = edge.get("sourceHandle", "out")
        th = edge.get("targetHandle", "in")
        if not can_connect(src["type"], tgt["type"], sh, th, manifest):
            errors.append(
                f"{scope}: edge {edge.get('id')}: incompatible pins "
                f"{src['type']}.{sh} -> {tgt['type']}.{th}"
            )

    positioned = [n for n in nodes if isinstance(n, dict) and "position" in n]
    style = layout_style(positioned)
    prefix = "" if scope == "root" else f"{scope} "
    if style == "horizontal":
        warnings.append(
            f"{prefix}layout looks left-to-right (legacy); use Houdini top-down (increase y per row)"
        )
    elif style == "mixed":
        warnings.append(f"{prefix}layout is mixed; prefer clear top-down spine at x=200")

    check_same_row_spacing(nodes, warnings)
    check_display_titles(nodes, manifest, warnings)
    check_wire_crossings(nodes, edges, warnings)
    check_subgraph_lane_layout(nodes, edges, scope, warnings)
    check_import_mesh_placeholders(nodes, scope, warnings)
    check_texture_input_pins(nodes, edges, manifest, scope, warnings)

    # Anti-pattern: MergeMesh (multi-input) → BevelMesh on assemblies
    inbound: dict[str, list[str]] = {}
    for edge in edges:
        if not isinstance(edge, dict):
            continue
        tgt_id = edge.get("target", "")
        src_id = edge.get("source", "")
        if tgt_id and src_id:
            inbound.setdefault(tgt_id, []).append(src_id)

    for node in nodes:
        if not isinstance(node, dict) or node.get("type") != "BevelMesh":
            continue
        bevel_id = node["id"]
        for pred_id in inbound.get(bevel_id, []):
            pred = by_id.get(pred_id)
            if not pred or pred.get("type") != "MergeMesh":
                continue
            merge_inputs = inbound.get(pred_id, [])
            if len(merge_inputs) >= 2:
                warnings.append(
                    f"{prefix}assembly anti-pattern: {pred_id}(MergeMesh, {len(merge_inputs)} inputs) "
                    f"→ {bevel_id}(BevelMesh); bevel each part then merge "
                    f"(see pcg-graph-authoring Bevel placement)"
                )


def check_import_mesh_placeholders(
    nodes: list,
    scope: str,
    warnings: list[str],
) -> None:
    """Flag ImportMesh nodes with empty path — they produce no geometry at cook time."""
    prefix = "" if scope == "root" else f"{scope} "
    for node in nodes:
        if not isinstance(node, dict) or node.get("type") != "ImportMesh":
            continue
        data = node.get("data") or {}
        path = data.get("path", "")
        if not isinstance(path, str) or not path.strip():
            warnings.append(
                f"{prefix}ImportMesh {node.get('id', '?')} has empty path "
                "(placeholder — substitute CreateBoxMesh/CreateCylinderMesh or bind a real asset path)"
            )


def check_texture_input_pins(
    nodes: list,
    edges: list,
    manifest: dict[str, dict],
    scope: str,
    warnings: list[str],
) -> None:
    """Warn when ProjectTexture (or any node with a Texture input) lacks a texture wire."""
    by_id = {n["id"]: n for n in nodes if isinstance(n, dict) and "id" in n}
    connected_texture_targets: set[tuple[str, str]] = set()
    for edge in edges:
        if not isinstance(edge, dict):
            continue
        target = edge.get("target")
        handle = edge.get("targetHandle") or "in"
        if target:
            connected_texture_targets.add((target, handle))

    prefix = "" if scope == "root" else f"{scope} "
    for node in nodes:
        if not isinstance(node, dict):
            continue
        ntype = node.get("type", "")
        defn = manifest.get(ntype) or {}
        texture_inputs = [
            p["id"]
            for p in defn.get("inputs", [])
            if isinstance(p, dict) and p.get("pinType") == "Texture"
        ]
        if not texture_inputs:
            continue
        nid = node.get("id", "?")
        for pin_id in texture_inputs:
            if (nid, pin_id) not in connected_texture_targets:
                warnings.append(
                    f"{prefix}{ntype} {nid}: texture input {pin_id!r} is not connected "
                    "(cook will fail or skip projection — wire ImageTexture or remove the node)"
                )


def check_server_parity(
    graph: dict,
    server_base: str,
    errors: list[str],
    warnings: list[str],
) -> None:
    """POST graph JSON to pcg-server /v1/validate — catches unknown node types on the running binary."""
    url = server_base.rstrip("/") + "/v1/validate"
    payload = json.dumps(graph).encode("utf-8")
    req = urllib.request.Request(
        url,
        data=payload,
        headers={"Content-Type": "application/json", "User-Agent": "validate_pcg/1.0"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=15.0) as resp:
            body = json.loads(resp.read().decode("utf-8"))
    except urllib.error.URLError as exc:
        warnings.append(
            f"server parity check skipped: pcg-server unreachable at {url} ({exc})"
        )
        return

    if body.get("ok"):
        return

    err = str(body.get("error") or "")
    if "unknown node type" in err.lower() or "unregistered" in err.lower():
        errors.append(
            f"manifest-server parity: running pcg-server rejected graph — {err} "
            "(rebuild pcg-server after pcg-core node changes; see pit-native-plugin-build-target-mismatch)"
        )
    else:
        warnings.append(f"server validate returned non-ok: {err or body}")


def validate_parameters(
    parameters: object,
    nodes: list,
    manifest: dict[str, dict],
    errors: list[str],
    warnings: list[str],
) -> None:
    """Validate root parameters[] bindings (Graph Blackboard / Graph Parameters)."""
    if parameters is None:
        return
    if not isinstance(parameters, list):
        errors.append("parameters must be an array")
        return

    node_by_id = {
        n.get("id"): n
        for n in nodes
        if isinstance(n, dict) and isinstance(n.get("id"), str)
    }
    seen_ids: set[str] = set()
    allowed_types = {"integer", "number", "boolean", "string"}

    for i, param in enumerate(parameters):
        if not isinstance(param, dict):
            errors.append(f"parameters[{i}] must be an object")
            continue

        pid = param.get("id")
        if not isinstance(pid, str) or not pid:
            errors.append(f"parameters[{i}] missing string id")
            continue
        if pid in seen_ids:
            errors.append(f"duplicate parameter id: {pid}")
        seen_ids.add(pid)

        name = param.get("name")
        if not isinstance(name, str) or not name:
            errors.append(f"parameter {pid}: missing string name")

        ptype = param.get("type", "number")
        if ptype not in allowed_types:
            errors.append(
                f"parameter {pid}: type {ptype!r} must be one of {sorted(allowed_types)}"
            )

        if "default" not in param:
            errors.append(f"parameter {pid}: missing default")

        target_node = param.get("targetNode")
        target_prop = param.get("targetProperty")
        if not isinstance(target_node, str) or not target_node:
            errors.append(f"parameter {pid}: missing targetNode")
            continue
        if not isinstance(target_prop, str) or not target_prop:
            errors.append(f"parameter {pid}: missing targetProperty")
            continue

        node = node_by_id.get(target_node)
        if node is None:
            errors.append(
                f"parameter {pid}: targetNode {target_node!r} not found in root nodes"
            )
            continue

        ntype = node.get("type")
        if ntype in STRUCTURAL_TYPES:
            warnings.append(
                f"parameter {pid}: targetNode {target_node!r} is structural ({ntype}); "
                "prefer binding to a concrete operator node"
            )
            continue

        defn = manifest.get(ntype)
        if not defn:
            warnings.append(
                f"parameter {pid}: target node type {ntype!r} not in manifest"
            )
            continue

        props = defn.get("properties") or {}
        if isinstance(props, dict) and target_prop not in props and target_prop != "__nodeTitle":
            errors.append(
                f"parameter {pid}: targetProperty {target_prop!r} not in "
                f"{ntype} manifest properties"
            )

        node_data = node.get("data") if isinstance(node.get("data"), dict) else {}
        if target_prop in node_data and "default" in param:
            baked = node_data.get(target_prop)
            default = param.get("default")
            # Compare as strings to tolerate 1 vs 1.0
            if str(baked) != str(default):
                warnings.append(
                    f"parameter {pid}: default {default!r} != "
                    f"nodes[{target_node!r}].data[{target_prop!r}]={baked!r} "
                    "(keep Graph Parameter default synced with baked data)"
                )

        if param.get("hasRange") and ptype in ("number", "integer"):
            try:
                lo = float(param.get("min", 0))
                hi = float(param.get("max", 1))
                if lo > hi:
                    errors.append(f"parameter {pid}: min ({lo}) > max ({hi})")
            except (TypeError, ValueError):
                errors.append(f"parameter {pid}: hasRange requires numeric min/max")


def validate_graph(
    graph_path: Path,
    manifest: dict[str, dict],
    *,
    server_base: str | None = None,
) -> int:
    errors: list[str] = []
    warnings: list[str] = []

    try:
        graph = json.loads(graph_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        print(f"ERROR: invalid JSON: {exc}")
        return 1

    version = graph.get("version")
    if version not in ("1.0", "2.0", "3.0"):
        errors.append(f'unsupported version {version!r} (expected 1.0, 2.0, or 3.0)')

    if version == "3.0":
        for node in graph.get("nodes") or []:
            if not isinstance(node, dict):
                continue
            if node.get("type") != "SubgraphAsset":
                continue
            data = node.get("data") or {}
            guid = str(data.get("assetGuid") or "")
            if len(guid) != 32 or any(c not in "0123456789abcdefABCDEF" for c in guid):
                errors.append(
                    f"SubgraphAsset '{node.get('id')}' assetGuid must be 32 hex characters"
                )
            if "subgraphInterface" not in node:
                errors.append(
                    f"SubgraphAsset '{node.get('id')}' requires subgraphInterface snapshot"
                )
        # Dependency resolution against target project assets is optional; mark when absent.
        unity_assets_dir = REPO_ROOT / "Unity" / "Assets"
        web_dir = REPO_ROOT / "web"
        if not unity_assets_dir.is_dir() and not web_dir.is_dir():
            warnings.append(
                "target project assets not found (neither Unity/Assets nor web/); "
                "SubgraphAsset GUID dependency resolution skipped"
            )

    definitions = validate_subgraph_defs(graph.get("subgraphs") or [], errors, warnings)
    nodes = graph.get("nodes", [])
    edges = graph.get("edges", [])

    validate_parameters(graph.get("parameters"), nodes, manifest, errors, warnings)

    validate_node_list(
        nodes, edges, manifest, definitions, "root", errors, warnings, require_output=True
    )
    check_subgraph_modularity(
        [n for n in nodes if isinstance(n, dict)], definitions, warnings
    )

    for sid, definition in definitions.items():
        validate_node_list(
            definition.get("nodes") or [],
            definition.get("edges") or [],
            manifest,
            definitions,
            f"subgraph:{sid}",
            errors,
            warnings,
            require_output=False,
        )

    if server_base and not errors:
        check_server_parity(graph, server_base, errors, warnings)

    for msg in warnings:
        print(f"WARN: {msg}")
    for msg in errors:
        print(f"ERROR: {msg}")

    if errors:
        print(f"\n{graph_path}: FAILED ({len(errors)} error(s), {len(warnings)} warning(s))")
        return 1

    n_sg = len(definitions)
    style = layout_style([n for n in nodes if isinstance(n, dict)]) if nodes else "single"
    print(
        f"\n{graph_path}: OK ({len(nodes)} root nodes, {len(edges)} edges, "
        f"{n_sg} subgraph def(s), layout={style})"
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate a PCG-AI .pcg graph against schema/node-manifest.json."
    )
    parser.add_argument("graphs", nargs="+", help=".pcg file(s) to validate")
    parser.add_argument(
        "--check-server",
        metavar="URL",
        default=None,
        help="POST graph to pcg-server /v1/validate after local checks (e.g. http://127.0.0.1:17890)",
    )
    args = parser.parse_args()

    if not MANIFEST_PATH.is_file():
        print(f"ERROR: manifest not found: {MANIFEST_PATH}")
        return 1

    manifest = load_manifest()
    code = 0
    for arg in args.graphs:
        path = Path(arg)
        if not path.is_file():
            print(f"ERROR: file not found: {path}")
            code = 1
            continue
        code = max(code, validate_graph(path, manifest, server_base=args.check_server))
    return code


if __name__ == "__main__":
    raise SystemExit(main())
