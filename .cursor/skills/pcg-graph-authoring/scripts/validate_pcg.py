#!/usr/bin/env python3
"""Validate a PCG-AI .pcg graph against schema/node-manifest.json."""

from __future__ import annotations

import json
import sys
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


def load_manifest() -> dict[str, dict]:
    data = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    return {node["type"]: node for node in data["nodes"]}


def pin_maps(defn: dict) -> tuple[dict[str, str], dict[str, str]]:
    inputs = {p["id"]: p["pinType"] for p in defn.get("inputs", [])}
    outputs = {p["id"]: p["pinType"] for p in defn.get("outputs", [])}
    return inputs, outputs


def can_connect(src_type: str, tgt_type: str, src_handle: str, tgt_handle: str, manifest: dict) -> bool:
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
    xs = [n["position"]["x"] for n in nodes]
    ys = [n["position"]["y"] for n in nodes]
    x_range = max(xs) - min(xs)
    y_range = max(ys) - min(ys)
    if y_range >= ROW_STEP_Y * 0.5 and y_range >= x_range:
        return "vertical"
    if x_range >= HORIZONTAL_STEP_X * 0.5 and x_range > y_range:
        return "horizontal"
    return "mixed"


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

    nodes = graph.get("nodes", [])
    edges = graph.get("edges", [])
    by_id = {n["id"]: n for n in nodes}

    if len(by_id) != len(nodes):
        errors.append("duplicate node ids")

    has_output = any(n.get("type") == "Output" for n in nodes)
    if not has_output:
        warnings.append("no Output node (runnable graphs should end with Output)")

    for node in nodes:
        ntype = node.get("type")
        if ntype not in manifest:
            errors.append(f"unknown node type: {ntype} (id={node.get('id')})")
            continue
        if "position" not in node or "x" not in node["position"] or "y" not in node["position"]:
            errors.append(f"node {node.get('id')} missing position.x/y")
        if "data" not in node:
            errors.append(f"node {node.get('id')} missing data")

    for edge in edges:
        for key in ("id", "source", "target"):
            if key not in edge:
                errors.append(f"edge missing {key}: {edge}")
        src = by_id.get(edge.get("source", ""))
        tgt = by_id.get(edge.get("target", ""))
        if not src:
            errors.append(f"edge {edge.get('id')}: unknown source {edge.get('source')}")
            continue
        if not tgt:
            errors.append(f"edge {edge.get('id')}: unknown target {edge.get('target')}")
            continue
        sh = edge.get("sourceHandle", "out")
        th = edge.get("targetHandle", "in")
        if not can_connect(src["type"], tgt["type"], sh, th, manifest):
            errors.append(
                f"edge {edge.get('id')}: incompatible pins "
                f"{src['type']}.{sh} -> {tgt['type']}.{th}"
            )

    style = layout_style(nodes)
    if style == "horizontal":
        warnings.append(
            "layout looks left-to-right (legacy); use Houdini top-down (increase y per row)"
        )
    elif style == "mixed":
        warnings.append("layout is mixed; prefer clear top-down spine at x=200")

    # Anti-pattern: MergeMesh (multi-input) → BevelMesh on assemblies
    inbound: dict[str, list[str]] = {}
    for edge in edges:
        tgt_id = edge.get("target", "")
        src_id = edge.get("source", "")
        if tgt_id and src_id:
            inbound.setdefault(tgt_id, []).append(src_id)

    for node in nodes:
        if node.get("type") != "BevelMesh":
            continue
        bevel_id = node["id"]
        for pred_id in inbound.get(bevel_id, []):
            pred = by_id.get(pred_id)
            if not pred or pred.get("type") != "MergeMesh":
                continue
            merge_inputs = inbound.get(pred_id, [])
            if len(merge_inputs) >= 2:
                warnings.append(
                    f"assembly anti-pattern: {pred_id}(MergeMesh, {len(merge_inputs)} inputs) "
                    f"→ {bevel_id}(BevelMesh); bevel each part then merge "
                    f"(see pcg-graph-authoring Bevel placement)"
                )

    for msg in warnings:
        print(f"WARN: {msg}")
    for msg in errors:
        print(f"ERROR: {msg}")

    if errors:
        print(f"\n{graph_path}: FAILED ({len(errors)} error(s), {len(warnings)} warning(s))")
        return 1

    print(f"\n{graph_path}: OK ({len(nodes)} nodes, {len(edges)} edges, layout={style})")
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
