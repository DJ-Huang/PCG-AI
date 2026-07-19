#!/usr/bin/env python3
"""Validate a PCG-AI .pcg graph against schema/node-manifest.json."""

from __future__ import annotations

import json
import sys
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
# Unity GraphView: pill ~61px + right title overlay up to 220px → need ≥320 same-row Δx
COL_STEP_X = 320
SAME_ROW_Y_TOL = ROW_STEP_Y * 0.5
MIN_SAME_ROW_DX = COL_STEP_X * 0.9  # allow tiny float/rounding slack

# Structural types (not in node-manifest); ports come from subgraph definitions.
STRUCTURAL_TYPES = frozenset({"Subgraph", "SubgraphInput", "SubgraphOutput"})
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
    return src_pin == tgt_pin or src_pin == "Any" or tgt_pin == "Any"


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
                    f"|Δx|={dx:.0f} < {COL_STEP_X} (Unity titles sit right of pills; "
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
            f"{', '.join(missing)}; Unity will show {default_name!r} for all of them"
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


def validate_parameters(
    parameters: object,
    nodes: list,
    manifest: dict[str, dict],
    errors: list[str],
    warnings: list[str],
) -> None:
    """Validate root parameters[] bindings (Unity Blackboard / Graph Parameters)."""
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


def validate_graph(graph_path: Path, manifest: dict[str, dict]) -> int:
    errors: list[str] = []
    warnings: list[str] = []

    try:
        graph = json.loads(graph_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        print(f"ERROR: invalid JSON: {exc}")
        return 1

    if graph.get("version") != "1.0":
        warnings.append(f'version is {graph.get("version")!r}, expected "1.0"')

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
    if len(sys.argv) < 2:
        print("Usage: validate_pcg.py <file.pcg> [more.pcg ...]")
        return 2
    if not MANIFEST_PATH.is_file():
        print(f"ERROR: manifest not found: {MANIFEST_PATH}")
        return 1

    manifest = load_manifest()
    code = 0
    for arg in sys.argv[1:]:
        path = Path(arg)
        if not path.is_file():
            print(f"ERROR: file not found: {path}")
            code = 1
            continue
        code = max(code, validate_graph(path, manifest))
    return code


if __name__ == "__main__":
    raise SystemExit(main())
